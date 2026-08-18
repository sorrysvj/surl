import { Agent, fetch, setGlobalDispatcher, ProxyAgent } from 'undici';
import { HttpResponse } from '../types/index.js';
import { isPrivateUrl } from '../utils/url.js';

interface HttpClientOptions {
  timeout: number;
  maxRedirects: number;
  userAgent: string;
  headers: Record<string, string>;
  proxy?: string;
  insecure?: boolean;
}

class HttpClient {
  private options: HttpClientOptions;
  private agent: Agent | ProxyAgent | null = null;

  constructor(options: Partial<HttpClientOptions> = {}) {
    this.options = {
      timeout: options.timeout ?? 30000,
      maxRedirects: options.maxRedirects ?? 10,
      userAgent: options.userAgent ?? 'surl/1.0',
      headers: options.headers ?? {},
      proxy: options.proxy,
      insecure: options.insecure ?? false,
    };

    this.setupAgent();
  }

  private setupAgent(): void {
    if (this.options.proxy) {
      this.agent = new ProxyAgent({
        uri: this.options.proxy,
        connect: {
          rejectUnauthorized: !this.options.insecure,
        },
      });
    } else {
      this.agent = new Agent({
        connect: {
          rejectUnauthorized: !this.options.insecure,
        },
        keepAliveTimeout: 30000,
        keepAliveMaxTimeout: 30000,
      });
    }

    setGlobalDispatcher(this.agent);
  }

  async request(
    url: string,
    options: {
      method?: string;
      headers?: Record<string, string>;
      body?: string | Buffer;
      signal?: AbortSignal;
    } = {}
  ): Promise<HttpResponse> {
    // SSRF protection - block private URLs
    if (isPrivateUrl(url)) {
      throw new Error(`Blocked request to private URL: ${url}`);
    }

    const headers: Record<string, string> = {
      'User-Agent': this.options.userAgent,
      'Accept': '*/*',
      'Accept-Encoding': 'gzip, deflate, br',
      ...this.options.headers,
      ...options.headers,
    };

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), this.options.timeout);

    try {
      const response = await fetch(url, {
        method: options.method ?? 'GET',
        headers,
        body: options.body,
        redirect: 'follow',
        signal: options.signal ?? controller.signal,
      });

      clearTimeout(timeoutId);

      return {
        status: response.status,
        statusText: response.statusText,
        headers: response.headers,
        body: response.body,
        url: response.url,
        redirected: response.redirected,
      };
    } catch (error) {
      clearTimeout(timeoutId);
      throw error;
    }
  }

  async get(url: string, options: { headers?: Record<string, string>; signal?: AbortSignal } = {}): Promise<HttpResponse> {
    return this.request(url, { method: 'GET', ...options });
  }

  async head(url: string, options: { headers?: Record<string, string>; signal?: AbortSignal } = {}): Promise<HttpResponse> {
    return this.request(url, { method: 'HEAD', ...options });
  }

  /**
   * Perform conditional GET with ETag/Last-Modified
   */
  async conditionalGet(
    url: string,
    options: {
      etag?: string;
      lastModified?: string;
      signal?: AbortSignal;
    } = {}
  ): Promise<HttpResponse> {
    const headers: Record<string, string> = {};

    if (options.etag) {
      headers['If-None-Match'] = options.etag;
    }

    if (options.lastModified) {
      headers['If-Modified-Since'] = options.lastModified;
    }

    return this.get(url, { headers, signal: options.signal });
  }

  /**
   * Download a resource and return the body as a buffer
   */
  async download(url: string, signal?: AbortSignal): Promise<{ response: HttpResponse; buffer: Buffer }> {
    const response = await this.get(url, { signal });

    if (!response.body) {
      return { response, buffer: Buffer.alloc(0) };
    }

    const chunks: Uint8Array[] = [];
    const reader = response.body.getReader();

    try {
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
      }
    } finally {
      reader.releaseLock();
    }

    const buffer = Buffer.concat(chunks);
    return { response, buffer };
  }

  updateOptions(options: Partial<HttpClientOptions>): void {
    this.options = { ...this.options, ...options };
    if (options.proxy !== undefined || options.insecure !== undefined) {
      this.setupAgent();
    }
  }

  destroy(): void {
    if (this.agent && 'close' in this.agent) {
      (this.agent as Agent).close();
    }
  }
}

export const httpClient = new HttpClient();

export function createHttpClient(options: Partial<HttpClientOptions>): HttpClient {
  return new HttpClient(options);
}
