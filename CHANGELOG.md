# Changelog

## 0.2.4

- Fix Windows runtime smoke harness: the function parameter was named `$Args`, colliding with PowerShell's automatic `$args` variable. As a result every smoke invocation could execute the binary with no arguments; `oec_h` falsely passed because no-arg also prints help, while `oec_version` exposed the bug.
- Rename the smoke parameter to `$CommandArgs` and use named parameter binding for every probe.
- Add an argument-sensitive `oecinit` smoke probe that expects exit code 2 plus OEC-specific usage text, proving argv reaches the OEC dispatcher.
- No archive format, EC format, or OEC command semantics changed.

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
