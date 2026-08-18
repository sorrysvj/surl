import { ParsedResource } from '../types/index.js';
import { resolveUrl, getResourceType, isAbsoluteUrl } from '../utils/url.js';

/**
 * Extract potential URLs from JavaScript (experimental)
 * This is a simple heuristic-based approach, not a full JS parser
 */
export function parseJs(js: string, jsUrl: string): ParsedResource[] {
  const resources: ParsedResource[] = [];
  const seen = new Set<string>();

  // Common patterns for static URLs in JS
  const patterns = [
    // String literals with paths
    /['"](\/[a-zA-Z0-9_\-\/\.]+\.[a-zA-Z]{2,4})['"]|"['"](?=[^<>])/g,

    // fetch() and similar
    /fetch\(['"]([^'"]+)['"]\)/g,

    // XMLHttpRequest
    /\.open\(['"][A-Z]+['"],\s*['"]([^'"]+)['"]\)/g,

    // Import statements (dynamic)
    /import\(['"]([^'"]+)['"]\)/g,

    // Webpack/Vite asset imports
    /(?:import|from)\s+['"]([^'"]+\.(?:png|jpg|jpeg|gif|svg|webp|css|json))['"]|require\(['"]([^'"]+\.(?:png|jpg|jpeg|gif|svg|webp|css|json))['"]\)/g,
  ];

  for (const pattern of patterns) {
    let match;
    while ((match = pattern.exec(js)) !== null) {
      const url = match[1] ?? match[2];
      if (!url) continue;

      // Skip data URLs and obvious non-URLs
      if (url.startsWith('data:') || url.startsWith('javascript:')) continue;
      if (url.length < 3 || url.length > 500) continue;

      // Skip things that don't look like URLs/paths
      if (!isAbsoluteUrl(url) && !url.startsWith('/') && !url.startsWith('.')) continue;

      const resolved = resolveUrl(url, jsUrl);

      if (!seen.has(resolved)) {
        seen.add(resolved);
        resources.push({
          url: resolved,
          type: getResourceType(resolved),
          tag: 'script',
          attribute: 'static-url',
        });
      }
    }
  }

  return resources;
}

/**
 * Check if JS scanning should be attempted on this content
 */
export function shouldScanJs(content: string): boolean {
  // Don't scan minified files that are too large
  if (content.length > 1000000) return false;

  // Check if it looks like source code (has line breaks)
  const lineBreaks = (content.match(/\n/g) ?? []).length;
  if (lineBreaks < 10 && content.length > 10000) return false;

  return true;
}
