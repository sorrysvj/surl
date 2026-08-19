<div align="center">

<img src="installer/windows/assets/surl-256.png" alt="SURL" width="112" height="112">

# SURL

**Fast native C++ CLI tool for creating local copies of publicly accessible web resources.**

[![CI](https://github.com/sorrysvj/surl/actions/workflows/ci.yml/badge.svg)](https://github.com/sorrysvj/surl/actions/workflows/ci.yml)
[![Release](https://github.com/sorrysvj/surl/actions/workflows/release.yml/badge.svg)](https://github.com/sorrysvj/surl/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](CMakeLists.txt)

[Download](https://github.com/sorrysvj/surl/releases/latest) ·
[Quick start](#quick-start) ·
[CLI reference](#cli) ·
[Security](SECURITY.md)

</div>

---

```bash
surl https://example.com -d ./website
```

That is the whole idea: point SURL at a public page and get a working offline
copy — HTML, CSS, JavaScript, images, fonts, media, favicons and web app
manifests — with every link rewritten so the copy opens correctly from disk.

## Features

- **Native and self-contained.** One `surl.exe`, ~800 KB, no runtime, no
  Node.js, no Python, no DLLs to install.
- **Complete asset capture.** Follows `<link>`, `<script>`, `<img>` (including
  `srcset`), `<video>`/`<audio>`/`<source>`, `<object>`, inline `style=`
  attributes, `<style>` blocks, and `url()` / `@import` inside stylesheets —
  which is how it picks up web fonts most tools miss.
- **Correct link rewriting.** Mirrored resources become relative paths;
  anything not mirrored is turned into an absolute URL so the page still works.
  Fragments, `<base href>`, redirects and `srcset` candidates are all handled.
- **Resumable and updatable.** `--resume` continues an interrupted run;
  `--update` re-checks every resource with conditional requests, so an
  unchanged site costs one `304` per file instead of a full re-download.
- **Polite by default.** Honours `robots.txt`, identifies itself in the
  `User-Agent`, and supports `--delay` and `--rate-limit`.
- **Safe by default.** Refuses private/loopback targets, enforces size and file
  budgets, and can never write outside the output directory.
- **Parallel.** A configurable worker pool saturates the network without
  hammering one host.

## Installation

### Windows installer (recommended)

1. Open [GitHub Releases](https://github.com/sorrysvj/surl/releases/latest).
2. Download **`surl-windows-x64-installer.exe`**.
3. Run it and choose your language (English or Русский).
4. Choose the installation scope — *Only for me* or *All users*.
5. Choose the installation directory (or accept the default).
6. Leave **Add SURL to PATH** enabled.
7. Install.
8. Open a **new** terminal.
9. Run:

   ```powershell
   surl --version
   ```

*Only for me* installs to `%LOCALAPPDATA%\Programs\SURL` and never asks for
administrator rights. *All users* installs to `C:\Program Files\SURL` and
requests elevation through UAC.

### Portable

Download **`surl-windows-x64-portable.zip`**, extract it anywhere, and run:

```powershell
.\surl.exe --help
```

The portable build changes nothing on your system: no PATH edits, no registry
entries, no uninstaller. Delete the folder and it is gone. If you later want it
on your PATH, run `surl install`.

### Verifying your download

Every release ships `checksums.txt`:

```powershell
(Get-FileHash .\surl-windows-x64-installer.exe -Algorithm SHA256).Hash
```

Compare it with the matching line in `checksums.txt`.

### Building from source

See [Building](#building).

## Quick start

```bash
surl https://example.com -d ./website
```

```
website/
├── index.html
├── css/
│   └── site.css
├── img/
│   └── logo.png
├── js/
│   └── app.js
└── .surl/            SURL's own metadata (manifest + staged originals)
```

Open `website/index.html` in a browser, or serve it properly:

```bash
surl serve ./website
```

## CLI

### Commands

| Command | What it does |
| --- | --- |
| `surl <url>` | Clone one page plus the assets it needs (default) |
| `surl clone <url>` | The same, written explicitly |
| `surl mirror <url>` | Recursively mirror a whole site (implies `--recursive`) |
| `surl serve <dir>` | Serve a mirrored directory over HTTP |
| `surl inspect <dir>` | Summarise a mirror: pages, assets, sizes, failures |
| `surl check <dir>` | Verify stored files against the manifest checksums |
| `surl clean <dir>` | Remove SURL metadata (`--all` also removes the files) |
| `surl doctor` | Report environment, PATH and network diagnostics |
| `surl config` | Show, get or set user configuration |
| `surl install` | Add this executable's directory to your PATH |
| `surl uninstall` | Remove it from your PATH again |
| `surl update [--check]` | Check GitHub releases for a newer SURL |
| `surl version [--check]` | Print the version |

### Options

**Output**

| Option | Default | Description |
| --- | --- | --- |
| `-d`, `--directory <dir>` | `./<host>` | Where to write the mirror |
| `-o`, `--output <dir>` | | Alias for `--directory` |

**Crawl shape**

| Option | Default | Description |
| --- | --- | --- |
| `-r`, `--recursive` | off | Follow links to other pages |
| `--max-depth <n>` | `5` | Maximum link depth when recursing |
| `--include <glob>` | | Only fetch URLs matching this pattern (repeatable) |
| `--exclude <glob>` | | Never fetch URLs matching this pattern (repeatable) |
| `--same-origin` | on | Restrict pages to the starting origin |
| `--cross-origin` | | Allow pages on other hosts |
| `--external-assets` | on | Fetch assets from other hosts |
| `--no-external-assets` | | Leave third-party assets as absolute URLs |
| `--no-assets` | | Fetch pages only, no images/CSS/JS |
| `--no-rewrite` | | Store original bytes without rewriting links |

**Transfer**

| Option | Default | Description |
| --- | --- | --- |
| `-c`, `--concurrency <n>` | 2× cores, max 32 | Parallel downloads |
| `-t`, `--timeout <dur>` | `30s` | Per-request timeout |
| `--retries <n>` | `2` | Retries per failed request |
| `--delay <dur>` | `0` | Pause between requests on each worker |
| `--rate-limit <size>` | unlimited | Cap total download rate, e.g. `500k` |
| `--proxy <url>` | system | HTTP proxy |
| `-H`, `--header <h>` | | Extra request header `Name: value` (repeatable) |
| `--cookie <c>` | | Cookie `name=value` (repeatable) |
| `-A`, `--user-agent <ua>` | `surl/<version>` | Override the User-Agent |
| `--insecure` | off | Do not verify TLS certificates (**dangerous**) |
| `--no-robots` | off | Ignore `robots.txt` |
| `--allow-private` | off | Permit localhost and private-network targets |

**Budgets**

| Option | Default | Description |
| --- | --- | --- |
| `--max-file-size <size>` | `64M` | Skip resources larger than this |
| `--max-total-size <size>` | `2G` | Stop once the mirror reaches this |
| `--max-files <n>` | `5000` | Stop after this many files |

**Run mode**

| Option | Description |
| --- | --- |
| `--resume` | Continue an interrupted run, skipping finished files |
| `--update` | Re-check every resource with conditional requests |
| `--force` | Re-download everything, ignoring the cache |
| `--dry-run` | Show what would be fetched without writing anything |
| `--list` | List the URLs that would be fetched, then exit |
| `--json` | Emit machine-readable JSON on stdout |

**Serve**

| Option | Default | Description |
| --- | --- | --- |
| `-p`, `--port <n>` | `8080` | Port for `surl serve` |
| `--bind <address>` | `127.0.0.1` | Interface to bind |

**Logging**

| Option | Description |
| --- | --- |
| `-v`, `--verbose` | Log every resource |
| `--debug` | Log protocol-level detail |
| `-q`, `--quiet` | Errors only |
| `--no-color` | Disable ANSI colour |
| `--ci` | Non-interactive output for build logs |
| `-h`, `--help` | Show help |
| `-V`, `--version` | Show the version |

### Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success |
| `1` | Runtime error |
| `2` | Usage error (bad arguments) |
| `3` | Completed, but some resources failed |

## Examples

Mirror a documentation site three levels deep:

```bash
surl mirror https://example.com -d ./site --max-depth 3
```

Grab only the HTML, no assets:

```bash
surl https://example.com -d ./pages --no-assets
```

Pick up where an interrupted run stopped:

```bash
surl https://example.com -d ./site --resume
```

Refresh an existing mirror cheaply:

```bash
surl https://example.com -d ./site --update
```

See what would be fetched without writing anything:

```bash
surl https://example.com --list
```

Restrict to a section of a site, politely:

```bash
surl mirror https://example.com -d ./docs --include "**/docs/**" --delay 500ms --rate-limit 1M
```

Machine-readable output for scripting:

```bash
surl https://example.com -d ./site --json --quiet
```

Preview the result:

```bash
surl serve ./site -p 8080
```

## Configuration

`surl config` manages a small JSON file of defaults:

| Platform | Location |
| --- | --- |
| Windows | `%APPDATA%\SURL\config.json` |
| Linux | `$XDG_CONFIG_HOME/surl/config.json` |
| macOS | `~/Library/Application Support/SURL/config.json` |

```bash
surl config show
surl config set concurrency 16
surl config set user_agent "my-crawler/1.0"
surl config get concurrency
surl config unset user_agent
surl config path
```

Command-line flags always override the config file, which always overrides the
built-in defaults.

Cache and logs live separately, and are never written inside the installation
directory:

| Platform | Cache / logs |
| --- | --- |
| Windows | `%LOCALAPPDATA%\SURL\cache`, `%LOCALAPPDATA%\SURL\logs` |
| Linux | `$XDG_CACHE_HOME/surl`, `$XDG_STATE_HOME/surl/logs` |
| macOS | `~/Library/Caches/SURL`, `~/Library/Logs/SURL` |

## Windows installer

The installer is built with [Inno Setup](https://jrsoftware.org/isinfo.php) 6;
its source is [`installer/windows/surl.iss`](installer/windows/surl.iss) so the
whole build is reproducible from this repository.

It walks through: language → welcome → licence → installation scope →
directory → shortcuts and PATH → installation source → ready → installing →
finish.

- **Languages:** English and Русский, with strings generated by
  [`scripts/make_translations.py`](scripts/make_translations.py); adding
  Deutsch, Français or Español is one dictionary plus one line in the `.iss`.
- **Scope:** *Only for me* (no UAC, `%LOCALAPPDATA%\Programs\SURL`) or
  *All users* (UAC, `C:\Program Files\SURL`).
- **PATH:** optional, scoped to the install (user PATH or system PATH), never
  duplicated, and stale SURL entries from previous installs are cleaned up.
- **Installation source:** the program files bundled in the installer (default,
  works offline) or the published release asset downloaded from GitHub and
  verified against the SHA-256 in `checksums.txt`. A failed download or a
  checksum mismatch aborts the installation instead of leaving a partial one.
- **Upgrades:** running a newer installer over an older install replaces the
  program files and keeps your PATH entry and configuration.

### Uninstalling

**Settings → Apps → Installed apps → SURL → Uninstall**, or
**Start Menu → SURL → Uninstall SURL**.

The uninstaller removes the installation directory, the PATH entry, the
shortcuts and the registry keys it created. It then asks whether to remove
SURL's configuration and cache — **the default is to keep them**.

**Websites you have downloaded are never touched.** If you ran
`surl https://example.com -d D:\Projects\website`, that directory is yours;
no part of SURL will ever delete it.

## Portable version

`surl-windows-x64-portable.zip` contains `surl.exe`, the licence, the README
and a short note. It does not modify PATH, does not write registry entries and
installs nothing.

## Building

Requirements:

- CMake 3.21+
- A C++20 compiler — MSVC 19.3+ (VS 2022/2026 Build Tools), GCC 11+, or Clang 14+
- Ninja (recommended)
- On Linux/macOS: libcurl development headers. On Windows nothing extra —
  networking uses WinHTTP from the OS.

### Windows

```powershell
./scripts/build.ps1 -Config Release -Test
```

The script locates MSVC via `vswhere`, imports the build environment, then
configures and builds with Ninja. Or do it by hand from a Developer prompt:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

### Linux / macOS

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

### Packaging

```powershell
./scripts/package.ps1
```

Produces, in `dist/`:

```
surl-windows-x64-installer.exe
surl-windows-x64-portable.zip
surl-windows-x64.zip
checksums.txt
```

## Development

```
surl/
├── include/surl/        public headers
│   ├── cli/             option parsing, help text
│   ├── cmd/             command implementations
│   ├── mirror/          crawler, cache, path mapping, rewriting
│   ├── net/             URL, HTTP client, robots.txt
│   ├── parse/           HTML and CSS scanners
│   └── util/            strings, JSON, hashing, filesystem, paths
├── src/                 implementations, mirroring the include layout
├── tests/               test suite plus a loopback HTTP fixture server
├── installer/windows/   Inno Setup source, icon, wizard art, translations
├── scripts/             build, package, checksum, asset generation
├── docs/                architecture and usage notes
└── .github/workflows/   CI and release pipelines
```

The test suite uses a small built-in framework (`tests/test_framework.hpp`) and
a real HTTP server bound to an ephemeral loopback port, so the crawler,
downloader, cache, resume and rewriting paths are exercised end to end rather
than mocked.

```powershell
./build/Release/tests/surl_tests.exe            # everything
./build/Release/tests/surl_tests.exe integration  # one suite
```

## Architecture

A run has two phases, which is what makes correct rewriting possible.

**Phase 1 — crawl.** A worker pool pulls URLs from a queue. Each response is
classified (HTML, CSS, JS, image, font, media, manifest), mapped to a
deterministic path under the output root, and stored. Binary assets are written
straight to their final location; HTML and CSS are staged untouched under
`.surl/raw/`. New links are discovered and queued.

**Phase 2 — rewrite.** Once every URL-to-path mapping is known, the staged
documents are rewritten and written to their final locations. Doing this second
is the only way a link to a page discovered *later* in the crawl can be
localised correctly.

Rewriting works on byte offsets recorded by the scanners, so the output is the
original document with only the reference substrings replaced — no
reserialisation, no reformatting, no lost markup quirks.

The manifest at `.surl/manifest.json` records every URL with its path, status,
size, SHA-256, ETag and `Last-Modified`. That is what `--resume`, `--update`,
`surl check` and `surl inspect` all read.

## Security

SURL fetches remote content and writes files, so it treats every server
response as untrusted. Path traversal, SSRF, resource exhaustion, TLS and
credential handling are covered in [SECURITY.md](SECURITY.md), along with how
to report a vulnerability.

## Limitations

SURL copies what a browser would receive for a plain request. It does not:

- **execute JavaScript** — content rendered client-side will not appear;
- **rewrite URLs built inside JavaScript** — string-concatenated paths in JS
  are left alone, because guessing at them corrupts working code more often
  than it helps;
- **bypass authentication, paywalls or bot protection** — use `--header` or
  `--cookie` to pass credentials you already legitimately hold;
- **follow `<iframe>` content across origins** unless you allow it;
- **capture WebSocket, XHR or streaming media traffic.**

Mirroring a site can be subject to copyright and to the site's terms of use.
SURL honours `robots.txt` by default; deciding whether you may copy a
particular site is your responsibility.

## Contributing

Bug reports, fixes and translations are welcome — see
[CONTRIBUTING.md](CONTRIBUTING.md). Adding an installer language is a
particularly easy first contribution.

## License

[MIT](LICENSE) © 2026 sorrysvj
