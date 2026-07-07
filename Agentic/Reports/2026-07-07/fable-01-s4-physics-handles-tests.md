# Fable 01 S4 Physics Handles Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

S4 is complete. `SKULLBONEZ_TESTS` now covers the first focused
PhysicsBodyStore and ColliderStore handle contracts.

## Changes

- Added `SkullbonezTests/TestPhysicsHandles.cpp`.
- Added `SkullbonezTests/TestTerrainLinkStubs.cpp`.
- Added `SkullbonezSource/Physics/PhysicsBodyStore.cpp` and
  `SkullbonezSource/Physics/ColliderStore.cpp` to `SKULLBONEZ_TESTS.vcxproj`.
- Added matching Tests/Physics filters to `SKULLBONEZ_TESTS.vcxproj.filters`.

## Evidence

- CodeGraph mapped `PhysicsBodyStore` and `ColliderStore` handle
  creation/destruction, lookup, dense-row move, and replay/scene-id lookup
  paths as uncovered.
- Discovery command `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Physics/PhysicsBodyStore.cpp
  SkullbonezSource/Physics/ColliderStore.cpp` returned no hits.
- Tests cover fresh body handles, model-index/handle inverse lookup,
  replay-id lookup through a good hint and stale hint fallback, middle-row
  body destruction with dense-row move, stale body generation rejection,
  collider lookup by model index, body handle, and scene object id, middle-row
  collider destruction with dense-row move, and stale collider generation
  rejection.
- `PhysicsBodyStore.cpp` references Terrain from uncalled integration helpers.
  `SkullbonezTests/TestTerrainLinkStubs.cpp` supplies loud test-only Terrain
  link stubs that throw if reached, so these unit tests cannot silently depend
  on fake terrain behavior.
- The first run with local store fixtures stack-overflowed because
  PhysicsBodyStore and ColliderStore own fixed-capacity runtime arrays. The
  final test uses static store fixtures and clears them between cases.

## Validation

- First `tools\validate_tests.bat` attempt failed at link on Terrain methods
  referenced by uncalled PhysicsBodyStore integration helpers.
- Second `tools\validate_tests.bat` attempt built and then the first handle
  case crashed with stack overflow from a local PhysicsBodyStore fixture.
- Final `tools\validate_tests.bat`: exit 0 in 4.011s, 27 doctest cases and
  174 assertions passed with 0 warnings/errors.

## Comment Audit

Touched source-bearing test files inspected against the comment-style guide:
`SkullbonezTests/TestPhysicsHandles.cpp` and
`SkullbonezTests/TestTerrainLinkStubs.cpp` both have learning headers with
glossary, invariants, and related links. The static store fixtures have local
`Why:` comments for fixed-array stack usage, and the Terrain stub has a local
`Hazard:` comment for the boundary leak it prevents.
