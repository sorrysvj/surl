import { CrawlTarget, ResourceType } from '../types/index.js';

export interface QueueItem extends CrawlTarget {
  priority: number;
  addedAt: number;
}

/**
 * Priority queue for crawl targets
 * - HTML pages have higher priority than assets
 * - Lower depth has higher priority
 * - Earlier discovered has higher priority
 */
export class CrawlQueue {
  private items: QueueItem[] = [];
  private seen: Set<string> = new Set();
  private processing: Set<string> = new Set();

  /**
   * Calculate priority for an item
   * Lower number = higher priority
   */
  private calculatePriority(target: CrawlTarget): number {
    let priority = target.depth * 100;

    // HTML pages get higher priority
    if (target.type === 'html') {
      priority -= 50;
    }

    // CSS/JS get medium priority
    if (target.type === 'css' || target.type === 'javascript') {
      priority -= 30;
    }

    return priority;
  }

  /**
   * Add a target to the queue
   */
  add(target: CrawlTarget): boolean {
    const key = target.url;

    // Skip if already seen or processing
    if (this.seen.has(key) || this.processing.has(key)) {
      return false;
    }

    this.seen.add(key);

    const item: QueueItem = {
      ...target,
      priority: this.calculatePriority(target),
      addedAt: Date.now(),
    };

    // Insert in sorted order (by priority, then by addedAt)
    const insertIndex = this.items.findIndex(
      (existing) =>
        existing.priority > item.priority ||
        (existing.priority === item.priority && existing.addedAt > item.addedAt)
    );

    if (insertIndex === -1) {
      this.items.push(item);
    } else {
      this.items.splice(insertIndex, 0, item);
    }

    return true;
  }

  /**
   * Add multiple targets
   */
  addMany(targets: CrawlTarget[]): number {
    let added = 0;
    for (const target of targets) {
      if (this.add(target)) {
        added++;
      }
    }
    return added;
  }

  /**
   * Get next item from queue
   */
  next(): CrawlTarget | undefined {
    const item = this.items.shift();

    if (item) {
      this.processing.add(item.url);
      return {
        url: item.url,
        depth: item.depth,
        source: item.source,
        type: item.type,
      };
    }

    return undefined;
  }

  /**
   * Mark a URL as completed (remove from processing)
   */
  complete(url: string): void {
    this.processing.delete(url);
  }

  /**
   * Re-queue a failed URL
   */
  requeue(target: CrawlTarget): void {
    this.processing.delete(target.url);
    this.seen.delete(target.url);
    this.add(target);
  }

  /**
   * Check if URL has been seen
   */
  hasSeen(url: string): boolean {
    return this.seen.has(url);
  }

  /**
   * Check if queue is empty
   */
  isEmpty(): boolean {
    return this.items.length === 0;
  }

  /**
   * Get queue size
   */
  size(): number {
    return this.items.length;
  }

  /**
   * Get number of items being processed
   */
  processingCount(): number {
    return this.processing.size;
  }

  /**
   * Get total seen count
   */
  seenCount(): number {
    return this.seen.size;
  }

  /**
   * Check if all work is done
   */
  isDone(): boolean {
    return this.isEmpty() && this.processing.size === 0;
  }

  /**
   * Get all URLs in queue
   */
  getUrls(): string[] {
    return this.items.map((item) => item.url);
  }

  /**
   * Get all seen URLs
   */
  getSeenUrls(): string[] {
    return [...this.seen];
  }

  /**
   * Clear the queue
   */
  clear(): void {
    this.items = [];
    this.processing.clear();
  }

  /**
   * Reset everything
   */
  reset(): void {
    this.items = [];
    this.seen.clear();
    this.processing.clear();
  }
}
