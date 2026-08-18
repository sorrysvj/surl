import { httpClient } from '../downloader/http-client.js';
import * as cheerio from 'cheerio';

export interface SitemapEntry {
  url: string;
  lastmod?: string;
  changefreq?: string;
  priority?: number;
}

/**
 * Parse a sitemap and extract URLs
 */
export async function parseSitemap(sitemapUrl: string): Promise<SitemapEntry[]> {
  const entries: SitemapEntry[] = [];

  try {
    const response = await httpClient.get(sitemapUrl);

    if (response.status !== 200 || !response.body) {
      return entries;
    }

    const reader = response.body.getReader();
    const chunks: Uint8Array[] = [];

    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
    }

    const content = Buffer.concat(chunks).toString('utf-8');
    const contentType = response.headers.get('content-type') ?? '';

    // Check if it's a sitemap index
    if (content.includes('<sitemapindex')) {
      const sitemaps = parseSitemapIndex(content);

      // Recursively parse child sitemaps
      for (const childUrl of sitemaps) {
        const childEntries = await parseSitemap(childUrl);
        entries.push(...childEntries);
      }
    } else if (content.includes('<urlset') || contentType.includes('xml')) {
      // Regular sitemap
      const parsed = parseSitemapXml(content);
      entries.push(...parsed);
    } else if (content.includes('http')) {
      // Plain text sitemap (one URL per line)
      const urls = content.split('\n')
        .map((line) => line.trim())
        .filter((line) => line.startsWith('http'));

      for (const url of urls) {
        entries.push({ url });
      }
    }
  } catch {
    // Failed to fetch/parse sitemap
  }

  return entries;
}

/**
 * Parse sitemap index XML
 */
function parseSitemapIndex(xml: string): string[] {
  const sitemaps: string[] = [];
  const $ = cheerio.load(xml, { xmlMode: true });

  $('sitemap loc').each((_, element) => {
    const url = $(element).text().trim();
    if (url) {
      sitemaps.push(url);
    }
  });

  return sitemaps;
}

/**
 * Parse sitemap XML
 */
function parseSitemapXml(xml: string): SitemapEntry[] {
  const entries: SitemapEntry[] = [];
  const $ = cheerio.load(xml, { xmlMode: true });

  $('url').each((_, element) => {
    const url = $(element).find('loc').text().trim();

    if (!url) return;

    const entry: SitemapEntry = { url };

    const lastmod = $(element).find('lastmod').text().trim();
    if (lastmod) entry.lastmod = lastmod;

    const changefreq = $(element).find('changefreq').text().trim();
    if (changefreq) entry.changefreq = changefreq;

    const priority = $(element).find('priority').text().trim();
    if (priority) {
      const p = parseFloat(priority);
      if (!isNaN(p)) entry.priority = p;
    }

    entries.push(entry);
  });

  return entries;
}

/**
 * Try to discover sitemap URL
 */
export async function discoverSitemap(baseUrl: string): Promise<string | null> {
  const commonPaths = [
    '/sitemap.xml',
    '/sitemap_index.xml',
    '/sitemap/sitemap.xml',
    '/sitemaps/sitemap.xml',
  ];

  for (const path of commonPaths) {
    try {
      const url = new URL(path, baseUrl).href;
      const response = await httpClient.head(url);

      if (response.status === 200) {
        return url;
      }
    } catch {
      // Continue to next path
    }
  }

  return null;
}
