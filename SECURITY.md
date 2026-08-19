# Security Policy

SURL fetches content from servers it does not control and writes it to your
filesystem. Everything a remote server sends is treated as hostile input. This
document describes what SURL defends against, what it deliberately does not,
and how to report a problem.

## Supported versions

| Version | Supported |
| --- | --- |
| 1.0.x | ✅ |
| < 1.0 | ❌ (pre-release TypeScript prototype, superseded) |

Fixes land in a new patch release. There are no long-term support branches.

## Reporting a vulnerability

**Please do not open a public issue for a security problem.**

Use GitHub's private reporting:
[**Report a vulnerability**](https://github.com/sorrysvj/surl/security/advisories/new)

Include, as far as you can:

- what the issue is and why it matters;
- the SURL version (`surl --version -v`) and your OS;
- a minimal reproduction — ideally a small HTML/CSS fixture or a URL;
- what you think an attacker gains.

You will get an acknowledgement within **72 hours** and an assessment within
**7 days**. Fixed issues are credited in the release notes unless you would
rather stay anonymous. SURL has no bug bounty.

## Threat model

The attacker is assumed to be **the website being mirrored**. They fully
control every URL, header, status code, redirect target, MIME type and byte of
content SURL receives, and they know the crawl is happening.

The security goals are:

1. SURL never writes outside the output directory you named.
2. SURL never reaches hosts you did not ask it to reach.
3. A hostile site cannot exhaust your disk, memory or time.
4. Credentials you supply go only to the host you supplied them for.
5. SURL never executes anything it downloads.

## Protections

### Path traversal

A URL path is entirely attacker-controlled, and `../` in it is the obvious way
to try to escape the output directory.

- URL paths are normalised with RFC 3986 `remove_dot_segments` before mapping.
- Every path component is then sanitised individually
  (`sanitize_path_component`): `.` becomes `_dot`, `..` becomes `_dotdot`, and
  `< > : " / \ | ? *`, control characters and `DEL` become `_`. Percent-encoded
  traversal (`%2e%2e`) is decoded *before* sanitisation, so it cannot sneak
  past.
- Windows reserved device names (`CON`, `NUL`, `COM1`, `LPT1`, …) are prefixed,
  and trailing dots and spaces — which Windows silently strips, letting two
  URLs collide on one file — are removed.
- Component length is capped at 180 bytes and total relative path length at
  200; longer paths collapse into a hashed name.
- As a final backstop, every write is checked with `is_inside(root, target)`,
  which compares canonicalised paths. A resource whose destination would fall
  outside the output root is refused and recorded as skipped, not written.

`surl serve` applies the same rule: request targets containing `.` or `..`
components are rejected outright rather than normalised, and the resolved file
is re-checked against the served root.

### Server-Side Request Forgery (SSRF)

SURL follows links a remote page provides, which is exactly the shape of an
SSRF primitive.

- By default SURL refuses any host that resolves to the local machine or a
  private network: `localhost` and `*.localhost`, `*.local`, `*.internal`,
  `*.home.arpa`, bare hostnames with no dot, `127.0.0.0/8`, `10/8`,
  `172.16/12`, `192.168/16`, `169.254/16` (link-local, including the cloud
  metadata address `169.254.169.254`), `100.64/10` (CGNAT), `0.0.0.0/8`,
  multicast and reserved space, `::1`, `fc00::/7`, `fe80::/10`, and
  IPv4-mapped forms of all of the above.
- This applies to the starting URL **and** to every link discovered during the
  crawl, so a public page cannot redirect the crawler onto your intranet.
- `--allow-private` disables the check. It exists for mirroring your own
  development server; do not use it on untrusted input.
- Redirects are limited (`--max-redirects`, default 10) and an HTTPS origin is
  never allowed to redirect down to plaintext HTTP.
- Only `http` and `https` are ever fetched. `file:`, `ftp:`, `data:`,
  `javascript:`, `mailto:` and friends are recognised and left alone.

**Known limitation:** the check is performed on the hostname, not on the
resolved address, so a DNS name that resolves to a private address (DNS
rebinding) is not caught. Do not point SURL at untrusted input from inside a
network whose reachability is itself the secret.

### Resource exhaustion

A hostile or merely enormous site must not be able to fill your disk or hang
the process.

| Limit | Default | Flag |
| --- | --- | --- |
| Per-file size | 64 MB | `--max-file-size` |
| Total mirror size | 2 GB | `--max-total-size` |
| File count | 5000 | `--max-files` |
| Link depth | 5 | `--max-depth` |
| Per-request timeout | 30 s | `--timeout` |
| Retries | 2 | `--retries` |

Downloads are aborted *while streaming* once the per-file limit is passed, so
an infinite response body cannot exhaust memory. The total-size and file-count
budgets stop the crawl cleanly and report why. Each URL is fetched at most once
per run, so link cycles terminate.

JSON parsing has a nesting depth limit (100) to prevent stack exhaustion from a
hostile web app manifest.

### TLS

- Windows uses WinHTTP with the system certificate store; Linux and macOS use
  libcurl with the system trust store. SURL ships no bundled CA list that could
  go stale.
- TLS 1.2 is the floor; TLS 1.3 is requested where the platform supports it.
- Certificates are verified by default.
- `--insecure` disables verification. It prints a warning every run, and is
  intended for a self-signed development server only. Using it on the public
  internet means any network position can read and rewrite what you download.

### Credential handling

- SURL has no credential store, no login flow and no session management.
- `--header` and `--cookie` values are sent **only** to the host you started
  the crawl on and hosts you allow via origin settings — they are attached to
  requests SURL makes, and SURL only makes requests to URLs that pass the scope
  filters.
- Credentials are never written to the manifest, never logged (even at
  `--debug`, only header *names* appear), and never included in `--json`
  output.
- Any userinfo in a URL (`https://user:pass@host/`) is kept only for the
  request; it is never used in the on-disk path and never written to the
  manifest.
- SURL does not read your browser profile, cookie jar or credential manager.

Be aware that a mirrored page can itself contain secrets — a session token
embedded in HTML, for instance. Treat the output directory with the same care
as the pages you fetched.

### Crawler politeness

Aggressive crawling is a denial-of-service against someone else's server.

- `robots.txt` is honoured by default, with longest-match evaluation,
  wildcards, `$` anchors and `Crawl-delay`. `--no-robots` exists for sites you
  control.
- The default `User-Agent` identifies SURL and links to this repository, so an
  operator can see who is fetching and contact you.
- `--delay` and `--rate-limit` throttle a run; `--concurrency` bounds
  parallelism (default 2× cores, hard maximum 32).
- Failed requests back off exponentially, capped at 8 s.

### Code execution

SURL **never executes downloaded content**. It does not evaluate JavaScript,
does not render pages, does not spawn a browser and does not run any code from
the network. Downloaded files are opaque bytes; only HTML and CSS are parsed,
by hand-written scanners that produce byte offsets rather than executing
anything.

Written files are not marked executable, and SURL does not associate itself
with any file type, so double-clicking a mirrored file does nothing SURL
arranged.

### Filesystem integrity

- Files are written to a unique temporary sibling and then renamed into place,
  so an interrupted run never leaves a half-written asset that a later
  `--resume` would trust.
- The manifest records a SHA-256 of every stored file; `surl check` verifies
  the mirror against it.
- SURL only ever deletes what you tell it to: `surl clean` removes its own
  `.surl` metadata, and `surl clean --all` requires `--force` and refuses to
  run on a directory with no SURL manifest.

## Installer and release security

- Release artefacts are built by GitHub Actions from tagged source; the
  workflows are in [`.github/workflows`](.github/workflows).
- Workflow permissions are minimal: `contents: read` everywhere except the
  single publish job, which has `contents: write`.
- Every release ships `checksums.txt` with SHA-256 for each asset. Verify your
  download before running it.
- The build toolchain is pinned: Inno Setup is downloaded from its official
  GitHub releases at a fixed version and **verified against a known SHA-256**
  before installation ([`scripts/install-innosetup.ps1`](scripts/install-innosetup.ps1)).
- The installer's optional download path fetches only from
  `https://github.com/sorrysvj/surl/releases/download/...`, verifies the
  archive against the `checksums.txt` published with that release, and
  **aborts the installation** on any mismatch rather than installing an
  unverified file. Temporary files are removed on both success and failure.
- The installer never downloads or runs source code, build tooling or
  third-party executables.
- Per-user installation requires no administrator rights. Machine-wide
  installation requests elevation through UAC and only then writes to
  `Program Files` and the system PATH.
- `surl.exe` ships an application manifest requesting `asInvoker` — the CLI
  never silently elevates.

### Release integrity is not code signing

SURL's binaries are **not** currently signed with an Authenticode certificate.
SmartScreen will warn on first run. Verify the SHA-256 from `checksums.txt`
against your download; that is the integrity guarantee on offer today.

## Updates

`surl update` only *checks* GitHub for a newer version and tells you where to
get it. SURL never updates itself, never downloads a binary in the background
and runs no background processes or telemetry of any kind.

## Reporting scope

In scope: path traversal, SSRF, resource exhaustion leading to crash or
disk exhaustion, credential leakage, integrity failures in the installer or
release pipeline, memory-safety bugs.

Out of scope: mirroring a site that forbids it in its terms of service (a
legal question, not a vulnerability); `--insecure`, `--no-robots` and
`--allow-private` behaving as documented; SmartScreen warnings caused by the
lack of code signing; DNS rebinding, as noted above.
