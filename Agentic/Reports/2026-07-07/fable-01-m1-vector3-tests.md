# Fable 01 M1 Vector3 Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

M1 is complete. `SKULLBONEZ_TESTS` now compiles `Vector3.cpp` and runs focused
Vector3 unit tests for the current pure-math behavior.

## Changes

- Added `SkullbonezTests/TestVector3.cpp`.
- Added `SkullbonezSource/Maths/Vector3.cpp` and `TestVector3.cpp` to
  `SKULLBONEZ_TESTS.vcxproj`.
- Added matching Tests/Maths filters to `SKULLBONEZ_TESTS.vcxproj.filters`.

## Evidence

- Discovery command `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Maths/Vector3.cpp` returned no hits.
- Tests cover zero-vector `Normalise()` throwing, non-zero normalization,
  dot/cross basis identities, and magnitude/squared-magnitude consistency.

## Validation

- `tools\validate_tests.bat`: exit 0 in 4.049s, 5 doctest cases and
  16 assertions passed with 0 warnings/errors.

## Comment Audit

Touched source-bearing test file inspected against the comment-style guide:
`SkullbonezTests/TestVector3.cpp` has a learning header with glossary,
invariants, and related links.
