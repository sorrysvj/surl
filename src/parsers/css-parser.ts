import * as cssTree from 'css-tree';
import { ParsedResource } from '../types/index.js';
import { resolveUrl, getResourceType } from '../utils/url.js';

/**
 * Parse CSS and extract all resource URLs
 */
export function parseCss(css: string, cssUrl: string): ParsedResource[] {
  const resources: ParsedResource[] = [];
  const seen = new Set<string>();

  function addResource(url: string): void {
    if (!url || url.startsWith('data:')) return;

    const resolved = resolveUrl(url.trim(), cssUrl);

    if (!seen.has(resolved)) {
      seen.add(resolved);
      resources.push({
        url: resolved,
        type: getResourceType(resolved),
        tag: 'css',
        attribute: 'url()',
      });
    }
  }

  try {
    const ast = cssTree.parse(css, {
      parseAtrulePrelude: true,
      parseRulePrelude: true,
      parseValue: true,
    });

    cssTree.walk(ast, {
      visit: 'Url',
      enter(node) {
        // Extract URL from url() function
        if (node.value) {
          let url: string;

          if (typeof node.value === 'string') {
            url = node.value;
          } else if (node.value.type === 'String') {
            url = node.value.value;
          } else if (node.value.type === 'Raw') {
            url = node.value.value;
          } else {
            return;
          }

          // Remove quotes if present
          url = url.replace(/^['"]|['"]$/g, '');
          addResource(url);
        }
      },
    });

    // Handle @import rules
    cssTree.walk(ast, {
      visit: 'Atrule',
      enter(node) {
        if (node.name === 'import' && node.prelude) {
          const prelude = cssTree.generate(node.prelude);

          // Extract URL from @import
          const urlMatch = prelude.match(/url\(['"]?([^'"\)]+)['"]?\)/i);
          if (urlMatch?.[1]) {
            const importResource: ParsedResource = {
              url: resolveUrl(urlMatch[1].trim(), cssUrl),
              type: 'css',
              tag: '@import',
              attribute: 'url()',
            };
            if (!seen.has(importResource.url)) {
              seen.add(importResource.url);
              resources.push(importResource);
            }
          } else {
            // @import "file.css" format
            const stringMatch = prelude.match(/['"]([^'"]+)['"]/);
            if (stringMatch?.[1]) {
              const importResource: ParsedResource = {
                url: resolveUrl(stringMatch[1].trim(), cssUrl),
                type: 'css',
                tag: '@import',
                attribute: 'string',
              };
              if (!seen.has(importResource.url)) {
                seen.add(importResource.url);
                resources.push(importResource);
              }
            }
          }
        }
      },
    });

    // Handle @font-face src
    cssTree.walk(ast, {
      visit: 'Atrule',
      enter(node) {
        if (node.name === 'font-face' && node.block) {
          cssTree.walk(node.block, {
            visit: 'Url',
            enter(urlNode) {
              if (urlNode.value) {
                let url: string;

                if (typeof urlNode.value === 'string') {
                  url = urlNode.value;
                } else if (urlNode.value.type === 'String') {
                  url = urlNode.value.value;
                } else if (urlNode.value.type === 'Raw') {
                  url = urlNode.value.value;
                } else {
                  return;
                }

                url = url.replace(/^['"]|['"]$/g, '');

                const fontResource: ParsedResource = {
                  url: resolveUrl(url, cssUrl),
                  type: 'font',
                  tag: '@font-face',
                  attribute: 'src',
                };

                if (!seen.has(fontResource.url)) {
                  seen.add(fontResource.url);
                  resources.push(fontResource);
                }
              }
            },
          });
        }
      },
    });
  } catch {
    // Fallback to regex parsing if CSS parser fails
    return parseCssWithRegex(css, cssUrl);
  }

  return resources;
}

/**
 * Fallback regex-based CSS parsing
 */
function parseCssWithRegex(css: string, cssUrl: string): ParsedResource[] {
  const resources: ParsedResource[] = [];
  const seen = new Set<string>();

  // Match url() declarations
  const urlRegex = /url\(['"]?([^'"\)]+)['"]?\)/gi;
  let match;

  while ((match = urlRegex.exec(css)) !== null) {
    if (match[1] && !match[1].startsWith('data:')) {
      const resolved = resolveUrl(match[1].trim(), cssUrl);

      if (!seen.has(resolved)) {
        seen.add(resolved);
        resources.push({
          url: resolved,
          type: getResourceType(resolved),
          tag: 'css',
          attribute: 'url()',
        });
      }
    }
  }

  // Match @import declarations
  const importRegex = /@import\s+(?:url\(['"]?([^'"\)]+)['"]?\)|['"]([^'"]+)['"])/gi;

  while ((match = importRegex.exec(css)) !== null) {
    const url = match[1] ?? match[2];
    if (url) {
      const resolved = resolveUrl(url.trim(), cssUrl);

      if (!seen.has(resolved)) {
        seen.add(resolved);
        resources.push({
          url: resolved,
          type: 'css',
          tag: '@import',
          attribute: url === match[1] ? 'url()' : 'string',
        });
      }
    }
  }

  return resources;
}
