<div align="center">

# surl

**⚡ Fast CLI tool for mirroring websites locally**

[![Node.js](https://img.shields.io/badge/Node.js-18%2B-green.svg)](https://nodejs.org/)
[![TypeScript](https://img.shields.io/badge/TypeScript-5.3-blue.svg)](https://www.typescriptlang.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Download HTML, CSS, JS, images with automatic URL rewriting for offline browsing

[Installation](#installation) • [Usage](#usage) • [Examples](#examples) • [Building](#building-from-source)

</div>

---

## ✨ Features

- ⚡ **Fast parallel downloads** — Configurable concurrency for optimal performance
- 🔄 **Smart caching** — Uses ETags and Last-Modified for incremental updates
- 🔗 **URL rewriting** — Automatically rewrites links for local browsing
- 📁 **Clean file structure** — Maintains original site structure
- 🤖 **robots.txt support** — Respects crawl rules by default
- 🗺️ **Sitemap support** — Discovers URLs from sitemap.xml
- 🔒 **Security built-in** — SSRF protection, path traversal prevention
- 💾 **Resume capability** — Continue interrupted downloads
- 📊 **Progress tracking** — Beautiful terminal UI with progress bars
- 🛠️ **Developer-friendly** — JSON output, CI mode, extensive CLI options

---

## 📦 Installation

### npm (recommended)

```bash
npm install -g surl
```

### npx (without installation)

```bash
npx surl https://example.com -d ./site
```

### yarn

```bash
yarn global add surl
```

### pnpm

```bash
pnpm add -g surl
```

### From source

```bash
git clone https://github.com/sorrysvj/surl.git
cd surl
npm install
npm run build
npm link
```

### Windows executable

Download `surl.exe` from [Releases](https://github.com/sorrysvj/surl/releases) and add to PATH.

---

## 🚀 Usage

### Basic

```bash
# Mirror a website
surl https://example.com

# Mirror to specific directory
surl https://example.com -d ./website

# Recursive crawl with depth limit
surl https://example.com --recursive --max-depth 5
```

### Advanced

```bash
# High concurrency download
surl https://example.com -c 16

# Include only specific paths
surl https://example.com --include "/docs/*"

# Exclude paths
surl https://example.com --exclude "/admin/*" --exclude "/api/*"

# Download external assets (CDN, fonts)
surl https://example.com --external-assets

# Resume interrupted download
surl https://example.com -d ./site --resume

# Preview without downloading
surl https://example.com --dry-run

# JSON output for scripting
surl https://example.com --json
```

### Commands

| Command | Description |
|---------|-------------|
| `surl <url>` | Mirror a website |
| `surl serve <dir>` | Start local HTTP server |
| `surl inspect <dir>` | Show mirror statistics |
| `surl check <dir>` | Verify mirror integrity |
| `surl clean <dir>` | Remove surl metadata |
| `surl doctor` | Check system requirements |
| `surl config --init` | Create config file |

---

## ⚙️ Options

### Main Options

| Option | Short | Description | Default |
|--------|-------|-------------|---------|
| `--directory` | `-d` | Output directory | Domain name |
| `--concurrency` | `-c` | Concurrent downloads | 8 |
| `--max-depth` | | Maximum crawl depth | 0 |
| `--recursive` | `-r` | Enable recursive crawling | false |
| `--timeout` | `-t` | HTTP timeout (ms) | 30000 |
| `--retries` | | Retry attempts | 3 |

### Asset Options

| Option | Description | Default |
|--------|-------------|---------|
| `--assets` | Download assets | true |
| `--no-assets` | Skip assets | - |
| `--external-assets` | Download CDN assets | false |
| `--same-origin` | Only same-origin URLs | true |

### Output Options

| Option | Short | Description |
|--------|-------|-------------|
| `--json` | | JSON output |
| `--verbose` | `-v` | Verbose output |
| `--quiet` | `-q` | Quiet mode |
| `--dry-run` | | Preview mode |

### All Options

```bash
surl --help
```

---

## 🔧 Configuration

Create `surl.config.json`:

```json
{
  "concurrency": 12,
  "timeout": 30000,
  "maxDepth": 5,
  "retries": 3,
  "sameOrigin": true,
  "rewrite": true,
  "exclude": ["/admin/*", "/api/*"]
}
```

Or generate with:

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

---

## 🏗️ Building from Source

### Requirements

- Node.js 18+
- npm or pnpm

### Build

```bash
# Clone
git clone https://github.com/sorrysvj/surl.git
cd surl

# Install dependencies
npm install

# Build TypeScript
npm run build

# Link globally
npm link

# Now you can use:
surl --version
```

### Build Windows Executable

```bash
# Build .exe file
npm run build:exe

# The executable will be at bin/surl.exe
```

### Development

```bash
# Run in dev mode
npm run dev -- https://example.com

# Run tests
npm test

# Lint
npm run lint

# Format
npm run format
```

---

## 📁 Project Structure

```
surl/
├── src/
│   ├── cli/          # Command-line interface
│   ├── crawler/      # URL crawling and discovery
│   ├── downloader/   # HTTP client and downloads
│   ├── parsers/      # HTML, CSS, JS parsing
│   ├── rewriting/    # URL rewriting
│   ├── cache/        # Caching and state
│   ├── config/       # Configuration
│   ├── terminal/     # Terminal UI
│   └── utils/        # Utilities
├── tests/            # Unit tests
├── dist/             # Compiled JavaScript
└── bin/              # Executables
```

---

## 🔒 Security

- **SSRF Protection** — Blocks requests to localhost and private IPs
- **Path Traversal Prevention** — Validates all file paths
- **TLS Verification** — Enabled by default

### Limitations

- Only downloads publicly accessible content
- Does not bypass authentication or CAPTCHA
- Does not copy server-side code

---

## 🤝 Contributing

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md).

1. Fork the repository
2. Create feature branch (`git checkout -b feature/amazing`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push (`git push origin feature/amazing`)
5. Open Pull Request

---

## 📄 License

[MIT](LICENSE) © 2026 sorrysvj

---

<div align="center">

**[⬆ Back to top](#surl)**

Made with ❤️ for developers who need offline access to documentation

</div>
