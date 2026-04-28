#pragma once


// --- Includes ---
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include <cstdint>
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

    Uniform spatial grid for broadphase collision detection.  Subdivides world space into fixed-size cells and generates
    candidate collision pairs from objects sharing cells.  Reduces pair testing from O(n^2) to O(n + k).
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SpatialGrid
{

  private:
    float cellSize;                                      // Width of each grid cell
    float inverseCellSize;                               // Precomputed 1/cellSize for fast floor division
    std::unordered_map<int64_t, std::vector<int>> cells; // Maps hashed cell key to list of model indices

    int64_t HashCell( int ix, int iy, int iz ); // Combines cell coords into a single hash key

  public:
    SpatialGrid( float fCellSize );                                       // Constructor
    void Clear();                                                         // Clears all cell contents
    void Insert( int index, const Vector3& position, float radius );      // Inserts an object into all overlapping cells
    void GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs ); // Generates unique candidate collision pairs
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
