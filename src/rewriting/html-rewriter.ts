import * as cheerio from 'cheerio';
import { RewriteMap, rewriteUrlWithFragment, shouldRewrite } from './url-rewriter.js';
import { resolveUrl } from '../utils/url.js';

// Attributes that contain URLs
const URL_ATTRIBUTES = [
  { selector: 'a', attr: 'href' },
  { selector: 'link', attr: 'href' },
  { selector: 'script', attr: 'src' },
  { selector: 'img', attr: 'src' },
  { selector: 'img', attr: 'srcset' },
  { selector: 'source', attr: 'src' },
  { selector: 'source', attr: 'srcset' },
  { selector: 'video', attr: 'src' },
  { selector: 'video', attr: 'poster' },
  { selector: 'audio', attr: 'src' },
  { selector: 'track', attr: 'src' },
  { selector: 'iframe', attr: 'src' },
  { selector: 'embed', attr: 'src' },
  { selector: 'object', attr: 'data' },
  { selector: 'form', attr: 'action' },
  { selector: 'input[type="image"]', attr: 'src' },
  { selector: 'body', attr: 'background' },
  { selector: 'table', attr: 'background' },
  { selector: 'td', attr: 'background' },
  { selector: 'th', attr: 'background' },
];

/**
 * Rewrite srcset attribute
 */
function rewriteSrcset(
  srcset: string,
  currentFilePath: string,
  rewriteMap: RewriteMap
): string {
  const parts = srcset.split(',');

  const rewrittenParts = parts.map((part) => {
    const trimmed = part.trim();
    const spaceIndex = trimmed.lastIndexOf(' ');

    if (spaceIndex > 0) {
      const url = trimmed.slice(0, spaceIndex).trim();
      const descriptor = trimmed.slice(spaceIndex).trim();

      if (shouldRewrite(url, rewriteMap.baseUrl, true)) {
        const resolved = resolveUrl(url, rewriteMap.baseUrl);
        const rewritten = rewriteUrlWithFragment(resolved, currentFilePath, rewriteMap);
        return `${rewritten} ${descriptor}`;
      }
      return trimmed;
    }

    if (shouldRewrite(trimmed, rewriteMap.baseUrl, true)) {
      const resolved = resolveUrl(trimmed, rewriteMap.baseUrl);
      return rewriteUrlWithFragment(resolved, currentFilePath, rewriteMap);
    }
    return trimmed;
  });

  return rewrittenParts.join(', ');
}

/**
 * Rewrite URLs in inline styles
 */
function rewriteInlineStyle(
  style: string,
  currentFilePath: string,
  rewriteMap: RewriteMap
): string {
  return style.replace(/url\(['"]?([^'"\)]+)['"]?\)/gi, (match, url) => {
    if (!shouldRewrite(url, rewriteMap.baseUrl, true)) {
      return match;
    }

    const resolved = resolveUrl(url, rewriteMap.baseUrl);
    const rewritten = rewriteUrlWithFragment(resolved, currentFilePath, rewriteMap);

    // Preserve quote style
    if (match.includes("'")) {
      return `url('${rewritten}')`;
    }
    if (match.includes('"')) {
      return `url("${rewritten}")`;
    }
    return `url(${rewritten})`;
  });
}

/**
 * Rewrite all URLs in HTML content
 */
export function rewriteHtml(
  html: string,
  currentFilePath: string,
  rewriteMap: RewriteMap
): string {
  const $ = cheerio.load(html, { decodeEntities: false });

  // Get base URL if present
  const baseHref = $('base').attr('href');
  const effectiveBaseUrl = baseHref
    ? resolveUrl(baseHref, rewriteMap.baseUrl)
    : rewriteMap.baseUrl;

  // Remove or update <base> tag
  $('base').remove();

  // Process URL attributes
  for (const { selector, attr } of URL_ATTRIBUTES) {
    $(selector).each((_, element) => {
      const value = $(element).attr(attr);
      if (!value) return;

      // Handle srcset
      if (attr === 'srcset') {
        const rewritten = rewriteSrcset(value, currentFilePath, rewriteMap);
        $(element).attr(attr, rewritten);
        return;
      }

      // Regular URL attribute
      if (!shouldRewrite(value, effectiveBaseUrl, true)) {
        return;
      }

      const resolved = resolveUrl(value, effectiveBaseUrl);
      const rewritten = rewriteUrlWithFragment(resolved, currentFilePath, rewriteMap);
      $(element).attr(attr, rewritten);
    });
  }

  // Process inline styles
  $('[style]').each((_, element) => {
    const style = $(element).attr('style');
    if (style) {
      const rewritten = rewriteInlineStyle(style, currentFilePath, rewriteMap);
      $(element).attr('style', rewritten);
    }
  });

  // Process <style> blocks
  $('style').each((_, element) => {
    const content = $(element).html();
    if (content) {
      const rewritten = rewriteInlineStyle(content, currentFilePath, rewriteMap);
      $(element).html(rewritten);
    }
  });

  return $.html();
}
