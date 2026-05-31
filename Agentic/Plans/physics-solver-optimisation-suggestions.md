# Plan - Physics Solver And Codebase Optimisation Suggestions

**Date:** 2026-06-01
**Status:** Draft for review
**Current edit type:** Documentation only
**Primary future impact areas:** Physics, broadphase, performance, scene validation

## Context

This plan is based on a read-through of the current solver code, broadphase path, existing
physics reference notes, and `Agentic/Audits/physics_optimization.md`.

The current code already contains several important wins:

- Terrain response in `SkullbonezSource/SkullbonezImpulseSolver.cpp` keeps per-body velocity
  and angular velocity in locals/registers and has an SSE path for the terrain solver loop.
- The prior physics audit reports large wins from sleeping, SSE terrain solving, and adaptive
  solver early-out.
- Broadphase uses a retained, zero-allocation spatial grid with generation stamping.
- The collection keeps retained vectors for candidate pairs, contact rows, contact caches, and
  per-model frame state.

The best next work is not a blanket rewrite. It should target the remaining duplicated solver
paths, scalar object-contact hot loops, coarse profiling, and broadphase work around sleeping
objects.

## Evidence From Current Code

| Area | Observation | Relevant code |
|------|-------------|---------------|
| Terrain solver | Single-body terrain response has stack contacts, per-contact effective masses, SSE/Profile path, and adaptive early-out. | `SkullbonezSource/SkullbonezImpulseSolver.cpp:130` |
| Object persistent contacts | Object-object resting contacts are built and solved separately in scalar code. `applyImpulse` reads and writes both bodies on every row update. | `SkullbonezSource/SkullbonezGameModelCollection.cpp:434` |
| Solver pipeline | The solver still runs immediate swept pair response first, then a separate persistent object-contact pass. | `SkullbonezSource/SkullbonezGameModelCollection.cpp:792` |
| Broadphase | Solver mode inserts all objects, including sleeping objects, then skips sleep/sleep or wake-checks later. | `SkullbonezSource/SkullbonezGameModelCollection.cpp:815` |
| Pair generation | Pair dedup clears a bitset sized from inserted object count and emits all pairs before later sleep filtering. | `SkullbonezSource/SkullbonezSpatialGrid.cpp:184` |
| Visual correction | Terrain response still has sphere roll-alignment work with `sqrtf`/`acosf` and direct orientation mutation. | `SkullbonezSource/SkullbonezImpulseSolver.cpp:805` |
| Safety clamp | `RigidBody::ThrottleAngularVelocity()` still clamps angular speed using `velocityLimit`. | `SkullbonezSource/SkullbonezRigidBody.cpp:192` |
| Config gaps | Several solver constants are hardcoded even though related config fields already exist. | `SkullbonezSource/SkullbonezConfig.h:79` |

## Priority 0 - Measure The Right Things First

Before changing solver behavior, split the profiler markers enough to prove which row costs remain.

Suggested markers:

- `Frame/Physics/Terrain/BuildContacts`
- `Frame/Physics/Terrain/Precompute`
- `Frame/Physics/Terrain/Solve`
- `Frame/Physics/Terrain/PostSolve`
- `Frame/Physics/Narrowphase/ImmediateSwept`
- `Frame/Physics/Narrowphase/PersistentBuild`
- `Frame/Physics/Narrowphase/PersistentWarmStart`
- `Frame/Physics/Narrowphase/PersistentSolve`
- `Frame/Physics/Narrowphase/PersistentPosition`
- `Frame/Physics/Broadphase/InsertAwake`
- `Frame/Physics/Broadphase/InsertSleeping`
- `Frame/Physics/Broadphase/Pairs`

Add or reuse benchmark scenes that isolate:

- 300 active falling boxes
- 300 settled sleeping boxes
- mixed active/sleeping box pile
- 300 balls in dense object-object contact
- ball/box collision storm
- slope terrain box contact

Validation for profiler-only changes should include `tools\validate_perf.bat`. If the change
touches `SkullbonezGameModelCollection*`, use the repo mapping and prefer `tools\validate_full.bat`
when behavior may also move.

## Priority 1 - Unify Contact Solving Around A Body-State Cache

