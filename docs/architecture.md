# SURL architecture

This document explains how SURL is put together and, more usefully, *why* the
awkward parts are the way they are.

## The core problem

Mirroring a website sounds like "download files and fix the links". The
difficulty is that **you cannot fix a link until you know whether its target
will be part of the mirror**, and you do not know that until the crawl is
finished.

Consider `index.html` containing `<a href="/about">`. When `index.html` is
downloaded, `/about` has not been fetched yet. Three outcomes are possible:

1. `/about` gets mirrored → the link must become `about.html`.
2. `/about` is filtered out, or fails → the link must become
   `https://example.com/about`, so it still works when the page is opened from
   disk.
3. `/about` is mirrored to a different path than you would guess, because the
   server returned a redirect, or a content type that changes the file
   extension.

Rewriting eagerly, as each document arrives, gets all three wrong. So SURL
does not.

## Two phases

```
                  ┌─────────────────────────────────────────┐
   start URL ───► │             PHASE 1: CRAWL              │
                  │                                         │
                  │  queue ──► worker pool ──► HTTP client   │
                  │    ▲            │                       │
                  │    │            ▼                       │
                  │    │      classify (MIME/extension)     │
                  │    │            │                       │
                  │    │            ▼                       │
                  │    │      map URL ──► relative path     │
                  │    │            │                       │
                  │    │      ┌─────┴──────┐                │
                  │    │      ▼            ▼                │
                  │    │   binary      HTML / CSS           │
                  │    │  → final      → .surl/raw/         │
                  │    │   location         │               │
                  │    │                    ▼               │
                  │    └──── discover links (scan)          │
                  │                                         │
                  │            manifest records everything  │
                  └─────────────────┬───────────────────────┘
                                    │  every URL→path mapping now known
                  ┌─────────────────▼───────────────────────┐
                  │            PHASE 2: REWRITE             │
                  │                                         │
                  │  for each staged HTML/CSS document:     │
                  │    resolve every reference against the  │
                  │    manifest ──► relative path, or       │
                  │                 absolute URL            │
                  │    splice replacements by byte offset   │
                  │    write to the final location          │
                  └─────────────────────────────────────────┘
```

**Phase 1** fetches everything. Binary assets go straight to their final
location — there is nothing in them to rewrite. HTML and CSS are staged
*untouched* under `.surl/raw/` at a name derived from the SHA-256 of their URL.

**Phase 2** runs once the queue is empty. Now the manifest knows the final path
of every resource, so each reference can be resolved correctly, and the staged
documents are rewritten into place.

Keeping the raw copies is what makes `--resume` work: a resumed run can
regenerate the output from the originals rather than re-rewriting
already-rewritten files, which would be subtly wrong. It costs disk only for
text resources, which are small next to images and video.

## Modules

| Area | Responsibility |
| --- | --- |
| `net/url` | RFC 3986 parsing, resolution, normalisation, private-host detection |
| `net/http_client` | Backend-agnostic request/response types |
| `net/http_winhttp` | Windows backend (WinHTTP, no external dependency) |
| `net/http_curl` | POSIX backend (libcurl) |
| `net/robots` | robots.txt parsing and longest-match evaluation |
| `parse/html` | Lenient HTML scanner producing byte-offset link records |
| `parse/css` | `url()` / `@import` scanner, same offset approach |
| `mirror/pathmap` | Deterministic URL → filesystem path mapping |
| `mirror/cache` | The manifest: load, save, query |
| `mirror/rewrite` | Reference resolution and byte-offset splicing |
| `mirror/crawler` | Queue, worker pool, filters, budgets, both phases |
| `cli/options` | Command line and config file |
| `cmd/*` | One file per group of commands |
| `util/*` | Strings, JSON, SHA-256, filesystem, platform paths, logging |

Everything except `main()` is in the `surl_core` static library so the tests
link against exactly the code that ships.

## Byte-offset rewriting

The scanners do not build a DOM. They walk the bytes and record, for each
reference, its **offset and length in the original document** plus the decoded
value:

```cpp
struct HtmlLink {
    std::size_t offset;   // byte offset of the raw attribute value
    std::size_t length;   // its byte length
    std::string value;    // entity-decoded
    LinkRole role;        // Navigation / Asset / SrcSet / InlineStyle / ...
    ...
};
```

Rewriting collects `{offset, length, replacement}` triples, sorts them, and
splices. The output is therefore **the original document with only the
reference substrings changed** — no reserialisation, no reformatting, no
normalised quotes, no lost `<!--[if IE]>` conditional comments, no dropped
non-standard attributes.

