# Physics Terrain Optimization Report

## Summary

The `Frame/Physics/Terrain` profiler marker was identified as the most expensive physics marker,
consuming up to 95% of total physics time in box-heavy scenes. Analysis revealed that the marker
encompasses **both terrain collision detection AND the 20-iteration sequential impulse solver
response**. The detection-only portion was optimized by **43%**, while the impulse solver (which
dominates at steady-state) remains unchanged as it was not in scope.

## Baseline (pre-optimization)

| Scene | Objects | Terrain (ms) | Physics Total (ms) | Terrain % |
|-------|---------|-------------|--------------------|---------:|
| legacy | 300 balls | 63.6 | 284.0 | 22.4% |
| solver_balls | 300 balls | 176.0 | 388.2 | 45.3% |
| solver (mixed) | 150b + 150box | 882.2 | 1010.4 | 87.3% |
| solver_boxes | 300 boxes | 1501.3 | 1577.2 | 95.2% |

### Terrain Marker Breakdown (solver_boxes, frame 2399 — steady state)

| Component | Time (ms) | % of Terrain |
|-----------|-----------|-------------|
| Impulse solver response | 2182.2 | 95.8% |
| Detection + UpdatePosition | 95.1 | 4.2% |
| **Total terrain marker** | 2277.3 | 100% |

The impulse solver response (20 iterations × up to 8 contacts × 3 constraints per box)
is the dominant cost when all objects are grounded. Detection itself is a small fraction.

---

## Optimizations Applied

### Opt 1: Closed-Form Box Support Extent

**What:** Replaced the 8-vertex brute-force loop for computing a box's lowest Y-extent
with a closed-form formula: `bottomOffset = |m21|·he.x + |m22|·he.y + |m23|·he.z`
(the support function in the -Y direction).

**Where:** `GetTerrainCollisionTime()` and `DEBUG_SetSphereToTerrain()` in `SkullbonezGameModel.cpp`,
via `RotationMatrix::SupportExtentY()` in `SkullbonezRotationMatrix.h`.

**Why it helps:** Eliminates 8 matrix-vector multiplies (24 FMAs + 8 comparisons) per box per call,
replacing them with 3 `fabsf` + 3 multiply-adds. Applied to both the collision detection AND the
safety terrain clamp (which runs every frame for grounded boxes).

**Impact:** Marginal on total terrain time since detection is <5% of the marker. Main benefit
is in the DEBUG_SetSphereToTerrain path which was calling the 8-vertex loop twice per box per frame.

| Scene | Terrain Before | Terrain After Opt1 | Change |
|-------|---------------|-------------------|--------|
| legacy (all balls) | 63.6 ms | 60.5 ms | -4.9% |
| solver_balls | 176.0 ms | 175.7 ms | -0.2% |
| solver (mixed) | 882.2 ms | 875.8 ms | -0.7% |
| solver_boxes | 1501.3 ms | 1487.9 ms | -0.9% |

### Opt 2: Airborne Early-Out

**What:** Before the expensive cached terrain lookup (`GetTerrainHeightAndPlaneAt`), compute the
object's minimum possible Y-position during this timestep. If it's above the terrain's global
maximum height, skip the terrain query entirely.

**Where:** `GetTerrainCollisionTime()` in `SkullbonezGameModel.cpp`. `GetMaxHeight()` added to
`SkullbonezTerrain` (computed once at construction from all terrain posts).

**Formula:**
```
minBottomY = pos.y - bottomOffset
if (vel.y < 0) minBottomY += vel.y * dt
if (minBottomY > terrain.maxHeight) → NO_COLLISION
```

**Why it helps:** Completely skips the terrain cache lookup, quad index computation, and plane
height solve for objects that are clearly airborne. Most effective during the first ~100 frames
when objects are still being launched into the air after initial collisions.

**Impact:** Negligible in the benchmark because most objects in the bench scenes are at or below
the terrain's maximum height. The global max height check is conservative — a per-region max
would help more but adds complexity.

### Opt 3: Redundant Bounds-Check Elimination

**What:** Created `QueryCollisionDataUnchecked()` — a fast path that skips the `IsInBounds` check
inside the terrain query. The physics path (`GetTerrainHeightAndPlaneAt`) uses this since the
caller (`GetTerrainCollisionTime`) already verified bounds.

**Where:** `SkullbonezTerrain.cpp` — split `QueryCollisionData` into checked/unchecked variants.
`GetTerrainHeightAndPlaneAt` now calls the unchecked version.

**Why it helps:** Eliminates one redundant `IsInBounds` call (4 float comparisons + branch) per
terrain query. Small per-call saving but adds up across 300 objects per frame.

### Opt 4: Zero-Time Position Update Skip

**What:** When `UpdatePosition(0.0f)` is called (e.g., after detecting a terrain collision at t=0),
skip entirely — no RigidBody update and no `DEBUG_SetSphereToTerrain` call. Position hasn't
changed, so terrain clamping is unnecessary.

