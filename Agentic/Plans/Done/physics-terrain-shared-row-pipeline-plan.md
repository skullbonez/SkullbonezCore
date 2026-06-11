# Terrain Solver Integration Plan

Date: 2026-06-09
Status: Implemented and validated
Impact area: physics architecture, collision pipeline, diagnostics, tests
Validation for implementation: `tools\validate_physics.bat`, `tools\validate_perf.bat`, and `tools\validate_full.bat`

## Implementation Notes

- Terrain detection still owns swept terrain timing and advances bodies to the
  time of impact.
- `GameModel::BuildTerrainContactManifold` now turns the cached terrain collision
  plane into terrain contact manifolds:
  - spheres use the bottom-pole contact,
  - boxes use the credible vertex cluster,
  - high-speed multi-point impacts collapse to a centroid contact,
  - `ClassifyBoxTerrainSupport` metadata is attached before solving.
- `GameModelCollection::SolvePersistentObjectContacts` appends terrain rows after
  object/object rows. Terrain rows use body B = `-1` as a static terrain sentinel.
- The shared row loop owns terrain effective mass, bias, friction, warm-start,
  PGS iteration, velocity writeback, position correction, cache storage, debug
  contacts, and SkullScope contact output.
- Stable terrain support keeps the gravity warm-start, friction floor, rolling
  rest damping, and sleep seeding. Edge/point terrain contacts solve impacts and
  penetration but inhibit sleep and do not receive rest-only support policy.
- Unsupported terrain rows are deliberately excluded from the persistent
  warm-start cache. This keeps edge/point impact impulses from being replayed as
  launch impulses on the next frame.
- The old terrain-only response adapter and impulse solver files have been
  removed from source and the Visual Studio project.
- Pipeline records now include `terrain_manifold` and `terrain_row` stages.
- SkullScope emits terrain contacts as `sphere/terrain` or `box/terrain` and
  terrain support seeds as `support_edge` rows with `source="terrain"`.
- Added `SkullbonezData/scenes/box_only_rest.scene` as a focused box-only rest
  diagnostic. The scene reaches 15/15 sleeping bodies after the cache gate.

## Goal

Bring terrain contacts into the same Catto-style solver row pipeline used for
object/object contacts.

Before this migration, terrain was partly Catto-shaped but still special-cased:

```text
RunSolverPhysics
-> CollisionDetectTerrain
-> GetTerrainCollisionTime
-> terrain-only response adapter
```

The target shape is:

```text
terrain query / swept terrain detection
-> terrain contact manifold generation
-> shared solver row setup
-> shared PGS / sequential impulse solve
-> shared writeback, correction, warm-start cache, sleep support, diagnostics
```

The solver should care about contact manifolds and rows, not terrain polygons.
Terrain polygon, plane, and heightfield lookup should be detection/manifold data
only. Once a terrain contact is generated, the response path should solve it the
same way it solves object contacts.

## Current Terrain Functions

Frame-level terrain ownership:

- `GameModelCollection::RunSolverPhysics`
  - Current terrain phase lives under `Frame/Physics/Terrain`.
  - Calls `CollisionDetectTerrain`.
  - If `IsResponseRequired()` is true, advances to collision time, calls
    `BuildTerrainContactManifold`, clears the response flag, records
    `TerrainHit`, emits collision time, and seeds or inhibits sleep support.

Detection and handoff:

- `GameModel::CollisionDetectTerrain`
  - Calls `GetTerrainCollisionTime`.
  - Sets `m_isResponseRequired`.
  - Stores `m_responseInformation.collidedPlane`,
    `m_responseInformation.collidedRay`, and collision time.
- `GameModel::GetTerrainCollisionTime`
  - Performs sphere/terrain and box/terrain swept detection.
  - Uses `GetClosestBoxTerrainVertex` for box support and current-contact
    checks.
  - Sweeps box vertices against terrain planes for fast motion.
- `GameModel::GetClosestBoxTerrainVertex`
  - Samples all OBB corners against terrain height/plane and returns the closest
    credible terrain vertex.

Shared row handoff:

- `GameModel::BuildTerrainContactManifold`
  - Builds explicit sphere/terrain and box/terrain manifolds from the cached
    swept terrain hit data.
  - Attaches `ClassifyBoxTerrainSupport` metadata before solving.
