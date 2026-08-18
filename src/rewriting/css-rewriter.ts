import { RewriteMap, rewriteUrlWithFragment, shouldRewrite } from './url-rewriter.js';
import { resolveUrl } from '../utils/url.js';

/**
 * Rewrite all URLs in CSS content
 */
export function rewriteCss(
  css: string,
  currentFilePath: string,
  rewriteMap: RewriteMap
): string {
  // Rewrite url() declarations
  let result = css.replace(/url\(['"]?([^'"\)]+)['"]?\)/gi, (match, url) => {
    if (!shouldRewrite(url, rewriteMap.baseUrl, false)) {
      return match;
    }

    const resolved = resolveUrl(url, rewriteMap.baseUrl);
    const rewritten = rewriteUrlWithFragment(resolved, currentFilePath, rewriteMap);

    // Preserve quote style
    if (match.includes("'")) {
      return `url('${rewritten}')`;
    }
    if (match.includes('"')) {
      return `url("${rewritten}")`;
    }
    return `url(${rewritten})`;
  });

  // Rewrite @import url() declarations
  result = result.replace(
    /@import\s+url\(['"]?([^'"\)]+)['"]?\)/gi,
    (match, url) => {
      if (!shouldRewrite(url, rewriteMap.baseUrl, false)) {
        return match;
      }

      const resolved = resolveUrl(url, rewriteMap.baseUrl);
      const rewritten = rewriteUrlWithFragment(resolved, currentFilePath, rewriteMap);

      if (match.includes("'")) {
        return `@import url('${rewritten}')`;
      }
      if (match.includes('"')) {
        return `@import url("${rewritten}")`;
      }
      return `@import url(${rewritten})`;
    }
  );

  // Rewrite @import "file.css" declarations
  result = result.replace(
    /@import\s+['"]([^'"]+)['"]/gi,
    (match, url) => {
      if (!shouldRewrite(url, rewriteMap.baseUrl, false)) {
        return match;
      }

      const resolved = resolveUrl(url, rewriteMap.baseUrl);
      const rewritten = rewriteUrlWithFragment(resolved, currentFilePath, rewriteMap);

      if (match.includes("'")) {
        return `@import '${rewritten}'`;
      }
      return `@import "${rewritten}"`;
    }
  );

  return result;
}