The terrain solver is already shaped like a hot loop. The persistent object solver is not.
It repeatedly calls `GetVelocity`, `SetLinearVelocity`, `GetAngularVelocity`, and
`SetAngularVelocity` inside `applyImpulse` for every contact row and iteration.

Suggested implementation:

1. Add a small internal `SolverBodyState` cache for each awake body:
   - linear velocity
   - angular velocity
   - inverse mass
   - world inverse inertia representation
   - sleep/awake flag
2. Build contact rows against body indices.
3. Run all PGS iterations against cached arrays only.
4. Write velocities back to `GameModel` once after the solver pass.

Expected benefit:

- Removes getter/setter churn from the persistent solver inner loop.
- Improves cache locality because row solving mutates compact body state rather than full
  `GameModel` objects.
- Creates a clean foundation for SIMD on object-object contacts.

Correctness note:

- Persistent object contacts currently use component-wise inverse inertia in
  `GameModelCollection::SolvePersistentObjectContacts`. For boxes, precomputing world inverse
  inertia once per body would make object contacts consistent with the terrain solver and reduce
  repeated orientation work.

## Priority 2 - Extract One Shared Contact Solver Kernel

There are currently multiple response paths:

- Terrain manifold solver in `ImpulseSolver::RespondCollisionTerrain`.
- Immediate sphere-sphere and mixed-shape response in `ImpulseSolver::RespondCollisionGameModels`.
- Persistent object-object contact solver in `GameModelCollection::SolvePersistentObjectContacts`.

The long-term optimisation is to make contact generation separate from solving:

1. Generate terrain contacts.
2. Generate swept impact contacts.
3. Generate persistent/speculative object contacts.
4. Feed all rows into one solver kernel.

Suggested data shape:

```cpp
struct SolverContactRow
{
    uint16_t bodyA;
    uint16_t bodyB; // sentinel for static terrain
    Vector3 normal;
    Vector3 tangent1;
    Vector3 tangent2;
    Vector3 rA;
    Vector3 rB;
    float normalMass;
    float tangentMass1;
    float tangentMass2;
    float bias;
    float frictionLimit;
    float accN;
    float accT1;
    float accT2;
};
```

Expected benefit:

- One place for normal/friction math.
- One place for early-out thresholds.
- One path to SIMD and future SoA packing.
- Fewer cases where immediate response and persistent response fight each other.

Validation:

- This is broad physics behavior. Run `tools\validate_physics.bat` and `tools\validate_perf.bat`.
- If `GameModelCollection*` is touched, include renderer validation per `AGENTS.md`; for a combined
  extraction, `tools\validate_full.bat` is the safest single command.

## Priority 3 - Optimise The Persistent Object-Contact Path

Once body state is cached, attack object-contact costs directly.

Recommended changes:

1. Replace linear warm-start cache lookup with sorted keys or a fixed-size hash table.
   - Current cache matching scans `m_persistentContactCache` for every new contact.
   - Pair-key matching is acceptable for one sphere contact per pair, but should become feature-key
     based before multiple box contact rows are cached.
2. Cache contact count and friction support data per body for the frame.
3. Use vector friction cone clamping rather than independent `tangent1` and `tangent2` clamps.
   - This is a correctness improvement and can reduce over-large diagonal friction.
4. Add adaptive iteration count per island/contact set.
   - Settled islands should early-out quickly.
   - Active impact islands can keep the larger iteration budget.
5. SIMD the object solver after data layout is compact.
   - The terrain SSE helpers can likely be reused after a shared solver kernel exists.

Guardrails:

- The old terrain contact caching experiment regressed performance in the audit. Do not assume warm
  starting is free. Measure cache hit rate, extra iterations, and total solve time before keeping it.
- The old centroid-collapse experiment also regressed. Do not collapse multi-point support contacts
  unless a scene-specific measurement proves it helps.

## Priority 4 - Reduce Broadphase Work Around Sleeping Objects

Solver mode currently inserts sleeping objects into the same grid as awake objects so they can be
woken by active bodies. That preserves correctness, but it also makes pair generation and pair
dedup do work for sleep/sleep pairs that will later be skipped.

Suggested structure:

1. Maintain an awake grid updated every frame.
2. Maintain a sleeping/static grid updated only when objects go to sleep, wake, teleport, or respawn.
3. Generate:
   - awake/awake pairs for normal solving
   - awake/sleeping pairs for wake tests
   - no sleeping/sleeping pairs
