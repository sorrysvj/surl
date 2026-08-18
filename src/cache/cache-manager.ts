import { Manifest, ManifestEntry, CrawlState, DownloadResult, ResourceType } from '../types/index.js';
import {
  createManifest,
  loadManifest,
  saveManifest,
  addManifestEntry,
  getManifestEntry,
  hasManifestEntry,
} from './manifest.js';
import {
  createCrawlState,
  loadCrawlState,
  saveCrawlState,
  updateCrawlState,
  markVisited,
  addToQueue,
  markFailed,
} from './state.js';
import { hashFile } from '../utils/hash.js';

export class CacheManager {
  private manifest: Manifest;
  private state: CrawlState;
  private baseDir: string;
  private saveInterval: NodeJS.Timeout | null = null;
  private dirty = false;

  constructor(baseUrl: string, baseDir: string) {
    this.baseDir = baseDir;
    this.manifest = createManifest(baseUrl, baseDir);
    this.state = createCrawlState(baseUrl, baseDir);
  }

  /**
   * Initialize cache manager, optionally loading existing state
   */
  async init(resume: boolean = false): Promise<boolean> {
    if (resume) {
      const existingManifest = await loadManifest(this.baseDir);
      const existingState = await loadCrawlState(this.baseDir);

      if (existingManifest && existingState) {
        this.manifest = existingManifest;
        this.state = existingState;
        this.state.status = 'running';
        return true;
      }
    }

    // Start auto-save
    this.startAutoSave();

    return false;
  }

  /**
   * Start periodic auto-save
   */
  private startAutoSave(): void {
    this.saveInterval = setInterval(async () => {
      if (this.dirty) {
        await this.save();
      }
    }, 5000);
  }

  /**
   * Stop auto-save
   */
  stopAutoSave(): void {
    if (this.saveInterval) {
      clearInterval(this.saveInterval);
      this.saveInterval = null;
    }
  }

  /**
   * Save current state to disk
   */
  async save(): Promise<void> {
    await Promise.all([
      saveManifest(this.manifest, this.baseDir),
      saveCrawlState(this.state, this.baseDir),
    ]);
    this.dirty = false;
  }

  /**
   * Record a successful download
   */
  async recordDownload(
    result: DownloadResult,
    type: ResourceType,
    source: string
  ): Promise<void> {
    if (result.error) {
      markFailed(this.state, result.url, result.error, 1);
      this.dirty = true;
      return;
    }

    // Calculate file hash
    let sha256 = '';
    try {
      sha256 = await hashFile(result.localPath);
    } catch {
      // File might not exist yet
    }

    const entry: ManifestEntry = {
      url: result.url,
      localPath: result.localPath,
      type,
      statusCode: result.status,
      contentType: result.contentType,
      contentLength: result.size,
      etag: result.etag,
      lastModified: result.lastModified,
      sha256,
      downloadedAt: new Date().toISOString(),
      source,
    };

    addManifestEntry(this.manifest, entry);
    markVisited(this.state, result.url);

    if (result.cached) {
      this.state.cached++;
    } else {
      this.state.downloaded++;
      this.state.totalBytes += result.size;
    }

    this.dirty = true;
  }

  /**
   * Check if URL was already downloaded
   */
  isDownloaded(url: string): boolean {
    return hasManifestEntry(this.manifest, url);
  }

  /**
   * Get cached entry info
   */
  getCachedEntry(url: string): ManifestEntry | undefined {
    return getManifestEntry(this.manifest, url);
  }

  /**
   * Check if URL was visited
   */
  isVisited(url: string): boolean {
    return this.state.visited.includes(url);
  }

  /**
   * Add URL to queue
   */
  enqueue(url: string): void {
    addToQueue(this.state, url);
    this.dirty = true;
  }

  /**
   * Get next URL from queue
   */
  dequeue(): string | undefined {
    const url = this.state.queue.shift();
    if (url) {
      this.dirty = true;
    }
    return url;
  }

  /**
   * Check if queue is empty
   */
  isQueueEmpty(): boolean {
    return this.state.queue.length === 0;
  }

  /**
   * Get queue size
   */
  getQueueSize(): number {
    return this.state.queue.length;
  }

  /**
   * Mark as completed
   */
  complete(): void {
    updateCrawlState(this.state, { status: 'completed' });
    this.dirty = true;
  }

  /**
   * Mark as interrupted
   */
  interrupt(): void {
    updateCrawlState(this.state, { status: 'interrupted' });
    this.dirty = true;
  }

  /**
   * Mark as failed
   */
  fail(): void {
    updateCrawlState(this.state, { status: 'failed' });
    this.dirty = true;
  }

  /**
   * Skip a URL
   */
  skip(url: string): void {
    markVisited(this.state, url);
    this.state.skipped++;
    this.dirty = true;
  }

  /**
   * Get current statistics
   */
  getStats(): {
    discovered: number;
    downloaded: number;
    cached: number;
    failed: number;
    skipped: number;
    queued: number;
    totalBytes: number;
  } {
    return {
      discovered: this.state.discovered,
      downloaded: this.state.downloaded,
      cached: this.state.cached,
      failed: this.state.failed,
      skipped: this.state.skipped,
      queued: this.state.queue.length,
      totalBytes: this.state.totalBytes,
    };
  }

  /**
   * Get the manifest
   */
  getManifest(): Manifest {
    return this.manifest;
  }

  /**
   * Get the crawl state
   */
  getState(): CrawlState {
    return this.state;
  }

  /**
   * Cleanup
   */
  async cleanup(): Promise<void> {
    this.stopAutoSave();
    await this.save();
  }
}
