# Fable 05 Phase 1 Error Policy

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

Phase 1 is implemented. The codebase now has named Lane F and Lane R Core
primitives, `AGENTS.md` records the three-lane error handling policy, and the
existing runtime-boundary checker throw ratchet is recorded as the phase-1
guardrail.

No throw conversions were performed in this slice.

## Changes

- Added `SkullbonezSource/Core/FatalError.h` and `.cpp` with `SB_FATAL(owner,
  ...)` for fatal invariants. The header stays independent of `Common.h`; the
  `.cpp` owns logging, flushing, Profile/Debug breaking, and aborting.
- Added `SkullbonezSource/Core/SbResult.h` with a minimal bounded Lane R
  `SbResult`/`SbError` carrier.
- Added the new Core files to `SKULLBONEZ_CORE.vcxproj` and `.filters`.
- Added the three-lane policy table to `AGENTS.md`.
- Updated the fable-05 plan/progress docs with phase-1 completion evidence.

## Existing Guardrail Evidence

`tools/check_runtime_boundaries.py` already contained the phase-1 throw ratchet:
`MAX_SOURCE_THROW_TOKENS = 355`, `check_throw_site_count`, and self-tests for a
budget-matched synthetic throw surface plus a grown surface that must fail.

## Validation

- `tools\validate_build.bat Profile`: exit 0 in 4.293s, with
  `FatalError.cpp` compiled and linked, 0 warnings/errors.
- `python tools\check_runtime_boundaries.py --self-test`:
  `SELF_TEST_PASS`, including the synthetic throw-ratchet growth case.
- `python tools\check_runtime_boundaries.py`: exit 0 in 15.408s, 0 errors.
- `tools\validate_project_filters.bat`: exit 0 after adding `FatalError` and
  `SbResult` to the Core filter rule, 554 project items matched.
- `tools\validate_fast.bat`: exit 0 in 35.331s. The gate passed formatting,
  project filters, staged file sizes, runtime boundaries, Profile build,
  doctest smoke test, and ready Profile/Debug builds with 0 warnings and
  0 errors.

## Comment Audit

Touched source-bearing files inspected against
`Agentic/Skills/comment-style-audit/skill.md` and
`Agentic/Reference/comment-style-guide.md`:

- `SkullbonezSource/Core/FatalError.h`
- `SkullbonezSource/Core/FatalError.cpp`
- `SkullbonezSource/Core/SbResult.h`
- `tools/validate_project_filters.py`

The new Core files have learning headers and nearby lane/invariant comments.
`validate_project_filters.py` already had a tool learning header; this slice
only extended the existing Core prefix table.