This matters because real HTML is full of quirks that a strict parser would
"fix" into something that renders differently.

The scanner is correspondingly forgiving: unquoted attribute values, single
quotes, uppercase tags, unterminated raw-text elements and malformed nesting
are all handled, because giving up on a malformed page means losing every link
in it.

## Path mapping

`PathMapper` must be **deterministic** — `--resume` and `--update` re-derive
paths from URLs on a later run and have to land on the same files — and
**injective enough** that two different URLs never overwrite each other.

| URL | Path |
| --- | --- |
| `https://site/` | `index.html` |
| `https://site/docs/` | `docs/index.html` |
| `https://site/about` (HTML) | `about.html` |
| `https://site/page.php` (HTML) | `page.php.html` |
| `https://site/css/a.css` | `css/a.css` |
| `https://site/search?q=cats` | `search@1a2b3c4d.html` |
| `https://cdn.other/lib.js` | `_external/cdn.other/lib.js` |

Query strings fold into a short FNV-1a hash appended to the stem, so
`?q=cats` and `?q=dogs` are different files and the same query always maps to
the same name. Extensionless HTML gains `.html` so browsers render it instead
of downloading it. Third-party hosts are parked under `_external/<host>/`, so a
mirror is visibly "this site, plus its third-party assets".

Sanitisation of each component is covered in [SECURITY.md](../SECURITY.md).

## The manifest

`.surl/manifest.json` is the record of a run:

```json
{
  "format": 1,
  "surl_version": "1.0.0",
  "source": "https://example.com/",
  "created_at": 1755600000,
  "updated_at": 1755600120,
  "entries": {
    "https://example.com/": {
      "path": "index.html",
      "status": 200,
      "size": 559,
      "kind": "html",
      "state": "done",
      "depth": 0,
      "fetched_at": 1755600000,
      "sha256": "…",
      "etag": "\"abc\"",
      "content_type": "text/html"
    }
  }
}
```

It powers four things:

- `--resume` — skip entries already `done` whose file still exists;
- `--update` — re-request with `If-None-Match` / `If-Modified-Since`, so an
  unchanged site costs one `304` per resource;
- `surl check` — verify stored files against `sha256`;
- `surl inspect` — counts, sizes and the list of failures.

The `format` field is checked on load: a manifest written by a newer SURL is
refused rather than silently misread.

## Concurrency

A classic queue-plus-worker-pool. Workers pop URLs, process them, and push
newly discovered ones back. Termination is "queue empty **and** no worker
active", which is the condition that avoids stopping while a worker is still
about to enqueue.

Shared state and its protection:

| State | Protection |
| --- | --- |
| Queue and seen-set | one mutex + condition variable |
| Manifest | its own mutex |
| Robots cache | its own mutex, one fetch per origin |
| Staged document list | its own mutex |
| Byte and file counters | atomics |
| Rate limiter | mutex, shared token bucket so the cap is global |

Only phase 1 is parallel. Phase 2 is single-threaded: it is I/O-light, runs
once, and being sequential makes it trivially correct.

## HTTP backends

One interface, two implementations chosen at build time:

- **Windows — WinHTTP.** Part of the OS, so `surl.exe` links no third-party
  library and needs no redistributable. Uses the system certificate store and
  system proxy configuration.
- **Linux/macOS — libcurl.** The obvious choice, present everywhere.

Both handle redirects internally and report the **final** URL, which phase 2
needs: a document that arrived after a redirect must have its relative
references resolved against where it actually came from, not where it was
requested.

## Error handling

No exceptions for control flow. Fallible functions return `bool` and fill a
`std::string& error`, which keeps the failure path visible at every call site.
`main()` has a catch-all purely so an unexpected throw becomes a clean error
message rather than a crash dialog.

A failed resource is recorded in the manifest as `failed` with the reason and
the crawl continues — one 404 must not abandon a 5000-page mirror. The process
exits `3` when anything failed, so scripts can tell a partial mirror from a
complete one.

## Version flow

`VERSION.txt` is the single source of truth:

```
VERSION.txt
    ├──► CMake project(VERSION)  ──► version.hpp  ──► surl --version
    │                            └──► surl.rc     ──► file properties
    ├──► surl.iss (ISPP reads it) ─►  installer version + AppVerName
    ├──► package.ps1              ─►  archive contents
    └──► release.yml              ─►  checked against the git tag
```

The release workflow fails if the tag and `VERSION.txt` disagree, so a release
cannot ship a binary whose version does not match its tag.
