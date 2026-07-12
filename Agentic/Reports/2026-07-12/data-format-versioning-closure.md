# Authored Data Format Versioning Closure

Date: 2026-07-12
Plan: `data-format-versioning`
Result: Complete — 5/5 phases

## Delivered Contract

- Asset libraries retain current version 1, load legacy unversioned/version 0
  through the named v0→v1 stamp step, and reject future versions through the
  scene parser's Lane-R result.
- Convex hulls retain their established current version 2 and now load the
  supported version-1 window. Unversioned v0 hulls remain outside the runtime
  window and are upgraded by the cold migration/bake writer.
- `engine.cfg` now stamps version 1. Its loader preflights legacy/current/future
  state before applying any setting, so future input cannot partially mutate
  startup configuration.
- Save Defaults preflights the same config reader, stamps legacy v0 on write,
  remains byte-idempotent on repeated saves, and never downgrades a future file.
- `tools/migrate_data_formats.py` provides idempotent `--write`, read-only
  `--check`, and deterministic self-tests for assets, hulls, and config. Scene
  v1/v2 behavior and committed scene files remain unchanged.
- `AGENTS.md` now requires each authored schema change to bump its owned
  version, add a migration, restamp committed data, and extend version tests.

## Review

The independent plan-end review found one blocking writer hazard: Save Defaults
could have restamped a directly supplied future config back to v1. Both writer
paths now preflight before mutation, and focused regression coverage proves the
future file's bytes remain unchanged. The review's focused recheck returned
clear. It also corrected stale plan wording about committed v1 scene fixtures.

## Validation

- Focused Profile builds: passed with zero warnings.
- Focused config, hull, asset-library, and render-default writer tests: passed.
- `python tools/migrate_data_formats.py --self-test`: passed.
- `python tools/migrate_data_formats.py --check`: 39/39 authored files current.
- `python tools/bake_hulls.py --check`: 35/35 hulls current.
- `tools\validate_tests.bat`: 172/172 tests and 3,963 assertions passed in 3.150s.
- `tools\validate_fast.bat`: passed in 36.531s with zero-warning Profile and
  Debug builds.
- `tools\validate_full.bat`: passed in 108.266s; formatting clean, all CPU
  lanes passed, zero DX12 validation errors, screenshots matched, and the
  44,401-line physics baseline was byte-exact.

The comment-style touched-file audit covered all 13/13 source-bearing files
with no deferrals. No physics CSV or DX12 screenshot baseline changed.