- `GameModelCollection::SolvePersistentObjectContacts`
  - Converts terrain manifolds to shared solver rows with body B set to the
    static terrain sentinel.
  - Solves terrain normal/friction/restitution response in the same row loop as
    object/object contacts.
- `GameModel::DEBUG_SetSphereToTerrain`
  - Safety clamp path after movement; name is historical and box-aware behavior
    now exists inside it.

Terrain query:

- `Terrain::GetTerrainHeightAt`
- `Terrain::GetTerrainNormalAt`
- `Terrain::GetTerrainHeightAndNormalAt`
- `Terrain::GetTerrainHeightAndPlaneAt`
- `Terrain::QueryCollisionData`
- `Terrain::QueryCollisionDataUnchecked`
- `Terrain::LocatePolygon`

Support policy:

- `ClassifyBoxTerrainSupport`
- `ProbeBoxTerrainVertices`
- `ComputeBoxTerrainBestFaceNormalDot`

## Definitions

Contact manifold:

- Geometric collision result.
- Answers "what touched what, where, and how deeply?"
- Contains body ids, contact points, normal, penetration/separation, feature id,
  material data, support policy metadata, and optional time-of-impact data.

Solver row:

- One mathematical constraint created from a manifold.
- Answers "what impulse axis must the solver enforce?"
- Contains body ids, contact offsets, row axis, effective mass, bias,
  lower/upper impulse limits, accumulated impulse, friction link, and debug id.

One manifold can generate many rows:

```text
sphere on terrain: 1 contact point -> 1 normal row + 2 friction rows
box flat on terrain: 4 contact points -> 4 normal rows + 8 friction rows
```

## Non-Negotiables

- Keep swept terrain detection until the shared row path proves equivalent or
  better for high-speed terrain bullets.
- Keep support classification explicit. Stable terrain support may seed sleep;
  edge, point, or unstable support must inhibit sleep even if collision response
  succeeds.
- Do not let terrain rows accidentally give edge/point contacts the old resting
  support privileges: gravity warm-start, static-friction floor, rolling/rest
  damping, or sleep seeding.
- Preserve deterministic physics regression behavior. Any intentional baseline
  update must explain the numerical reason.
- Keep terrain diagnostics cheap and queryable through SkullScope. Do not rely
  on raw trace dumps as the primary analysis artifact.
- Make the visual pipeline stepper see terrain manifolds and rows the same way
  it sees object manifolds and rows.

## Removed Or Reduced

These items were removed or reduced by the shared terrain-row migration:

- The terrain-only runtime response adapter.
- The terrain-only impulse solver source files.
- Terrain use of `m_isResponseRequired`
  - Reduced to a swept-detection handoff flag that is cleared once terrain
    manifolds are emitted.
- Terrain response dependence on `m_responseInformation.collidedPlane` and
  `collidedRay`
  - Reduced to detection input for manifold generation. The generated manifold
    carries contact normal, points, penetration, and time-of-impact metadata.
- Resting support policy hidden inside terrain response
  - The policy is attached to terrain manifolds/rows.
- `DEBUG_SetSphereToTerrain`
  - Remove only after solver stabilization and regression coverage make the
    clamp unnecessary. Until then, make it an assertion/safety net rather than
    the real response path.

These should remain:

- Terrain height/plane/normal queries.
- Flat slope analytic terrain.
- Terrain bounds and max-height fast-outs.
- Swept terrain detection.
- Terrain collision-time logging for bullet regressions.
- Support classification semantics.

## Proposed Data Model

Add a terrain manifold representation parallel to object contact manifolds:

```cpp
struct TerrainContactPoint
{
    Vector3 worldPoint;
    Vector3 rA;
    float penetration;
    uint32_t featureId;
};

struct TerrainContactManifold
{
    int bodyA;
    int bodyB; // static terrain sentinel, probably -1
    Vector3 normal;
    Vector3 tangent1;
    Vector3 tangent2;
    TerrainContactPoint points[8];
    int pointCount;
    float timeOfImpact;
    bool sweptHit;
    bool supportsRestingPolicy;
    bool inhibitsSleep;
    uint32_t terrainCellId;
    uint32_t materialId;
};
```

