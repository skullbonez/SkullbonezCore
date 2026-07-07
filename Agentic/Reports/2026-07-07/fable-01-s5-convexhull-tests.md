# Fable 01 S5 ConvexHull Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

S5 is complete. The fable-01 physics primitives and stores phase is closed.
`SKULLBONEZ_TESTS` now covers committed baked convex hull asset loading.

## Changes

- Added `SkullbonezTests/TestConvexHull.cpp`.
- Added the test file to `SKULLBONEZ_TESTS.vcxproj`.
- Added the matching Tests filter to `SKULLBONEZ_TESTS.vcxproj.filters`.

## Evidence

- CodeGraph mapped `ConvexHullShape::LoadFromFile`, baked topology getters,
  and mass/inertia getters as uncovered.
- Discovery command `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Physics/ConvexHullShape.cpp` returned no hits.
- The smallest committed baked hull is `SkullbonezData/hulls/pyramid.hull`
  at 1,150 bytes.
- `ConvexHullShape.cpp` was already compiled into `SKULLBONEZ_TESTS` from the
  bounds slice, so S5 only adds the focused test file and project filter entry.
- Tests cover the pyramid fixture name, counts, centered vertices, base face
  span, face normal lengths, face index ranges, edge vertex/face ranges,
  adjacent-face endpoint membership, baked center of mass, volume, default
  mass, bounding radius, projected surface area, inertia half-extents, and
  `ComputeBoxApproxInertia()`.

## Validation

- `tools\validate_tests.bat`: exit 0 in 3.744s, 30 doctest cases and 305
  assertions passed with 0 warnings/errors.

## Comment Audit

Touched source-bearing test file inspected against the comment-style guide:
`SkullbonezTests/TestConvexHull.cpp` has a learning header with glossary,
invariants, and related links. The adjacency helper has a local `Invariant:`
comment for the topology rule it encodes.
