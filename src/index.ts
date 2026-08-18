#!/usr/bin/env node

import { createParser, parseOptions } from './cli/parser.js';
import { mirror, serve, inspect, check, clean, doctor, config } from './cli/commands.js';
import { resolveConfig } from './config/config.js';
import { logger } from './terminal/logger.js';
import { setColorEnabled } from './terminal/colors.js';
import { setSpinnerEnabled } from './terminal/spinner.js';
import { setProgressEnabled } from './terminal/progress.js';
import { EXIT_CODES } from './types/index.js';

async function main(): Promise<number> {
  const program = createParser();

  // Add subcommands
  program
    .command('serve <directory>')
    .description('Start a local HTTP server for the mirrored site')
    .option('-p, --port <number>', 'Port to listen on', (v) => parseInt(v, 10), 3000)
    .action(async (directory: string, opts: { port: number }) => {
      const exitCode = await serve(directory, opts.port);
      process.exit(exitCode);
    });

  program
    .command('inspect <directory>')
    .description('Show information about a mirrored site')
    .option('--json', 'Output as JSON')
    .action(async (directory: string, opts: { json?: boolean }) => {
      const exitCode = await inspect(directory, opts.json ?? false);
      process.exit(exitCode);
    });

  program
    .command('check <directory>')
    .description('Verify a mirrored site for broken links and missing assets')
    .option('--json', 'Output as JSON')
    .action(async (directory: string, opts: { json?: boolean }) => {
      const exitCode = await check(directory, opts.json ?? false);
      process.exit(exitCode);
    });

  program
    .command('clean <directory>')
    .description('Remove surl metadata from a mirrored site')
    .action(async (directory: string) => {
      const exitCode = await clean(directory);
      process.exit(exitCode);
    });

  program
    .command('doctor')
    .description('Check system requirements')
    .action(async () => {
      const exitCode = await doctor();
      process.exit(exitCode);
    });

  program
    .command('config')
    .description('Show or initialize configuration')
    .option('--init', 'Create a new configuration file')
    .action(async (opts: { init?: boolean }) => {
      const exitCode = await config(opts.init ?? false);
      process.exit(exitCode);
    });

  // Alias commands
  program
    .command('clone <url>')
    .description('Alias for the main command')
    .action(async (url: string) => {
      process.argv = [process.argv[0] ?? 'node', process.argv[1] ?? '', url, ...process.argv.slice(4)];
      const exitCode = await runMain();
      process.exit(exitCode);
    });

  program
    .command('mirror <url>')
    .description('Alias for the main command')
    .action(async (url: string) => {
      process.argv = [process.argv[0] ?? 'node', process.argv[1] ?? '', url, ...process.argv.slice(4)];
      const exitCode = await runMain();
      process.exit(exitCode);
    });

  program.parse();

  return runMain();
}

async function runMain(): Promise<number> {
  const program = createParser();
  program.parse();

  const { url, options } = parseOptions(program);

  // Configure output based on options
  if (options.noColor || options.ci) {
    setColorEnabled(false);
  }

  if (options.ci || options.quiet || options.json) {
    setSpinnerEnabled(false);
    setProgressEnabled(false);
  }

  // Configure logger
  logger.configure({
    verbose: options.verbose ?? false,
    quiet: options.quiet ?? false,
    debug: options.debug ?? false,
    json: options.json ?? false,
  });

  // If no URL provided, show help
  if (!url) {
    // Check if it's a subcommand (handled by commander)
    const args = process.argv.slice(2);
    const subcommands = ['serve', 'inspect', 'check', 'clean', 'doctor', 'config', 'clone', 'mirror'];

    if (args.length === 0 || !subcommands.includes(args[0] ?? '')) {
      program.help();
      return EXIT_CODES.INVALID_ARGUMENTS;
    }

    return EXIT_CODES.SUCCESS;
  }

  // Validate URL
  try {
    new URL(url);
  } catch {
    logger.error(`Invalid URL: ${url}`);
    return EXIT_CODES.INVALID_ARGUMENTS;
  }

  // Resolve configuration
  const config = await resolveConfig(url, options);

  // Run main mirror command
  return mirror(config);
}

main()
  .then((exitCode) => {
    process.exit(exitCode);
  })
  .catch((error) => {
    logger.error('Fatal error', error instanceof Error ? error : new Error(String(error)));
    process.exit(EXIT_CODES.GENERAL_ERROR);
  });
