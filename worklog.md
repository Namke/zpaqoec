# Worklog

## 0.2.0

- Reframed the fork around OEC: Optimize + Error Correction.
- Replaced public extension command names with `oecinit` and `oec_a`.
- Added OEC read-command router for `oec_l`, `oec_i`, `oec_x`, `oec_e`.
- Metadata routing: `oec_l/i` target only the inferred/overridden zero part.
- Payload routing: `oec_x/e` target the multipart data pattern; zero part is required as OEC authority but is not misused as payload.
- Added future `.idx` insertion point through centralized OEC layout/routing logic.
- Added legacy `.ecstate` read compatibility and new `OECST1` writes.
- Migrated injector marker without duplicate hooks.
- Regression results: EC PASS; injector PASS; OEC_A PASS; OECINIT PASS; OEC command routing PASS.

## Next

- Implement disk-backed/mmap `.idx` cache with explicit path selection and generation fingerprinting against `000`.
- Integrate `.idx` lookup into every profitable `oec_*` command, starting with metadata lookup and then dedup/add hot paths.
- Hook EC generation into the ordered ZPAQ output writer to remove the post-close reread for newly created parts.

## 0.2.1 - Windows common-entry dispatcher + OEC help

- Reproduced the architectural flaw in the 0.2.0 injector: it selected the first textual `int main(...)`, which is unsafe in zpaqfranz's platform-conditional monolith.
- Moved OEC dispatch to `zpaq_main_internal(int, const char**)`, confirmed present in upstream Windows diagnostics and used as the common command path.
- Added automatic migration/removal of legacy hook blocks and kept include placement after upstream platform compatibility setup.
- Added `oec_init` alias, no-argument OEC help, `oec_help`, and `oec_h`.
- Added `docs/OEC_COMMANDS.md`.
- Tests: injector PASS, EC PASS, OEC_A PASS, OECINIT PASS, OEC COMMAND PASS.


## 0.2.2 Windows dispatcher hardening
- Reproduced the architectural gap: hooking only `zpaq_main_internal()` does not guarantee interception before every platform-specific outer command validator.
- Added bridge dispatch to all outer `int main()` entries plus common internal entry.
- Added mandatory runtime smoke tests so an executable without working OEC dispatch can no longer be reported as a successful build.
- Added stale executable detection and optional explicit install destination.

## 0.2.3 signature-aware entry injection

Windows GCC 16/UCRT64 exposed an injector bug: zpaqfranz contains platform-conditional `int main()` definitions with no parameters. 0.2.2 injected `argc, argv` into every textual `main`, causing compile failure on the active no-arg variant. The injector now only instruments function signatures that actually expose both identifiers, while instrumenting every eligible `zpaq_main_internal` definition. Repatch is self-healing because old OEC hook blocks are removed before reinjection.

## 0.2.4 Windows smoke argv fix

Observed on a real MSYS2/UCRT64 build: compile succeeded, no-arg and oec_h smoke appeared to pass, but oec_version returned quick help. Root cause was the PowerShell smoke helper parameter `$Args`, which collides case-insensitively with the automatic `$args` variable. The smoke helper therefore invoked the fresh executable without the requested command. Renamed it to `$CommandArgs`, switched probes to named binding, and added an oecinit missing-archive probe requiring rc=2 and OEC usage text.
