import { URL } from 'node:url';
import { getRelativePath } from '../utils/paths.js';
import { normalizeUrl, resolveUrl, isSameOrigin } from '../utils/url.js';

export interface RewriteMap {
  // Maps original absolute URLs to local file paths
  urlToPath: Map<string, string>;
  // The base URL of the site being mirrored
  baseUrl: string;
  // The base directory for local files
  baseDir: string;
}

/**
 * Calculate the rewritten URL for a resource
 */
export function rewriteUrl(
  originalUrl: string,
  currentFilePath: string,
  rewriteMap: RewriteMap
): string {
  try {
    // Normalize the URL
    const normalized = normalizeUrl(originalUrl, rewriteMap.baseUrl);

    // Check if we have this URL in our map
    const localPath = rewriteMap.urlToPath.get(normalized);

    if (localPath) {
      // Calculate relative path from current file to target
      return getRelativePath(currentFilePath, localPath);
    }

    // URL not in our map - check if it's same origin
    if (isSameOrigin(normalized, rewriteMap.baseUrl)) {
      // Same origin but not downloaded - might be excluded or failed
      // Return as-is or relative path based on settings
      return originalUrl;
    }

    // External URL - return as-is
    return originalUrl;
  } catch {
    // Can't process - return as-is
    return originalUrl;
  }
}

/**
 * Check if a URL should be rewritten
 */
export function shouldRewrite(url: string, baseUrl: string, sameOriginOnly: boolean): boolean {
  // Never rewrite data URIs, javascript:, or fragments
  if (url.startsWith('data:') || url.startsWith('javascript:') || url.startsWith('#')) {
    return false;
  }

  // If same origin only, check origin
  if (sameOriginOnly) {
    try {
      const resolved = resolveUrl(url, baseUrl);
      return isSameOrigin(resolved, baseUrl);
    } catch {
      return false;
    }
  }

  return true;
}

/**
 * Extract the fragment from a URL
 */
export function extractFragment(url: string): { url: string; fragment: string } {
  const hashIndex = url.indexOf('#');

  if (hashIndex === -1) {
    return { url, fragment: '' };
  }

  return {
    url: url.slice(0, hashIndex),
    fragment: url.slice(hashIndex),
  };
}

/**
 * Rewrite a URL, preserving the fragment
 */
export function rewriteUrlWithFragment(
  originalUrl: string,
  currentFilePath: string,
  rewriteMap: RewriteMap
): string {
  const { url, fragment } = extractFragment(originalUrl);
  const rewritten = rewriteUrl(url, currentFilePath, rewriteMap);

  return fragment ? rewritten + fragment : rewritten;
}
