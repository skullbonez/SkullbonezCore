# Fable 01 S3 SpatialGrid Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

S3 is complete. `SKULLBONEZ_TESTS` now covers the first SpatialGrid broadphase
contracts.

## Changes

- Added `SkullbonezTests/TestSpatialGrid.cpp`.
- Added `SkullbonezSource/Physics/SpatialGrid.cpp` to
  `SKULLBONEZ_TESTS.vcxproj`.
- Added matching Tests/Physics filters to `SKULLBONEZ_TESTS.vcxproj.filters`.

## Evidence

- CodeGraph mapped `SpatialGrid` as an uncovered fixed-capacity broadphase
  index with generation-stamped clearing and caller-owned candidate-pair output.
- Discovery command `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Physics/SpatialGrid.cpp` returned no hits.
- Tests cover insert/query round-trip, pair deduplication, cell-boundary
  straddling, swept insertion into a later cell, and `Clear()`-then-query
  emptiness.
- The current public API has `Clear()` but no per-object remove; the old S3
  "remove" wording is covered as full-grid generation clear.
- The first validation run built but stack-overflowed because a local
  `SpatialGrid` fixture put large fixed arrays on the doctest thread stack.
  The final test uses static storage and resets by `Clear()`/`SetCellSize()`.

## Validation

- First `tools\validate_tests.bat` attempt: build succeeded, then the first
  SpatialGrid case crashed with stack overflow from a local fixture.
- Final `tools\validate_tests.bat`: exit 0 in 3.710s, 23 doctest cases and
  128 assertions passed with 0 warnings/errors.

## Comment Audit

Touched source-bearing test file inspected against the comment-style guide:
`SkullbonezTests/TestSpatialGrid.cpp` has a learning header with glossary,
invariants, and related links. The static fixture has a local `Why:` comment
for the fixed-array stack hazard.
