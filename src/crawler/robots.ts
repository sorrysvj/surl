import { URL } from 'node:url';
import { httpClient } from '../downloader/http-client.js';

interface RobotsRule {
  userAgent: string;
  allow: string[];
  disallow: string[];
  crawlDelay?: number;
  sitemaps: string[];
}

export class RobotsParser {
  private rules: RobotsRule[] = [];
  private sitemaps: string[] = [];
  private defaultCrawlDelay?: number;
  private loaded = false;

  /**
   * Fetch and parse robots.txt for a URL
   */
  async load(baseUrl: string): Promise<boolean> {
    try {
      const robotsUrl = new URL('/robots.txt', baseUrl).href;
      const response = await httpClient.get(robotsUrl);

      if (response.status !== 200 || !response.body) {
        return false;
      }

      const reader = response.body.getReader();
      const chunks: Uint8Array[] = [];

      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
      }

      const content = Buffer.concat(chunks).toString('utf-8');
      this.parse(content);
      this.loaded = true;
      return true;
    } catch {
      return false;
    }
  }

  /**
   * Parse robots.txt content
   */
  private parse(content: string): void {
    const lines = content.split('\n');
    let currentRule: RobotsRule | null = null;

    for (const line of lines) {
      const trimmed = line.trim();

      // Skip comments and empty lines
      if (trimmed.startsWith('#') || trimmed === '') {
        continue;
      }

      const colonIndex = trimmed.indexOf(':');
      if (colonIndex === -1) continue;

      const directive = trimmed.slice(0, colonIndex).trim().toLowerCase();
      const value = trimmed.slice(colonIndex + 1).trim();

      switch (directive) {
        case 'user-agent':
          // Start new rule
          currentRule = {
            userAgent: value.toLowerCase(),
            allow: [],
            disallow: [],
            sitemaps: [],
          };
          this.rules.push(currentRule);
          break;

        case 'allow':
          if (currentRule && value) {
            currentRule.allow.push(value);
          }
          break;

        case 'disallow':
          if (currentRule && value) {
            currentRule.disallow.push(value);
          }
          break;

        case 'crawl-delay':
          const delay = parseInt(value, 10);
          if (!isNaN(delay)) {
            if (currentRule) {
              currentRule.crawlDelay = delay * 1000;
            } else {
              this.defaultCrawlDelay = delay * 1000;
            }
          }
          break;

        case 'sitemap':
          if (value.startsWith('http')) {
            this.sitemaps.push(value);
            if (currentRule) {
              currentRule.sitemaps.push(value);
            }
          }
          break;
      }
    }
  }

  /**
   * Check if a path is allowed for the given user agent
   */
  isAllowed(path: string, userAgent: string = 'surl'): boolean {
    const ua = userAgent.toLowerCase();

    // Find matching rules (specific first, then wildcard)
    const matchingRules = this.rules.filter(
      (rule) => rule.userAgent === ua || rule.userAgent === '*'
    );

    // If no rules, everything is allowed
    if (matchingRules.length === 0) {
      return true;
    }

    // Prefer specific user-agent match
    const specificRule = matchingRules.find((rule) => rule.userAgent === ua);
    const wildcardRule = matchingRules.find((rule) => rule.userAgent === '*');
    const rule = specificRule ?? wildcardRule;

    if (!rule) {
      return true;
    }

    // Check allows first (more specific)
    for (const pattern of rule.allow) {
      if (this.matchPath(path, pattern)) {
        return true;
      }
    }

    // Check disallows
    for (const pattern of rule.disallow) {
      if (this.matchPath(path, pattern)) {
        return false;
      }
    }

    // Default to allowed
    return true;
  }

  /**
   * Match a path against a robots.txt pattern
   */
  private matchPath(path: string, pattern: string): boolean {
    // Empty pattern matches nothing
    if (!pattern) return false;

    // * matches any sequence
    // $ matches end of URL
    let regexPattern = pattern
      .replace(/[.+?^${}()|[\]\\]/g, '\\$&')
      .replace(/\*/g, '.*');

    if (pattern.endsWith('$')) {
      regexPattern = regexPattern.slice(0, -2) + '$';
    } else {
      regexPattern += '.*';
    }

    try {
      const regex = new RegExp(`^${regexPattern}`);
      return regex.test(path);
    } catch {
      return path.startsWith(pattern);
    }
  }

  /**
   * Get crawl delay for user agent
   */
  getCrawlDelay(userAgent: string = 'surl'): number | undefined {
    const ua = userAgent.toLowerCase();

    const specificRule = this.rules.find((rule) => rule.userAgent === ua);
    if (specificRule?.crawlDelay !== undefined) {
      return specificRule.crawlDelay;
    }

    const wildcardRule = this.rules.find((rule) => rule.userAgent === '*');
    if (wildcardRule?.crawlDelay !== undefined) {
      return wildcardRule.crawlDelay;
    }

    return this.defaultCrawlDelay;
  }

  /**
   * Get discovered sitemaps
   */
  getSitemaps(): string[] {
    return [...this.sitemaps];
  }

  /**
   * Check if robots.txt was loaded
   */
  isLoaded(): boolean {
    return this.loaded;
  }
}
