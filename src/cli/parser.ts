import { Command } from 'commander';
import { CLIOptions } from '../types/index.js';
import { DEFAULT_OPTIONS, BROWSER_USER_AGENT } from '../config/defaults.js';
import { parseFileSize } from '../config/config.js';

const VERSION = '1.0.0';

export function createParser(): Command {
  const program = new Command();

  program
    .name('surl')
    .description('Fast, modern CLI tool for local website mirroring')
    .version(VERSION, '-V, --version', 'Show version number')
    .argument('[url]', 'URL of the website to mirror')
    .option('-d, --directory <path>', 'Output directory', DEFAULT_OPTIONS.directory)
    .option('-o, --output <path>', 'Alias for --directory')
    .option('-c, --concurrency <number>', 'Number of concurrent downloads', (v) => parseInt(v, 10), DEFAULT_OPTIONS.concurrency)
    .option('--max-depth <number>', 'Maximum crawl depth', (v) => parseInt(v, 10), DEFAULT_OPTIONS.maxDepth)
    .option('--depth <number>', 'Alias for --max-depth', (v) => parseInt(v, 10))
    .option('-r, --recursive', 'Enable recursive crawling', DEFAULT_OPTIONS.recursive)
    .option('--assets', 'Download assets (CSS, JS, images, etc.)', DEFAULT_OPTIONS.assets)
    .option('--no-assets', 'Skip downloading assets')
    .option('--same-origin', 'Only crawl same-origin URLs', DEFAULT_OPTIONS.sameOrigin)
    .option('--external-assets', 'Download external assets (CDN, etc.)', DEFAULT_OPTIONS.externalAssets)
    .option('--rewrite', 'Rewrite URLs in downloaded files', DEFAULT_OPTIONS.rewrite)
    .option('--no-rewrite', 'Skip URL rewriting')
    .option('-t, --timeout <ms>', 'HTTP timeout in milliseconds', (v) => parseInt(v, 10), DEFAULT_OPTIONS.timeout)
    .option('--retries <number>', 'Number of retry attempts', (v) => parseInt(v, 10), DEFAULT_OPTIONS.retries)
    .option('--delay <ms>', 'Delay between requests in milliseconds', (v) => parseInt(v, 10), DEFAULT_OPTIONS.delay)
    .option('--rate-limit <rate>', 'Rate limit (e.g., 2mb, 500kb)')
    .option('-u, --user-agent <ua>', 'User-Agent string (use "browser" for browser-like UA)', DEFAULT_OPTIONS.userAgent)
    .option('-H, --header <header>', 'Custom HTTP header (can be used multiple times)', collect, [])
    .option('--cookie <cookie>', 'Cookie to send with requests (can be used multiple times)', collect, [])
    .option('--include <pattern>', 'Include URL pattern (can be used multiple times)', collect, [])
    .option('--exclude <pattern>', 'Exclude URL pattern (can be used multiple times)', collect, [])
    .option('--extensions <exts>', 'Only download files with these extensions (comma-separated)')
    .option('--max-file-size <size>', 'Maximum file size to download (e.g., 50mb)')
    .option('--max-total-size <size>', 'Maximum total download size (e.g., 1gb)')
    .option('--max-files <number>', 'Maximum number of files to download', (v) => parseInt(v, 10))
    .option('--max-redirects <number>', 'Maximum number of redirects to follow', (v) => parseInt(v, 10), DEFAULT_OPTIONS.maxRedirects)
    .option('--proxy <url>', 'HTTP/HTTPS proxy URL')
    .option('--resume', 'Resume a previous crawl', DEFAULT_OPTIONS.resume)
    .option('--force', 'Force re-download of all files', DEFAULT_OPTIONS.force)
    .option('--update', 'Update existing mirror with changes', DEFAULT_OPTIONS.update)
    .option('--dry-run', 'Show what would be downloaded without downloading', DEFAULT_OPTIONS.dryRun)
    .option('--list', 'List discovered URLs without downloading', DEFAULT_OPTIONS.list)
    .option('--json', 'Output results as JSON', DEFAULT_OPTIONS.json)
    .option('-v, --verbose', 'Verbose output', DEFAULT_OPTIONS.verbose)
    .option('-q, --quiet', 'Quiet mode (only errors)', DEFAULT_OPTIONS.quiet)
    .option('--debug', 'Debug mode (very verbose)', DEFAULT_OPTIONS.debug)
    .option('--no-color', 'Disable colored output')
    .option('--ci', 'CI mode (no interactive elements)')
    .option('--no-robots', 'Ignore robots.txt', DEFAULT_OPTIONS.noRobots)
    .option('--sitemap', 'Use sitemap.xml for URL discovery', DEFAULT_OPTIONS.sitemap)
    .option('--sitemap-only', 'Only use sitemap.xml (no link following)', DEFAULT_OPTIONS.sitemapOnly)
    .option('--query-mode <mode>', 'Query string handling: ignore, preserve, hash', DEFAULT_OPTIONS.queryMode)
    .option('--preserve-query', 'Preserve query strings in filenames', DEFAULT_OPTIONS.preserveQuery)
    .option('--scan-js', 'Scan JavaScript for static URLs (experimental)', DEFAULT_OPTIONS.scanJs)
    .option('--insecure', 'Allow insecure TLS connections', DEFAULT_OPTIONS.insecure)
    .helpOption('-h, --help', 'Show help')
    .addHelpText('after', `

Examples:
  $ surl https://example.com
  $ surl https://example.com -d ./website
  $ surl https://example.com --recursive --max-depth 5
  $ surl https://example.com --concurrency 16
  $ surl https://example.com --dry-run
  $ surl https://example.com --update
  $ surl https://example.com --resume
  $ surl https://example.com --external-assets
  $ surl https://example.com --include "/docs/*"
  $ surl https://example.com --exclude "/admin/*"
  $ surl https://example.com --json

Commands:
  serve <directory>    Start a local HTTP server
  inspect <directory>  Show information about a mirrored site
  check <directory>    Verify a mirrored site
  clean <directory>    Remove surl metadata
  doctor               Check system requirements
  config               Show or initialize configuration
`);

  return program;
}

