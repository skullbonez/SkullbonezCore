#pragma once


// --- Includes ---
#include <vector>
#include <utility>
#include <cstdint>
#include <cstring>
#include <cmath>
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
    generation stamping (no per-frame clearing) and a flat index pool.  Pair deduplication via triangular bit array.
    Complexity: O(n + k) where n = objects and k = candidate pairs.  No heap allocations after construction.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SpatialGrid
{

  private:
    static constexpr int TABLE_SIZE = 1024;
    static constexpr int TABLE_MASK = TABLE_SIZE - 1;
    static constexpr int MAX_CELL_ENTRIES = 4096;
    static constexpr int PAIR_WORDS = ( MAX_GAME_MODELS * ( MAX_GAME_MODELS - 1 ) / 2 + 63 ) / 64;

    struct Bucket
    {
        int64_t key;
        uint32_t generation;
        uint16_t start;
        uint16_t count;
    };

    float cellSize;
    float inverseCellSize;
    uint32_t generation;
    int indexPoolUsed;
    int objectCount;

    Bucket buckets[TABLE_SIZE];
    int indexPool[MAX_CELL_ENTRIES];
    uint64_t pairSeen[PAIR_WORDS];

    int FindOrCreate( int64_t key );

  public:
    SpatialGrid( float fCellSize );
    void Clear();
    void Insert( int index, const Vector3& position, float radius );
    void GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs );
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
