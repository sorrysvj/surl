import path from 'node:path';
import { URL } from 'node:url';
import crypto from 'node:crypto';

// Windows reserved names
const WINDOWS_RESERVED = new Set([
  'CON', 'PRN', 'AUX', 'NUL',
  'COM1', 'COM2', 'COM3', 'COM4', 'COM5', 'COM6', 'COM7', 'COM8', 'COM9',
  'LPT1', 'LPT2', 'LPT3', 'LPT4', 'LPT5', 'LPT6', 'LPT7', 'LPT8', 'LPT9',
]);

// Invalid characters for filenames
const INVALID_CHARS = /[<>:"\|?*\x00-\x1f]/g;

/**
 * Convert a URL to a local filesystem path
 */
export function urlToLocalPath(
  urlString: string,
  baseDir: string,
  options: { preserveQuery?: boolean; queryMode?: 'ignore' | 'preserve' | 'hash' } = {}
): string {
  const { preserveQuery = false, queryMode = 'ignore' } = options;

  try {
    const url = new URL(urlString);
    let pathname = decodeURIComponent(url.pathname);

    // Handle root path
    if (pathname === '/' || pathname === '') {
      pathname = '/index.html';
    }

    // Handle directory paths (ending with /)
    if (pathname.endsWith('/')) {
      pathname = pathname + 'index.html';
    }

    // Handle paths without extension that look like pages
    const ext = path.extname(pathname);
    if (!ext && !pathname.includes('.')) {
      // Check if it might be a file without extension
      const lastSegment = pathname.split('/').pop() ?? '';
      if (!lastSegment.includes('.')) {
        pathname = pathname + '/index.html';
      }
    }

    // Handle query string
    if (url.search && (preserveQuery || queryMode === 'preserve')) {
      const queryHash = crypto
        .createHash('md5')
        .update(url.search)
        .digest('hex')
        .slice(0, 8);
      const extname = path.extname(pathname);
      const basename = path.basename(pathname, extname);
      const dirname = path.dirname(pathname);
      pathname = path.join(dirname, `${basename}_${queryHash}${extname}`);
    } else if (url.search && queryMode === 'hash') {
      const queryHash = crypto
        .createHash('md5')
        .update(url.search)
        .digest('hex')
        .slice(0, 8);
      const extname = path.extname(pathname);
      const basename = path.basename(pathname, extname);
      const dirname = path.dirname(pathname);
      pathname = path.join(dirname, `${basename}_${queryHash}${extname}`);
    }

    // Sanitize each path segment
    const segments = pathname.split('/').filter(Boolean);
    const sanitizedSegments = segments.map(sanitizeFilename);

    // Join with base directory
    const localPath = path.join(baseDir, ...sanitizedSegments);

    // Ensure the path stays within baseDir (prevent path traversal)
    const resolvedPath = path.resolve(localPath);
    const resolvedBase = path.resolve(baseDir);

    if (!resolvedPath.startsWith(resolvedBase)) {
      throw new Error(`Path traversal detected: ${urlString}`);
    }

    return resolvedPath;
  } catch (error) {
    if (error instanceof Error && error.message.includes('Path traversal')) {
      throw error;
    }
    // Fallback for invalid URLs
    const hash = crypto.createHash('md5').update(urlString).digest('hex').slice(0, 16);
    return path.join(baseDir, `resource_${hash}`);
  }
}

/**
 * Sanitize a filename to be safe for all platforms
 */
export function sanitizeFilename(filename: string): string {
  let sanitized = filename;

  // Remove invalid characters
  sanitized = sanitized.replace(INVALID_CHARS, '_');

  // Handle Windows reserved names
  const upperName = sanitized.toUpperCase();
  const baseName = upperName.split('.')[0];
  if (baseName && WINDOWS_RESERVED.has(baseName)) {
    sanitized = `_${sanitized}`;
  }

  // Trim whitespace and dots from ends
  sanitized = sanitized.replace(/^[\s.]+|[\s.]+$/g, '');

  // Ensure filename is not empty
  if (!sanitized) {
    sanitized = '_empty_';
  }

  // Limit filename length (255 is common max)
  if (sanitized.length > 200) {
    const ext = path.extname(sanitized);
    const base = path.basename(sanitized, ext);
    const hash = crypto.createHash('md5').update(base).digest('hex').slice(0, 8);
    sanitized = `${base.slice(0, 180)}_${hash}${ext}`;
  }

  return sanitized;
}

/**
 * Ensure a directory exists
 */
export async function ensureDir(dirPath: string): Promise<void> {
  const { mkdir } = await import('node:fs/promises');
  await mkdir(dirPath, { recursive: true });
}

/**
 * Get relative path from one file to another
 */
export function getRelativePath(from: string, to: string): string {
  const fromDir = path.dirname(from);
  let relative = path.relative(fromDir, to);

  // Ensure forward slashes for web compatibility
  relative = relative.replace(/\\/g, '/');

  // Ensure relative path starts with ./ or ../
  if (!relative.startsWith('.') && !relative.startsWith('/')) {
    relative = './' + relative;
  }

  return relative;
}

/**
 * Check if a path is safe (doesn't escape base directory)
 */
export function isPathSafe(localPath: string, baseDir: string): boolean {
  const resolvedPath = path.resolve(localPath);
  const resolvedBase = path.resolve(baseDir);
  return resolvedPath.startsWith(resolvedBase);
}

/**
 * Get the surl metadata directory path
 */
export function getSurlDir(baseDir: string): string {
  return path.join(baseDir, '.surl');
}

/**
 * Get manifest file path
 */
export function getManifestPath(baseDir: string): string {
  return path.join(getSurlDir(baseDir), 'manifest.json');
}

/**
 * Get state file path
 */
export function getStatePath(baseDir: string): string {
  return path.join(getSurlDir(baseDir), 'state.json');
}

/**
 * Get cache directory path
 */
export function getCacheDir(baseDir: string): string {
  return path.join(getSurlDir(baseDir), 'cache');
}

/**
 * Get logs directory path
 */
export function getLogsDir(baseDir: string): string {
  return path.join(getSurlDir(baseDir), 'logs');
}
