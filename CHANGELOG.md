# Changelog

## 0.3.6 - resilient zpaqfranz list parser for JSON catalogs

- Fixes `oec_json: could not parse file records` on real zpaqfranz 64.8 list layouts that differ from the original fake-harness format.
- Adds ANSI stripping and accepts ISO/slash/compact date-time representations plus pipe-status and plain/legacy list layouts.
- Makes metadata field detection tolerant of spacing and column-order drift while preserving filenames containing spaces.
- If the current `l -terse` pass yields no parseable records, retries `l -all -terse`, parses explicit version/status records, and collapses them to the current live file set.
- Deleted latest records are excluded during the `-all` fallback.
- The same resilient parser is used by progressive JSON refresh after `oec_a`.
- Adds format-drift regressions for slash+compact timestamps, plain records, filenames with spaces, deleted histories, and `-all` fallback.
- No ZPAQ, EC, IDX, or JSON schema format changes.

## 0.3.5 - progressive JSON MD5 catalog

- `oec_json --force-md5` extracts the archive once to an isolated temporary tree, calculates whole-file MD5 values, and writes them into the JSON catalog.
- JSON schema is format version 2 with `md5`, `md5_source`, `md5_file_count`, and `md5_complete`.
- `oec_a` progressively refreshes an existing archive JSON after a successful add and calculates MD5 directly from source files.
- `oec_a --json-force` / `--force-json` creates the JSON when it does not already exist; without the flag, missing JSON remains untouched.
- Progressive updates retain old MD5 only when file path, size, and modified timestamp are unchanged.
- JSON replacement is staged through a temporary file; an update failure leaves the old JSON intact and does not invalidate the archive transaction.

## 0.3.4 - immutable JSON file catalog

- Added `oec_json` with alias `oec_j`.
- Writes a UTF-8 JSON catalog beside the archive: `aaa???.zpaq -> aaa.json`, `bbb.zpaq -> bbb.json`.
- Existing JSON is never overwritten: command exits immediately with a clear error before reading the archive.
- Catalog includes path, byte size, modification timestamp, saved attributes, file/directory type, version, status and compression ratio when available.
- Whole-file hash is emitted as `null` because standard ZPAQ list metadata does not expose a stored whole-file MD5/SHA; fragment-level SHA-1 integrity is documented separately.
- Uses an existing valid mmap `.idx` list view when it is parseable; otherwise performs one native `l <000> -terse -nocolor` pass.
- `PASSWORD_FOLDER`/`FRANZKEY` resolution applies to JSON generation for encrypted zero parts.
- Added JSON naming, non-overwrite, IDX-hit, single-part, multipart, wildcard and encrypted/password-folder regression tests.

## 0.3.3 - PASSWORD_FOLDER plaintext key lookup

- Adds `PASSWORD_FOLDER` as an optional password-file source before interactive AES input.
- Precedence: explicit `-key`/`-franzen` > existing `FRANZKEY` > matching `PASSWORD_FOLDER/<stem>.password` > normal upstream interactive prompt.
- Naming normalization includes `test???.zpaq -> test.password`, `compress.??? -> compress.password`, `nen.zpaq -> nen.password`, and OEC zero part `nen.000.zpaq -> nen.password`.
- Reads only the first plaintext line, strips UTF-8 BOM and CR/LF, never prints the password, and exports it to the current process as `FRANZKEY` so native child passes inherit it without command-line exposure.
- Applies at the OEC bridge before upstream parsing for OEC commands and native `a/e/l/x/i/t`. Missing/unreadable/empty password files retain normal interactive fallback.

## 0.3.2 - encrypted IDX hang/security fix

- Fixes apparent `oecinit` hang after `protected zero part`: IDX materialization spawned native `l`/`i` with captured output, hiding AES password prompts.
- Adds explicit IDX stage progress and visible authentication-wait diagnostics.
- Propagates `-key`/`-franzen` authentication options to IDX metadata passes; `FRANZKEY`/`FRANZFRANZEN` are inherited naturally.
- Standard AES-encrypted zero-part indexes no longer generate plaintext `.idx` metadata caches by default. Use `--idx-plaintext` to opt in.
- Authentication chatter is stripped from plaintext IDX snapshots.
- Encrypted OEC metadata commands fall back to the authoritative `.000` parser unless plaintext IDX is explicitly enabled.


## 0.3.1 - legacy single-part OEC mode

- `oecinit` / `oec_init` now detects an exact existing single-file archive such as `archive.zpaq` instead of blindly expanding it to `archive.zpaq.???`.
- Single-file layout keeps the original payload untouched and creates `archive.zpaq.ec`, `archive.000.zpaq`, `archive.000.zpaq.ec`, and `archive.idx`.
- Data EC is created **before** zero-part generation in single-file mode. If native `x -index` fails, the original archive still retains its minimum `.ec` protection and OEC returns a partial-failure status.
- `oec_l`, `oec_i`, `oec_x`, `oec_e`, `oec_idx`, and `oec_a` now detect exact existing single-file archives and route metadata/payload correctly.
- `oec_a archive.zpaq ...` appends to the existing single archive using `archive.000.zpaq` as its external index and regenerates the archive EC sidecar.
- Multipart behavior and on-disk ZPAQ/EC/IDX formats are unchanged.

## 0.3.0 - mmap `.idx` acceleration cache

