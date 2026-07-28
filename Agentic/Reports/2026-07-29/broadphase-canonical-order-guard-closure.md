# Broadphase Canonical-Order Guard Closure

Date: 2026-07-29
Branch: `nightrunner-29th-JUL-26`
Plan: completed and removed from `Agentic/Plans/TODO/` under inventory rule 4.

## Result

The candidate-pair radix layout, triangular dedup identity, and visualization
cell projection now state and enforce their representation limits.

- Both radix passes derive their digit widths, masks, and bucket counts from
  `Scene::Capacity::MAX_SCENE_OBJECTS`.
- The maximum triangular pair identity is calculated in 64 bits and must fit
  the retained signed-int index before the `pairSeen` array is declared.
- Runtime triangular multiplication, reset pair count, and rounded word count
  remain 64-bit until their guarded narrowing.
- Exact cell coordinates remain full-width integers. The Bucket/ActiveCell
  visualization projection alone saturates to [-32,768, 32,767], while the
  current world/cell limits explicitly pin the exact +/-200,000 range.
- A focused test reaches body index 8,191, crosses both radix digits, discovers
  pairs in non-canonical order, and requires ascending normalized emission from
  both the unfiltered tooling path and production-filtered path.

## Behavior-Neutral Evidence

BG0's Profile instruction comparison, temporary compile-failure proof, focused
tests, and byte-exact Physics gate are recorded in
`broadphase-canonical-order-guard-bg0.md`.

BG1 focused Profile evidence:

- Profile solution build: PASS, zero warnings/errors.
- `SpatialGrid:*`: 16/16 cases and 8,524/8,524 assertions pass.

## Comment Audit

Touched source scope: 3 files.

- [x] `SkullbonezSource/Physics/SpatialGrid.h`
- [x] `SkullbonezSource/Physics/SpatialGrid.cpp`
- [x] `SkullbonezTests/TestSpatialGrid.cpp`

The header now states the exact-versus-visualization coordinate ownership and
numeric limits. The implementation explains the widened triangular
multiplication, and the focused regression names the ceiling/radix hazard.
Checked: 3. Deferred: 0.

## Formal Validation

- `tools\validate_format.bat`: PASS in 44.1 seconds; 571 source files and
  317 headers are pipeline-clean and all `Related:` paths resolve.
- `tools\validate_tests.bat`: PASS in 21.8 seconds. The final Profile executable
  passes 439/439 cases and 2,419,238/2,419,238 assertions.
- `tools\validate_physics.bat`: PASS in 59.76 seconds. Debug/Profile builds and
  lifecycle smoke pass; `physics_regression_varied.csv` matches all 44,401 rows
  byte-for-byte (`output runs=2`, `baseline runs=1`).
- `tools\validate_perf.bat`: PASS in 91.2 seconds. DX12 and Physics Bench pass
  absolute budgets and committed-baseline comparisons with no regressions.

## Ownership Review

- Aggregate inventory: 85/85 gated rows ruled, zero unruled.
- Extraction-scar inventory: 1/1 retained row ruled, zero unruled.
- Wide-signature inventory: every operation at or above 12 parameters has a
  current ruling; no changed operation reaches the trigger.
- Independent run `/root/bg1_rubber_duck` initially found two blockers: signed
  reset intermediates and a ceiling test that covered only the unfiltered radix
  path. Both were fixed. The follow-up review reports zero blocking or
  non-blocking findings and explicitly clears aggregate ownership, capability
  slices, extraction scars, rename evasion, and false claims.
- Review accounting: initial prompt/response 1,055/1,932 characters; follow-up
  prompt/response 678/1,106 characters.

No baseline refresh is authorized or expected.
