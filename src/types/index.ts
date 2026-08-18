// Resource types
export type ResourceType =
  | 'html'
  | 'css'
  | 'javascript'
  | 'image'
  | 'font'
  | 'video'
  | 'audio'
  | 'document'
  | 'manifest'
  | 'other';

export type DownloadStatus =
  | 'pending'
  | 'queued'
  | 'downloading'
  | 'downloaded'
  | 'cached'
  | 'failed'
  | 'skipped';

export interface CrawlTarget {
  url: string;
  depth: number;
  source: string;
  type: ResourceType;
}

export interface DownloadTask {
  url: URL;
  localPath: string;
  type: ResourceType;
  depth: number;
  source: string;
  retryCount: number;
}

export interface DownloadResult {
  url: string;
  localPath: string;
  status: number;
  size: number;
  contentType: string;
  cached: boolean;
  etag?: string;
  lastModified?: string;
  error?: string;
}

export interface ResourceInfo {
  url: string;
  localPath: string;
  type: ResourceType;
  status: DownloadStatus;
  statusCode?: number;
  contentType?: string;
  contentLength?: number;
  etag?: string;
  lastModified?: string;
  sha256?: string;
  downloadedAt?: string;
  source?: string;
  error?: string;
}

export interface ManifestEntry {
  url: string;
  localPath: string;
  type: ResourceType;
  statusCode: number;
  contentType: string;
  contentLength: number;
  etag?: string;
  lastModified?: string;
  sha256: string;
  downloadedAt: string;
  source: string;
}

export interface Manifest {
  version: string;
  createdAt: string;
  updatedAt: string;
  baseUrl: string;
  outputDirectory: string;
  entries: Record<string, ManifestEntry>;
}

export interface CrawlState {
  version: string;
  url: string;
  outputDirectory: string;
  startedAt: string;
  lastUpdatedAt: string;
  status: 'running' | 'completed' | 'interrupted' | 'failed';
  discovered: number;
  downloaded: number;
  cached: number;
  failed: number;
  skipped: number;
  totalBytes: number;
  queue: string[];
  visited: string[];
  failed_urls: Array<{ url: string; error: string; attempts: number }>;
}

export interface CLIOptions {
  directory: string;
  concurrency: number;
  maxDepth: number;
  recursive: boolean;
  assets: boolean;
  sameOrigin: boolean;
  externalAssets: boolean;
  rewrite: boolean;
  timeout: number;
  retries: number;
  delay: number;
  rateLimit?: string;
  userAgent: string;
  headers: string[];
  cookies: string[];
  include: string[];
  exclude: string[];
  extensions: string[];
  maxFileSize?: number;
  maxTotalSize?: number;
  maxFiles?: number;
  maxRedirects: number;
  proxy?: string;
  resume: boolean;
  force: boolean;
  update: boolean;
  dryRun: boolean;
  list: boolean;
  json: boolean;
  verbose: boolean;
  quiet: boolean;
  debug: boolean;
  noColor: boolean;
  ci: boolean;
  noRobots: boolean;
  sitemap: boolean;
  sitemapOnly: boolean;
  queryMode: 'ignore' | 'preserve' | 'hash';
  preserveQuery: boolean;
  scanJs: boolean;
  insecure: boolean;
}

export interface Config extends CLIOptions {
  url: string;
}

export interface ParsedResource {
  url: string;
  type: ResourceType;
  tag?: string;
  attribute?: string;
}

export interface HttpResponse {
  status: number;
  statusText: string;
  headers: Headers;
  body: ReadableStream<Uint8Array> | null;
  url: string;
  redirected: boolean;
}

export interface CacheEntry {
  url: string;
  etag?: string;
  lastModified?: string;
  sha256: string;
  localPath: string;
  cachedAt: string;
}

export interface ProgressStats {
  discovered: number;
  queued: number;
  downloading: number;
  downloaded: number;
  cached: number;
  failed: number;
  skipped: number;
  totalBytes: number;
  currentSpeed: number;
  startTime: number;
}

export interface InspectResult {
  totalFiles: number;
  htmlFiles: number;
  cssFiles: number;
  jsFiles: number;
  imageFiles: number;
  fontFiles: number;
  otherFiles: number;
  totalSize: number;
  brokenReferences: string[];
  externalReferences: string[];
}

export interface CheckResult {
  brokenLinks: Array<{ file: string; link: string }>;
  missingAssets: Array<{ file: string; asset: string }>;
  externalReferences: Array<{ file: string; url: string }>;
  invalidPaths: string[];
}

export interface DoctorResult {
  nodeVersion: { ok: boolean; version: string; required: string };
  filesystem: { ok: boolean; message: string };
  network: { ok: boolean; message: string };
  tls: { ok: boolean; message: string };
  config: { ok: boolean; message: string };
}

export const EXIT_CODES = {
  SUCCESS: 0 as const,
  GENERAL_ERROR: 1 as const,
  INVALID_ARGUMENTS: 2 as const,
  NETWORK_ERROR: 3 as const,
  FILESYSTEM_ERROR: 4 as const,
  PARTIAL_FAILURE: 5 as const,
  LIMITS_EXCEEDED: 6 as const,
};

export type ExitCode = 0 | 1 | 2 | 3 | 4 | 5 | 6;
