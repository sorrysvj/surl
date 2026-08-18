import { readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import pLimit from 'p-limit';
import { Config, CrawlTarget, DownloadTask, DownloadResult, ProgressStats, ResourceType } from '../types/index.js';
import { Downloader } from '../downloader/downloader.js';
import { CacheManager } from '../cache/cache-manager.js';
import { CrawlQueue } from './queue.js';
import { UrlFilter } from './url-filter.js';
import { RobotsParser } from './robots.js';
import { parseSitemap, discoverSitemap } from './sitemap.js';
import { parseHtml, extractAssets, extractLinks } from '../parsers/html-parser.js';
import { parseCss } from '../parsers/css-parser.js';
import { parseJs, shouldScanJs } from '../parsers/js-parser.js';
import { rewriteHtml } from '../rewriting/html-rewriter.js';
import { rewriteCss } from '../rewriting/css-rewriter.js';
import { RewriteMap } from '../rewriting/url-rewriter.js';
import { urlToLocalPath, ensureDir } from '../utils/paths.js';
import { normalizeUrl, getResourceType, isSameOrigin } from '../utils/url.js';
import { logger } from '../terminal/logger.js';
import { startSpinner, stopSpinner, updateSpinner } from '../terminal/spinner.js';

export class Crawler {
  private config: Config;
  private downloader: Downloader;
  private cache: CacheManager;
  private queue: CrawlQueue;
  private urlFilter: UrlFilter;
  private robots: RobotsParser;
  private rewriteMap: RewriteMap;
  private limit: ReturnType<typeof pLimit>;
  private aborted = false;
  private stats: ProgressStats;
  private downloadedFiles: Map<string, string> = new Map(); // url -> localPath

  constructor(config: Config) {
    this.config = config;
    this.downloader = new Downloader(config);
    this.cache = new CacheManager(config.url, config.directory);
    this.queue = new CrawlQueue();
    this.urlFilter = new UrlFilter({
      baseUrl: config.url,
      sameOrigin: config.sameOrigin,
      externalAssets: config.externalAssets,
      include: config.include,
      exclude: config.exclude,
      extensions: config.extensions,
      maxDepth: config.maxDepth,
    });
    this.robots = new RobotsParser();
    this.limit = pLimit(config.concurrency);

    this.rewriteMap = {
      urlToPath: new Map(),
      baseUrl: config.url,
      baseDir: config.directory,
    };

    this.stats = {
      discovered: 0,
      queued: 0,
      downloading: 0,
      downloaded: 0,
      cached: 0,
      failed: 0,
      skipped: 0,
      totalBytes: 0,
      currentSpeed: 0,
      startTime: Date.now(),
    };
  }

  /**
   * Start crawling
   */
  async crawl(): Promise<ProgressStats> {
    const spinner = startSpinner('Initializing...');

    try {
      // Create output directory
      await ensureDir(this.config.directory);

      // Initialize cache
      const resumed = await this.cache.init(this.config.resume);
      if (resumed) {
        logger.info('Resuming previous crawl session');
      }

      // Load robots.txt unless disabled
      if (!this.config.noRobots) {
        updateSpinner('Loading robots.txt...');
        await this.robots.load(this.config.url);

        // Check crawl delay
        const crawlDelay = this.robots.getCrawlDelay();
        if (crawlDelay && crawlDelay > this.config.delay) {
          logger.info(`robots.txt specifies crawl delay: ${crawlDelay}ms`);
        }
      }

      // Load sitemap if enabled
      if (this.config.sitemap || this.config.sitemapOnly) {
        updateSpinner('Loading sitemap...');
        await this.loadSitemap();
      }

      // Add initial URL
      const normalizedUrl = normalizeUrl(this.config.url);
      this.queue.add({
        url: normalizedUrl,
        depth: 0,
        source: 'initial',
        type: 'html',
      });

      stopSpinner();

      // Start processing
      logger.info('Starting crawl...');
      await this.processQueue();

      // Rewrite files if enabled
      if (this.config.rewrite && !this.config.dryRun) {
        const rewriteSpinner = startSpinner('Rewriting links...');
        await this.rewriteFiles();
        stopSpinner();
      }

      // Save final state
      this.cache.complete();
      await this.cache.cleanup();

      return this.stats;
    } catch (error) {
      stopSpinner();
      this.cache.fail();
      await this.cache.cleanup();
      throw error;
    }
  }

  /**
   * Load sitemap URLs
   */
  private async loadSitemap(): Promise<void> {
    // Check robots.txt for sitemap URLs
    const sitemapsFromRobots = this.robots.getSitemaps();

    // Try to discover sitemap
    const discoveredSitemap = await discoverSitemap(this.config.url);

    const sitemapUrls = [...sitemapsFromRobots];
    if (discoveredSitemap && !sitemapUrls.includes(discoveredSitemap)) {
      sitemapUrls.push(discoveredSitemap);
    }

    for (const sitemapUrl of sitemapUrls) {
      const entries = await parseSitemap(sitemapUrl);
      logger.info(`Found ${entries.length} URLs in sitemap: ${sitemapUrl}`);

      for (const entry of entries) {
        if (this.urlFilter.shouldCrawl(entry.url, 0)) {
          this.queue.add({
            url: entry.url,
            depth: 0,
            source: 'sitemap',
            type: 'html',
          });
        }
      }
    }
  }

  /**
   * Process the crawl queue
   */
  private async processQueue(): Promise<void> {
    const promises: Promise<void>[] = [];

    while (!this.queue.isDone() && !this.aborted) {
      const target = this.queue.next();

      if (!target) {
        // Wait for in-flight requests
        if (promises.length > 0) {
          await Promise.race(promises);
          continue;
        }
        break;
      }

      // Check if allowed by robots.txt
      if (!this.config.noRobots) {
        const urlPath = new URL(target.url).pathname;
        if (!this.robots.isAllowed(urlPath)) {
          logger.verbose(`Blocked by robots.txt: ${target.url}`);
          this.queue.complete(target.url);
          this.stats.skipped++;
          continue;
        }
      }

      // Check limits
      if (this.config.maxFiles && this.stats.downloaded >= this.config.maxFiles) {
        logger.warn(`Max files limit reached (${this.config.maxFiles})`);
        break;
      }

      if (this.config.maxTotalSize && this.stats.totalBytes >= this.config.maxTotalSize) {
        logger.warn(`Max total size limit reached (${this.config.maxTotalSize})`);
        break;
      }

      // Process target
      const promise = this.limit(async () => {
        try {
          await this.processTarget(target);
        } finally {
          this.queue.complete(target.url);
          // Remove completed promise
          const index = promises.indexOf(promise);
          if (index > -1) {
            promises.splice(index, 1);
          }
        }
      });

      promises.push(promise);
      this.stats.queued = this.queue.size();
    }

    // Wait for remaining promises
    await Promise.all(promises);
  }

  /**
   * Process a single target
   */
  private async processTarget(target: CrawlTarget): Promise<void> {
    const localPath = urlToLocalPath(target.url, this.config.directory, {
      queryMode: this.config.queryMode,
      preserveQuery: this.config.preserveQuery,
    });

    // Dry run mode - just log
    if (this.config.dryRun) {
      logger.verbose(`Would download: ${target.url} -> ${localPath}`);
      this.stats.discovered++;
      return;
    }

    // List mode - just log
    if (this.config.list) {
      console.log(target.url);
      this.stats.discovered++;
      return;
    }

    this.stats.downloading++;

    const task: DownloadTask = {
      url: new URL(target.url),
      localPath,
      type: target.type,
      depth: target.depth,
      source: target.source,
      retryCount: 0,
    };

    // Check cache
    const cachedEntry = this.cache.getCachedEntry(target.url);
    let result: DownloadResult;

    if (cachedEntry && !this.config.force) {
      // Try conditional GET
      result = await this.downloader.conditionalDownload(
        task,
        cachedEntry.etag,
        cachedEntry.lastModified
      );
    } else {
      result = await this.downloader.download(task);
    }

    this.stats.downloading--;

    if (result.error) {
      this.stats.failed++;
      await this.cache.recordDownload(result, target.type, target.source);
      return;
    }

    if (result.cached) {
      this.stats.cached++;
    } else {
      this.stats.downloaded++;
      this.stats.totalBytes += result.size;
    }

    // Record in cache
    await this.cache.recordDownload(result, target.type, target.source);

    // Store for rewriting
    this.downloadedFiles.set(target.url, localPath);
    this.rewriteMap.urlToPath.set(target.url, localPath);

    // Parse and discover more resources
    if (this.config.assets || this.config.recursive) {
      await this.discoverResources(target, result);
    }

    this.updateProgress();
  }

  /**
   * Discover resources from downloaded content
   */
  private async discoverResources(target: CrawlTarget, result: DownloadResult): Promise<void> {
    const contentType = result.contentType.toLowerCase();

    try {
      const content = await readFile(result.localPath, 'utf-8');

      if (contentType.includes('html')) {
        await this.discoverFromHtml(target, content);
      } else if (contentType.includes('css')) {
        await this.discoverFromCss(target, content);
      } else if (this.config.scanJs && (contentType.includes('javascript') || contentType.includes('ecmascript'))) {
        await this.discoverFromJs(target, content);
      }
    } catch {
      // Failed to read file - might be binary
    }
  }

  /**
   * Discover resources from HTML
   */
  private async discoverFromHtml(target: CrawlTarget, html: string): Promise<void> {
    const resources = parseHtml(html, target.url);
    const nextDepth = target.depth + 1;

    for (const resource of resources) {
      const isAsset = resource.type !== 'html' || resource.tag !== 'a';
      const isLink = resource.type === 'html' && resource.tag === 'a';

      // Skip links if not recursive or sitemap-only mode
      if (isLink && (this.config.sitemapOnly || !this.config.recursive)) {
        continue;
      }

      // Skip assets if disabled
      if (isAsset && !this.config.assets) {
        continue;
      }

      // Check URL filter
      if (!this.urlFilter.shouldCrawl(resource.url, isLink ? nextDepth : target.depth, isAsset)) {
        continue;
      }

      const added = this.queue.add({
        url: resource.url,
        depth: isLink ? nextDepth : target.depth,
        source: target.url,
        type: resource.type,
      });

      if (added) {
        this.stats.discovered++;
      }
    }
  }

  /**
   * Discover resources from CSS
   */
  private async discoverFromCss(target: CrawlTarget, css: string): Promise<void> {
    const resources = parseCss(css, target.url);

    for (const resource of resources) {
      if (!this.urlFilter.shouldCrawl(resource.url, target.depth, true)) {
        continue;
      }

      const added = this.queue.add({
        url: resource.url,
        depth: target.depth,
        source: target.url,
        type: resource.type,
      });

      if (added) {
        this.stats.discovered++;
      }
    }
  }

  /**
   * Discover resources from JavaScript (experimental)
   */
  private async discoverFromJs(target: CrawlTarget, js: string): Promise<void> {
    if (!shouldScanJs(js)) {
      return;
    }

    const resources = parseJs(js, target.url);

    for (const resource of resources) {
      if (!this.urlFilter.shouldCrawl(resource.url, target.depth, true)) {
        continue;
      }

      const added = this.queue.add({
        url: resource.url,
        depth: target.depth,
        source: target.url,
        type: resource.type,
      });

      if (added) {
        this.stats.discovered++;
      }
    }
  }

  /**
   * Rewrite URLs in downloaded files
   */
  private async rewriteFiles(): Promise<void> {
    for (const [url, localPath] of this.downloadedFiles) {
      try {
        const content = await readFile(localPath, 'utf-8');
        const type = getResourceType(url);
        let rewritten: string;

        if (type === 'html') {
          rewritten = rewriteHtml(content, localPath, this.rewriteMap);
        } else if (type === 'css') {
          rewritten = rewriteCss(content, localPath, this.rewriteMap);
        } else {
          continue;
        }

        if (rewritten !== content) {
          await writeFile(localPath, rewritten);
          logger.verbose(`Rewrote: ${localPath}`);
        }
      } catch {
        // Skip binary files
      }
    }
  }

  /**
   * Update progress display
   */
  private updateProgress(): void {
    const elapsed = (Date.now() - this.stats.startTime) / 1000;
    this.stats.currentSpeed = elapsed > 0 ? this.stats.totalBytes / elapsed : 0;
  }

  /**
   * Stop crawling
   */
  abort(): void {
    this.aborted = true;
    this.downloader.abort();
    this.cache.interrupt();
  }

  /**
   * Get current stats
   */
  getStats(): ProgressStats {
    return { ...this.stats };
  }

  /**
   * Get list of discovered URLs
   */
  getDiscoveredUrls(): string[] {
    return this.queue.getSeenUrls();
  }
}
