import { URL } from 'node:url';
import { ResourceType } from '../types/index.js';

/**
 * Normalize a URL by removing fragments, sorting query params, etc.
 */
export function normalizeUrl(urlString: string, base?: string): string {
  try {
    const url = base ? new URL(urlString, base) : new URL(urlString);
    // Remove fragment
    url.hash = '';
    // Ensure trailing slash consistency
    if (url.pathname === '') {
      url.pathname = '/';
    }
    return url.href;
  } catch {
    return urlString;
  }
}

/**
 * Resolve a potentially relative URL against a base URL
 */
export function resolveUrl(urlString: string, base: string): string {
  try {
    // Handle protocol-relative URLs
    if (urlString.startsWith('//')) {
      const baseUrl = new URL(base);
      return `${baseUrl.protocol}${urlString}`;
    }
    return new URL(urlString, base).href;
  } catch {
    return urlString;
  }
}

/**
 * Check if a URL is absolute
 */
export function isAbsoluteUrl(urlString: string): boolean {
  try {
    new URL(urlString);
    return true;
  } catch {
    return false;
  }
}

/**
 * Check if two URLs have the same origin
 */
export function isSameOrigin(url1: string, url2: string): boolean {
  try {
    const u1 = new URL(url1);
    const u2 = new URL(url2);
    return u1.origin === u2.origin;
  } catch {
    return false;
  }
}

/**
 * Extract the hostname from a URL
 */
export function getHostname(urlString: string): string {
  try {
    return new URL(urlString).hostname;
  } catch {
    return '';
  }
}

/**
 * Check if a URL is an HTTP(S) URL
 */
export function isHttpUrl(urlString: string): boolean {
  try {
    const url = new URL(urlString);
    return url.protocol === 'http:' || url.protocol === 'https:';
  } catch {
    return false;
  }
}

/**
 * Check if a URL points to a private/local address (SSRF protection)
 */
export function isPrivateUrl(urlString: string): boolean {
  try {
    const url = new URL(urlString);
    const hostname = url.hostname.toLowerCase();

    // Localhost
    if (hostname === 'localhost' || hostname === '127.0.0.1' || hostname === '::1') {
      return true;
    }

    // Private IPv4 ranges
    const ipv4Match = hostname.match(/^(\d+)\.(\d+)\.(\d+)\.(\d+)$/);
    if (ipv4Match) {
      const [, a, b] = ipv4Match.map(Number);
      // 10.0.0.0/8
      if (a === 10) return true;
      // 172.16.0.0/12
      if (a === 172 && b !== undefined && b >= 16 && b <= 31) return true;
      // 192.168.0.0/16
      if (a === 192 && b === 168) return true;
      // 169.254.0.0/16 (link-local, including AWS metadata)
      if (a === 169 && b === 254) return true;
      // 0.0.0.0
      if (a === 0) return true;
    }

    return false;
  } catch {
    return true; // Be safe - if we can't parse, assume private
  }
}

/**
 * Get file extension from URL
 */
export function getExtensionFromUrl(urlString: string): string {
  try {
    const url = new URL(urlString);
    const pathname = url.pathname;
    const lastDot = pathname.lastIndexOf('.');
    const lastSlash = pathname.lastIndexOf('/');

    if (lastDot > lastSlash && lastDot < pathname.length - 1) {
      return pathname.slice(lastDot + 1).toLowerCase();
    }
    return '';
  } catch {
    return '';
  }
}

/**
 * Determine resource type from URL and optional content-type
 */
export function getResourceType(urlString: string, contentType?: string): ResourceType {
  const ext = getExtensionFromUrl(urlString);

  // Check content-type first if available
  if (contentType) {
    const ct = contentType.toLowerCase();
    if (ct.includes('text/html')) return 'html';
    if (ct.includes('text/css')) return 'css';
    if (ct.includes('javascript') || ct.includes('ecmascript')) return 'javascript';
    if (ct.includes('image/')) return 'image';
    if (ct.includes('font/') || ct.includes('application/font')) return 'font';
    if (ct.includes('video/')) return 'video';
    if (ct.includes('audio/')) return 'audio';
    if (ct.includes('application/pdf')) return 'document';
    if (ct.includes('application/manifest') || ct.includes('application/json')) return 'manifest';
  }

  // Fall back to extension
  const extMap: Record<string, ResourceType> = {
    html: 'html',
    htm: 'html',
    xhtml: 'html',
    css: 'css',
    js: 'javascript',
    mjs: 'javascript',
    ts: 'javascript',
    jsx: 'javascript',
    tsx: 'javascript',
    png: 'image',
    jpg: 'image',
    jpeg: 'image',
    gif: 'image',
    webp: 'image',
    avif: 'image',
    svg: 'image',
    ico: 'image',
    bmp: 'image',
    woff: 'font',
    woff2: 'font',
    ttf: 'font',
    otf: 'font',
    eot: 'font',
    mp4: 'video',
    webm: 'video',
    ogg: 'video',
    mp3: 'audio',
    wav: 'audio',
    flac: 'audio',
    pdf: 'document',
    json: 'manifest',
    webmanifest: 'manifest',
  };

  return extMap[ext] ?? 'other';
}

/**
 * Remove query string from URL while preserving the path
 */
export function removeQuery(urlString: string): string {
  try {
    const url = new URL(urlString);
    url.search = '';
    return url.href;
  } catch {
    return urlString;
  }
}

/**
 * Get the path portion of a URL
 */
export function getUrlPath(urlString: string): string {
  try {
    return new URL(urlString).pathname;
  } catch {
    return urlString;
  }
}

/**
 * Check if URL matches any of the given glob patterns
 */
export function matchesPattern(urlString: string, patterns: string[]): boolean {
  if (patterns.length === 0) return true;

  try {
    const url = new URL(urlString);
    const path = url.pathname;

    return patterns.some((pattern) => {
      // Convert glob to regex
      const regexPattern = pattern
        .replace(/[.+^${}()|[\]\\]/g, '\\$&')
        .replace(/\*/g, '.*')
        .replace(/\?/g, '.');

      const regex = new RegExp(`^${regexPattern}$`);
      return regex.test(path);
    });
  } catch {
    return false;
  }
}
