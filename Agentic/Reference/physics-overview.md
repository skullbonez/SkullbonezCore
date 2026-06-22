# Physics Overview

SkullbonezCore currently uses one shared contact-row solver:

| Solver | Scope | Notes |
|--------|-------|-------|
| Persistent contact solver | Object/object and object/terrain contacts for spheres and oriented boxes | Catto-style sequential impulse contact rows with warm starting, friction, restitution bias, stabilization, position correction, impulse caching, and terrain support metadata. |

Object/object swept tests are a CCD front-end only. They build candidate timing,
advance bodies to a time of impact when needed, and wake sleeping pairs, but
they do not apply the object/object impulse response. `SolvePersistentObjectContacts`
owns dynamic object velocity response and post-solve object position cleanup.

Terrain still uses its swept collision path before the shared solve, but the
detection phase now emits terrain contact manifolds instead of running the
legacy terrain impulse response. Terrain manifolds are appended after
object/object rows with body B set to the static terrain sentinel (`-1`). The
shared row solve owns velocity response, writeback, position correction, cache
storage, diagnostics, and visual pipeline records for both object and terrain
contacts. Terrain support classification remains explicit metadata: stable
terrain support may seed sleep, while edge/point terrain contacts inhibit sleep
and do not receive rest-only warm-start or damping policy.

## Time Step

The physics clock runs at a fixed 120 Hz:

```cpp
accumulator += frameDt;
while ( accumulator >= PHYSICS_FIXED_DT )
{
    RunPhysics( PHYSICS_FIXED_DT );
    accumulator -= PHYSICS_FIXED_DT;
}
```

Scene files can force `fixed_step`, which maps one physics tick to each rendered frame for deterministic test output.

## Validation Expectations

Before committing PR-bound physics changes, run:

```bat
tools\validate_physics.bat
```

Hot-path or broadphase changes usually also need this targeted PR gate:

```bat
tools\validate_perf.bat
```

Physics CSV baselines live in `TestOutput/baselines/` and are byte-exact. A single differing byte is a real behavioral change until proven intentional.

## Debugging

The in-game physics overlay supports a pipeline stage mode:

```bat
Profile\SKULLBONEZ_CORE.exe --physics-debug pipeline --scene SkullbonezData\scenes\solver_smoke.scene.json
```

`--physics-debug-pipeline on` and the scene directive
`physics_debug_pipeline on` enable the same overlay component. In-game, F7 and
F8 step backward and forward through the recorded stage cursor.

SkullScope emits compact `pipeline_stages` rows that count bounded per-frame
records by stage. Use `tools\physics_query.bat` for summaries instead of
loading raw NDJSON or CSV artifacts into the model:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson pipeline --frames 0:1000
```

## Useful Code Areas

| Area | Files |
|------|-------|
| Rigid body state | `SkullbonezSource/Physics/RigidBody*` |
| Shared row solver | `SkullbonezSource/GameObjects/GameModelCollection*`, `SkullbonezSource/Physics/PersistentContactSolver*` |
| Terrain support policy | `SkullbonezSource/World/TerrainSupportClassifier.h` |
| Shapes | `SkullbonezSource/Physics/BoundingSphere*`, `SkullbonezSource/Physics/BoundingBox*`, `SkullbonezSource/Physics/ConvexHullShape*`, `SkullbonezSource/Physics/CollisionShape.h` |
| Broadphase | `SkullbonezSource/Physics/SpatialGrid*` |
| Main physics loop | `SkullbonezSource/GameObjects/GameModelCollection*`, `SkullbonezSource/Physics/PhysicsWorld*`, `SkullbonezSource/Physics/SimulationSystem*` |
