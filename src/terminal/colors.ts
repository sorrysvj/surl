import chalk from 'chalk';

let colorEnabled = true;

export function setColorEnabled(enabled: boolean): void {
  colorEnabled = enabled;
}

export function isColorEnabled(): boolean {
  return colorEnabled;
}

function wrap(fn: (s: string) => string) {
  return (text: string): string => (colorEnabled ? fn(text) : text);
}

export const colors = {
  // Basic colors
  red: wrap(chalk.red),
  green: wrap(chalk.green),
  yellow: wrap(chalk.yellow),
  blue: wrap(chalk.blue),
  magenta: wrap(chalk.magenta),
  cyan: wrap(chalk.cyan),
  white: wrap(chalk.white),
  gray: wrap(chalk.gray),

  // Bright colors
  brightRed: wrap(chalk.redBright),
  brightGreen: wrap(chalk.greenBright),
  brightYellow: wrap(chalk.yellowBright),
  brightBlue: wrap(chalk.blueBright),
  brightMagenta: wrap(chalk.magentaBright),
  brightCyan: wrap(chalk.cyanBright),
  brightWhite: wrap(chalk.whiteBright),

  // Styles
  bold: wrap(chalk.bold),
  dim: wrap(chalk.dim),
  italic: wrap(chalk.italic),
  underline: wrap(chalk.underline),

  // Semantic colors
  success: wrap(chalk.green),
  error: wrap(chalk.red),
  warning: wrap(chalk.yellow),
  info: wrap(chalk.blue),
  highlight: wrap(chalk.cyan),
  muted: wrap(chalk.gray),

  // Combined styles
  title: wrap((s: string) => chalk.bold.cyan(s)),
  url: wrap((s: string) => chalk.underline.blue(s)),
  path: wrap((s: string) => chalk.yellow(s)),
  number: wrap((s: string) => chalk.magenta(s)),
  label: wrap((s: string) => chalk.dim(s)),
};

// Symbols
export const symbols = {
  tick: colorEnabled ? chalk.green('✓') : '[OK]',
  cross: colorEnabled ? chalk.red('✗') : '[FAIL]',
  warning: colorEnabled ? chalk.yellow('⚠') : '[WARN]',
  info: colorEnabled ? chalk.blue('ℹ') : '[INFO]',
  arrow: colorEnabled ? chalk.cyan('→') : '->',
  bullet: colorEnabled ? '•' : '*',
  line: '─',
};

/**
 * Format bytes to human readable string
 */
export function formatBytes(bytes: number): string {
  if (bytes === 0) return '0 B';

  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  const k = 1024;
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  const value = bytes / Math.pow(k, i);

  return `${value.toFixed(i > 0 ? 2 : 0)} ${units[i]}`;
}

/**
 * Format duration in milliseconds to human readable string
 */
export function formatDuration(ms: number): string {
  if (ms < 1000) return `${ms}ms`;

  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours = Math.floor(minutes / 60);

  if (hours > 0) {
    const m = minutes % 60;
    const s = seconds % 60;
    return `${hours.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
  }

  if (minutes > 0) {
    const s = seconds % 60;
    return `${minutes.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
  }

  return `${seconds}s`;
}

/**
 * Format a speed value (bytes per second)
 */
export function formatSpeed(bytesPerSecond: number): string {
  return `${formatBytes(bytesPerSecond)}/s`;
}

/**
 * Draw a horizontal line
 */
export function horizontalLine(width = 40): string {
  return symbols.line.repeat(width);
}

/**
 * Truncate a string to a maximum length with ellipsis
 */
export function truncate(str: string, maxLength: number): string {
  if (str.length <= maxLength) return str;
  return str.slice(0, maxLength - 3) + '...';
}
