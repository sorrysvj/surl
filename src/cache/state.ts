import { readFile, writeFile } from 'node:fs/promises';
import { CrawlState } from '../types/index.js';
import { getStatePath, ensureDir, getSurlDir } from '../utils/paths.js';
import { STATE_VERSION } from '../config/defaults.js';

/**
 * Create a new crawl state
 */
export function createCrawlState(url: string, outputDirectory: string): CrawlState {
  const now = new Date().toISOString();

  return {
    version: STATE_VERSION,
    url,
    outputDirectory,
    startedAt: now,
    lastUpdatedAt: now,
    status: 'running',
    discovered: 0,
    downloaded: 0,
    cached: 0,
    failed: 0,
    skipped: 0,
    totalBytes: 0,
    queue: [],
    visited: [],
    failed_urls: [],
  };
}

/**
 * Load crawl state from disk
 */
export async function loadCrawlState(baseDir: string): Promise<CrawlState | null> {
  const statePath = getStatePath(baseDir);

  try {
    const content = await readFile(statePath, 'utf-8');
    const state = JSON.parse(content) as CrawlState;

    // Validate version
    if (state.version !== STATE_VERSION) {
      console.warn(`State version mismatch: ${state.version} vs ${STATE_VERSION}`);
    }

    return state;
  } catch {
    return null;
  }
}

/**
 * Save crawl state to disk
 */
export async function saveCrawlState(state: CrawlState, baseDir: string): Promise<void> {
  await ensureDir(getSurlDir(baseDir));

  const statePath = getStatePath(baseDir);
  state.lastUpdatedAt = new Date().toISOString();

  await writeFile(statePath, JSON.stringify(state, null, 2));
}

/**
 * Update crawl state with new values
 */
export function updateCrawlState(
  state: CrawlState,
  updates: Partial<Omit<CrawlState, 'version' | 'url' | 'outputDirectory' | 'startedAt'>>
): void {
  Object.assign(state, updates);
  state.lastUpdatedAt = new Date().toISOString();
}

/**
 * Add URL to visited set
 */
export function markVisited(state: CrawlState, url: string): void {
  if (!state.visited.includes(url)) {
    state.visited.push(url);
  }
  // Remove from queue if present
  const queueIndex = state.queue.indexOf(url);
  if (queueIndex !== -1) {
    state.queue.splice(queueIndex, 1);
  }
}

/**
 * Add URL to queue
 */
export function addToQueue(state: CrawlState, url: string): void {
  if (!state.visited.includes(url) && !state.queue.includes(url)) {
    state.queue.push(url);
    state.discovered++;
  }
}

/**
 * Mark URL as failed
 */
export function markFailed(state: CrawlState, url: string, error: string, attempts: number): void {
  const existing = state.failed_urls.find((f) => f.url === url);

  if (existing) {
    existing.error = error;
    existing.attempts = attempts;
  } else {
    state.failed_urls.push({ url, error, attempts });
  }

  state.failed++;
}

/**
 * Check if crawl can be resumed
 */
export function canResume(state: CrawlState): boolean {
  return (
    state.status === 'interrupted' &&
    state.queue.length > 0
  );
}

/**
 * Get URLs remaining in queue
 */
export function getRemainingUrls(state: CrawlState): string[] {
  return [...state.queue];
}
