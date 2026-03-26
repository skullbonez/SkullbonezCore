# Broadphase Collision Detection — Progress

## Task 1: broadphase-constants — Add Constants
**Status:** Pending

### Step 1.1 — Add BROADPHASE_CELL_SIZE to SkullbonezCommon.h
- Open `SkullbonezSource/SkullbonezCommon.h`
- Locate the physics constants block (after `GRAVITATIONAL_FORCE` around line 121)
- Add: `#define BROADPHASE_CELL_SIZE 22.0f  // 2 * max sphere radius (11)`
- This value guarantees any two colliding spheres share at least one grid cell

---

## Task 2: broadphase-radius — Expose Bounding Radius
**Status:** Pending

### Step 2.1 — Declare GetBoundingRadius in GameModel header
- Open `SkullbonezSource/SkullbonezGameModel.h`
- In the public section (after `AddBoundingSphere` on line 116), add:
  ```cpp
  float GetBoundingRadius(void);
  ```
- This provides a clean public API so the spatial grid doesn't need to know about BoundingSphere internals

### Step 2.2 — Implement GetBoundingRadius in GameModel cpp
- Open `SkullbonezSource/SkullbonezGameModel.cpp`
- Add new method (after AddBoundingSphere implementation):
  ```cpp
  float GameModel::GetBoundingRadius(void)
  {
      auto* sphere = dynamic_cast<BoundingSphere*>(this->boundingVolume.get());
      if(!sphere) return 0.0f;
      return sphere->GetRadius();
  }
  ```
- Uses the same dynamic_cast pattern already used in StaticOverlapResponseGameModel and GetTerrainCollisionTime

---

## Task 3: broadphase-grid — Create SpatialGrid Class
**Status:** Pending

### Step 3.1 — Create SkullbonezSpatialGrid.h
- Create new file `SkullbonezSource/SkullbonezSpatialGrid.h`
- Include guards: `SKULLBONEZ_SPATIAL_GRID_H`
- Includes: `<unordered_map>`, `<unordered_set>`, `<vector>`, `<utility>`, `<cstdint>`, `"SkullbonezCommon.h"`, `"SkullbonezVector3.h"`
- Namespace: `SkullbonezCore::Math::CollisionDetection`
- Class `SpatialGrid` with:
  - Private members:
    - `float cellSize` — width of each grid cell
    - `float inverseCellSize` — precomputed `1.0f / cellSize` for fast floor division
    - `std::unordered_map<int64_t, std::vector<int>> cells` — maps hashed cell key to list of model indices
  - Private method:
    - `int64_t HashCell(int ix, int iy, int iz)` — combines cell coords via large prime XOR
  - Public methods:
    - `SpatialGrid(float cellSize)` — constructor, stores cellSize and computes inverse
    - `void Clear()` — clears all cell contents (retains bucket allocation via `.clear()` on each vector, then clears the map)
    - `void Insert(int index, const Vector3& position, float radius)` — computes AABB min/max cell coords, inserts index into all overlapping cells
    - `void GetCandidatePairs(std::vector<std::pair<int,int>>& outPairs)` — iterates all non-empty cells, generates (min,max)-ordered pairs within each cell, deduplicates across cells

### Step 3.2 — Create SkullbonezSpatialGrid.cpp
- Create new file `SkullbonezSource/SkullbonezSpatialGrid.cpp`
- Include `"SkullbonezSpatialGrid.h"`
- Using clause: `using namespace SkullbonezCore::Math::CollisionDetection;`

#### HashCell implementation:
```cpp
int64_t SpatialGrid::HashCell(int ix, int iy, int iz)
{
    // Large prime XOR hash — distributes 3D cell coords into flat hash space
    int64_t h = (int64_t(ix) * 73856093) ^ (int64_t(iy) * 19349663) ^ (int64_t(iz) * 83492791);
    return h;
}
```

#### Constructor implementation:
```cpp
SpatialGrid::SpatialGrid(float cellSize)
    : cellSize(cellSize), inverseCellSize(1.0f / cellSize) {}
```

#### Clear implementation:
```cpp
void SpatialGrid::Clear()
{
    for(auto& pair : cells)
        pair.second.clear();
    cells.clear();
}
```

#### Insert implementation:
```cpp
void SpatialGrid::Insert(int index, const Vector3& position, float radius)
{
    // Compute AABB of sphere, convert to cell coordinate range
    int minX = (int)floorf((position.x - radius) * inverseCellSize);
    int minY = (int)floorf((position.y - radius) * inverseCellSize);
    int minZ = (int)floorf((position.z - radius) * inverseCellSize);
    int maxX = (int)floorf((position.x + radius) * inverseCellSize);
    int maxY = (int)floorf((position.y + radius) * inverseCellSize);
    int maxZ = (int)floorf((position.z + radius) * inverseCellSize);

    for(int ix = minX; ix <= maxX; ++ix)
        for(int iy = minY; iy <= maxY; ++iy)
            for(int iz = minZ; iz <= maxZ; ++iz)
                cells[HashCell(ix, iy, iz)].push_back(index);
}
```
- Uses `floorf` to handle negative coordinates correctly
- Each sphere occupies at most 2×2×2 = 8 cells when straddling cell boundaries

