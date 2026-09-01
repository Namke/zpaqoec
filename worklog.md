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