- Add `OECIDX1` versioned, disposable `.idx` cache. `.000` remains the authoritative standard ZPAQ metadata index.
- Add explicit cache placement with `--idx PATH`, intended for SSD/NVMe independent of archive storage.
- Add cross-platform memory mapping: `CreateFileMappingW`/`MapViewOfFile` on Windows and `mmap` on Unix/Linux.
- Add transactional `.idx.tmp` replacement plus header/section CRC32C validation.
- Add zero-part staleness fingerprint: size, mtime, and first/middle/last 64 KiB CRC32C samples.
- Add `oec_idx build|verify|info|drop`.
- `oecinit` now builds/reuses `.idx` by default, supports `--no-idx`, and reuses an existing valid cache without reparsing `.000`.
- Default `oec_l` and `oec_i` are served directly from mmap cache on a valid hit; missing/stale/corrupt cache is rebuilt lazily unless `--idx-no-rebuild` is used.
- Option-rich `oec_l/i` variants continue to use native `.000` parsing for exact upstream option semantics.
- `oec_a` supports `--idx`, `--no-idx`, and `--idx-refresh`; default behavior lets the changed `.000` fingerprint invalidate cache and refresh lazily on the next metadata read.
- `oec_x/e` accept/validate the cache but still delegate payload extraction to the native multipart path; no direct fragment locator is claimed in v1.
- Explicitly document the current deep-integration boundary: upstream `Jidac::add` still reconstructs HT/DT state and builds its native dedup index. 0.3.0 does not claim full add-RAM offload.
- Add `docs/OEC_IDX_FORMAT.md` and update OEC command/README documentation.
- Add stale-cache, corrupt-cache, lazy-rebuild, mmap-hit/no-native-call, custom SSD path, and cache-drop regression tests.
- Retain the Windows smoke-harness `$CommandArgs` fix and argument-sensitive runtime dispatch gate from 0.2.4 development.

## 0.2.3

- Fix MinGW build regression in the 0.2.2 multi-entry dispatcher injector.
- Signature-aware hook injection: `int main()` entry points without `argc/argv` are now skipped instead of receiving an invalid bridge call.
- All `zpaq_main_internal(int argc, ... argv)` definitions are still instrumented, covering platform-conditional Windows/Linux parser entries.
- Repatching a source partially patched by 0.2.2 removes the bad hook and reinjects only eligible entry points; no clean upstream checkout is required.
- Injector regression now includes a no-argument `main()` decoy and multiple conditional internal entries.
- Windows/Linux runtime smoke gates remain mandatory and verify OEC dispatch before the build is reported successful.

## 0.2.2

- Fix OEC dispatch placement across platform-specific outer `main()` paths.
- Add lightweight forward-declared bridge so OEC is intercepted before upstream unknown-command validation without moving heavy headers before platform setup.
- Keep secondary dispatch in `zpaq_main_internal()` for common internal entry coverage.
- Add `oec_version` build identity command.
- Windows build now has mandatory post-build runtime smoke gates for no-arg help, `oec_h`, and `oec_version`.
- Windows build warns when bare `zpaqoec.exe` resolves to a different/stale binary and supports explicit `-InstallTo`.
- Linux build also performs OEC runtime smoke gates.
- Harden injector function matching so comments mentioning `int main()` cannot be mistaken for function definitions.
- Add/refresh OEC command documentation.

## 0.2.1

- Fixed Windows OEC command dispatch by moving the injected hook from the first textual `main()` to upstream `zpaq_main_internal()`, the common command entry used by the monolithic source.
- Injector migrates existing 0.1.x/0.2.0 hook placements automatically and remains idempotent.
- Added `oec_init` as an alias of `oecinit`.
- Running `zpaqoec` with no parameters now prints OEC quick help instead of immediately falling through to upstream help.
- Added `oec_help` / `oec_h` explicit quick-help commands.
- Added `docs/OEC_COMMANDS.md` with Windows/Linux usage and OEC command behavior.
- Regression harness now contains a decoy/conditional `main()` and verifies dispatch is inserted into `zpaq_main_internal()`.
- `.idx` remains planned, not implemented; OEC routing stays cache-ready.

## 0.2.0 - OEC namespace milestone

- Established **OEC = Optimize + Error Correction** as the fork's primary target/identity.
- Renamed public `trunkinit` -> `oecinit`.
- Renamed public `trunkadd` -> `oec_a`.
- Added `oec_l`, optimized metadata listing through the zero-part index only.
- Added `oec_i`, optimized version/info routing through the zero-part index only.
- Added `oec_x` and `oec_e`, preserving native payload extraction semantics over multipart data while requiring the OEC zero part as archive authority.
- Added `--oec-index PATH` and common pattern/index inference for OEC read commands.
- Removed dispatch of the old `trunkinit`/`trunkadd` extension aliases.
- Changed newly written `.ecstate` magic to `OECST1`; reading legacy `ZFEXT1` remains supported.
- Renamed documented `--no-trunk-ec` to `--no-index-ec`; legacy spelling remains accepted.
- Default build artifact renamed to `zpaqoec` / `zpaqoec.exe`.
- Injector marker renamed to `ZPAQFRANZ_OEC_DISPATCH` with automatic migration from 0.1.x patched sources.
- Added routing regressions proving `oec_l/oec_i` receive only `000`, while `oec_x/oec_e` receive the data-part pattern.
- `.idx` is explicitly reserved as the next acceleration layer; 0.2.0 does not claim disk-backed cache support yet.

## 0.1.6

- Fixed MinGW/UCRT include order by injecting the extension immediately before upstream `main()` rather than at line 1.
- Preserved automatic migration of already patched upstream source.

## 0.1.5 and earlier

- Added zero-part index orchestration, retrofit initialization, independent EC sidecars, Windows compiler selection/diagnostics, and EC recovery tests.
