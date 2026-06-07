# Physics Overview

SkullbonezCore currently uses one solver:

| Solver | Scope | Notes |
|--------|-------|-------|
| Impulse | Spheres and oriented boxes | Sequential impulse contact solver with friction and stabilization. |

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

## Useful Code Areas

| Area | Files |
|------|-------|
| Rigid body state | `SkullbonezSource/SkullbonezRigidBody*` |
| Impulse solver | `SkullbonezSource/SkullbonezImpulseSolver*` |
| Shapes | `SkullbonezSource/SkullbonezBoundingSphere*`, `SkullbonezSource/SkullbonezDynamicsObject*` |
| Broadphase | `SkullbonezSource/SkullbonezSpatialGrid*` |
| Main physics loop | `SkullbonezSource/SkullbonezGameModelCollection*` |