#### GetCandidatePairs implementation:
```cpp
void SpatialGrid::GetCandidatePairs(std::vector<std::pair<int,int>>& outPairs)
{
    outPairs.clear();
    std::unordered_set<int64_t> seen;

    for(const auto& cell : cells)
    {
        const std::vector<int>& indices = cell.second;
        for(int i = 0; i < (int)indices.size() - 1; ++i)
        {
            for(int j = i + 1; j < (int)indices.size(); ++j)
            {
                int a = (std::min)(indices[i], indices[j]);
                int b = (std::max)(indices[i], indices[j]);
                int64_t key = (int64_t(a) << 32) | int64_t(b);

                if(seen.insert(key).second)
                    outPairs.push_back(std::make_pair(a, b));
            }
        }
    }
}
```
- Orders each pair as (min,max) for consistent deduplication
- Packs two 32-bit indices into one 64-bit key for the hash set
- `seen.insert().second` returns true only on first insertion — single lookup per pair

---

## Task 4: broadphase-integrate — Integrate into RunPhysics
**Status:** Pending

### Step 4.1 — Add SpatialGrid include and member to GameModelCollection header
- Open `SkullbonezSource/SkullbonezGameModelCollection.h`
- Add `#include "SkullbonezSpatialGrid.h"` after existing includes
- Add using clause: `using namespace SkullbonezCore::Math::CollisionDetection;`
- Add private member to class:
  ```cpp
  SpatialGrid spatialGrid;  // Broadphase spatial grid for collision culling
  ```

### Step 4.2 — Initialise SpatialGrid in constructor
- Open `SkullbonezSource/SkullbonezGameModelCollection.cpp`
- Modify the constructor to initialise `spatialGrid` with `BROADPHASE_CELL_SIZE`:
  ```cpp
  GameModelCollection::GameModelCollection(void) : spatialGrid(BROADPHASE_CELL_SIZE)
  {
      this->gameModels.reserve(200);
  };
  ```

### Step 4.3 — Add candidate pairs vector as local in RunPhysics
- In `RunPhysics`, after the `ApplyForces` loop (line 84), add:
  ```cpp
  // broadphase: populate spatial grid and generate candidate pairs
  this->spatialGrid.Clear();
  for(int i = 0; i < (int)this->gameModels.size(); ++i)
      this->spatialGrid.Insert(i, this->gameModels[i].GetPosition(),
                                   this->gameModels[i].GetBoundingRadius());

  std::vector<std::pair<int,int>> candidatePairs;
  this->spatialGrid.GetCandidatePairs(candidatePairs);
  ```

### Step 4.4 — Replace O(n²) nested loop with broadphase pair iteration
- Remove the existing nested for-loop (lines 87-121):
  ```cpp
  for(int x=0; x<(int)this->gameModels.size()-1; ++x)
  {
      for(int y=x+1; y<(int)this->gameModels.size(); ++y)
      { ... }
  }
  ```
- Replace with iteration over candidate pairs:
  ```cpp
  for(const auto& cp : candidatePairs)
  {
      int x = cp.first;
      int y = cp.second;

      // skip pairs where either ball has exhausted its frame time
      if(timeRemaining[x] <= 0.0f || timeRemaining[y] <= 0.0f) continue;

      // use the minimum remaining time window for this pair
      float availableTime = (std::min)(timeRemaining[x], timeRemaining[y]);

      // check the collision time
      float colTime = this->gameModels[x].CollisionDetectGameModel(this->gameModels[y], availableTime);

      // if there is a response required, perform it
      if(this->gameModels[x].IsResponseRequired() && this->gameModels[y].IsResponseRequired())
      {
          // advance both models to the collision point
          this->gameModels[x].UpdatePosition(colTime);
          this->gameModels[y].UpdatePosition(colTime);

          // subtract consumed time
          timeRemaining[x] -= colTime;
          timeRemaining[y] -= colTime;

          // velocity-only response (clears isResponseRequired on both models)
          this->gameModels[x].CollisionResponseGameModel(this->gameModels[y]);
      }
      else
      {
          // sweep test found no collision — check for static overlap
          this->gameModels[x].StaticOverlapResponseGameModel(this->gameModels[y]);
      }
  }
  ```
- The inner body is **identical** to the existing code — only the pair source changes
- Terrain collision loop and final position advancement remain unchanged

---

## Task 5: broadphase-project — Update vcxproj
**Status:** Pending

### Step 5.1 — Add new source file to SKULLBONEZ_CORE.vcxproj
- Open `SKULLBONEZ_CORE.vcxproj`
- Locate the `<ItemGroup>` containing `<ClCompile>` entries
- Add: `<ClCompile Include="SkullbonezSource\SkullbonezSpatialGrid.cpp" />`

### Step 5.2 — Add new header file to SKULLBONEZ_CORE.vcxproj
- Locate the `<ItemGroup>` containing `<ClInclude>` entries
- Add: `<ClInclude Include="SkullbonezSource\SkullbonezSpatialGrid.h" />`

### Step 5.3 — Add new files to SKULLBONEZ_CORE.vcxproj.filters
- Locate the `<ItemGroup>` containing `<ClCompile>` entries with `<Filter>` children
- Add entry for `SkullbonezSpatialGrid.cpp` in the `Source Files` filter
- Locate the `<ItemGroup>` containing `<ClInclude>` entries with `<Filter>` children
- Add entry for `SkullbonezSpatialGrid.h` in the `Header Files` filter

---

## Task 6: broadphase-build — Build & Verify
**Status:** Pending

### Step 6.1 — Build Debug configuration
- Run MSBuild with `Configuration=Debug`, `Platform=x86`
- Expect: 0 errors, 0 warnings

### Step 6.2 — Build Release configuration
- Run MSBuild with `Configuration=Release`, `Platform=x86`
- Expect: 0 errors, 0 warnings

### Step 6.3 — Verify pair count reduction
- Optionally add temporary debug output in RunPhysics to log `candidatePairs.size()` vs `n*(n-1)/2`
- With 500 spheres spread across 400 units of space and 22-unit cells, expect a dramatic reduction
- Remove debug output after verification
