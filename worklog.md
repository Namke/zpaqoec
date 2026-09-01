# Worklog

## 2026-09-01 / 0.1.0

- Confirmed upstream indexed multipart path is the correct trunk mechanism.
- Implemented standalone ZFEC core in C++11, no third-party dependencies.
- Implemented P/Q GF(256) recovery and interleaved windows.
- Implemented integrated command dispatcher and `trunkadd` recursive invocation.
- Added `.ecstate` normal-path checkpoint.
- Injector tested against a synthetic zpaqfranz-style monolith.
- Functional tests PASS.
- ASan/UBSan test run PASS.
- Real upstream 64.8 monolith build remains to be run after the upstream source file is present locally; the current environment could inspect the public repo but could not materialize its multi-megabyte source file.

## 0.1.1 retrofit command

- Added `trunkinit ARCHIVE_PATTERN`.
- Uses native ZPAQ `extract -index` behavior to build the zero-part metadata index without extracting files.
- Existing archive parts remain byte-for-byte untouched.
- Added EC generation for all existing parts and the index.
- Default no-overwrite behavior; `--force` rebuilds index/EC.
- `run_ec_tests.sh`: PASS.
- `run_trunkadd_tests.sh`: PASS.
- `run_trunkinit_tests.sh`: PASS.

### 2026-09-01 / 0.1.3
- Diagnosed user Windows build log: include path fixed, but compiler selected POSIX code paths across both extension and upstream.
- Root cause category: wrong compiler target selected from PATH, not dozens of independent missing POSIX functions.
- Hardened Windows build to validate `g++ -dumpmachine` and prefer MSYS2 UCRT64 MinGW.
- Added explicit compiler override and fail-fast diagnostics.
