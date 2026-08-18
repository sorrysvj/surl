<div align="center">

# surl

**Fast, modern CLI tool for local website mirroring**

[![Node.js](https://img.shields.io/badge/Node.js-18%2B-green.svg)](https://nodejs.org/)
[![TypeScript](https://img.shields.io/badge/TypeScript-5.3-blue.svg)](https://www.typescriptlang.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

[Installation](#installation) • [Usage](#usage) • [Examples](#examples) • [Configuration](#configuration) • [Documentation](#documentation)

</div>

---

## Overview

`surl` is a powerful command-line tool for creating local mirrors of websites. It downloads HTML pages along with all their assets (CSS, JavaScript, images, fonts, etc.) and rewrites URLs so the site works completely offline.

### Features

- ⚡ **Fast parallel downloads** - Configurable concurrency for optimal performance
- 🔄 **Smart caching** - Uses ETags and Last-Modified for incremental updates
- 🔗 **URL rewriting** - Automatically rewrites links for local browsing
- 📁 **Clean file structure** - Maintains original site structure
- 🤖 **robots.txt support** - Respects crawl rules by default
- 🗺️ **Sitemap support** - Discovers URLs from sitemap.xml
- 🔒 **Security built-in** - SSRF protection, path traversal prevention
- 💾 **Resume capability** - Continue interrupted downloads
- 📊 **Progress tracking** - Beautiful terminal UI with progress bars
- 🛠️ **Developer-friendly** - JSON output, CI mode, extensive CLI options

---

## Installation

### From npm

```bash
npm install -g surl
```

### From source

```bash
git clone https://github.com/user/surl.git
cd surl
npm install
npm run build
npm link
```

### Pre-built binary (Windows)

Download `surl.exe` from the [Releases](https://github.com/user/surl/releases) page and add it to your PATH.

---

## Usage

### Basic Usage

```bash
# Mirror a website to a directory named after the domain
surl https://example.com

# Mirror to a specific directory
surl https://example.com -d ./website

# Mirror with recursive crawling
surl https://example.com --recursive --max-depth 5
```

### Quick Reference

| Command | Description |
|---------|-------------|
| `surl <url>` | Mirror a website |
| `surl serve <dir>` | Start local HTTP server |
| `surl inspect <dir>` | Show mirror statistics |
| `surl check <dir>` | Verify mirror integrity |
| `surl clean <dir>` | Remove surl metadata |
| `surl doctor` | Check system requirements |
| `surl config` | Show/init configuration |

---

## Examples

### Basic Mirroring

```bash
# Simple mirror
surl https://example.com

# Custom output directory
surl https://example.com -d ./my-mirror
surl https://example.com --directory ./my-mirror
```

### Recursive Crawling

```bash
# Crawl up to 5 levels deep
surl https://example.com --recursive --max-depth 5

# Short form
surl https://example.com -r --depth 5
```

### Performance Options

```bash
# Increase concurrent downloads
surl https://example.com --concurrency 16

# Add delay between requests
surl https://example.com --delay 100
```

### Filtering

```bash
# Only download specific paths
surl https://example.com --include "/docs/*"

# Exclude certain paths
surl https://example.com --exclude "/admin/*" --exclude "/api/*"

# Only download specific file types
surl https://example.com --extensions html,css,js,png,jpg
```

### External Assets

```bash
# Download external assets (CDN, fonts, etc.)
surl https://example.com --external-assets
```

### Incremental Updates

```bash
# Resume an interrupted download
surl https://example.com -d ./site --resume

# Update an existing mirror
surl https://example.com -d ./site --update

# Force re-download everything
surl https://example.com -d ./site --force
```

### Preview Mode

```bash
# Show what would be downloaded
surl https://example.com --dry-run

# List all discovered URLs
surl https://example.com --list
```

### Custom Headers

```bash
# Add custom headers
surl https://example.com --header "Accept-Language: en-US"

# Use browser-like User-Agent
surl https://example.com --user-agent browser
```

### JSON Output

```bash
# Output results as JSON
surl https://example.com --json
```

### Using Sitemap

```bash
# Use sitemap for URL discovery
surl https://example.com --sitemap

# Only use sitemap (no link following)
surl https://example.com --sitemap-only
```

### Local Server

```bash
# Start a local server for the mirrored site
surl serve ./website

# Use custom port
surl serve ./website --port 8080
```

---

## CLI Reference

### Main Options

| Option | Short | Description | Default |
|--------|-------|-------------|---------|
| `--directory <path>` | `-d` | Output directory | Domain name |
| `--output <path>` | `-o` | Alias for --directory | - |
| `--concurrency <n>` | `-c` | Concurrent downloads | 8 |
| `--max-depth <n>` | - | Maximum crawl depth | 0 |
| `--recursive` | `-r` | Enable recursive crawling | false |
| `--timeout <ms>` | `-t` | HTTP timeout | 30000 |
| `--retries <n>` | - | Retry attempts | 3 |
| `--delay <ms>` | - | Delay between requests | 0 |

### Asset Options

| Option | Description | Default |
|--------|-------------|---------|
| `--assets` | Download assets | true |
| `--no-assets` | Skip assets | - |
| `--external-assets` | Download external assets | false |
| `--same-origin` | Only same-origin URLs | true |

### URL Rewriting

| Option | Description | Default |
|--------|-------------|---------|
| `--rewrite` | Rewrite URLs | true |
| `--no-rewrite` | Skip URL rewriting | - |
| `--query-mode` | Query handling: ignore/preserve/hash | ignore |
| `--preserve-query` | Keep query in filenames | false |

### Filtering

| Option | Description |
|--------|-------------|
| `--include <pattern>` | Include URL pattern (multiple allowed) |
| `--exclude <pattern>` | Exclude URL pattern (multiple allowed) |
| `--extensions <exts>` | Comma-separated list of extensions |

### Limits

| Option | Description | Default |
|--------|-------------|---------|
| `--max-file-size <size>` | Max file size (e.g., 50mb) | None |
| `--max-total-size <size>` | Max total size (e.g., 1gb) | None |
| `--max-files <n>` | Max number of files | None |
| `--max-redirects <n>` | Max redirects | 10 |

### HTTP Options

| Option | Short | Description |
|--------|-------|-------------|
| `--user-agent <ua>` | `-u` | Custom User-Agent |
| `--header <header>` | `-H` | Custom header (multiple allowed) |
| `--cookie <cookie>` | - | Cookie to send (multiple allowed) |
| `--proxy <url>` | - | Proxy URL |
| `--insecure` | - | Allow insecure TLS |

### Crawl Control

| Option | Description | Default |
|--------|-------------|---------|
| `--no-robots` | Ignore robots.txt | false |
| `--sitemap` | Use sitemap.xml | false |
| `--sitemap-only` | Only use sitemap | false |
| `--scan-js` | Scan JS for URLs (experimental) | false |

### Modes

| Option | Description |
|--------|-------------|
| `--resume` | Resume previous crawl |
| `--force` | Force re-download |
| `--update` | Update existing mirror |
| `--dry-run` | Preview without downloading |
| `--list` | List discovered URLs |

### Output

| Option | Short | Description |
|--------|-------|-------------|
| `--json` | - | JSON output |
| `--verbose` | `-v` | Verbose output |
| `--quiet` | `-q` | Quiet mode |
| `--debug` | - | Debug output |
| `--no-color` | - | Disable colors |
| `--ci` | - | CI mode |

---

## Configuration

### Configuration File

Create `surl.config.json` in your project directory:

```json
{
  "concurrency": 12,
  "timeout": 30000,
  "maxDepth": 5,
  "retries": 3,
  "sameOrigin": true,
  "rewrite": true,
  "include": [],
  "exclude": ["/admin/*", "/api/*"]
}
```

Generate a sample config:

```bash
surl config --init
```

### Environment Variables

| Variable | Description |
|----------|-------------|
| `SURL_CONCURRENCY` | Default concurrency |
| `SURL_TIMEOUT` | Default timeout |
| `SURL_USER_AGENT` | Default User-Agent |
| `SURL_PROXY` | Proxy URL |
| `SURL_OUTPUT` | Default output directory |

### Priority Order

1. CLI arguments (highest)
2. Environment variables
3. Configuration file
4. Default values (lowest)

---

## Building from Source

### Requirements

- Node.js 18+
- npm or pnpm

### Build Steps

```bash
# Clone the repository
git clone https://github.com/user/surl.git
cd surl

# Install dependencies
npm install

# Build
npm run build

# Link globally
npm link
```

### Building Executable (Windows)

```bash
# Build standalone .exe
npm run build:exe
```

The executable will be created at `bin/surl.exe`.

To add to PATH:

1. Copy `surl.exe` to a directory in your PATH, or
2. Add the `bin` directory to your PATH environment variable

### Development

```bash
# Run in development mode
npm run dev -- https://example.com

# Run tests
npm test

# Lint
npm run lint

# Format
npm run format
```

---

## Architecture

```
surl/
├── src/
│   ├── cli/          # Command-line interface
│   ├── crawler/      # URL crawling and discovery
│   ├── downloader/   # HTTP client and downloads
│   ├── parsers/      # HTML, CSS, JS parsing
│   ├── rewriting/    # URL rewriting
│   ├── cache/        # Caching and state management
│   ├── config/       # Configuration handling
│   ├── terminal/     # Terminal UI
│   ├── types/        # TypeScript types
│   └── utils/        # Utility functions
├── tests/            # Test files
├── docs/             # Documentation
└── scripts/          # Build scripts
```

---

## Security

### Built-in Protections

- **SSRF Protection**: Blocks requests to localhost, private IPs, and metadata endpoints
- **Path Traversal Prevention**: Validates all file paths stay within output directory
- **TLS Verification**: Enabled by default (use `--insecure` to disable)

### Limitations

- `surl` only downloads publicly accessible content
- Does not bypass authentication, CAPTCHA, or WAF
- Does not copy backend/server-side code
- Does not extract cookies from browsers

---

## Troubleshooting

### Common Issues

**"Module not found" errors**
```bash
# Rebuild the project
npm run clean
npm run build
```

**Permission denied**
```bash
# Check write permissions
surl doctor
```

**Network errors**
```bash
# Check with doctor command
surl doctor

# Try with proxy
surl https://example.com --proxy http://localhost:8080
```

**Large sites timing out**
```bash
# Increase timeout and reduce concurrency
surl https://example.com --timeout 60000 --concurrency 4
```

---

## Contributing

Contributions are welcome! Please read our [Contributing Guide](CONTRIBUTING.md) first.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## License

MIT License - see [LICENSE](LICENSE) for details.

---

## Acknowledgments

- [cheerio](https://cheerio.js.org/) - HTML parsing
- [undici](https://undici.nodejs.org/) - HTTP client
- [commander](https://github.com/tj/commander.js) - CLI framework
- [chalk](https://github.com/chalk/chalk) - Terminal colors
- [ora](https://github.com/sindresorhus/ora) - Terminal spinners

---

<div align="center">

Made with ❤️ for developers who need offline access to documentation

</div>
