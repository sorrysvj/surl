import cliProgress, { SingleBar, MultiBar, Presets } from 'cli-progress';
import { colors, formatBytes, formatSpeed, formatDuration, horizontalLine } from './colors.js';
import { ProgressStats } from '../types/index.js';

let progressEnabled = true;
let activeBar: SingleBar | null = null;

export function setProgressEnabled(enabled: boolean): void {
  progressEnabled = enabled;
}

export interface ProgressBarOptions {
  total: number;
  format?: string;
}

export function createProgressBar(options: ProgressBarOptions): SingleBar {
  if (!progressEnabled) {
    // Return a no-op progress bar
    return {
      start: () => {},
      update: () => {},
      increment: () => {},
      stop: () => {},
      setTotal: () => {},
    } as unknown as SingleBar;
  }

  // Stop any existing bar
  if (activeBar) {
    activeBar.stop();
  }

  const format = options.format ?? `${colors.highlight('Progress')} [{bar}] {percentage}% | {value}/{total}`;

  activeBar = new cliProgress.SingleBar({
    format,
    barCompleteChar: '█',
    barIncompleteChar: '░',
    hideCursor: true,
    clearOnComplete: false,
    stopOnComplete: true,
  });

  activeBar.start(options.total, 0);

  return activeBar;
}

export function stopProgressBar(): void {
  if (activeBar) {
    activeBar.stop();
    activeBar = null;
  }
}

/**
 * Display crawl statistics in a nice format
 */
export function displayStats(stats: ProgressStats, quiet: boolean = false): void {
  if (quiet) return;

  const elapsed = Date.now() - stats.startTime;

  console.log();
  console.log(horizontalLine(50));
  console.log();

  const data = {
    'Pages': stats.downloaded,
    'Assets': stats.discovered - stats.downloaded,
    'Downloaded': stats.downloaded,
    'Cached': stats.cached,
    'Failed': stats.failed,
    'Skipped': stats.skipped,
  };

  const maxKeyLength = Math.max(...Object.keys(data).map((k) => k.length));

  for (const [key, value] of Object.entries(data)) {
    const paddedKey = key.padEnd(maxKeyLength);
    let valueColor = colors.white;
    if (key === 'Failed' && value > 0) valueColor = colors.red;
    if (key === 'Downloaded' && value > 0) valueColor = colors.green;
    if (key === 'Cached' && value > 0) valueColor = colors.cyan;

    console.log(`${colors.label(paddedKey)}  ${valueColor(value.toString())}`);
  }

  console.log();
  console.log(`${colors.label('Speed')}       ${formatSpeed(stats.currentSpeed)}`);
  console.log(`${colors.label('Transferred')} ${formatBytes(stats.totalBytes)}`);
  console.log(`${colors.label('Time')}        ${formatDuration(elapsed)}`);
  console.log();
  console.log(horizontalLine(50));
}

/**
 * Display completion summary
 */
export function displaySummary(
  url: string,
  outputDir: string,
  stats: ProgressStats,
  status: 'success' | 'partial' | 'failed'
): void {
  const elapsed = Date.now() - stats.startTime;
  const avgSpeed = stats.totalBytes / (elapsed / 1000);

  console.log();
  console.log(horizontalLine(50));

  const statusText = {
    success: colors.success('SURL COMPLETE'),
    partial: colors.warning('SURL COMPLETED WITH WARNINGS'),
    failed: colors.error('SURL FAILED'),
  }[status];

  console.log(statusText);
  console.log(horizontalLine(50));
  console.log();

  console.log(`${colors.label('Target:')}          ${colors.url(url)}`);
  console.log(`${colors.label('Output:')}          ${colors.path(outputDir)}`);
  console.log();
  console.log(`${colors.label('Pages:')}           ${colors.number(stats.downloaded.toString())}`);
  console.log(`${colors.label('Assets:')}          ${colors.number((stats.discovered - stats.downloaded).toString())}`);
  console.log(`${colors.label('Downloaded:')}      ${colors.number(stats.downloaded.toString())}`);
  console.log(`${colors.label('Cached:')}          ${colors.number(stats.cached.toString())}`);
  console.log(`${colors.label('Skipped:')}         ${colors.number(stats.skipped.toString())}`);

  if (stats.failed > 0) {
    console.log(`${colors.label('Failed:')}          ${colors.error(stats.failed.toString())}`);
  }

  console.log();
  console.log(`${colors.label('Total:')}           ${formatBytes(stats.totalBytes)}`);
  console.log(`${colors.label('Time:')}            ${formatDuration(elapsed)}`);
  console.log(`${colors.label('Average speed:')}   ${formatSpeed(avgSpeed)}`);
  console.log();
  console.log(horizontalLine(50));
}

/**
 * Display header with version info
 */
export function displayHeader(version: string): void {
  console.log();
  console.log(colors.title(`surl v${version}`));
  console.log();
}

/**
 * Display target and output info
 */
export function displayTargetInfo(url: string, outputDir: string): void {
  console.log(`${colors.label('Target:')} ${colors.url(url)}`);
  console.log(`${colors.label('Output:')} ${colors.path(outputDir)}`);
  console.log();
  console.log(horizontalLine(50));
  console.log();
}