Use the same row representation for object and terrain contacts. Terrain body B
should behave as infinite mass with zero velocity, zero inverse inertia, and no
writeback.

## Phase 0: Confirm Current Behavior

Purpose: lock down what the current terrain path does before moving ownership.

1. Document current terrain call order in the plan and physics overview.
2. Confirm terrain regression scenes cover:
   - sphere drop on flat terrain,
   - rolling sphere on slope,
   - box flat landing,
   - box edge/point instability,
   - high-speed terrain bullet,
   - varied benchmark with sleep enabled and no-sleep.
3. Add or verify SkullScope queries for:
   - terrain hit count,
   - terrain support seed count,
   - terrain sleep inhibit count,
   - terrain collision time,
   - terrain row/contact counts once rows exist.

Expected result:

- The old behavior is observable before any response ownership changes.

Validation for code/test changes:

```bat
tools\validate_physics.bat
```

## Phase 1: Split Terrain Manifold Generation From Response

Purpose: create terrain manifolds independently from response ownership.

1. Extract terrain manifold generation from
   the old terrain response path and `GameModel::GetTerrainCollisionTime`.
2. Generate sphere terrain manifolds from the bottom-pole contact point.
3. Generate box terrain manifolds from the credible vertex cluster against the
   detected terrain plane.
4. Preserve current high-speed impact behavior:
   - keep swept detection,
   - keep time-of-impact,
   - keep impact manifold collapse behavior if required for stability.
5. Attach `ClassifyBoxTerrainSupport` result to the manifold.
6. Emit debug/pipeline records for the manifold, but keep old response solving
   active.

Expected result:

- Terrain detection can produce contact geometry independently of terrain
  response.
- The visualizer and SkullScope can inspect terrain manifolds before rows are
  responsible for response.

Validation:

```bat
tools\validate_physics.bat
```

## Phase 2: Add Terrain Rows In Parallel

Purpose: build terrain rows without letting them affect gameplay yet.

1. Convert terrain manifolds into shared solver rows:
   - one normal row per contact point,
   - two friction rows per contact point,
   - shared tangent basis,
   - linked friction limits,
   - terrain support metadata.
2. Treat terrain as static body B.
3. Use the same effective-mass, bias, restitution, friction, and accumulated
   impulse fields as object/object rows.
4. Add pipeline records for:
   - terrain manifold built,
   - terrain rows built,
   - row effective mass,
   - bias,
   - support policy,
   - sleep support eligibility.
5. Compare old terrain response output against dry-run row calculations in
   SkullScope.

Expected result:

- Terrain rows exist and are inspectable.
- No gameplay behavior should change yet.

Validation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

## Phase 3: Solve Terrain Rows Behind A Toggle

Purpose: run terrain through shared rows while the old path is still available
as an escape hatch during staging.

1. Add an internal runtime toggle for terrain shared-row response.
2. When enabled, route terrain rows into the shared PGS loop.
3. Write solved velocities back through the same body state path as
   object/object contacts.
4. Keep sleep seeding controlled by terrain support metadata, not by raw impulse
   magnitude.
5. Keep terrain collision-time logging unchanged.
6. Compare staged and shared response using targeted scenes:
   - `bullet_sweep_terrain.scene`,
   - `box_flush_test.scene`,
   - `box_slope_test.scene`,
   - `box_crater_edge_repro.scene`,
   - `physics_bench_varied.scene`.

Expected result:

- Terrain can be solved by shared rows on demand.
- Failures can be bisected by toggling the old response path.

Validation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Use `tools\validate_full.bat` if frame ordering, UI, renderer-facing debug, or
`SkullbonezRun*` behavior changes.

## Phase 4: Move Terrain Into The Normal Solver Path

Purpose: make shared terrain rows the default response owner.

1. Insert terrain manifold generation before the shared solver solve.
2. Feed terrain rows and object rows into one row solve pass.
3. Ensure row order is deterministic:
   - object/object rows,
   - terrain rows,
   - or a fixed sorted order by body/contact key.
4. Keep support propagation order explicit:
   - terrain support seeds,
   - object support propagation,
   - sleep island evaluation.
5. Verify direct position correction or split correction still handles terrain
   penetration consistently.
