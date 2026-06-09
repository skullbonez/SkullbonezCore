# Physics Overview

SkullbonezCore currently uses one object/object solver:

| Solver | Scope | Notes |
|--------|-------|-------|
| Persistent contact solver | Spheres and oriented boxes | Catto-style sequential impulse contact rows with warm starting, friction, restitution bias, stabilization, position correction, and impulse caching. |

Object/object swept tests are a CCD front-end only. They build candidate timing,
advance bodies to a time of impact when needed, and wake sleeping pairs, but
they do not apply the object/object impulse response. `SolvePersistentObjectContacts`
owns dynamic object velocity response and post-solve object position cleanup.

Terrain still uses its separate swept collision path before the persistent
object/object solve. Terrain support information then feeds the sleep/support
classification used by object sleep islands.

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

Physics changes must pass:

```bat
tools\validate_physics.bat
```

Hot-path or broadphase changes usually also need:

```bat
tools\validate_perf.bat
```

Physics CSV baselines live in `TestOutput/baselines/` and are byte-exact. A single differing byte is a real behavioral change until proven intentional.

## Debugging

The in-game physics overlay supports a pipeline stage mode:

```bat
Profile\SKULLBONEZ_CORE.exe --physics-debug pipeline --scene SkullbonezData\scenes\solver_smoke.scene
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
| Rigid body state | `SkullbonezSource/SkullbonezRigidBody*` |
| Impulse solver | `SkullbonezSource/SkullbonezImpulseSolver*` |
| Shapes | `SkullbonezSource/SkullbonezBoundingSphere*`, `SkullbonezSource/SkullbonezDynamicsObject*` |
| Broadphase | `SkullbonezSource/SkullbonezSpatialGrid*` |
| Main physics loop | `SkullbonezSource/SkullbonezGameModelCollection*` |
