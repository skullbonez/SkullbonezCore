# Terrain Shared Row Pipeline Plan

Goal: bring terrain contacts into the same Catto-style row/body pipeline used by
object/object contacts, while preserving terrain-specific support classification,
swept collision timing, and the current stable sleep behavior.

## Current State

- Object/object swept detection is a CCD front-end only. It advances fast hits to
  contact candidates, wakes sleepers, and emits pipeline records. Persistent
  object rows own velocity response and warm-started contact solving.
- Terrain still uses `CollisionDetectTerrain` plus `CollisionResponseTerrain`.
  That path consumes cached terrain plane/ray state, applies its own impulse and
  friction policy, and reports whether the hit is credible sleep support.
- Terrain is the root of sleep support. Object contacts can propagate support
  only after terrain has produced a stable support seed.
- Debug regression now includes high-speed bullet sweeps into fixed wall/object
  targets and terrain with collision-time CSV baselines.

## Non-Negotiables

- Keep terrain swept detection until the row pipeline can prove equivalent or
  better bullet behavior.
- Preserve the support classifier semantics:
  stable terrain support may seed sleep; edge/point/unstable hits must inhibit
  sleep even when collision response succeeds.
- Do not remove the existing terrain response path until the shared row path has
  deterministic baselines and SkullScope counters for support, slip, and row
  convergence.
- Maintain byte-exact physics regression CSVs and collision-time baselines.

## Phase 1: Terrain Contact Data Model

1. Add a terrain contact manifold struct parallel to object contact manifolds:
   dynamic body index, terrain sentinel body, point, normal, penetration,
   tangent basis, support classification, terrain cell/plane metadata.
2. Build terrain manifolds from the existing detection data:
   sphere bottom point for sphere/terrain, lowest credible vertices for
   box/terrain, and swept hit plane for high-speed impacts.
3. Emit SkullScope terrain contact summaries without replacing response yet.
4. Validate with `tools\validate_physics.bat` and targeted bullet sweep CSVs.

## Phase 2: Shared Row Setup

1. Extract row setup helpers shared by object rows and terrain rows:
   effective mass, tangent basis, bias, friction limits, accumulated impulse
   fields, and solver body state access.
2. Represent terrain as body B with infinite mass and zero velocity.
3. Keep terrain support classification as metadata on generated rows rather than
   as a side effect of solver impulse magnitude.
4. Add debug counters for terrain row count, supported row count, inhibited row
   count, warm-start hits, and max correction.

## Phase 3: Parallel Terrain Row Solve

1. Run the terrain rows through the same projected Gauss-Seidel loop used by
   persistent object contacts.
2. Initially keep terrain rows non-persistent to avoid stale terrain cache risks.
3. Compare terrain row output against the existing terrain response path behind a
   debug/runtime toggle.
4. Add a narrow regression scene for each terrain class:
   sphere drop, box flat slope, box edge contact, high-speed terrain bullet.

## Phase 4: Persistence And Removal

1. Add terrain row cache only if profiling or stability measurements justify it.
2. Once shared rows pass determinism, perf, and visual checks, remove the
   immediate terrain impulse response from the normal path.
3. Keep support classification explicit and separately testable after removal.
4. Retire terrain response flags only after no terrain path depends on cached
   plane/ray state.

## Validation

- During development: `tools\validate_physics.bat`
- When row setup or broadphase ordering changes: `tools\validate_physics.bat`
  plus `tools\validate_perf.bat`
- Before removing the old terrain response path: `tools\validate_full.bat`
- Every terrain-row milestone must keep these artifacts byte-exact unless the
  baseline update is intentional and explained:
  `physics_regression_solver.csv`, `bullet_sweep_wall.csv`,
  `bullet_sweep_object.csv`, and `bullet_sweep_terrain.csv`

## Known Risks

- Terrain support is policy, not just contact geometry. Folding it too deeply
  into generic rows can accidentally let edge contacts seed sleep.
- The existing terrain path has hot SSE/profile-specific behavior. A shared row
  path must be measured before replacing it.
- Box/terrain contact can be multi-point and uneven. Collapse-to-one-row impact
  behavior may be correct for high-speed hits, but resting support needs enough
  contact rows to prevent rocking and false edge rest.
