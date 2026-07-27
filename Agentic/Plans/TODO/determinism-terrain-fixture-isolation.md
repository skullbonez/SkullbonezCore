# Determinism Terrain Fixture Isolation

Date: 2026-07-28
Status: TODO — 0/3 phases complete
Impact area: Physics tests, terrain lifetime, test order independence
Owner: Tests + Physics terrain
Priority: Medium

## Problem And Evidence

`SkullbonezTests/TestDeterminism.cpp` exposes `FlatTestTerrain()` and
`DeepSpaceTestTerrain()` as mutable function-local statics shared by test cases.
Physics borrows each terrain view, so the long lifetime is currently convenient,
but a future terrain cache or mutation would make tests order-dependent.

## Goal

Give each test or explicit test fixture its own config/terrain lifetime while
keeping every borrowed PhysicsTerrainView valid for the engine operations that
consume it.

## Phases

- [ ] **TF0 — Map terrain borrows and test lifetimes.** Identify every helper
  and test using the shared terrains, the exact view lifetime, and any cache or
  allocation behavior that fixture construction must preserve.
- [ ] **TF1 — Introduce an owning per-test fixture.** Place config, terrain, and
  engine in honest lifetime order; pass terrain explicitly to helpers and
  remove default arguments/function statics. Do not add a generic test context
  bag—one fixture owns the terrain-borrow invariant and states it.
- [ ] **TF2 — Prove order independence.** Add repeated/reordered construction
  coverage, run the determinism suite with randomized doctest order where
  supported, audit comments, and complete Physics/broad validation.

## Acceptance

No mutable terrain object is shared implicitly across test cases, every borrowed
view has a locally provable owner lifetime, and repeated or reordered runs
produce the same byte-exact results.

## Validation

Focused determinism tests, `tools\validate_tests.bat`,
`tools\validate_physics.bat`, and `tools\validate_full.bat`.