4. Only wake a sleeper after an actual overlap or conservative predicted overlap.

Expected benefit:

- Lower broadphase insertion cost in settled scenes.
- Fewer candidate pairs reaching narrowphase.
- Better scaling when most objects are asleep.

Validation:

- `tools\validate_physics.bat`
- `tools\validate_perf.bat`
- Extra manual review of wake behavior for stacked balls and boxes.

## Priority 5 - Terrain Contact And Grounded Fast Paths

Terrain collision detection was already optimised, and the audit says detection is no longer the
main bottleneck. The remaining useful terrain ideas are narrow and should be measured carefully.

Suggestions:

1. Cache each grounded body's last terrain cell/quad and plane.
   - If the body remains within the same terrain quad and movement is small, reuse the plane for
     contact generation.
   - Invalidate on cell change, large displacement, wake, teleport, or high angular speed.
2. Add per-region terrain height maxima.
   - The global max-height early-out helps only when objects are above the highest point anywhere.
   - A coarse height grid could skip terrain queries for airborne objects over lower regions.
3. Precompute box local corner vectors.
   - `RespondCollisionTerrain` rebuilds the 8 local corner vectors while building box contacts.
   - Store or expose canonical local corners from `BoundingBox` to reduce per-contact setup.
4. Gate visual roll alignment more aggressively while it exists.
   - The config has speed, omega, and interval fields, but the terrain impulse path only checks
     enabled state and water before doing expensive orientation math.
   - Prefer deleting visual alignment after natural rolling is stable.

Validation:

- Terrain changes are physics changes: `tools\validate_physics.bat`.
- If the fast path is performance motivated, also run `tools\validate_perf.bat`.

## Priority 6 - Remove Expensive Stability Shims After Solver Improvements

Some current work is both a correctness smell and a CPU cost. These should be removed only after
tests prove the shared solver handles the same scenes.

Candidates:

- Direct position projection after terrain solve.
- Direct position projection after persistent contacts.
- Visual pole alignment in terrain response.
- Angular velocity throttling in normal `ApplyForces` flow.
- Separate sphere-sphere angular and linear response functions.
- Hardcoded rolling friction in terrain response.

Replacement direction:

- Use split impulse or carefully capped Baumgarte for penetration correction.
- Let tangent friction produce rolling instead of visual orientation correction.
- Use config-backed solver thresholds for iteration count, slop, beta, sleep, rolling friction,
  and spin friction.
- Keep any emergency velocity clamps debug-only and report them as diagnostics, not normal physics.

## Lower-Priority Codebase Ideas

These are not as directly tied to the new solver, but they are worth keeping in the backlog:

- Convert `SkullbonezConfig.cpp` to a small parse table so adding solver tuning fields does not
  grow the long string-compare chain.
- Add a perf artifact that records top profiler markers for physics scenes, not only render-facing
  perf scenes.
- Keep renderer optimisation work separate from this physics pass. Existing audits already call out
  GL state/uniform caching, DX11 state caching, and DX12 allocator/upload-buffer work.

## Proposed Implementation Order

1. Add profiler splits and capture fresh baselines.
2. Convert persistent object solving to cached `SolverBodyState` arrays.
3. Precompute world inverse inertia once per awake body.
4. Replace linear contact-cache lookup with sorted or hashed matching.
5. Extract a shared contact solver kernel.
6. Feed terrain and persistent object contacts through the shared kernel.
7. Rework broadphase sleeping partition.
8. Add terrain grounded-plane cache only if profiling still points at terrain setup.
9. Remove or debug-gate visual alignment and velocity clamps after regression scenes pass.

## Suggested Review Questions

- Should the next implementation pass optimise the current solver in place, or first extract the
  shared solver kernel?
- Are sleeping-heavy scenes the most important target, or do active collision storms matter more?
- Is exact baseline compatibility required for solver optimisation, or are intentional physics CSV
  baseline updates acceptable when the math improves?
- Should box/object contacts become precise OBB manifolds now, or stay bounding-radius based until
  the solver hot loop is cheaper?

## Validation Plan For This Markdown Change

Run:

```bat
tools\validate_fast.bat
```

Future code implementation should use the stricter validation listed in each priority section.
