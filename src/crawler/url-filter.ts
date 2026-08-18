import { URL } from 'node:url';
import { isSameOrigin, matchesPattern, getExtensionFromUrl, isHttpUrl } from '../utils/url.js';

export interface UrlFilterOptions {
  baseUrl: string;
  sameOrigin: boolean;
  externalAssets: boolean;
  include: string[];
  exclude: string[];
  extensions: string[];
  maxDepth: number;
}

export class UrlFilter {
  private options: UrlFilterOptions;

  constructor(options: UrlFilterOptions) {
    this.options = options;
  }

  /**
   * Check if a URL should be crawled
   */
  shouldCrawl(url: string, depth: number, isAsset: boolean = false): boolean {
    // Must be HTTP(S)
    if (!isHttpUrl(url)) {
      return false;
    }

    // Check depth limit
    if (depth > this.options.maxDepth) {
      return false;
    }

    // Check same origin
    if (this.options.sameOrigin) {
      const sameOrigin = isSameOrigin(url, this.options.baseUrl);

      // Allow external assets if configured
      if (!sameOrigin && !(isAsset && this.options.externalAssets)) {
        return false;
      }
    }

    // Check include patterns
    if (this.options.include.length > 0 && !matchesPattern(url, this.options.include)) {
      return false;
    }

    // Check exclude patterns
    if (this.options.exclude.length > 0 && matchesPattern(url, this.options.exclude)) {
      return false;
    }

    // Check extensions filter
    if (this.options.extensions.length > 0) {
      const ext = getExtensionFromUrl(url);
      if (ext && !this.options.extensions.includes(ext)) {
        return false;
      }
    }

    return true;
  }

  /**
   * Check if external assets are allowed
   */
  allowsExternalAssets(): boolean {
    return this.options.externalAssets;
  }

  /**
   * Get the base URL
   */
  getBaseUrl(): string {
    return this.options.baseUrl;
  }

  /**
   * Update options
   */
  updateOptions(options: Partial<UrlFilterOptions>): void {
    this.options = { ...this.options, ...options };
  }
}