6. Keep the old terrain response toggle for one milestone.

Expected result:

- Terrain response is owned by the shared row solver.
- Terrain support remains the root of sleep support.
- Pipeline visualization can step from terrain manifold to terrain rows to
  solver iterations.

Validation:

```bat
tools\validate_physics.bat
tools\validate_perf.bat
```

Before merging or committing broadly:

```bat
tools\validate_full.bat
```

## Phase 5: Remove The Old Terrain Response Path

Purpose: delete the special-case response code once the shared path is proven.

1. Remove the old terrain-only runtime response adapter.
2. Remove the terrain-only impulse solver source files.
3. Reduce terrain dependence on `m_isResponseRequired` to swept-detection
   handoff.
4. Replace terrain plane/ray response handoff with manifold data.
5. Keep terrain collision-time data only where needed for swept detection and
   regression output.
6. Convert `DEBUG_SetSphereToTerrain` into:
   - a debug assertion,
   - a disabled safety clamp,
   - or delete it if validation proves it is dead.
7. Update `Agentic/Reference/physics-overview.md` and runtime/debug docs.

Expected result:

- There is one velocity response owner for object/object and terrain contacts.
- Terrain-specific code is detection/manifold/support policy, not response.

Validation:

```bat
tools\validate_full.bat
```

## Test Matrix

Required targeted scenes:

- High-speed terrain bullet:
  - Confirms swept terrain hit timing survives the migration.
- Sphere drop and bounce:
  - Confirms normal impulse and restitution.
- Rolling sphere on slope:
  - Confirms tangential friction and angular response.
- Box flat landing:
  - Confirms multi-point support.
- Box slope landing:
  - Confirms oriented box support and terrain normal handling.
- Box edge/point repro:
  - Confirms unstable contacts inhibit sleep and do not receive rest-only
    privileges.
- Varied physics benchmark:
  - Confirms mixed terrain/object contacts, sleeping, and performance.

Required diagnostics:

- Collision-time CSV for high-speed terrain bullets.
- SkullScope terrain manifold and row counts.
- SkullScope support-seed and sleep-inhibit counts.
- Pipeline visualizer stages for terrain manifold and terrain rows.
- Perf markers that separate:
  - terrain detection,
  - terrain manifold generation,
  - terrain row build,
  - shared solver terrain row cost.

## Performance Notes

The current `Frame/Physics/Terrain` marker mixes detection, support policy, and
terrain response. The integrated path should split these markers so future slow
runs are not mysterious:

```text
Frame/Physics/Terrain/Detect
Frame/Physics/Terrain/Manifold
Frame/Physics/Terrain/Rows
Frame/Physics/Solver/ObjectRows
Frame/Physics/Solver/TerrainRows
Frame/Physics/Solver/Iterations
```

Avoid making every terrain row persistent until measurement proves it helps.
Terrain feature ids should be stable enough for diagnostics and possible future
warm starting, but the first integrated path can be non-persistent to reduce
stale heightfield-cache risk.

## Risks

- False sleep support:
  - Edge or point terrain contacts may look solved but must not become support
    anchors.
- Row explosion:
  - Multi-point box terrain contacts create many rows. Budget and profiling
    need to remain visible.
- Restitution mismatch:
  - The old immediate path may have impact-specific behavior that differs from
    persistent rows.
- Ordering drift:
  - Moving terrain before or inside the shared solver changes object support
    propagation timing.
- Debug clamp masking:
  - `DEBUG_SetSphereToTerrain` can hide solver defects if it remains active as a
    real correction path.
- Baseline churn:
  - Terrain feeds sleep islands, so small contact changes can alter long-tail
    deterministic CSVs.

## Definition Of Done

- Terrain contacts enter the same shared solver row path as object contacts.
- The terrain-only impulse solver files are removed.
- The terrain polygon/plane lookup is confined to detection and manifold
  generation.
- Terrain support classification is explicit metadata consumed by sleep and
  rest-policy code.
- The pipeline visualizer can step through terrain detection, manifold
  generation, row setup, warm start, solver iterations, writeback, correction,
  cache, and sleep support.
- `tools\validate_physics.bat`, `tools\validate_perf.bat`, and
  `tools\validate_full.bat` pass after the code migration.
