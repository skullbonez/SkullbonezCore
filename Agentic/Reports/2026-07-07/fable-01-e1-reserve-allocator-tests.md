# Fable 01 E1 RuntimeReserveAllocator Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

E1 is complete. `SKULLBONEZ_TESTS` now covers the RuntimeReserveAllocator replay
growth policy surface.

## Changes

- Added `SkullbonezTests/TestReserveAllocator.cpp`.
- Added `SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.cpp` to
  `SKULLBONEZ_TESTS.vcxproj`.
- Added matching Tests and Runtime/Allocation filters to
  `SKULLBONEZ_TESTS.vcxproj.filters`.

## Evidence

- CodeGraph mapped `RegisterOwner`, `RequestGrowth`, `ResetCounters`,
  `CopyRecentGrowthEvents`, growth-event counters, policy-violation counters,
  and `RuntimeReserveGrowthScope` as the public test surface.
- Discovery command `rg -n "RegisterOwner|RequestGrowth"
  SkullbonezSource/Runtime/Allocation` confirmed registration persists in
  fixed process-global owner storage, while `ResetCounters()` clears owner
  counters, policy violations, and recent growth events.
- Tests use unique owner names per case. They cover replay growth grants under
  cap, event byte accounting, replay-growth scope approval only in the granted
  replay window, over-cap denial, policy violation counting, growth-count limit
  denial, event reason fields, and `ResetCounters()` preserving owner
  registration while clearing diagnostics.

## Validation

- First `tools\validate_tests.bat` attempt failed in project-filter validation:
  `RuntimeReserveAllocator.cpp` expected `Source Files\Runtime\Allocation`.
- Final `tools\validate_tests.bat`: exit 0 in 3.798s, 35 doctest cases and
  364 assertions passed with 0 warnings/errors.

## Comment Audit

Touched source-bearing test file inspected against the comment-style guide:
`SkullbonezTests/TestReserveAllocator.cpp` has a learning header with glossary,
invariants, and related links. A local `Why:` comment explains the unique owner
names used for process-lifetime registration.