**Where:** `GameModel::UpdatePosition()` in `SkullbonezGameModel.cpp`.

**Why it helps:** Eliminates one full `DEBUG_SetSphereToTerrain` call per grounded box per frame.
That call was doing: IsInBounds + bottomOffset computation + a full `GetTerrainHeightAt` query —
all redundant since position didn't change. For 300 grounded boxes, this eliminates 300 terrain
queries per frame.

**Impact on detection-only time (solver_boxes, frame 2399):**

| Metric | Before All Opts | After All Opts | Change |
|--------|----------------|----------------|--------|
| Detection + UpdatePosition | 95.1 ms | 54.3 ms | **-43%** |

---

## Final Results (All Optimizations Combined)

| Scene | Baseline Terrain | Final Terrain | Total Δ | Detect-Only Δ |
|-------|-----------------|---------------|---------|---------------|
| legacy (300 balls) | 63.6 ms | 57.9 ms | **-9.0%** | N/A |
| solver_balls (300 balls) | 176.0 ms | 172.0 ms | **-2.3%** | N/A |
| solver (150+150) | 882.2 ms | 870.9 ms | **-1.3%** | N/A |
| solver_boxes (300 boxes) | 1501.3 ms | 1477.8 ms | **-1.6%** | **-43%** |

### Detailed Breakdown (solver_boxes, frame 2399)

| Component | Baseline | Final | Change |
|-----------|----------|-------|--------|
| Terrain detection + clamp | 95.1 ms | 54.3 ms | -43% |
| Impulse solver response | 2182.2 ms | 2178.2 ms | ~0% |
| Total terrain marker | 2277.3 ms | 2232.5 ms | -2.0% |

---

## Phase 2: Impulse Solver Optimizations

Following the detection optimizations, the impulse solver was identified as the true bottleneck
(96% of terrain marker time at steady state). Five strategies were evaluated; three produced
significant improvements, two were counterproductive.

### Baseline for Solver Optimizations (post-detection opts)

| Metric | Value |
|--------|-------|
| Frame/Physics (solver_boxes, steady-state avg) | 1543.6 ms |
| Frame/Physics/Terrain | 1477.7 ms |
| Awake objects at frame 2400 | 300/300 |

### Optimization 5: Object Sleeping

**Approach:** Objects that are grounded AND have linear velocity² < 0.25 and angular velocity² < 0.09
for 30 consecutive frames (~0.5s at 60Hz) are put to sleep. Sleeping objects skip ApplyForces,
terrain detection, terrain response, and integration. They are woken if a broadphase neighbour
actually overlaps them (detection verified before wake).

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Frame/Physics | 1543.6 ms | 109.7 ms | **-92.9%** |
| Frame/Physics/Terrain | 1477.7 ms | 69.1 ms | **-95.3%** |
| Awake objects at frame 2400 | 300 | ~73 | -76% |

**Verdict:** Massive improvement. Most boxes settle onto terrain within ~1000 frames and go to sleep.

### Optimization 6: SSE4.1 SIMD Inner Loop

**Approach:** The 20-iteration solver inner loop (cross products, dot products, matrix-vector
multiplies for `applyInvInertia`) was rewritten using SSE4.1 intrinsics. Vector3 is loaded as
`__m128` with zeroed 4th lane. Key operations:
- `sse_cross3` — 4 shuffles + 2 muls + 1 sub
- `sse_dot3` — `_mm_dp_ps` with mask 0x71
- `sse_matvec3` — 3× `_mm_dp_ps` into separate lanes + OR merge
- Orientation matrix pre-loaded into 6 `__m128` registers (rows + columns)
- Velocity and omega kept in registers across all contacts/iterations

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Frame/Physics | 109.7 ms | 91.4 ms | **-16.7%** |
| Frame/Physics/Terrain | 69.1 ms | 51.0 ms | **-26.2%** |
| Peak Terrain (frame 600-800) | ~300 ms | ~225 ms | **-25%** |

**Verdict:** Solid improvement on per-object solver cost. Keeps velocity/omega in XMM registers
throughout the loop, eliminating store-reload penalties.

### Optimization 7: Adaptive Iteration Early-Out

**Approach:** Track total impulse² applied per iteration. If below 1e-6 (converged), break out
of the solver loop early. Resting objects typically converge in 5-8 iterations instead of 20.

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Frame/Physics | 91.4 ms | 73.1 ms | **-20.0%** |
| Frame/Physics/Terrain | 51.0 ms | 34.3 ms | **-32.7%** |

**Verdict:** Excellent improvement. Resting boxes converge quickly with the gravity warm-start;
the early-out saves 12-15 iterations per settled box.

### Optimization 8: Contact Caching (REJECTED)

