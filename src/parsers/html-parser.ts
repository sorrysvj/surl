import * as cheerio from 'cheerio';
import { ParsedResource, ResourceType } from '../types/index.js';
import { resolveUrl, getResourceType } from '../utils/url.js';

// HTML attributes that contain URLs
const URL_ATTRIBUTES: Array<{ tag: string; attr: string; type?: ResourceType }> = [
  // Links and navigation
  { tag: 'a', attr: 'href', type: 'html' },
  { tag: 'area', attr: 'href', type: 'html' },

  // Styles
  { tag: 'link', attr: 'href' },

  // Scripts
  { tag: 'script', attr: 'src', type: 'javascript' },

  // Images
  { tag: 'img', attr: 'src', type: 'image' },
  { tag: 'img', attr: 'srcset' },
  { tag: 'picture source', attr: 'srcset' },
  { tag: 'source', attr: 'src' },

  // Media
  { tag: 'video', attr: 'src', type: 'video' },
  { tag: 'video', attr: 'poster', type: 'image' },
  { tag: 'audio', attr: 'src', type: 'audio' },
  { tag: 'track', attr: 'src' },

  // Embeds
  { tag: 'iframe', attr: 'src', type: 'html' },
  { tag: 'embed', attr: 'src' },
  { tag: 'object', attr: 'data' },

  // Other
  { tag: 'form', attr: 'action', type: 'html' },
  { tag: 'input', attr: 'src', type: 'image' },
  { tag: 'body', attr: 'background', type: 'image' },
  { tag: 'table', attr: 'background', type: 'image' },
  { tag: 'td', attr: 'background', type: 'image' },
  { tag: 'th', attr: 'background', type: 'image' },
];

// Link rel values that indicate assets
const ASSET_REL_VALUES = [
  'stylesheet',
  'icon',
  'shortcut icon',
  'apple-touch-icon',
  'apple-touch-icon-precomposed',
  'manifest',
  'preload',
  'prefetch',
  'modulepreload',
];

/**
 * Parse srcset attribute and extract URLs
 */
function parseSrcset(srcset: string): string[] {
  const urls: string[] = [];

  // srcset format: "url1 1x, url2 2x" or "url1 100w, url2 200w"
  const parts = srcset.split(',');

  for (const part of parts) {
    const trimmed = part.trim();
    const spaceIndex = trimmed.lastIndexOf(' ');

    if (spaceIndex > 0) {
      const url = trimmed.slice(0, spaceIndex).trim();
      if (url) urls.push(url);
    } else if (trimmed) {
      urls.push(trimmed);
    }
  }

  return urls;
}

/**
 * Extract the base URL from HTML if present
 */
export function extractBaseUrl(html: string, pageUrl: string): string {
  const $ = cheerio.load(html);
  const baseHref = $('base').attr('href');

  if (baseHref) {
    return resolveUrl(baseHref, pageUrl);
  }

  return pageUrl;
}

/**
 * Parse HTML and extract all resource URLs
 */
export function parseHtml(html: string, pageUrl: string): ParsedResource[] {
  const $ = cheerio.load(html);
  const resources: ParsedResource[] = [];
  const seen = new Set<string>();

  // Get base URL
  const baseUrl = extractBaseUrl(html, pageUrl);

  function addResource(url: string, type: ResourceType, tag: string, attr: string): void {
    if (!url || url.startsWith('data:') || url.startsWith('javascript:') || url.startsWith('#')) {
      return;
    }

    const resolved = resolveUrl(url.trim(), baseUrl);

    if (!seen.has(resolved)) {
      seen.add(resolved);
      resources.push({
        url: resolved,
        type,
        tag,
        attribute: attr,
      });
    }
  }

  // Process standard URL attributes
  for (const { tag, attr, type } of URL_ATTRIBUTES) {
    $(tag).each((_, element) => {
      const value = $(element).attr(attr);

      if (!value) return;

      // Handle srcset specially
      if (attr === 'srcset') {
        const urls = parseSrcset(value);
        for (const url of urls) {
          addResource(url, type ?? getResourceType(url), tag, attr);
        }
        return;
      }

      // Handle link elements - determine type from rel
      if (tag === 'link') {
        const rel = $(element).attr('rel')?.toLowerCase() ?? '';

        // Skip non-asset links
        if (!ASSET_REL_VALUES.some((v) => rel.includes(v))) {
          // Still include canonical, alternate, etc. for discovery
          if (rel === 'canonical' || rel === 'alternate') {
            addResource(value, 'html', tag, attr);
          }
          return;
        }

        // Determine type from rel
        let linkType: ResourceType = 'other';
        if (rel.includes('stylesheet')) linkType = 'css';
        else if (rel.includes('icon')) linkType = 'image';
        else if (rel.includes('manifest')) linkType = 'manifest';
        else linkType = getResourceType(value);

        addResource(value, linkType, tag, attr);
        return;
      }

      // Regular URL attribute
      const resourceType = type ?? getResourceType(value);
      addResource(value, resourceType, tag, attr);
    });
  }

  // Extract URLs from inline styles
  $('[style]').each((_, element) => {
    const style = $(element).attr('style');
    if (style) {
      const urlMatches = style.matchAll(/url\(['"]?([^'"\)]+)['"]?\)/gi);
      for (const match of urlMatches) {
        if (match[1]) {
          addResource(match[1], 'image', 'style', 'url()');
        }
      }
    }
  });

  // Extract URLs from <style> blocks
  $('style').each((_, element) => {
    const cssContent = $(element).html();
    if (cssContent) {
      const urlMatches = cssContent.matchAll(/url\(['"]?([^'"\)]+)['"]?\)/gi);
      for (const match of urlMatches) {
        if (match[1]) {
          addResource(match[1], getResourceType(match[1]), 'style', 'url()');
        }
      }
    }
  });

  return resources;
}

/**
 * Extract only links (for crawling)
 */
export function extractLinks(html: string, pageUrl: string): string[] {
  const resources = parseHtml(html, pageUrl);

  return resources
    .filter((r) => r.type === 'html' && r.tag === 'a')
    .map((r) => r.url);
}

/**
 * Extract only assets (CSS, JS, images, etc.)
 */
export function extractAssets(html: string, pageUrl: string): ParsedResource[] {
  const resources = parseHtml(html, pageUrl);

  return resources.filter((r) => r.type !== 'html' || r.tag !== 'a');
}

/**
 * Extract meta information
 */
export function extractMeta(html: string): { canonical?: string; robots?: string; title?: string } {
  const $ = cheerio.load(html);

  const canonical = $('link[rel="canonical"]').attr('href');
  const robots = $('meta[name="robots"]').attr('content');
  const title = $('title').text();

  return { canonical, robots, title };
}
