import { describe, it, expect } from 'vitest';
import { parseCss } from '../../src/parsers/css-parser.js';

describe('parseCss', () => {
  it('should extract url() references', () => {
    const css = `
      .header {
        background: url('../images/bg.png');
      }
      .logo {
        background-image: url('/images/logo.svg');
      }
    `;

    const resources = parseCss(css, 'https://example.com/css/style.css');
    const urls = resources.map(r => r.url);

    expect(urls).toContain('https://example.com/images/bg.png');
    expect(urls).toContain('https://example.com/images/logo.svg');
  });

  it('should extract @import rules', () => {
    const css = `
      @import url('base.css');
      @import 'reset.css';
    `;

    const resources = parseCss(css, 'https://example.com/css/style.css');
    const urls = resources.map(r => r.url);

    expect(urls).toContain('https://example.com/css/base.css');
    expect(urls).toContain('https://example.com/css/reset.css');
  });

  it('should extract @font-face src', () => {
    const css = `
      @font-face {
        font-family: 'Inter';
        src: url('/fonts/inter.woff2') format('woff2');
      }
    `;

    const resources = parseCss(css, 'https://example.com/css/style.css');
    const urls = resources.map(r => r.url);

    expect(urls).toContain('https://example.com/fonts/inter.woff2');
  });

  it('should skip data URLs', () => {
    const css = `
      .icon {
        background: url('data:image/svg+xml,...');
      }
    `;

    const resources = parseCss(css, 'https://example.com/css/style.css');
    expect(resources).toHaveLength(0);
  });
});
