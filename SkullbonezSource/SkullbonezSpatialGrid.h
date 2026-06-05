#pragma once


// --- Includes ---
#include <vector>
#include <utility>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cassert>
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"


// --- Usings ---
using namespace SkullbonezCore::Math::Vector;


namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
/* -- Spatial Grid ------------------------------------------------------------------------------------------------------------------------------------------

    Zero-allocation uniform spatial grid for broadphase collision detection.  Uses open-addressing hash table with
    generation stamping (no per-frame clearing) and a flat index pool with linked lists per cell.  Pair deduplication
    via triangular bit array.  Complexity: O(n + k) where n = objects and k = candidate pairs.
    No heap allocations after construction.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SpatialGrid
{

  private:
    // --- Capacity derivation ---
    // Each object of radius R in a grid of cell size C spans at most ceil(2R/C + 1) cells
    // per axis. With max ball radius = 5.5 and cell size = 24.0, diameter/cell = 0.46 so
    // an object spans at most 2 cells per axis → 2×2×2 = 8 cells worst case (on boundary).
    //
    // MAX_CELL_ENTRIES: total linked-list pool entries across all cells.
    //   = MAX_GAME_MODELS × 8 (worst case: every object straddles all 3 axis boundaries)
    //   = 512 × 8 = 4096, but +1 because the insert check is strict-less-than (<).
    //   Round up to 4100 for a small safety margin against edge rounding.
    //
    // TABLE_SIZE: open-addressing hash table slots. Load factor must stay well below 1.0
    //   to keep linear-probe chains short. Worst-case distinct cells = MAX_CELL_ENTRIES = 4096.
    //   At 75% max load: 4096 / 0.75 = 5461 → next power-of-2 = 8192.
    //   However, spatial locality means most cells are shared so actual fill ≪ 4096.
    //   Use 2048 (50% load for realistic worst case of ~1000 distinct cells with 512 models)
    //   which keeps the struct ≤ 200KB while handling 512 models cleanly.
    static constexpr int TABLE_SIZE = 4096;
    static constexpr int TABLE_MASK = TABLE_SIZE - 1;
    static constexpr int MAX_CELL_ENTRIES = MAX_GAME_MODELS * 8 + 4;
    static constexpr int PAIR_WORDS = ( MAX_GAME_MODELS * ( MAX_GAME_MODELS - 1 ) / 2 + 63 ) / 64;

    struct Entry
    {
        int objectIndex;
        int next; // index into entries[], -1 = end of list
    };

    struct Bucket
    {
        int64_t key;
        uint32_t generation;
        int head; // index into entries[], -1 = empty
        int count;
        int16_t ix, iy, iz; // Cell grid coordinates (stored for visualization)
    };

    float cellSize;
    float inverseCellSize;
    uint32_t generation;
    int entryPoolUsed;
    int objectCount;
    int activeBucketCount;

    Bucket buckets[TABLE_SIZE];
    int activeBuckets[TABLE_SIZE];
    Entry entries[MAX_CELL_ENTRIES];
    uint64_t pairSeen[PAIR_WORDS];

    int FindOrCreate( int64_t key, int16_t cx, int16_t cy, int16_t cz );

  public:
    static constexpr int MAX_BUCKETS = TABLE_SIZE;

    struct ActiveCell
    {
        int16_t ix, iy, iz;
        int objectCount;
    };

    SpatialGrid( float fCellSize );
    void Clear();
    void Insert( int index, const Vector3& position, float radius );
    void GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs );
    float GetCellSize() const
    {
        return cellSize;
    }
    int GetActiveCellCount() const
    {
        return activeBucketCount;
    }
    void GetActiveCells( ActiveCell* outCells, int maxCells ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
