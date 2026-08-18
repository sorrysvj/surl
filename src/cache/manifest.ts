import { readFile, writeFile } from 'node:fs/promises';
import { Manifest, ManifestEntry, ResourceType } from '../types/index.js';
import { getManifestPath, ensureDir, getSurlDir } from '../utils/paths.js';
import { MANIFEST_VERSION } from '../config/defaults.js';

/**
 * Create a new empty manifest
 */
export function createManifest(baseUrl: string, outputDirectory: string): Manifest {
  const now = new Date().toISOString();

  return {
    version: MANIFEST_VERSION,
    createdAt: now,
    updatedAt: now,
    baseUrl,
    outputDirectory,
    entries: {},
  };
}

/**
 * Load manifest from disk
 */
export async function loadManifest(baseDir: string): Promise<Manifest | null> {
  const manifestPath = getManifestPath(baseDir);

  try {
    const content = await readFile(manifestPath, 'utf-8');
    const manifest = JSON.parse(content) as Manifest;

    // Validate version
    if (manifest.version !== MANIFEST_VERSION) {
      console.warn(`Manifest version mismatch: ${manifest.version} vs ${MANIFEST_VERSION}`);
    }

    return manifest;
  } catch {
    return null;
  }
}

/**
 * Save manifest to disk
 */
export async function saveManifest(manifest: Manifest, baseDir: string): Promise<void> {
  await ensureDir(getSurlDir(baseDir));

  const manifestPath = getManifestPath(baseDir);
  manifest.updatedAt = new Date().toISOString();

  await writeFile(manifestPath, JSON.stringify(manifest, null, 2));
}

/**
 * Add or update an entry in the manifest
 */
export function addManifestEntry(
  manifest: Manifest,
  entry: ManifestEntry
): void {
  manifest.entries[entry.url] = entry;
}

/**
 * Get an entry from the manifest
 */
export function getManifestEntry(
  manifest: Manifest,
  url: string
): ManifestEntry | undefined {
  return manifest.entries[url];
}

/**
 * Check if a URL exists in the manifest
 */
export function hasManifestEntry(manifest: Manifest, url: string): boolean {
  return url in manifest.entries;
}

/**
 * Remove an entry from the manifest
 */
export function removeManifestEntry(manifest: Manifest, url: string): void {
  delete manifest.entries[url];
}

/**
 * Get all entries of a specific type
 */
export function getEntriesByType(manifest: Manifest, type: ResourceType): ManifestEntry[] {
  return Object.values(manifest.entries).filter((entry) => entry.type === type);
}

/**
 * Get manifest statistics
 */
export function getManifestStats(manifest: Manifest): {
  total: number;
  byType: Record<ResourceType, number>;
  totalSize: number;
} {
  const entries = Object.values(manifest.entries);
  const byType: Record<string, number> = {};
  let totalSize = 0;

  for (const entry of entries) {
    byType[entry.type] = (byType[entry.type] ?? 0) + 1;
    totalSize += entry.contentLength;
  }

  return {
    total: entries.length,
    byType: byType as Record<ResourceType, number>,
    totalSize,
  };
}
