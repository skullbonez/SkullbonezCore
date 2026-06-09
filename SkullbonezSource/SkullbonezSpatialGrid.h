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
    // Static objects of radius R in a grid of cell size C span at most
    // ceil(2R/C + 1) cells per axis. With the default 24-unit cells, normal
    // scene bodies are expected to use at most 2x2x2 = 8 cells.
    //
    // Fast dynamic bodies can insert their swept AABB for CCD pairing. That is
    // intentionally a limited escape hatch for bullets and other rare high-speed
    // movers, not a promise that every body can sweep across the whole world in
    // one tick. Large projectile clouds should use a dedicated ray/query path.
    static constexpr int TABLE_SIZE = 4096;
    static constexpr int TABLE_MASK = TABLE_SIZE - 1;
    static constexpr int MAX_STATIC_CELL_ENTRIES = MAX_GAME_MODELS * 8;
    static constexpr int MAX_SWEPT_CELL_ENTRIES = 4096;
    static constexpr int MAX_CELL_ENTRIES = MAX_STATIC_CELL_ENTRIES + MAX_SWEPT_CELL_ENTRIES + 4;
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
    void InsertBounds( int index, const Vector3& minBounds, const Vector3& maxBounds );

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
    void InsertSwept( int index, const Vector3& position, const Vector3& displacement, float radius );
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
