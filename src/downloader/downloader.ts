import { writeFile, rename, unlink } from 'node:fs/promises';
import path from 'node:path';
import { createHttpClient } from './http-client.js';
import { withRetry, isRetryableStatus, isPermanentError, sleep } from './retry.js';
import { DownloadTask, DownloadResult, Config } from '../types/index.js';
import { ensureDir } from '../utils/paths.js';
import { hashBuffer } from '../utils/hash.js';
import { logger } from '../terminal/logger.js';

export interface DownloaderOptions {
  timeout: number;
  retries: number;
  delay: number;
  userAgent: string;
  headers: Record<string, string>;
  cookies: string[];
  proxy?: string;
  insecure: boolean;
  maxFileSize?: number;
}

export class Downloader {
  private httpClient;
  private options: DownloaderOptions;
  private abortController: AbortController;

  constructor(config: Config) {
    this.options = {
      timeout: config.timeout,
      retries: config.retries,
      delay: config.delay,
      userAgent: config.userAgent,
      headers: this.parseHeaders(config.headers),
      cookies: config.cookies,
      proxy: config.proxy,
      insecure: config.insecure,
      maxFileSize: config.maxFileSize,
    };

    this.httpClient = createHttpClient({
      timeout: this.options.timeout,
      userAgent: this.options.userAgent,
      headers: this.options.headers,
      proxy: this.options.proxy,
      insecure: this.options.insecure,
    });

    this.abortController = new AbortController();
  }

  private parseHeaders(headers: string[]): Record<string, string> {
    const result: Record<string, string> = {};

    for (const header of headers) {
      const colonIndex = header.indexOf(':');
      if (colonIndex > 0) {
        const key = header.slice(0, colonIndex).trim();
        const value = header.slice(colonIndex + 1).trim();
        result[key] = value;
      }
    }

    // Add cookies as header
    if (this.options.cookies.length > 0) {
      result['Cookie'] = this.options.cookies.join('; ');
    }

    return result;
  }

  async download(task: DownloadTask): Promise<DownloadResult> {
    const url = task.url.href;
    const tempPath = task.localPath + '.tmp';

    // Add delay if configured
    if (this.options.delay > 0) {
      await sleep(this.options.delay);
    }

    try {
      const result = await withRetry(
        async () => {
          const { response, buffer } = await this.httpClient.download(
            url,
            this.abortController.signal
          );

          // Check status
          if (!response.status.toString().startsWith('2')) {
            if (isPermanentError(response.status)) {
              throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }
            if (isRetryableStatus(response.status)) {
              throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
          }

          // Check file size limit
          if (this.options.maxFileSize && buffer.length > this.options.maxFileSize) {
            throw new Error(`File size ${buffer.length} exceeds limit ${this.options.maxFileSize}`);
          }

          return { response, buffer };
        },
        { maxRetries: this.options.retries }
      );

      const { response, buffer } = result;

      // Ensure directory exists
      await ensureDir(path.dirname(task.localPath));

      // Write to temp file first (atomic write)
      await writeFile(tempPath, buffer);

      // Rename to final path
      await rename(tempPath, task.localPath);

      const contentType = response.headers.get('content-type') ?? 'application/octet-stream';
      const etag = response.headers.get('etag') ?? undefined;
      const lastModified = response.headers.get('last-modified') ?? undefined;

      logger.download(url, 'complete', `${buffer.length} bytes`);

      return {
        url,
        localPath: task.localPath,
        status: response.status,
        size: buffer.length,
        contentType,
        cached: false,
        etag,
        lastModified,
      };
    } catch (error) {
      // Clean up temp file if exists
      try {
        await unlink(tempPath);
      } catch {
        // Ignore cleanup errors
      }

      const errorMessage = error instanceof Error ? error.message : String(error);
      logger.download(url, 'failed', errorMessage);

      return {
        url,
        localPath: task.localPath,
        status: 0,
        size: 0,
        contentType: '',
        cached: false,
        error: errorMessage,
      };
    }
  }

  /**
   * Download with conditional GET (for updates)
   */
  async conditionalDownload(
    task: DownloadTask,
    etag?: string,
    lastModified?: string
  ): Promise<DownloadResult> {
    const url = task.url.href;

    if (this.options.delay > 0) {
      await sleep(this.options.delay);
    }

    try {
      const response = await this.httpClient.conditionalGet(url, {
        etag,
        lastModified,
        signal: this.abortController.signal,
      });

      // 304 Not Modified
      if (response.status === 304) {
        logger.download(url, 'cached');
        return {
          url,
          localPath: task.localPath,
          status: 304,
          size: 0,
          contentType: response.headers.get('content-type') ?? '',
          cached: true,
          etag: response.headers.get('etag') ?? etag,
          lastModified: response.headers.get('last-modified') ?? lastModified,
        };
      }

      // Need to download
      return this.download(task);
    } catch (error) {
      // Fall back to regular download
      return this.download(task);
    }
  }

  /**
   * Fetch text content (for HTML/CSS parsing)
   */
  async fetchText(url: string): Promise<{ content: string; contentType: string; finalUrl: string } | null> {
    try {
      const response = await this.httpClient.get(url, {
        signal: this.abortController.signal,
      });

      if (!response.body || !response.status.toString().startsWith('2')) {
        return null;
      }

      const reader = response.body.getReader();
      const chunks: Uint8Array[] = [];

      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
      }

      const buffer = Buffer.concat(chunks);
      const content = buffer.toString('utf-8');
      const contentType = response.headers.get('content-type') ?? 'text/plain';

      return {
        content,
        contentType,
        finalUrl: response.url,
      };
    } catch {
      return null;
    }
  }

  abort(): void {
    this.abortController.abort();
  }

  destroy(): void {
    this.abort();
    this.httpClient.destroy();
  }
}