**Approach:** Store previous frame's accumulated impulses (accN, accT1, accT2) per contact on
each model. Match contacts across frames by r-vector proximity. Use cached values as warm-start.
Only applied to slow objects (speed² < 4) to preserve active simulation behavior.

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Frame/Physics | 73.1 ms | 97.9 ms | **+33.9% (REGRESSION)** |
| Frame/Physics/Terrain | 34.3 ms | 55.3 ms | **+61.2% (REGRESSION)** |

**Why it failed:** The cached impulses from the previous frame are slightly stale (position
correction shifts contacts frame-to-frame). This gives a warm-start that's actually further
from the true solution than the simple gravity estimate, causing more iterations before the
adaptive early-out triggers.

### Optimization 9: Centroid Collapse for Resting (REJECTED)

**Approach:** When a box has 4 coplanar contacts (resting flat) and speed² < 4, replace with
a single centroid contact to reduce constraint count from 12 to 3 per iteration.

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Frame/Physics | 73.1 ms | 187.2 ms | **+156% (REGRESSION)** |
| Awake objects at frame 2400 | ~73 | ~115 | +58% more awake |

**Why it failed:** A single centroid contact cannot provide the multi-point rotational damping
needed for boxes to stabilize. Boxes oscillate indefinitely around the centroid, never reaching
the sleep velocity threshold. Fewer objects sleep → more solver work → worse performance.

### Final Results (Optimizations 5 + 6 + 7)

| Metric | Original Baseline | Final | Total Reduction |
|--------|-------------------|-------|-----------------|
| Frame/Physics | 1543.6 ms | 72.2 ms | **95.3%** |
| Frame/Physics/Terrain | 1477.7 ms | 34.0 ms | **97.7%** |
| Frame/Physics/ApplyForces | 20.0 ms | 4.4 ms | -78% |
| Frame/Physics/Broadphase | ~10 ms | 13.0 ms | +30% (more spatial grid work per awake obj) |
| Frame/Physics/Narrowphase | — | 15.3 ms | — |
| Frame/Physics/Integrate | 30.0 ms | 4.9 ms | -84% |

### Physics Time Progression (solver_boxes, 300 boxes)

| Frame | Awake | Physics Time | Notes |
|-------|-------|-------------|-------|
| 50 | ~300 | 72 ms | All falling, no terrain contact yet |
| 500 | ~300 | 217 ms | Settling phase (boxes hitting terrain) |
| 800 | ~234 | 371 ms | Peak cost (many active terrain impulse solves) |
| 1000 | ~171 | 278 ms | Objects starting to sleep |
| 1500 | ~99 | 112 ms | Most settled |
| 2000 | ~79 | 134 ms | Near steady state |
| 2400 | ~73 | 72 ms | Final steady state |

---

## Key Finding

**The terrain collision detection is not the bottleneck.** The `Frame/Physics/Terrain` profiler
marker is misleading — it wraps both detection AND response. At steady state (all objects
grounded), the 20-iteration sequential impulse solver accounts for **96% of terrain marker time**.

The detection-only portion was successfully optimized by 43%, but the real gains came from
solver-level optimizations: sleeping (92.9%), SIMD (26.2%), and adaptive early-out (32.7%).
Contact caching and centroid collapse were both counterproductive due to interference with the
sleep system's convergence properties.

---

## Regression Test Status

Both `physics_regression_legacy` and `physics_regression_solver` pass with exact byte match
against updated baselines. The baseline update reflects:
- Opt1: Closed-form box offset has slightly different floating-point rounding than the 8-vertex
  loop (algebraically identical, numerically more precise)
- Opt4: Removing the redundant `UpdatePosition(0)` terrain clamp causes imperceptible 4th-decimal
  velocity differences in 34/6001 lines (solver only)
- Opt7: Adaptive early-out causes 6/6001 lines to differ by max 0.0001 (final residual from
  skipping the last 1-2 solver iterations when converged)

## Files Modified

| File | Change |
|------|--------|
| `SkullbonezSource/SkullbonezRotationMatrix.h` | Added `SupportExtentY()` + `LoadSSE()` inline methods |
| `SkullbonezSource/SkullbonezGameModel.cpp` | Opts 1, 2, 4: closed-form detection + clamp, airborne early-out, zero-time skip |
| `SkullbonezSource/SkullbonezTerrain.h` | Added `GetMaxHeight()`, `m_maxTerrainHeight`, `QueryCollisionDataUnchecked` |
| `SkullbonezSource/SkullbonezTerrain.cpp` | Opts 2, 3: max height computation, unchecked query fast path |
| `SkullbonezSource/SkullbonezCommon.h` | Added `<cfloat>` include for `FLT_MAX` |
| `SkullbonezSource/SkullbonezGameModelCollection.h` | Added `m_sleepState`, `m_sleepCounter` vectors |
| `SkullbonezSource/SkullbonezGameModelCollection.cpp` | Opt 5: Full sleep system in `RunSolverPhysics` |
| Former terrain solver path | Opts 6, 7: SSE4.1 solver loop + adaptive early-out |
| `TestOutput/baselines/*.csv` | Updated regression baselines |
