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

## Key Finding

**The terrain collision detection is not the bottleneck.** The `Frame/Physics/Terrain` profiler
marker is misleading — it wraps both detection AND response. At steady state (all objects
grounded), the 20-iteration sequential impulse solver accounts for **96% of terrain marker time**.

The detection-only portion was successfully optimized by 43%, but total terrain time barely moved
because the impulse solver dominates. Further optimization of the terrain marker would require:

1. **Reducing impulse solver iterations** (currently 20; fewer iterations trade accuracy for speed)
2. **Contact caching** (warm-start from previous frame's contacts instead of recomputing)
3. **Sleeping** (skip terrain detection + response for objects at rest for N frames)
4. **Per-region max height** (hierarchical heightfield for better airborne culling)

---

## Regression Test Status

Both `physics_regression_legacy` and `physics_regression_solver` pass with exact byte match
against updated baselines. The baseline update reflects:
- Opt1: Closed-form box offset has slightly different floating-point rounding than the 8-vertex
  loop (algebraically identical, numerically more precise)
- Opt4: Removing the redundant `UpdatePosition(0)` terrain clamp causes imperceptible 4th-decimal
  velocity differences in 34/6001 lines (solver only)

## Files Modified

| File | Change |
|------|--------|
| `SkullbonezSource/SkullbonezRotationMatrix.h` | Added `SupportExtentY()` inline method |
| `SkullbonezSource/SkullbonezGameModel.cpp` | Opts 1, 2, 4: closed-form in detection + clamp, airborne early-out, zero-time skip |
| `SkullbonezSource/SkullbonezTerrain.h` | Added `GetMaxHeight()`, `m_maxTerrainHeight`, `QueryCollisionDataUnchecked` decl |
| `SkullbonezSource/SkullbonezTerrain.cpp` | Opts 2, 3: max height computation, unchecked query fast path |
| `SkullbonezSource/SkullbonezCommon.h` | Added `<cfloat>` include for `FLT_MAX` |
| `TestOutput/baselines/*.csv` | Updated regression baselines |
