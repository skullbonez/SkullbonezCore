# Fable-01 Phase 4 Determinism Tests

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Summary

Completed fable-01 D1-D3 and closure. The unit harness now has a fast
`PhysicsEngine` determinism property and a solver/body snapshot restore
losslessness test.

## Source Changes

- Added `SkullbonezTests/TestDeterminism.cpp`.
- Added `SkullbonezTests/TestDiagnosticsLinkStubs.cpp`.
- Promoted `SkullbonezTests/TestTerrainLinkStubs.cpp` from a loud Terrain query
  stub into a deterministic flat-plane terrain fixture for focused physics
  tests.
- Reused real `PhysicsEngine`, `PhysicsScene`, `PhysicsWorld`, solver, body,
  collider, diagnostics-sink, and replay recorder translation units in
  `SKULLBONEZ_TESTS`.
- Updated `AGENTS.md` so bug fixes in covered subsystems add or update a
  regression test in the same commit unless explicitly scoped otherwise.

## Coverage

- D1: CodeGraph mapped the minimal engine path: default-construct
  `PhysicsEngine`, apply deterministic config, register authored bodies and
  colliders, step with `PhysicsWorldForces` and a local `WorkerPool`, and use
  the solver snapshot/body restore API.
- D2: `PhysicsEngine determinism: micro-world matches at fixed tick intervals`
  steps two identical three-body micro-worlds for 240 fixed ticks and
  byte-compares body pose and velocity state every 60 ticks.
- D3: `PhysicsEngine determinism: solver snapshot plus body state restores
  losslessly` captures `ReplaySolverWorldSnapshot` plus body replay state at
  tick 120, steps a 60-tick replay window, restores, re-steps, and byte-compares
  against the uninterrupted engine.

## Validation

- `tools\validate_tests.bat` final: passed in 5.203s with 42 doctest cases and
  527 assertions, 0 warnings/errors.
- `tools\validate_physics.bat` final: passed in 15.907s; Debug/Profile builds
  succeeded with 0 warnings/errors and `physics_regression_solver.csv` matched
  the 20,001-line baseline byte-exactly.

## Iteration Notes

- Unit attempt 2 failed because direct `PhysicsEngine::Step` reached the terrain
  sweep with null body terrain pointers. The fix was to pass a test-owned
  flat-plane Terrain fixture through the authored body descriptor.
- The first physics gate failed before running the baseline because Debug linked
  uncalled diagnostics sink methods that Profile discarded. The fix was a
  no-op diagnostics link-stub file for Debug-only `SkullScope` and `EngineLog`
  symbols.
- Comment audit covered all touched source-bearing files:
  `TestDeterminism.cpp`, `TestDiagnosticsLinkStubs.cpp`,
  `TestReplayRecorderLinkStubs.cpp`, `TestSceneParserLinkStubs.cpp`, and
  `TestTerrainLinkStubs.cpp`. No files were deferred.
