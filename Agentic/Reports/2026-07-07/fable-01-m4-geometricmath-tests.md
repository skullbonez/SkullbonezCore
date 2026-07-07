# Fable 01 M4 GeometricMath Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

M4 is complete for the current public `GeometricMath` API, and fable-01 phase 1
is ready for the `validate_fast` phase gate.

## Changes

- Added `SkullbonezTests/TestGeometricMath.cpp`.
- Added `SkullbonezSource/Maths/GeometricMath.cpp` and
  `TestGeometricMath.cpp` to `SKULLBONEZ_TESTS.vcxproj`.
- Added matching Tests/Maths filters to `SKULLBONEZ_TESTS.vcxproj.filters`.

## Evidence

- CodeGraph mapped `GeometricMath` as uncovered and identified plane,
  triangle, and ray-segment helpers as the public M4 surface.
- Discovery command `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Maths/GeometricMath.cpp` returned no hits.
- Focused source search found no public ray-sphere or ray-box helper under
  `SkullbonezSource/Maths`; those helpers currently live under runtime/physics
  surfaces and are outside this pure-math phase.
- Tests cover plane construction, sloped-plane height, ray-plane hit/miss and
  segment boundary times, `NO_COLLISION` sentinel behavior, zero-normal throws,
  out-of-segment intersection throws, and collinear-triangle throws.
- The private barycentric throw site is not reachable through the current
  public API without a test-only access hack, so it is recorded as not directly
  exercised by M4.

## Validation

- `tools\validate_tests.bat`: exit 0 in 3.812s, 15 doctest cases and
  105 assertions passed with 0 warnings/errors.
- `tools\validate_fast.bat`: exit 0 in 33.651s with format, project filters,
  staged file sizes, runtime boundaries, Profile/Debug builds, and unit tests
  all passing with 0 warnings/errors.

## Comment Audit

Touched source-bearing test file inspected against the comment-style guide:
`SkullbonezTests/TestGeometricMath.cpp` has a learning header with glossary,
invariants, and related links. The test bodies are direct public API contracts,
so no extra local concept/hazard comments were needed.
