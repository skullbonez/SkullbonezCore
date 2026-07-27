# Coverage Gate Test Reorganization — CG0 Ownership Map

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: CG0

## Result

All five `TEST_CASE` rows in
`SkullbonezTests/TestCoverageFloorContracts.cpp` have one subsystem owner and
one existing destination. No test is deleted, split, renamed, or left
unassigned. The move baseline is 231 assertions across the five cases.

## Method

`tools\validate_coverage.bat` was run directly from the current source. Each
gate-named case was then run alone under OpenCppCoverage, with product-source
instrumentation unchanged, to identify the production lines reached by that
case. These per-case traces are attribution evidence, not new floors: line sets
can overlap because several cases legitimately use the same shape, allocation,
and recorder helpers.

The exact machine-readable traces are in the local validation workspace at
`TestOutput/coverage/cg0-attribution/*.xml`. The table below records the primary
owner lines from those traces and the direct public-entry regions that the test
drives, so the CG1 destination is reviewable without treating incidental helper
coverage as ownership.

## Complete Test Assignment

| Current test (source lines) | Assertions | Owning subsystem | CG1 destination | Primary product lines reached |
|---|---:|---|---|---|
| `full replay tracks round-trip owner values` (214-410) | 49 | Replay artifact codec | `TestReplayArtifact.cpp` | `ReplayV2Artifact.cpp`: 1,213 hit lines, including save/load entry regions 2894-3173; `ReplayRecorder.cpp`: 1,320 hit lines, including capture/sample/event paths 1954-3367. Physics stores and `SceneEntityStore` are setup collaborators, not the asserted owner. |
| `every object manifold shape pair publishes contacts` (412-452) | 110 | Object contact manifold | `TestObjectContactManifold.cpp` | `ObjectContactManifold.cpp`: 728 hit lines across shape dispatch, sweep, manifold construction, and public entry regions 1939-2149; `ConvexHullShape.cpp`: 292 supporting hit lines from the real pyramid fixture. |
| `box and hull buoyancy stay finite under partial submersion` (454-458) | 14 | Physics body/store force application | `TestPhysicsHandles.cpp` | `PhysicsBodyStore.cpp`: 400 hit lines, including `ApplyForces` 2199-2251; `ColliderStore.cpp`: 113 hit lines; shape metrics in `CollisionShape.h` 177-201; `ConvexHullShape.cpp`: 283 fixture/supporting hit lines. |
| `terrain sweep and manifold support every collision shape` (460-513) | 35 | Terrain contact manifold | `TestTerrain.cpp` | `TerrainContactManifold.cpp`: 263 hit lines across implementation/public entry regions 370-627; `PhysicsTerrainView.cpp`: 25; `Terrain.cpp`: 45; `ConvexHullShape.cpp`: 286 fixture/supporting hit lines. |
| `replay timeline applies retention and sequences owner events atomically` (515-567) | 23 | Replay retained timeline | `TestReplayRecorder.cpp` | `ReplayTimeline.cpp`: 100 hit lines, including configure/toggle/policy/event/stats regions 51-169 and 232-298; `ReplayRecorder.cpp`: 442 supporting hit lines. |

The helpers follow their only consuming test:

- `FullArtifactPath` moves with the artifact case.
- `CheckContactPair` moves with the object-manifold case.
- `ShapeKind` and `CheckUnderwaterForcePath` move with the physics-store case.
- `FlatCoverageTerrain` moves with the terrain-contact case.
- `SphereShape` and `BoxShape` are small construction helpers used by multiple
  destinations. CG1 will place equivalent local helpers in the receiving files
  only where that receiving file does not already have one; test assertions and
  test names remain byte-for-byte unchanged.

No new test file or target is required.

## Before Coverage

Direct `tools\validate_coverage.bat` result:

| Subsystem | Covered / instrumented | Before | Floor |
|---|---:|---:|---:|
| maths | 853 / 985 | 86.60% | 85.00% |
| core_primitives | 1,051 / 1,189 | 88.39% | 85.00% |
| physics_stores | 1,588 / 2,065 | 76.90% | 70.00% |
| physics_stages_and_solver | 4,304 / 5,355 | 80.37% | 70.00% |
| replay_artifact_codecs | 1,278 / 1,674 | 76.34% | 70.00% |
| startup | 1,118 / 1,220 | 91.64% | 70.00% |
| config_and_schema | 404 / 426 | 94.84% | 70.00% |
| runtime_input_and_interaction | 578 / 758 | 76.25% | 50.00% |
| scene_logic | 38 / 39 | 97.44% | 50.00% |
| replay_value_seams | 2,054 / 2,431 | 84.49% | 50.00% |

Whole instrumented product output was 21,044 / 28,282 lines (74.41%); it is
reported but not gated. All ratified subsystem floors passed unchanged.

## CG1 Binding

CG1 moves the five complete test bodies and their fixtures to the destinations
above, deletes `TestCoverageFloorContracts.cpp`, and removes only that compile
row from the project and filters. Acceptance compares against the ten exact
before values above and the 231 moved assertions; coverage policy files remain
untouched.
