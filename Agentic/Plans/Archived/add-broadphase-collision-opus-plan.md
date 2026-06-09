# Broadphase Collision Detection — Uniform Spatial Grid

## Problem

The game-model collision loop in `RunPhysics` is O(n²) — every pair is tested via the
expensive swept-sphere narrow-phase. With 500 spheres that is ~125K pair tests per frame.
Scaling to thousands of spheres makes this unusable. A broadphase culling pass is needed
to eliminate distant pairs before any narrow-phase work.

## Approach — Uniform Grid (Spatial Hash)

A uniform grid subdivides the world into axis-aligned cells. Each sphere is inserted into
every cell its AABB overlaps. Candidate pairs are generated only from objects sharing at
least one cell. Pairs are deduplicated so each (i,j) is tested at most once.

**Why uniform grid over octree?**
- All objects are the same type (spheres) with similar radii (1–11 units).
- O(n) insert, O(n + k) pair generation where k = candidate pairs — much faster than O(n²).
- Simple to implement, very cache-friendly, no tree rebalancing.
- Constant cell size avoids pathological subdivision.

**Cell size selection:**
- Cell size = 2 × max_radius = 22 units.
- Guarantees any two colliding spheres share at least one cell.
- Each sphere overlaps at most 2×2×2 = 8 cells.

## Architecture

### New files
| File | Purpose |
|------|---------|
| `SkullbonezSpatialGrid.h` | `SpatialGrid` class declaration |
| `SkullbonezSpatialGrid.cpp` | `SpatialGrid` implementation |

### Modified files
| File | Change |
|------|--------|
| `SkullbonezGameModelCollection.h` | Add `#include`, add `SpatialGrid` member |
| `SkullbonezGameModelCollection.cpp` | Populate grid, replace O(n²) loop with pair iteration |
| `SkullbonezGameModel.h` | Add `GetBoundingRadius()` public method |
| `SkullbonezGameModel.cpp` | Implement `GetBoundingRadius()` |
| `SkullbonezCommon.h` | Add `BROADPHASE_CELL_SIZE` constant |
| `SKULLBONEZ_CORE.vcxproj` | Add new .h/.cpp to project |
| `SKULLBONEZ_CORE.vcxproj.filters` | Add new files to source/header filters |

### SpatialGrid class design
```
class SpatialGrid
{
    float cellSize;
    float inverseCellSize;   // precomputed 1/cellSize for fast division
    std::unordered_map<int64_t, std::vector<int>> cells;

    int64_t HashCell(int ix, int iy, int iz);

public:
    SpatialGrid(float cellSize);
    void Clear();
    void Insert(int index, const Vector3& position, float radius);
    void GetCandidatePairs(std::vector<std::pair<int,int>>& outPairs);
};
```

- `HashCell`: combines (ix, iy, iz) via large prime XOR → int64_t key.
- `Insert`: computes min/max cell coords from AABB, loops 3D range, pushes index.
- `GetCandidatePairs`: iterates cells, for each cell emits (min,max) ordered pairs,
  uses `std::unordered_set<int64_t>` to deduplicate across cells.

### RunPhysics integration
```
// BEFORE (O(n²)):
for x in 0..n-1:
  for y in x+1..n:
    test(x, y)

// AFTER (broadphase):
grid.Clear()
for i in 0..n:
  grid.Insert(i, models[i].GetPosition(), models[i].GetBoundingRadius())
grid.GetCandidatePairs(pairs)
for (x,y) in pairs:
  test(x, y)
```

The body inside the pair loop (timeRemaining check, CollisionDetectGameModel,
CollisionResponseGameModel, StaticOverlapResponseGameModel) remains **unchanged**.

## Tasks

| ID | Task | Depends On | Description |
|----|------|------------|-------------|
| `broadphase-constants` | Add constants | — | Add `BROADPHASE_CELL_SIZE 22.0f` to SkullbonezCommon.h |
| `broadphase-radius` | Expose radius | — | Add `GetBoundingRadius()` to GameModel |
| `broadphase-grid` | Create SpatialGrid | constants | New .h/.cpp with Clear/Insert/GetCandidatePairs |
| `broadphase-integrate` | Integrate into RunPhysics | grid, radius | Replace O(n²) loop, add SpatialGrid member |
| `broadphase-project` | Update vcxproj | grid | Add new files to SKULLBONEZ_CORE.vcxproj and filters |
| `broadphase-build` | Build & verify | all above | 0 errors, 0 warnings, Debug + Release |

## Out of Scope
- Dynamic cell size (could be tuned later based on actual radius distribution)
- Multi-threaded broadphase (future optimisation)
- Terrain collision broadphase (already O(n), not a bottleneck)
- Narrowphase optimisation (sweep test itself is already efficient)
