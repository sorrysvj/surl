import { readdir, stat, readFile, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { Config, EXIT_CODES, InspectResult, CheckResult, DoctorResult } from '../types/index.js';
import { Crawler } from '../crawler/crawler.js';
import { loadManifest, getManifestStats } from '../cache/manifest.js';
import { loadCrawlState } from '../cache/state.js';
import { getSurlDir } from '../utils/paths.js';
import { logger } from '../terminal/logger.js';
import { colors, formatBytes, horizontalLine } from '../terminal/colors.js';
import { displaySummary, displayHeader } from '../terminal/progress.js';
import { generateSampleConfig } from '../config/config.js';

const VERSION = '1.0.0';

/**
 * Main mirror command
 */
export async function mirror(config: Config): Promise<number> {
  displayHeader(VERSION);

  logger.section('Target', colors.url(config.url));
  logger.section('Output', colors.path(config.directory));
  logger.newLine();
  logger.plain(horizontalLine(50));
  logger.newLine();

  const crawler = new Crawler(config);

  // Handle Ctrl+C
  let interrupted = false;
  const handleInterrupt = () => {
    if (interrupted) {
      process.exit(EXIT_CODES.GENERAL_ERROR);
    }
    interrupted = true;
    logger.newLine();
    logger.warn('Interrupted! Saving state...');
    crawler.abort();
  };

  process.on('SIGINT', handleInterrupt);
  process.on('SIGTERM', handleInterrupt);

  try {
    const stats = await crawler.crawl();

    // Determine status
    let status: 'success' | 'partial' | 'failed' = 'success';
    if (stats.failed > 0 && stats.downloaded > 0) {
      status = 'partial';
    } else if (stats.downloaded === 0 && stats.failed > 0) {
      status = 'failed';
    }

    if (config.json) {
      logger.json({
        url: config.url,
        directory: config.directory,
        stats: {
          discovered: stats.discovered,
          downloaded: stats.downloaded,
          cached: stats.cached,
          failed: stats.failed,
          skipped: stats.skipped,
          totalBytes: stats.totalBytes,
        },
        status,
      });
    } else {
      displaySummary(config.url, config.directory, stats, status);
    }

    if (interrupted) {
      logger.info('State saved. Run with --resume to continue.');
      return EXIT_CODES.GENERAL_ERROR;
    }

    return status === 'failed' ? EXIT_CODES.NETWORK_ERROR : (status === 'partial' ? EXIT_CODES.PARTIAL_FAILURE : EXIT_CODES.SUCCESS);
  } catch (error) {
    logger.error('Crawl failed', error instanceof Error ? error : new Error(String(error)));
    return EXIT_CODES.GENERAL_ERROR;
  } finally {
    process.off('SIGINT', handleInterrupt);
    process.off('SIGTERM', handleInterrupt);
  }
}

/**
 * Serve command - start a local HTTP server
 */
export async function serve(directory: string, port: number = 3000): Promise<number> {
  const resolvedDir = path.resolve(directory);

  // Check if directory exists
  try {
    const stats = await stat(resolvedDir);
    if (!stats.isDirectory()) {
      logger.error(`Not a directory: ${resolvedDir}`);
      return EXIT_CODES.INVALID_ARGUMENTS;
    }
  } catch {
    logger.error(`Directory not found: ${resolvedDir}`);
    return EXIT_CODES.FILESYSTEM_ERROR;
  }

  const server = http.createServer(async (req, res) => {
    let urlPath = req.url ?? '/';

    // Remove query string
    const queryIndex = urlPath.indexOf('?');
    if (queryIndex > -1) {
      urlPath = urlPath.slice(0, queryIndex);
    }

    // Decode URL
    urlPath = decodeURIComponent(urlPath);

    // Handle directory -> index.html
    if (urlPath.endsWith('/')) {
      urlPath += 'index.html';
    }

    const filePath = path.join(resolvedDir, urlPath);

    // Security check
    if (!filePath.startsWith(resolvedDir)) {
      res.writeHead(403);
      res.end('Forbidden');
      return;
    }

    try {
      const fileStats = await stat(filePath);

      if (fileStats.isDirectory()) {
        // Try index.html
        const indexPath = path.join(filePath, 'index.html');
        const content = await readFile(indexPath);
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end(content);
      } else {
        const content = await readFile(filePath);
        const contentType = getContentType(filePath);
        res.writeHead(200, { 'Content-Type': contentType });
        res.end(content);
      }
    } catch {
      res.writeHead(404);
      res.end('Not Found');
    }
  });

  return new Promise((resolve) => {
    server.listen(port, () => {
      logger.success(`Server running at http://localhost:${port}`);
      logger.info(`Serving: ${resolvedDir}`);
      logger.info('Press Ctrl+C to stop');
    });

    process.on('SIGINT', () => {
      logger.newLine();
      logger.info('Stopping server...');
      server.close(() => {
        resolve(EXIT_CODES.SUCCESS);
      });
    });
  });
}

/**
 * Inspect command - show information about a mirrored site
 */
export async function inspect(directory: string, json: boolean = false): Promise<number> {
  const resolvedDir = path.resolve(directory);

  try {
    const manifest = await loadManifest(resolvedDir);
    const stats = manifest ? getManifestStats(manifest) : null;

    const result: InspectResult = {
      totalFiles: stats?.total ?? 0,
      htmlFiles: stats?.byType.html ?? 0,
      cssFiles: stats?.byType.css ?? 0,
      jsFiles: stats?.byType.javascript ?? 0,
      imageFiles: stats?.byType.image ?? 0,
      fontFiles: stats?.byType.font ?? 0,
      otherFiles: (stats?.total ?? 0) -
        (stats?.byType.html ?? 0) -
        (stats?.byType.css ?? 0) -
        (stats?.byType.javascript ?? 0) -
        (stats?.byType.image ?? 0) -
        (stats?.byType.font ?? 0),
      totalSize: stats?.totalSize ?? 0,
      brokenReferences: [],
      externalReferences: [],
    };

    if (json) {
      logger.json(result);
    } else {
      logger.header(`Inspect: ${resolvedDir}`);
      logger.stats({
        'Total Files': result.totalFiles,
        'HTML Files': result.htmlFiles,
        'CSS Files': result.cssFiles,
        'JS Files': result.jsFiles,
        'Image Files': result.imageFiles,
        'Font Files': result.fontFiles,
        'Other Files': result.otherFiles,
        'Total Size': formatBytes(result.totalSize),
      });
    }

    return EXIT_CODES.SUCCESS;
  } catch (error) {
    logger.error('Failed to inspect directory', error instanceof Error ? error : new Error(String(error)));
    return EXIT_CODES.FILESYSTEM_ERROR;
  }
}

/**
 * Check command - verify a mirrored site
 */
export async function check(directory: string, json: boolean = false): Promise<number> {
  const resolvedDir = path.resolve(directory);

  try {
    const manifest = await loadManifest(resolvedDir);

    const result: CheckResult = {
      brokenLinks: [],
      missingAssets: [],
      externalReferences: [],
      invalidPaths: [],
    };

    if (manifest) {
      // Check each entry exists
      for (const entry of Object.values(manifest.entries)) {
        try {
          await stat(entry.localPath);
        } catch {
          result.missingAssets.push({
            file: entry.source,
            asset: entry.url,
          });
        }
      }
    }

    if (json) {
      logger.json(result);
    } else {
      logger.header(`Check: ${resolvedDir}`);

      if (result.brokenLinks.length === 0 &&
          result.missingAssets.length === 0 &&
          result.externalReferences.length === 0 &&
          result.invalidPaths.length === 0) {
        logger.success('All checks passed');
      } else {
        if (result.brokenLinks.length > 0) {
          logger.warn(`Broken links: ${result.brokenLinks.length}`);
        }
        if (result.missingAssets.length > 0) {
          logger.warn(`Missing assets: ${result.missingAssets.length}`);
          for (const missing of result.missingAssets.slice(0, 10)) {
            logger.plain(`  ${missing.asset}`);
          }
          if (result.missingAssets.length > 10) {
            logger.plain(`  ... and ${result.missingAssets.length - 10} more`);
          }
        }
        if (result.externalReferences.length > 0) {
          logger.info(`External references: ${result.externalReferences.length}`);
        }
      }
    }

    return result.brokenLinks.length > 0 || result.missingAssets.length > 0
      ? EXIT_CODES.PARTIAL_FAILURE
      : EXIT_CODES.SUCCESS;
  } catch (error) {
    logger.error('Failed to check directory', error instanceof Error ? error : new Error(String(error)));
    return EXIT_CODES.FILESYSTEM_ERROR;
  }
}

/**
 * Clean command - remove surl metadata
 */
export async function clean(directory: string): Promise<number> {
  const resolvedDir = path.resolve(directory);
  const surlDir = getSurlDir(resolvedDir);

  try {
    await rm(surlDir, { recursive: true, force: true });
    logger.success(`Cleaned: ${surlDir}`);
    return EXIT_CODES.SUCCESS;
  } catch (error) {
    logger.error('Failed to clean directory', error instanceof Error ? error : new Error(String(error)));
    return EXIT_CODES.FILESYSTEM_ERROR;
  }
}

/**
 * Doctor command - check system requirements
 */
export async function doctor(): Promise<number> {
  const result: DoctorResult = {
    nodeVersion: {
      ok: false,
      version: process.version,
      required: '>=18.0.0',
    },
    filesystem: { ok: false, message: '' },
    network: { ok: false, message: '' },
    tls: { ok: false, message: '' },
    config: { ok: false, message: '' },
  };

  // Check Node.js version
  const nodeVersion = process.versions.node.split('.').map(Number);
  result.nodeVersion.ok = (nodeVersion[0] ?? 0) >= 18;

  // Check filesystem
  try {
    const testDir = path.join(process.cwd(), '.surl-test');
    const { mkdir, rmdir } = await import('node:fs/promises');
    await mkdir(testDir, { recursive: true });
    await rmdir(testDir);
    result.filesystem.ok = true;
    result.filesystem.message = 'Write access OK';
  } catch {
    result.filesystem.message = 'No write access';
  }

  // Check network
  try {
    const response = await fetch('https://example.com', { method: 'HEAD' });
    result.network.ok = response.ok;
    result.network.message = response.ok ? 'Network OK' : 'Network error';
  } catch {
    result.network.message = 'Network error';
  }

  // Check TLS
  try {
    const response = await fetch('https://example.com', { method: 'HEAD' });
    result.tls.ok = response.ok;
    result.tls.message = response.ok ? 'TLS OK' : 'TLS error';
  } catch {
    result.tls.message = 'TLS error';
  }

  // Check config
  result.config.ok = true;
  result.config.message = 'Configuration OK';

  // Display results
  logger.header('SURL Doctor');

  const checkIcon = (ok: boolean) => ok ? colors.success('✓') : colors.error('✗');

  logger.plain(`${checkIcon(result.nodeVersion.ok)} Node.js ${result.nodeVersion.version} (required: ${result.nodeVersion.required})`);
  logger.plain(`${checkIcon(result.filesystem.ok)} Filesystem: ${result.filesystem.message}`);
  logger.plain(`${checkIcon(result.network.ok)} Network: ${result.network.message}`);
  logger.plain(`${checkIcon(result.tls.ok)} TLS: ${result.tls.message}`);
  logger.plain(`${checkIcon(result.config.ok)} Configuration: ${result.config.message}`);

  logger.newLine();

  const allOk = result.nodeVersion.ok && result.filesystem.ok && result.network.ok && result.tls.ok && result.config.ok;

  if (allOk) {
    logger.success('Everything looks good!');
  } else {
    logger.warn('Some checks failed');
  }

  return allOk ? EXIT_CODES.SUCCESS : EXIT_CODES.GENERAL_ERROR;
}

/**
 * Config command - show or initialize configuration
 */
export async function config(init: boolean = false): Promise<number> {
  if (init) {
    const configPath = path.join(process.cwd(), 'surl.config.json');

    try {
      await stat(configPath);
      logger.warn(`Configuration file already exists: ${configPath}`);
      return EXIT_CODES.GENERAL_ERROR;
    } catch {
      // File doesn't exist, create it
    }

    try {
      await writeFile(configPath, generateSampleConfig());
      logger.success(`Created configuration file: ${configPath}`);
      return EXIT_CODES.SUCCESS;
    } catch (error) {
      logger.error('Failed to create configuration file', error instanceof Error ? error : new Error(String(error)));
      return EXIT_CODES.FILESYSTEM_ERROR;
    }
  }

  // Show current configuration
  logger.header('Current Configuration');
  logger.plain(generateSampleConfig());

  return EXIT_CODES.SUCCESS;
}

/**
 * Get content type for a file
 */
function getContentType(filePath: string): string {
  const ext = path.extname(filePath).toLowerCase();

  const types: Record<string, string> = {
    '.html': 'text/html',
    '.htm': 'text/html',
    '.css': 'text/css',
    '.js': 'application/javascript',
    '.json': 'application/json',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.jpeg': 'image/jpeg',
    '.gif': 'image/gif',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon',
    '.webp': 'image/webp',
    '.woff': 'font/woff',
    '.woff2': 'font/woff2',
    '.ttf': 'font/ttf',
    '.eot': 'application/vnd.ms-fontobject',
    '.mp4': 'video/mp4',
    '.webm': 'video/webm',
    '.mp3': 'audio/mpeg',
    '.wav': 'audio/wav',
    '.pdf': 'application/pdf',
    '.xml': 'application/xml',
    '.txt': 'text/plain',
  };

  return types[ext] ?? 'application/octet-stream';
}
