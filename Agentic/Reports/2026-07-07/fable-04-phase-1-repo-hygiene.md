# Fable 04 Phase 1 Repo Hygiene

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

Phase 1 is complete at the tip-tree level. `Agentic/Temp/` is ignored, no
`Agentic/Temp` paths are tracked in the current tree, and `validate_fast` now
includes a staged-file-size guard so oversized scratch artifacts are caught
before commit.

No history rewrite was performed. The existing 542 MiB pack-size issue remains
a user-owned decision because shrinking it requires rewriting historical
`Agentic/Temp` blobs and coordinating re-clones.

## Changes

- Added `tools/check_staged_file_sizes.py`, a git-index checker with synthetic
  self-tests and JSON summary output under
  `TestOutput/validation/staged_file_sizes/summary.json`.
- Wired the checker into `tools/validate_fast.bat` after project-filter
  validation.
- Updated the fable-04 plan/progress docs and `Agentic/SessionState.md` to
  record the completed tip hygiene and the deferred pack-size decision.

## Evidence

- `.gitignore:52` contains `Agentic/Temp/`.
- `git ls-files Agentic/Temp` returned no tracked paths.
- The checker blocks staged added/modified files above 5 MiB unless they live
  under `SkullbonezData/` or `TestOutput/baselines/`.

## Validation

- `python tools\check_staged_file_sizes.py --self-test`:
  `SELF_TEST_PASS` for synthetic clean/fail cases, including oversized
  disallowed paths.
- `python tools\check_staged_file_sizes.py --max-bytes 123456`: exit 0,
  summary wrote the configured budget and found 0 violations with the then-empty
  staged set.
- `tools\validate_fast.bat`: exit 0 in 32.100s. The gate passed formatting,
  project filters, staged file sizes, runtime boundaries, Profile build, doctest
  smoke test, and ready Profile/Debug builds with 0 warnings and 0 errors.
