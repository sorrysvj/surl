# Contributing to SURL

Thanks for wanting to help. SURL is a small, deliberately dependency-free C++20
project, so getting from a clean checkout to a running build takes about a
minute.

## Before you start

- **Bugs and features:** open an issue first if the change is more than a fix.
  It is much cheaper to agree on an approach than to rework a finished PR.
- **Security problems:** do **not** open an issue. See [SECURITY.md](SECURITY.md).
- **Good first contributions:** adding an installer language, extending the
  MIME table, adding a test for a real-world page that mirrors badly.

## Setting up

### Requirements

- CMake 3.21+
- A C++20 compiler: MSVC 19.3+ (VS 2022/2026 Build Tools), GCC 11+, Clang 14+
- Ninja (recommended)
- Linux/macOS only: libcurl development headers
- Python 3 (only to regenerate the icon and installer translations)
- Windows only, to build the installer: [Inno Setup 6](https://jrsoftware.org/isdl.php)

Nothing else. SURL vendors no third-party libraries and downloads nothing at
build time.

### Build and test

**Windows**

```powershell
./scripts/build.ps1 -Config Release -Test
```

The script finds MSVC through `vswhere`, imports the environment and builds
with Ninja. From a Developer prompt you can use the presets directly:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

**Linux / macOS**

```bash
cmake --preset linux-release      # or macos-release
cmake --build --preset linux-release
ctest --preset linux-release
```

### Run the tests directly

The test binary takes an optional substring filter:

```bash
./build/windows-release/tests/surl_tests          # everything
./build/windows-release/tests/surl_tests url      # just the url suite
./build/windows-release/tests/surl_tests integration
```

Integration tests spin up a real HTTP server on an ephemeral loopback port, so
they need no network access and no fixtures on disk.

### Build the installer

```powershell
./scripts/package.ps1
./scripts/test-installer.ps1
```

`test-installer.ps1` performs a real per-user install, checks PATH handling,
re-installs to prove PATH is not duplicated, uninstalls, and confirms user data
survived. It runs unattended and needs no administrator rights.

## Project layout

```
include/surl/     public headers, grouped by area
  cli/            option parsing and help text
  cmd/            command entry points
  mirror/         crawler, cache/manifest, path mapping, rewriting
  net/            URL, HTTP client, robots.txt
  parse/          HTML and CSS scanners
  util/           strings, JSON, hashing, filesystem, platform paths
src/              implementations, mirroring the include layout
tests/            test suite and the loopback fixture server
installer/windows Inno Setup source, icon, wizard art, translations
scripts/          build, package, checksum, asset generation
docs/             architecture and usage notes
```

Everything except `main()` lives in the `surl_core` static library, which is
what the tests link against.

## Code style

Match the surrounding code. Concretely:

- **C++20**, 4-space indent, 96-column soft limit, `.clang-format` is
  authoritative — run `clang-format -i` on files you touch.
- `snake_case` for functions and variables, `PascalCase` for types,
  `kConstantCase` for constants, trailing `_` on private members.
- Prefer `std::string_view` for non-owning parameters and `const` locals.
- No exceptions for control flow: functions that can fail return `bool` and
  fill a `std::string& error`. `main()` catches as a last resort.
- No new third-party dependencies without discussing it first. The
  single-binary, no-runtime property is a feature, not an accident.
- Platform-specific code goes behind `#ifdef _WIN32` in the narrowest possible
  scope, ideally in one file per backend (see `src/net/http_winhttp.cpp` and
  `src/net/http_curl.cpp`).

### Comments

Explain **why**, not what. A comment that restates the code is noise; a comment
that records the reason a non-obvious decision was made is worth keeping. If
you are handling a real-world quirk, say which one — a future reader needs to
know whether the workaround is still required.

## Tests

Every behavioural change needs a test. The framework is
`tests/test_framework.hpp` — about 60 lines, no dependencies:

```cpp
SURL_TEST(suite_name, describes_the_behaviour) {
    CHECK(condition);
    CHECK_EQ(actual, expected);
    CHECK_CONTAINS(haystack, needle);
    CHECK_NOT_CONTAINS(haystack, needle);
}
```

Name the case after the behaviour it pins down, not the function it calls:
`resolves_protocol_relative_references` beats `test_resolve_url_2`.

For anything involving fetching, add to `tests/test_integration.cpp` and extend
the fixture site in `populate()` rather than reaching for the network.

## Adding an installer language

This is genuinely easy:

1. Open [`scripts/make_translations.py`](scripts/make_translations.py).
2. Copy the `ENGLISH` dictionary, rename it (`GERMAN`, `FRENCH`, `SPANISH`…)
   and translate the values. Keep `%n` (line break) and the meaning of each
   message intact.
3. Add an entry to `LANGUAGES` with the file name, the Inno Setup
   `LanguageName` and the Windows language ID (`0407` German, `040C` French,
   `0C0A` Spanish).
4. Run `python scripts/make_translations.py`.
5. Add one line to the `[Languages]` section of
   [`installer/windows/surl.iss`](installer/windows/surl.iss):

   ```
   Name: "german"; MessagesFile: "compiler:Languages\German.isl,translations\surl.de.isl"
   ```

6. Build the installer and check your strings fit the wizard.

The generator fails the build if a language is missing a message, so nothing
can fall back to English silently.

## Commits and pull requests

- Write commit messages in the imperative mood: *"Fix srcset rewriting for
  data URIs"*, not *"Fixed…"* or *"Fixes…"*.
- Explain the *why* in the body when it is not obvious from the diff.
- Keep one logical change per commit. Rebase rather than merge.
- Before opening a PR:

  ```powershell
  ./scripts/build.ps1 -Config Release -Test
  ```

  and, if you touched anything under `installer/` or `scripts/`:

  ```powershell
  ./scripts/package.ps1
  ./scripts/test-installer.ps1
  ```

- CI runs on Windows, Ubuntu and macOS with warnings-as-errors, verifies that
  generated assets match their generators, and performs a full install /
  uninstall cycle. It must be green.

## Releasing

Maintainers only:

1. Update `VERSION.txt` — it is the single source of truth for the binary, the
   installer, the manifest and the release workflow.
2. Update `CHANGELOG.md`.
3. Commit, then tag:

   ```bash
   git tag -a v1.0.1 -m "SURL 1.0.1"
   git push origin main --tags
   ```

The release workflow verifies that the tag matches `VERSION.txt` (and fails
loudly if not), builds and tests on all three platforms, packages the installer
and portable ZIP, runs the installer test, generates `checksums.txt` and
publishes the GitHub release. No asset is ever uploaded by hand.

## Code of conduct

Be decent to each other. Assume good faith, critique the code and not the
person, and remember that the maintainer is doing this in their spare time —
as, probably, are you.

## License

By contributing you agree that your work is licensed under the
[MIT License](LICENSE).
