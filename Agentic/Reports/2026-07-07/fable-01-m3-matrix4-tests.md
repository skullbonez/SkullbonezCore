# Fable 01 M3 Matrix4 Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

M3 is complete. `SKULLBONEZ_TESTS` now compiles `Matrix4.cpp` and runs focused
Matrix4 unit tests for the current pure-math transform contracts.

## Changes

- Added `SkullbonezTests/TestMatrix4.cpp`.
- Added `SkullbonezSource/Maths/Matrix4.cpp` and `TestMatrix4.cpp` to
  `SKULLBONEZ_TESTS.vcxproj`.
- Added matching Tests/Maths filters to `SKULLBONEZ_TESTS.vcxproj.filters`.

## Evidence

- CodeGraph mapped Matrix4 as uncovered and identified the default constructor,
  transform factory methods, composition operator, `Data()`, and `Inverse()` as
  the M3 test surface.
- Discovery command `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Maths/Matrix4.cpp` returned no hits.
- Tests cover `Inverse()` of identity, TRS composition against manual
  column-major values, `Data()` aliasing public matrix storage, and
  `original.Inverse() * original` returning identity within tolerance.

## Validation

- `tools\validate_tests.bat`: exit 0 in 3.834s, 12 doctest cases and
  86 assertions passed with 0 warnings/errors.

## Comment Audit

Touched source-bearing test file inspected against the comment-style guide:
`SkullbonezTests/TestMatrix4.cpp` has a learning header with glossary,
invariants, and related links. No extra local concept/hazard comments were
needed because the helpers and test bodies are direct matrix API contracts.
