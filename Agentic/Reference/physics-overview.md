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

## Fixed-Step Ownership

`PhysicsWorld` is the composition root and deterministic sequencer. Concrete
owners retain their own state and accept only typed values or synchronous
borrows; no owner stores a pointer or reference back to `PhysicsWorld` or to a
sibling stage.

```text
mirror sleep flags
  -> force stage (+ facade-sequenced tornado forces)
  -> broadphase
  -> narrowphase and ordered event commit
  -> terrain detection and ordered commit
  -> persistent contact solver
  -> point-joint solve
  -> force-stage integration of remaining CCD time
  -> sleep-island transitions
  -> diagnostic views/output at the caller boundary
```

| Owner | Retained authority |
|---|---|
| `PhysicsForceStage` | Bounded mutual-gravity rows, force dispatch, and remaining-time integration |
| `PhysicsBroadphaseStage` | Spatial grid, candidate-pair order, and collision-cell keys |
| `PhysicsNarrowphaseStage` | Pair/island scratch and typed ordered events |
| `PhysicsTerrainStage` | Detection candidates, terrain manifolds, and rest-policy rows |
| `PhysicsContactSolverStage` | Persistent contacts/cache, solver scratch/statistics, and consequence queues |
| `PhysicsSleepController` | Sleep/wake state, support graphs, island transitions, and traversal scratch |
| `PhysicsStepDiagnostics` | Collision visuals, debug contacts, pipeline trace, and output sink |

Four values deliberately stay on the facade. `m_timeRemaining` is the shared
CCD clock written by narrowphase, terrain, and integration. Point-joint rows are
a top-level constraint lane borrowed by contact/sleep sequencing.
`TornadoGameplay` is already a cohesive sibling owner sequenced with forces.
The Debug-only suppression flag is a scoped facade override; it owns no
diagnostic rows. Public forwarding is accepted only where it terminates at one
of these concrete owners.

Immediate wake-up cannot be deferred out of narrowphase because a later pair
in the same deterministic pass must observe the newly awake body. Narrowphase
and tornado therefore receive a scoped `PhysicsNarrowphaseWakeAccess` value
containing only the body/sleep rows required for that transition. Sleep receives
a similarly narrow contact-cache invalidation capability. Neither value exposes
or retains a concrete sibling owner.

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

## Determinism Envelope

Byte-exact physics is certified for one binary built inside the repository's
pinned Windows x64 MSVC toolchain envelope and run against the gated content
committed with it. It is not an unconditional source-level promise across
binaries or compiler versions. Every project and configuration explicitly uses
`/fp:precise` and force-includes `FloatingPointContract.h`, which applies
`#pragma fp_contract(off)` before each translation unit. Changing the compiler,
toolset, floating-point flags, x64 instruction policy, fixed-step ordering,
worker reduction order, scenes, config, or baselines changes the certified
envelope and requires the mapped gates.

Release and `Profile-WPO` retain whole-program optimization for the engine and
for non-solver physics code, but three arithmetic owners are deliberate native
object boundaries: `ObjectContactManifold.cpp`, `TerrainContactManifold.cpp`,
and `PersistentContactSolver.cpp` compile with WPO disabled. Link-time code
generation may optimize callers and the rest of the product, but it cannot
recompile these contact feature-selection and impulse-solving bodies in the
context of unrelated modules. Ordinary `Profile` and Debug already compile the
physics project without WPO. Adding another contact/solver arithmetic owner,
removing one of these boundaries, or enabling WPO for it reopens the certified
envelope and requires paired clean-rebuild physics evidence plus the performance
gate.

Inlining, compiler, flag, and SIMD changes can alter instruction selection and
flip knife-edge contact or feature-selection branches even when the source
formula looks equivalent. The byte-exact gates detect that drift; the compiler
settings do not prevent every possible drift. Physics fixtures must therefore
be constructed away from floating-point selection boundaries. If a fixture is
moved away from a boundary, its nearby comment and owning report must record
the observed flip that motivated the move. The ff6e780e persistent-contact
fixture is the historical example: Profile changed from a two-point to a
four-point face after Vector3 inlining, while other configurations were already
on different sides of that fixture's boundary.

The 2026-07-12 worker audit found no thread-count-sensitive floating-point
accumulation: physics/replay/tornado workers write independent indexed slots and
stable serial stages consume them; rendering's chunk reductions use exact
integer counts or grouping-independent min/max. Worker count is therefore not
pinned. New chunked floating-point sums or averages require per-item staging
and stable serial reduction, or an explicit owner decision to pin the
validation worker count.

When any determinism input changes, regenerate affected CSV and SkullScope
baselines from the final Debug executable and committed scene/config state in
the same commit. Then rerun `tools\validate_physics.bat` for the core varied
baseline or `tools\validate_physics_deep.bat` for the broader baseline set;
copied artifacts are not evidence until the matching gate compares them
byte-exactly.

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
| Shared row solver | `SkullbonezSource/Physics/PhysicsBodyStore*`, `SkullbonezSource/Physics/PersistentContactSolver*` |
| Terrain support policy | `SkullbonezSource/World/TerrainSupportClassifier.h` |
| Shapes | `SkullbonezSource/Physics/BoundingSphere*`, `SkullbonezSource/Physics/BoundingBox*`, `SkullbonezSource/Physics/ConvexHullShape*`, `SkullbonezSource/Physics/CollisionShape.h` |
| Broadphase | `SkullbonezSource/Physics/SpatialGrid*`, `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage*` |
| Fixed-step owners | `SkullbonezSource/Physics/Stages/PhysicsForceStage*`, `PhysicsNarrowphaseStage*`, `PhysicsTerrainStage*`, `PhysicsContactSolverStage*`, `PhysicsSleepController*`, `PhysicsStepDiagnostics*` |
| Main physics sequence | `SkullbonezSource/Runtime/Scene/SceneController.Objects*`, `SkullbonezSource/Physics/PhysicsWorld*`, `SkullbonezSource/Physics/SimulationSystem*` |
