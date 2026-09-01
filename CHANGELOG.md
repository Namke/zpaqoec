# Changelog

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
