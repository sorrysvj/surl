import { readFile, stat } from 'node:fs/promises';
import path from 'node:path';
import { Config, CLIOptions } from '../types/index.js';
import { DEFAULT_OPTIONS, BROWSER_USER_AGENT } from './defaults.js';
import { getHostname } from '../utils/url.js';

const CONFIG_FILE_NAMES = ['surl.config.json', '.surlrc.json', '.surlrc'];

interface ConfigFile {
  concurrency?: number;
  timeout?: number;
  maxDepth?: number;
  retries?: number;
  delay?: number;
  sameOrigin?: boolean;
  rewrite?: boolean;
  userAgent?: string;
  headers?: string[];
  include?: string[];
  exclude?: string[];
  extensions?: string[];
}

/**
 * Load configuration from file if exists
 */
async function loadConfigFile(dir: string): Promise<ConfigFile | null> {
  for (const filename of CONFIG_FILE_NAMES) {
    const filepath = path.join(dir, filename);
    try {
      const stats = await stat(filepath);
      if (stats.isFile()) {
        const content = await readFile(filepath, 'utf-8');
        return JSON.parse(content) as ConfigFile;
      }
    } catch {
      // File doesn't exist or can't be read, continue
    }
  }
  return null;
}

/**
 * Load configuration from environment variables
 */
function loadEnvConfig(): Partial<CLIOptions> {
  const config: Partial<CLIOptions> = {};

  const env = process.env;

  if (env['SURL_CONCURRENCY']) {
    const val = parseInt(env['SURL_CONCURRENCY'], 10);
    if (!isNaN(val)) config.concurrency = val;
  }

  if (env['SURL_TIMEOUT']) {
    const val = parseInt(env['SURL_TIMEOUT'], 10);
    if (!isNaN(val)) config.timeout = val;
  }

  if (env['SURL_USER_AGENT']) {
    config.userAgent = env['SURL_USER_AGENT'];
  }

  if (env['SURL_PROXY']) {
    config.proxy = env['SURL_PROXY'];
  }

  if (env['SURL_OUTPUT']) {
    config.directory = env['SURL_OUTPUT'];
  }

  return config;
}

/**
 * Parse rate limit string (e.g., "2mb", "500kb")
 */
export function parseRateLimit(value: string): number | undefined {
  const match = value.toLowerCase().match(/^(\d+(?:\.\d+)?)(kb|mb|gb)?$/);
  if (!match) return undefined;

  let bytes = parseFloat(match[1] ?? '0');
  const unit = match[2];

  switch (unit) {
    case 'kb':
      bytes *= 1024;
      break;
    case 'mb':
      bytes *= 1024 * 1024;
      break;
    case 'gb':
      bytes *= 1024 * 1024 * 1024;
      break;
  }

  return Math.floor(bytes);
}

/**
 * Parse file size string (e.g., "50mb", "1gb")
 */
export function parseFileSize(value: string): number | undefined {
  return parseRateLimit(value);
}

/**
 * Resolve final configuration from all sources
 */
export async function resolveConfig(
  url: string,
  cliOptions: Partial<CLIOptions>
): Promise<Config> {
  // Start with defaults
  let config: CLIOptions = { ...DEFAULT_OPTIONS };

  // Load config file
  const cwd = process.cwd();
  const fileConfig = await loadConfigFile(cwd);
  if (fileConfig) {
    config = mergeConfig(config, fileConfig);
  }

  // Load environment config
  const envConfig = loadEnvConfig();
  config = { ...config, ...envConfig };

  // Apply CLI options (highest priority)
  config = { ...config, ...cliOptions };

  // Handle special user-agent values
  if (config.userAgent === 'browser') {
    config.userAgent = BROWSER_USER_AGENT;
  }

  // Determine output directory if not specified
  if (!config.directory) {
    const hostname = getHostname(url);
    config.directory = hostname || 'download';
  }

  // Resolve to absolute path
  config.directory = path.resolve(config.directory);

  // Enable recursive mode if maxDepth > 0
  if (config.maxDepth > 0) {
    config.recursive = true;
  }

  return {
    ...config,
    url,
  };
}

/**
 * Merge config file options into CLIOptions
 */
function mergeConfig(base: CLIOptions, file: ConfigFile): CLIOptions {
  return {
    ...base,
    ...(file.concurrency !== undefined && { concurrency: file.concurrency }),
    ...(file.timeout !== undefined && { timeout: file.timeout }),
    ...(file.maxDepth !== undefined && { maxDepth: file.maxDepth }),
    ...(file.retries !== undefined && { retries: file.retries }),
    ...(file.delay !== undefined && { delay: file.delay }),
    ...(file.sameOrigin !== undefined && { sameOrigin: file.sameOrigin }),
    ...(file.rewrite !== undefined && { rewrite: file.rewrite }),
    ...(file.userAgent !== undefined && { userAgent: file.userAgent }),
    ...(file.headers !== undefined && { headers: file.headers }),
    ...(file.include !== undefined && { include: file.include }),
    ...(file.exclude !== undefined && { exclude: file.exclude }),
    ...(file.extensions !== undefined && { extensions: file.extensions }),
  };
}

/**
 * Generate a sample config file
 */
export function generateSampleConfig(): string {
  const sample = {
    concurrency: DEFAULT_OPTIONS.concurrency,
    timeout: DEFAULT_OPTIONS.timeout,
    maxDepth: 5,
    retries: DEFAULT_OPTIONS.retries,
    sameOrigin: DEFAULT_OPTIONS.sameOrigin,
    rewrite: DEFAULT_OPTIONS.rewrite,
    include: [],
    exclude: ['/admin/*', '/api/*'],
  };

  return JSON.stringify(sample, null, 2);
}