/**
 * Parse command options into CLIOptions
 */
export function parseOptions(program: Command): { url: string | undefined; options: Partial<CLIOptions> } {
  const args = program.args;
  const opts = program.opts();

  // Get URL from first argument
  const url = args[0];

  // Build options object
  const options: Partial<CLIOptions> = {
    directory: opts['output'] ?? opts['directory'] ?? '',
    concurrency: opts['concurrency'],
    maxDepth: opts['depth'] ?? opts['maxDepth'],
    recursive: opts['recursive'],
    assets: opts['assets'],
    sameOrigin: opts['sameOrigin'],
    externalAssets: opts['externalAssets'],
    rewrite: opts['rewrite'],
    timeout: opts['timeout'],
    retries: opts['retries'],
    delay: opts['delay'],
    rateLimit: opts['rateLimit'],
    userAgent: opts['userAgent'],
    headers: opts['header'] ?? [],
    cookies: opts['cookie'] ?? [],
    include: opts['include'] ?? [],
    exclude: opts['exclude'] ?? [],
    extensions: opts['extensions'] ? opts['extensions'].split(',').map((e: string) => e.trim()) : [],
    maxFileSize: opts['maxFileSize'] ? parseFileSize(opts['maxFileSize']) : undefined,
    maxTotalSize: opts['maxTotalSize'] ? parseFileSize(opts['maxTotalSize']) : undefined,
    maxFiles: opts['maxFiles'],
    maxRedirects: opts['maxRedirects'],
    proxy: opts['proxy'],
    resume: opts['resume'],
    force: opts['force'],
    update: opts['update'],
    dryRun: opts['dryRun'],
    list: opts['list'],
    json: opts['json'],
    verbose: opts['verbose'],
    quiet: opts['quiet'],
    debug: opts['debug'],
    noColor: opts['color'] === false,
    ci: opts['ci'],
    noRobots: opts['robots'] === false,
    sitemap: opts['sitemap'],
    sitemapOnly: opts['sitemapOnly'],
    queryMode: opts['queryMode'] as 'ignore' | 'preserve' | 'hash' | undefined,
    preserveQuery: opts['preserveQuery'],
    scanJs: opts['scanJs'],
    insecure: opts['insecure'],
  };

  return { url, options };
}

/**
 * Collect multiple option values
 */
function collect(value: string, previous: string[]): string[] {
  return [...previous, value];
}
