import { colors, symbols, formatBytes, formatDuration } from './colors.js';

export type LogLevel = 'debug' | 'info' | 'success' | 'warn' | 'error';

interface LoggerOptions {
  verbose: boolean;
  quiet: boolean;
  debug: boolean;
  json: boolean;
}

class Logger {
  private options: LoggerOptions = {
    verbose: false,
    quiet: false,
    debug: false,
    json: false,
  };

  configure(options: Partial<LoggerOptions>): void {
    this.options = { ...this.options, ...options };
  }

  private shouldLog(level: LogLevel): boolean {
    if (this.options.json) return false;
    if (this.options.quiet && level !== 'error') return false;
    if (level === 'debug' && !this.options.debug) return false;
    return true;
  }

  private formatMessage(level: LogLevel, message: string): string {
    const timestamp = new Date().toISOString();

    switch (level) {
      case 'debug':
        return `${colors.gray(`[${timestamp}]`)} ${colors.muted('DEBUG')} ${message}`;
      case 'info':
        return `${symbols.info} ${message}`;
      case 'success':
        return `${symbols.tick} ${message}`;
      case 'warn':
        return `${symbols.warning} ${colors.warning(message)}`;
      case 'error':
        return `${symbols.cross} ${colors.error(message)}`;
      default:
        return message;
    }
  }

  debug(message: string, data?: unknown): void {
    if (!this.shouldLog('debug')) return;
    console.log(this.formatMessage('debug', message));
    if (data !== undefined) {
      console.log(colors.gray(JSON.stringify(data, null, 2)));
    }
  }

  info(message: string): void {
    if (!this.shouldLog('info')) return;
    console.log(this.formatMessage('info', message));
  }

  success(message: string): void {
    if (!this.shouldLog('success')) return;
    console.log(this.formatMessage('success', message));
  }

  warn(message: string): void {
    if (!this.shouldLog('warn')) return;
    console.warn(this.formatMessage('warn', message));
  }

  error(message: string, error?: Error): void {
    if (!this.shouldLog('error')) return;
    console.error(this.formatMessage('error', message));
    if (error && this.options.debug) {
      console.error(colors.gray(error.stack ?? error.message));
    }
  }

  // Verbose logging - only shown with --verbose flag
  verbose(message: string): void {
    if (!this.options.verbose || this.options.quiet) return;
    console.log(colors.muted(message));
  }

  // HTTP request logging
  http(method: string, url: string, status?: number, size?: number): void {
    if (!this.options.verbose || this.options.quiet) return;

    let statusColor = colors.green;
    if (status !== undefined) {
      if (status >= 400) statusColor = colors.red;
      else if (status >= 300) statusColor = colors.yellow;
    }

    const parts = [
      colors.muted(method),
      colors.url(url),
    ];

    if (status !== undefined) {
      parts.push(statusColor(`${status}`));
    }

    if (size !== undefined) {
      parts.push(colors.muted(formatBytes(size)));
    }

    console.log(parts.join(' '));
  }

  // Download progress logging
  download(url: string, status: 'start' | 'complete' | 'cached' | 'failed', details?: string): void {
    if (!this.options.verbose || this.options.quiet) return;

    const icon = {
      start: colors.blue('↓'),
      complete: colors.green('✓'),
      cached: colors.cyan('●'),
      failed: colors.red('✗'),
    }[status];

    const parts = [icon, colors.url(url)];
    if (details) {
      parts.push(colors.muted(`(${details})`));
    }

    console.log(parts.join(' '));
  }

  // Plain output (always shown unless JSON mode)
  plain(message: string): void {
    if (this.options.json) return;
    console.log(message);
  }

  // JSON output
  json(data: unknown): void {
    console.log(JSON.stringify(data, null, 2));
  }

  // New line
  newLine(): void {
    if (this.options.json || this.options.quiet) return;
    console.log();
  }

  // Header with title
  header(title: string): void {
    if (this.options.json || this.options.quiet) return;
    console.log();
    console.log(colors.title(title));
    console.log();
  }

  // Section with label and value
  section(label: string, value: string): void {
    if (this.options.json || this.options.quiet) return;
    console.log(`${colors.label(label + ':')} ${value}`);
  }

  // Stats table
  stats(data: Record<string, string | number>): void {
    if (this.options.json || this.options.quiet) return;

    const maxKeyLength = Math.max(...Object.keys(data).map((k) => k.length));

    for (const [key, value] of Object.entries(data)) {
      const paddedKey = key.padEnd(maxKeyLength);
      const formattedValue = typeof value === 'number' ? colors.number(value.toString()) : value;
      console.log(`${colors.label(paddedKey)}  ${formattedValue}`);
    }
  }
}

export const logger = new Logger();
