# Fable 01 S1-S2 Store Discovery And Bounds Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

S1 and S2 are complete. Store tests can be written standalone, and
`SKULLBONEZ_TESTS` now covers the first physics primitive broadphase contracts.

## Changes

- Recorded `PhysicsBodyStore` and `ColliderStore` standalone construction
  discovery in `fable_plans/01-unit-test-pyramid-progress.md`.
- Added `SkullbonezTests/TestBounds.cpp`.
- Added `BoundingSphere.cpp`, `BoundingBox.cpp`, and required
  `ConvexHullShape.cpp` to `SKULLBONEZ_TESTS.vcxproj`.
- Added matching Tests/Physics filters to `SKULLBONEZ_TESTS.vcxproj.filters`.

## Evidence

- `PhysicsBodyStore` exposes a default constructor plus direct
  `CreateBodyRecord`, `DestroyBodyRecord`, handle lookup, containment, and
  record access APIs.
- `ColliderStore` exposes a default constructor plus direct
  `CreateColliderRecord`, `DestroyColliderRecord`, handle lookup, containment,
  and record access APIs.
- CodeGraph mapped `BoundingSphere` and `BoundingBox` as broadphase swept
  collision helpers. Focused discovery found no config/global-service access in
  the touched bounds sources.
- The bounds translation units define convex-hull overloads, so the test
  project needs the real `ConvexHullShape.cpp` dependency rather than a stub.
- Tests cover sphere swept hit/miss/tangent times, sphere static
  overlap/touching returning `NO_COLLISION`, box broadphase bounding-radius
  touching/miss/sweep behavior, and sphere-box symmetry for a shared setup.

## Validation

- First `tools\validate_tests.bat` attempt failed project-filter validation
  because physics sources were placed under the Maths filter.
- Second `tools\validate_tests.bat` attempt reached link and failed until the
  real `ConvexHullShape.cpp` dependency was added.
- Final `tools\validate_tests.bat`: exit 0 in 3.748s, 19 doctest cases and
  115 assertions passed with 0 warnings/errors.

## Comment Audit

Touched source-bearing test file inspected against the comment-style guide:
`SkullbonezTests/TestBounds.cpp` has a learning header with glossary,
invariants, and related links. The test bodies directly encode broadphase time
contracts, so no extra local concept/hazard comments were needed.
