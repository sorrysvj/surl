import { describe, it, expect } from 'vitest';
import { parseHtml, extractLinks, extractAssets, extractBaseUrl } from '../../src/parsers/html-parser.js';

const sampleHtml = `
<!DOCTYPE html>
<html>
<head>
  <base href="/subdir/">
  <link rel="stylesheet" href="/css/style.css">
  <script src="/js/app.js"></script>
</head>
<body>
  <a href="/about">About</a>
  <a href="https://external.com">External</a>
  <img src="/images/logo.png" srcset="/images/logo-2x.png 2x">
  <video src="/video/intro.mp4" poster="/images/poster.jpg"></video>
  <div style="background: url('/images/bg.png')"></div>
</body>
</html>
`;

describe('parseHtml', () => {
  it('should extract all resources', () => {
    const resources = parseHtml(sampleHtml, 'https://example.com/page/');

    expect(resources.length).toBeGreaterThan(0);

    const urls = resources.map(r => r.url);
    expect(urls).toContain('https://example.com/css/style.css');
    expect(urls).toContain('https://example.com/js/app.js');
    expect(urls).toContain('https://example.com/images/logo.png');
  });

  it('should handle srcset', () => {
    const resources = parseHtml(sampleHtml, 'https://example.com/');
    const urls = resources.map(r => r.url);
    expect(urls).toContain('https://example.com/images/logo-2x.png');
  });

  it('should extract inline style URLs', () => {
    const resources = parseHtml(sampleHtml, 'https://example.com/');
    const urls = resources.map(r => r.url);
    expect(urls).toContain('https://example.com/images/bg.png');
  });
});

describe('extractLinks', () => {
  it('should extract only links', () => {
    const links = extractLinks(sampleHtml, 'https://example.com/');
    expect(links).toContain('https://example.com/about');
    expect(links).toContain('https://external.com/');
  });
});

describe('extractAssets', () => {
  it('should extract only assets', () => {
    const assets = extractAssets(sampleHtml, 'https://example.com/');
    const types = assets.map(a => a.type);
    expect(types).toContain('css');
    expect(types).toContain('javascript');
    expect(types).toContain('image');
  });
});

describe('extractBaseUrl', () => {
  it('should extract base URL from HTML', () => {
    const base = extractBaseUrl(sampleHtml, 'https://example.com/page/');
    expect(base).toBe('https://example.com/subdir/');
  });

  it('should return page URL when no base tag', () => {
    const html = '<html><body></body></html>';
    const base = extractBaseUrl(html, 'https://example.com/page/');
    expect(base).toBe('https://example.com/page/');
  });
});
