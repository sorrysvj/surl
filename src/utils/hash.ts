import crypto from 'node:crypto';
import { createReadStream } from 'node:fs';

/**
 * Calculate SHA-256 hash of a string
 */
export function hashString(content: string): string {
  return crypto.createHash('sha256').update(content).digest('hex');
}

/**
 * Calculate SHA-256 hash of a buffer
 */
export function hashBuffer(buffer: Buffer): string {
  return crypto.createHash('sha256').update(buffer).digest('hex');
}

/**
 * Calculate SHA-256 hash of a file
 */
export async function hashFile(filePath: string): Promise<string> {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash('sha256');
    const stream = createReadStream(filePath);

    stream.on('data', (chunk) => hash.update(chunk));
    stream.on('end', () => resolve(hash.digest('hex')));
    stream.on('error', reject);
  });
}

/**
 * Generate a short hash for identification
 */
export function shortHash(content: string, length = 8): string {
  return hashString(content).slice(0, length);
}
