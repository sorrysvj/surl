import { describe, it, expect } from 'vitest';
import path from 'node:path';
import {
  urlToLocalPath,
  sanitizeFilename,
  getRelativePath,
  isPathSafe,
} from '../../src/utils/paths.js';

describe('urlToLocalPath', () => {
  const baseDir = '/output';

  it('should convert root URL to index.html', () => {
    const result = urlToLocalPath('https://example.com/', baseDir);
    expect(result).toContain('index.html');
  });

  it('should handle paths with extensions', () => {
    const result = urlToLocalPath('https://example.com/assets/app.js', baseDir);
    expect(result).toContain('app.js');
  });

  it('should handle directory paths', () => {
    const result = urlToLocalPath('https://example.com/about/', baseDir);
    expect(result).toContain('index.html');
  });

  it('should throw on path traversal attempt', () => {
    expect(() => urlToLocalPath('https://example.com/../../../etc/passwd', baseDir)).toThrow();
  });
});

describe('sanitizeFilename', () => {
  it('should remove invalid characters', () => {
    expect(sanitizeFilename('file<>name.txt')).toBe('file__name.txt');
  });

  it('should handle Windows reserved names', () => {
    expect(sanitizeFilename('CON')).toBe('_CON');
    expect(sanitizeFilename('PRN.txt')).toBe('_PRN.txt');
  });

  it('should handle empty input', () => {
    expect(sanitizeFilename('')).toBe('_empty_');
  });

  it('should truncate long filenames', () => {
    const longName = 'a'.repeat(300) + '.txt';
    const result = sanitizeFilename(longName);
    expect(result.length).toBeLessThanOrEqual(200);
  });
});

describe('getRelativePath', () => {
  it('should calculate relative path', () => {
    const from = '/output/pages/about/index.html';
    const to = '/output/assets/style.css';
    const result = getRelativePath(from, to);
    expect(result).toBe('../../assets/style.css');
  });

  it('should handle same directory', () => {
    const from = '/output/index.html';
    const to = '/output/style.css';
    const result = getRelativePath(from, to);
    expect(result).toBe('./style.css');
  });
});

describe('isPathSafe', () => {
  it('should allow paths within base directory', () => {
    expect(isPathSafe('/output/file.txt', '/output')).toBe(true);
    expect(isPathSafe('/output/sub/file.txt', '/output')).toBe(true);
  });

  it('should reject paths outside base directory', () => {
    expect(isPathSafe('/other/file.txt', '/output')).toBe(false);
    expect(isPathSafe('/output/../other/file.txt', '/output')).toBe(false);
  });
});
