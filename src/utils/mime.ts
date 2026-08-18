import { ResourceType } from '../types/index.js';

// MIME type to extension mapping
const MIME_TO_EXT: Record<string, string> = {
  'text/html': '.html',
  'text/css': '.css',
  'text/javascript': '.js',
  'application/javascript': '.js',
  'application/x-javascript': '.js',
  'text/ecmascript': '.js',
  'application/ecmascript': '.js',
  'image/png': '.png',
  'image/jpeg': '.jpg',
  'image/gif': '.gif',
  'image/webp': '.webp',
  'image/avif': '.avif',
  'image/svg+xml': '.svg',
  'image/x-icon': '.ico',
  'image/vnd.microsoft.icon': '.ico',
  'image/bmp': '.bmp',
  'font/woff': '.woff',
  'font/woff2': '.woff2',
  'font/ttf': '.ttf',
  'font/otf': '.otf',
  'application/font-woff': '.woff',
  'application/font-woff2': '.woff2',
  'application/x-font-ttf': '.ttf',
  'application/x-font-otf': '.otf',
  'application/vnd.ms-fontobject': '.eot',
  'video/mp4': '.mp4',
  'video/webm': '.webm',
  'video/ogg': '.ogg',
  'audio/mpeg': '.mp3',
  'audio/wav': '.wav',
  'audio/ogg': '.ogg',
  'audio/flac': '.flac',
  'application/pdf': '.pdf',
  'application/json': '.json',
  'application/manifest+json': '.webmanifest',
  'application/xml': '.xml',
  'text/xml': '.xml',
  'text/plain': '.txt',
};

// Extension to MIME type mapping
const EXT_TO_MIME: Record<string, string> = {
  '.html': 'text/html',
  '.htm': 'text/html',
  '.xhtml': 'application/xhtml+xml',
  '.css': 'text/css',
  '.js': 'application/javascript',
  '.mjs': 'application/javascript',
  '.json': 'application/json',
  '.xml': 'application/xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.webp': 'image/webp',
  '.avif': 'image/avif',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.bmp': 'image/bmp',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.ttf': 'font/ttf',
  '.otf': 'font/otf',
  '.eot': 'application/vnd.ms-fontobject',
  '.mp4': 'video/mp4',
  '.webm': 'video/webm',
  '.ogg': 'video/ogg',
  '.mp3': 'audio/mpeg',
  '.wav': 'audio/wav',
  '.flac': 'audio/flac',
  '.pdf': 'application/pdf',
  '.txt': 'text/plain',
  '.webmanifest': 'application/manifest+json',
};

// Extension to ResourceType mapping
const EXT_TO_TYPE: Record<string, ResourceType> = {
  '.html': 'html',
  '.htm': 'html',
  '.xhtml': 'html',
  '.css': 'css',
  '.js': 'javascript',
  '.mjs': 'javascript',
  '.ts': 'javascript',
  '.jsx': 'javascript',
  '.tsx': 'javascript',
  '.png': 'image',
  '.jpg': 'image',
  '.jpeg': 'image',
  '.gif': 'image',
  '.webp': 'image',
  '.avif': 'image',
  '.svg': 'image',
  '.ico': 'image',
  '.bmp': 'image',
  '.woff': 'font',
  '.woff2': 'font',
  '.ttf': 'font',
  '.otf': 'font',
  '.eot': 'font',
  '.mp4': 'video',
  '.webm': 'video',
  '.ogg': 'video',
  '.mp3': 'audio',
  '.wav': 'audio',
  '.flac': 'audio',
  '.pdf': 'document',
  '.json': 'manifest',
  '.webmanifest': 'manifest',
};

/**
 * Get file extension from MIME type
 */
export function mimeToExtension(mimeType: string): string {
  // Remove charset and other params
  const baseMime = mimeType.split(';')[0]?.trim().toLowerCase() ?? '';
  return MIME_TO_EXT[baseMime] ?? '';
}

/**
 * Get MIME type from file extension
 */
export function extensionToMime(extension: string): string {
  const ext = extension.startsWith('.') ? extension.toLowerCase() : `.${extension.toLowerCase()}`;
  return EXT_TO_MIME[ext] ?? 'application/octet-stream';
}

/**
 * Get ResourceType from file extension
 */
export function extensionToType(extension: string): ResourceType {
  const ext = extension.startsWith('.') ? extension.toLowerCase() : `.${extension.toLowerCase()}`;
  return EXT_TO_TYPE[ext] ?? 'other';
}

/**
 * Get ResourceType from MIME type
 */
export function mimeToType(mimeType: string): ResourceType {
  const baseMime = mimeType.split(';')[0]?.trim().toLowerCase() ?? '';

  if (baseMime.includes('html')) return 'html';
  if (baseMime.includes('css')) return 'css';
  if (baseMime.includes('javascript') || baseMime.includes('ecmascript')) return 'javascript';
  if (baseMime.startsWith('image/')) return 'image';
  if (baseMime.startsWith('font/') || baseMime.includes('font')) return 'font';
  if (baseMime.startsWith('video/')) return 'video';
  if (baseMime.startsWith('audio/')) return 'audio';
  if (baseMime.includes('pdf')) return 'document';
  if (baseMime.includes('manifest') || baseMime.includes('json')) return 'manifest';

  return 'other';
}

/**
 * Check if content type is textual (should be processed for links)
 */
export function isTextContent(mimeType: string): boolean {
  const baseMime = mimeType.split(';')[0]?.trim().toLowerCase() ?? '';
  return (
    baseMime.startsWith('text/') ||
    baseMime.includes('javascript') ||
    baseMime.includes('json') ||
    baseMime.includes('xml') ||
    baseMime.includes('+xml') ||
    baseMime.includes('+json')
  );
}

/**
 * Check if extension is supported for processing
 */
export function isSupportedExtension(extension: string): boolean {
  const ext = extension.startsWith('.') ? extension.toLowerCase() : `.${extension.toLowerCase()}`;
  return ext in EXT_TO_TYPE;
}
