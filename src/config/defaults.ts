import { CLIOptions } from '../types/index.js';

export const DEFAULT_USER_AGENT = 'Mozilla/5.0 (compatible; surl/1.0; +https://github.com/user/surl)';

export const BROWSER_USER_AGENT = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36';

export const DEFAULT_OPTIONS: CLIOptions = {
  directory: '',
  concurrency: 8,
  maxDepth: 0,
  recursive: false,
  assets: true,
  sameOrigin: true,
  externalAssets: false,
  rewrite: true,
  timeout: 30000,
  retries: 3,
  delay: 0,
  rateLimit: undefined,
  userAgent: DEFAULT_USER_AGENT,
  headers: [],
  cookies: [],
  include: [],
  exclude: [],
  extensions: [],
  maxFileSize: undefined,
  maxTotalSize: undefined,
  maxFiles: undefined,
  maxRedirects: 10,
  proxy: undefined,
  resume: false,
  force: false,
  update: false,
  dryRun: false,
  list: false,
  json: false,
  verbose: false,
  quiet: false,
  debug: false,
  noColor: false,
  ci: false,
  noRobots: false,
  sitemap: false,
  sitemapOnly: false,
  queryMode: 'ignore',
  preserveQuery: false,
  scanJs: false,
  insecure: false,
};

export const MANIFEST_VERSION = '1.0.0';
export const STATE_VERSION = '1.0.0';

// Retryable HTTP status codes
export const RETRYABLE_STATUS_CODES = [408, 429, 500, 502, 503, 504];

// Non-retryable (permanent) status codes
export const PERMANENT_ERROR_CODES = [400, 401, 403, 404, 405, 410, 451];

// Supported extensions for different resource types
export const HTML_EXTENSIONS = ['html', 'htm', 'xhtml'];
export const CSS_EXTENSIONS = ['css'];
export const JS_EXTENSIONS = ['js', 'mjs'];
export const IMAGE_EXTENSIONS = ['png', 'jpg', 'jpeg', 'gif', 'webp', 'avif', 'svg', 'ico', 'bmp'];
export const FONT_EXTENSIONS = ['woff', 'woff2', 'ttf', 'otf', 'eot'];
export const VIDEO_EXTENSIONS = ['mp4', 'webm', 'ogg'];
export const AUDIO_EXTENSIONS = ['mp3', 'wav', 'flac', 'ogg'];

// All supported extensions
export const ALL_EXTENSIONS = [
  ...HTML_EXTENSIONS,
  ...CSS_EXTENSIONS,
  ...JS_EXTENSIONS,
  ...IMAGE_EXTENSIONS,
  ...FONT_EXTENSIONS,
  ...VIDEO_EXTENSIONS,
  ...AUDIO_EXTENSIONS,
];
