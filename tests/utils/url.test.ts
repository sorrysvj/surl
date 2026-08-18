import { describe, it, expect } from 'vitest';
import {
  normalizeUrl,
  resolveUrl,
  isAbsoluteUrl,
  isSameOrigin,
  getHostname,
  isHttpUrl,
  isPrivateUrl,
  getExtensionFromUrl,
  getResourceType,
  matchesPattern,
} from '../../src/utils/url.js';

describe('normalizeUrl', () => {
  it('should remove fragment', () => {
    expect(normalizeUrl('https://example.com/page#section')).toBe('https://example.com/page');
  });

  it('should add trailing slash to root', () => {
    expect(normalizeUrl('https://example.com')).toBe('https://example.com/');
  });

  it('should resolve relative URL with base', () => {
    expect(normalizeUrl('/path', 'https://example.com')).toBe('https://example.com/path');
  });
});

describe('resolveUrl', () => {
  it('should resolve relative URL', () => {
    expect(resolveUrl('/path', 'https://example.com')).toBe('https://example.com/path');
  });

  it('should resolve protocol-relative URL', () => {
    expect(resolveUrl('//cdn.example.com/file.js', 'https://example.com')).toBe('https://cdn.example.com/file.js');
  });

  it('should keep absolute URL', () => {
    expect(resolveUrl('https://other.com/path', 'https://example.com')).toBe('https://other.com/path');
  });
});

describe('isAbsoluteUrl', () => {
  it('should return true for absolute URLs', () => {
    expect(isAbsoluteUrl('https://example.com')).toBe(true);
    expect(isAbsoluteUrl('http://example.com')).toBe(true);
  });

  it('should return false for relative URLs', () => {
    expect(isAbsoluteUrl('/path')).toBe(false);
    expect(isAbsoluteUrl('./path')).toBe(false);
  });
});

describe('isSameOrigin', () => {
  it('should return true for same origin', () => {
    expect(isSameOrigin('https://example.com/a', 'https://example.com/b')).toBe(true);
  });

  it('should return false for different origins', () => {
    expect(isSameOrigin('https://example.com', 'https://other.com')).toBe(false);
  });

  it('should return false for different protocols', () => {
    expect(isSameOrigin('https://example.com', 'http://example.com')).toBe(false);
  });
});

describe('isPrivateUrl', () => {
  it('should detect localhost', () => {
    expect(isPrivateUrl('http://localhost')).toBe(true);
    expect(isPrivateUrl('http://127.0.0.1')).toBe(true);
  });

  it('should detect private IP ranges', () => {
    expect(isPrivateUrl('http://10.0.0.1')).toBe(true);
    expect(isPrivateUrl('http://192.168.1.1')).toBe(true);
    expect(isPrivateUrl('http://172.16.0.1')).toBe(true);
  });

  it('should detect AWS metadata endpoint', () => {
    expect(isPrivateUrl('http://169.254.169.254')).toBe(true);
  });

  it('should allow public URLs', () => {
    expect(isPrivateUrl('https://example.com')).toBe(false);
  });
});

describe('getExtensionFromUrl', () => {
  it('should extract extension', () => {
    expect(getExtensionFromUrl('https://example.com/file.js')).toBe('js');
    expect(getExtensionFromUrl('https://example.com/style.css')).toBe('css');
  });

  it('should handle no extension', () => {
    expect(getExtensionFromUrl('https://example.com/path')).toBe('');
  });
});

describe('getResourceType', () => {
  it('should detect HTML', () => {
    expect(getResourceType('https://example.com/page.html')).toBe('html');
  });

  it('should detect CSS', () => {
    expect(getResourceType('https://example.com/style.css')).toBe('css');
  });

  it('should detect JavaScript', () => {
    expect(getResourceType('https://example.com/app.js')).toBe('javascript');
  });

  it('should detect images', () => {
    expect(getResourceType('https://example.com/logo.png')).toBe('image');
    expect(getResourceType('https://example.com/photo.jpg')).toBe('image');
  });

  it('should use content-type if provided', () => {
    expect(getResourceType('https://example.com/api', 'text/html')).toBe('html');
  });
});

describe('matchesPattern', () => {
  it('should match wildcard patterns', () => {
    expect(matchesPattern('https://example.com/docs/page', ['/docs/*'])).toBe(true);
  });

  it('should not match when pattern does not match', () => {
    expect(matchesPattern('https://example.com/admin/page', ['/docs/*'])).toBe(false);
  });

  it('should return true for empty patterns', () => {
    expect(matchesPattern('https://example.com/any', [])).toBe(true);
  });
});
