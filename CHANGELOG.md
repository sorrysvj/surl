# Changelog

All notable changes to SURL are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Nothing yet.

## [1.0.0] - 2026-08-19

First release of the native C++ implementation. This replaces the previous
TypeScript/Node.js prototype entirely: SURL is now a single self-contained
executable with no runtime dependency.

### Added

**Core**

- C++20 implementation built with CMake, producing one static `surl.exe`
  (~800 KB) that needs no redistributables.
- Native HTTP: WinHTTP on Windows, libcurl on Linux and macOS.
- Two-phase mirroring — crawl, then rewrite — so links to pages discovered
  late in a crawl are still localised correctly.
- RFC 3986 URL parser, resolver and normaliser.
- Lenient HTML scanner that records byte offsets, so rewritten documents are
  the originals with only the reference substrings replaced.
- CSS scanner covering `url()`, `@import`, inline `style=` attributes and
  `<style>` blocks — which is how web fonts get picked up.
- `srcset` / `imagesrcset` candidate rewriting, `<base href>` neutralisation,
  meta-refresh targets, and Open Graph image references.
- Deterministic URL-to-path mapping, including query-string disambiguation,
  Windows reserved-name handling and traversal-proof sanitisation.
- Manifest at `.surl/manifest.json` recording path, status, size, SHA-256,
  ETag and `Last-Modified` for every resource.
- `--resume`, `--update` (conditional requests) and `--force`.
- `robots.txt` support with longest-match evaluation, wildcards, `$` anchors
  and `Crawl-delay`.
- Budgets: `--max-file-size`, `--max-total-size`, `--max-files`.
- Politeness: `--delay`, `--rate-limit`, and a version-stamped `User-Agent`.
- Self-contained SHA-256 implementation; no crypto dependency.

**CLI**

- Commands: `clone`, `mirror`, `serve`, `inspect`, `check`, `clean`, `doctor`,
  `config`, `install`, `uninstall`, `update`, `version`.
- 40+ options covering crawl shape, transfer, budgets, run mode and logging.
- `--json` machine-readable output, `--dry-run` and `--list`.
- Distinct exit codes for success, runtime error, usage error and partial
  completion.

**Windows installer**

- Inno Setup installer with language selection (English, Русский), licence,
  installation scope, directory, shortcut and PATH pages.
- Per-user installs need no administrator rights; machine-wide installs
  request elevation through UAC.
- PATH integration scoped to the install, never duplicated, with stale SURL
  entries from earlier installs cleaned up.
- Optional download of the published release asset from GitHub, verified
  against the SHA-256 in `checksums.txt` before anything is installed.
- Uninstaller removes the program, PATH entry, shortcuts and registry keys,
  and asks — defaulting to *keep* — before removing configuration and cache.
  Downloaded websites are never touched.

**Project**

- GitHub Actions CI on Windows, Ubuntu and macOS.
- Release pipeline that builds, tests, packages, checksums and publishes the
  installer, portable ZIP and program archive on a `v*` tag.
- Test suite of 100+ cases including end-to-end crawls against a loopback
  HTTP fixture server.

### Removed

- The entire TypeScript/Node.js implementation, its npm packaging and its
  `install.ps1` bootstrap script.

[Unreleased]: https://github.com/sorrysvj/surl/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/sorrysvj/surl/releases/tag/v1.0.0
