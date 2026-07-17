/*
File: SkullbonezSource/Physics/SpatialGrid.cpp
Purpose:
  Partitions space into broadphase cells so physics can test nearby objects cheaply.

Summary:
  SpatialGrid.cpp partitions space into broadphase cells so physics can test
  nearby objects cheaply. As an implementation unit, keep edits anchored on
  deterministic physics, diagnostics, or world-state flow and on the
  glossary/invariants below.

Glossary:
  AABB (Axis-Aligned Bounding Box): Box aligned to world axes, often used for
  cheap broadphase overlap tests.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  Lane F: Fatal invariant lane for should-never-happen engine state.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - SpatialGrid capacity and index failures are Lane F invariants: callers
    cannot recover while preserving the frame's broadphase contract.

Related:
  - SkullbonezSource/Physics/SpatialGrid.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
// Concept: the grid is a cheap maybe-colliding filter, not a collision solver.
//
// Each occupied cell emits every pair inside the cell, so a cell that is too
// large turns into a local O(n^2) pair factory. PhysicsWorld sizes the cell from
// current scene primitives before insertion; this type only owns hashing,
// storage, and duplicate-pair suppression.


#include "SpatialGrid.h"
#include "SolverBroadphaseStage.h"
#include "Stages/Kernels/NarrowphasePruneKernel.h"
#include "../Core/FatalError.h"
#include <algorithm>
#include <cfloat>
#include <climits>


using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Vector;

namespace
{
void ValidateBroadphaseBounds( int index, const Vector3& minBounds, const Vector3& maxBounds, float inverseCellSize )
{
    const bool finite = std::isfinite( minBounds.x ) && std::isfinite( minBounds.y ) && std::isfinite( minBounds.z ) &&
                        std::isfinite( maxBounds.x ) && std::isfinite( maxBounds.y ) && std::isfinite( maxBounds.z );
    const float extent = SpatialGrid::MAX_WORLD_COORDINATE;
    const bool insideExtent = finite && fabsf( minBounds.x ) <= extent && fabsf( minBounds.y ) <= extent &&
                              fabsf( minBounds.z ) <= extent && fabsf( maxBounds.x ) <= extent &&
                              fabsf( maxBounds.y ) <= extent && fabsf( maxBounds.z ) <= extent;
    const bool ordered =
        finite && minBounds.x <= maxBounds.x && minBounds.y <= maxBounds.y && minBounds.z <= maxBounds.z;
    constexpr double MAX_CONVERTIBLE_CELL_COORDINATE = static_cast<double>( INT_MAX ) - 1024.0;
    const auto cellCoordinateIsRepresentable = [&]( float value )
    {
        // Why: perform the guard in double. Converting INT_MAX-1 to float rounds
        // up to 2^31 on MSVC, which would bless the very value the later int
        // conversion cannot represent.
        return std::fabs( static_cast<double>( value ) * static_cast<double>( inverseCellSize ) ) <=
               MAX_CONVERTIBLE_CELL_COORDINATE;
    };
    const bool convertible =
        insideExtent && cellCoordinateIsRepresentable( minBounds.x ) && cellCoordinateIsRepresentable( minBounds.y ) &&
        cellCoordinateIsRepresentable( minBounds.z ) && cellCoordinateIsRepresentable( maxBounds.x ) &&
        cellCoordinateIsRepresentable( maxBounds.y ) && cellCoordinateIsRepresentable( maxBounds.z );
    if ( !insideExtent || !ordered || !convertible )
    {
        // Lane F: non-finite, inverted, or unrepresentable physics bounds are
        // corrupt engine state. Continuing would either hide the body from
        // broadphase or invoke undefined float-to-int conversion behavior.
        SB_FATAL( "Physics/SpatialGrid",
                  "SpatialGrid bounds invalid: body=%d min=(%.9g,%.9g,%.9g) max=(%.9g,%.9g,%.9g) "
                  "max_world_coordinate=%.9g.",
                  index,
                  minBounds.x,
                  minBounds.y,
                  minBounds.z,
                  maxBounds.x,
                  maxBounds.y,
                  maxBounds.z,
                  extent );
    }
}

int16_t ClampVisualizationCell( int cell )
{
    // The hash key retains the full cell coordinate. Only the debug
    // visualization payload is narrowed, so clamp instead of wrapping it.
    return static_cast<int16_t>(
        (std::max)( static_cast<int>( INT16_MIN ), (std::min)( static_cast<int>( INT16_MAX ), cell ) ) );
}
} // namespace


SpatialGrid::SpatialGrid( float fCellSize )
    : cellSize( 1.0f ), inverseCellSize( 1.0f ), generation( 0 ), entryPoolUsed( 0 ), objectCount( 0 ),
      activeBucketCount( 0 ), overflowBucketCount( 0 ), fullBucketLookupReady( false ),
      overflowStorage( std::make_unique<OverflowStorage>() )
{
    memset( buckets, 0, sizeof( buckets ) );
    SetCellSize( fCellSize );
}


SpatialGrid::SpatialGrid( const SpatialGrid& other ) : SpatialGrid( other.cellSize )
{
    *this = other;
}


SpatialGrid& SpatialGrid::operator=( const SpatialGrid& other )
{
    if ( this == &other )
    {
        return *this;
    }

    // Lifetime: each copy retains its own startup-owned cold block. Replay
    // assignment copies bytes into that existing block and never allocates.
    cellSize = other.cellSize;
    inverseCellSize = other.inverseCellSize;
    generation = other.generation;
    entryPoolUsed = other.entryPoolUsed;
    objectCount = other.objectCount;
    activeBucketCount = other.activeBucketCount;
    frameStats = other.frameStats;
    memcpy( buckets, other.buckets, sizeof( buckets ) );
    memcpy( activeBuckets, other.activeBuckets, sizeof( activeBuckets ) );
    memcpy( entries, other.entries, sizeof( entries ) );
    memcpy( pairSeen, other.pairSeen, sizeof( pairSeen ) );
    overflowBucketCount = other.overflowBucketCount;
    fullBucketLookupReady = other.fullBucketLookupReady;
    *overflowStorage = *other.overflowStorage;
    return *this;
}


void SpatialGrid::SetCellSize( float fCellSize )
{
    if ( fCellSize < MIN_CELL_SIZE || !std::isfinite( fCellSize ) )
    {
        // Lane F: an invalid cell size makes every subsequent float-to-cell
        // conversion unsafe; construction and runtime reconfiguration share
        // this owner boundary.
        SB_FATAL( "Physics/SpatialGrid",
                  "SpatialGrid cell size invalid: value=%.9g minimum=%.9g.",
                  fCellSize,
                  MIN_CELL_SIZE );
    }

    cellSize = fCellSize;
    inverseCellSize = 1.0f / fCellSize;
}


// Advance generation counter — all existing buckets become "stale" without
// needing to zero them. O(1) clear instead of O(TABLE_SIZE).
void SpatialGrid::Clear()
{
    ++generation;
    entryPoolUsed = 0;
    objectCount = 0;
    activeBucketCount = 0;
    overflowBucketCount = 0;
    fullBucketLookupReady = false;
    frameStats = FrameStatsStorage{};
}


// Look up or create a bucket for the given hash key.
// Uses LINEAR PROBING: if the target slot is occupied by a different key,
// try the next slot, then the next, etc.
// Output is the bucket index for this key.
int SpatialGrid::FindOrCreate( int64_t key, int16_t cx, int16_t cy, int16_t cz )
{
    if ( fullBucketLookupReady )
    {
        // Why: readiness already implies primary saturation. Testing the flag
        // directly keeps the unsaturated per-cell route to one predictable
        // branch while the bounded overflow tier owns all indexed lookups.
        return FindOrCreateFullBucket( key, cx, cy, cz );
    }

    int idx = static_cast<int>( static_cast<uint64_t>( key ) & TABLE_MASK );

    for ( int probe = 0; probe < TABLE_SIZE; ++probe )
    {
        Bucket& b = buckets[idx];

        if ( b.generation != generation )
        {
            b.key = key;
            b.generation = generation;
            b.head = -1;
            b.count = 0;
            b.ix = cx;
            b.iy = cy;
            b.iz = cz;
            assert( activeBucketCount < TABLE_SIZE && "activeBuckets overflow" );
            activeBuckets[activeBucketCount++] = idx;
            return idx;
        }

        if ( b.key == key )
        {
            return idx;
        }

        idx = ( idx + 1 ) & TABLE_MASK;
    }

    if ( activeBucketCount == TABLE_SIZE )
    {
        // Why: filling the primary table is common at 1,000 bodies, but that
        // scene only revisits admitted keys. Delay the 32 KiB rebuild until a
        // probe proves that a genuinely new cell needs the overflow tier.
        BuildFullBucketLookup();
        return FindOrCreateFullBucket( key, cx, cy, cz );
    }

    SB_FATAL( "Physics/SpatialGrid", "SpatialGrid primary lookup exhausted before reaching its admission capacity" );
}


void SpatialGrid::InsertCell( int index, int ix, int iy, int iz )
{
    // The same object can overlap a cell through multiple sampled bounds during
    // a swept insert. Before appending, scan this bucket's linked list so one
    // object contributes at most once to a cell's candidate-pair list.
    const int64_t key = ( int64_t( ix ) * 73856093 ) ^ ( int64_t( iy ) * 19349663 ) ^ ( int64_t( iz ) * 83492791 );
    const int bi =
        FindOrCreate( key, ClampVisualizationCell( ix ), ClampVisualizationCell( iy ), ClampVisualizationCell( iz ) );
    // Why: one unsigned range check preserves the legacy primary-cell branch
    // shape. Negative and overflow results both enter the cold validation path.
    if ( static_cast<unsigned int>( bi ) >= static_cast<unsigned int>( TABLE_SIZE ) )
    {
        if ( bi < 0 || bi >= TOTAL_BUCKET_COUNT )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid bucket index out of bounds" );
        }
        InsertOverflowCellEntry( index, bi - TABLE_SIZE );
        return;
    }

    Bucket& b = buckets[bi];
    for ( int cur = b.head; cur != -1; cur = entries[cur].next )
    {
        ++frameStats.bucketChainEntriesInspected;
        assert( cur >= 0 && cur < MAX_CELL_ENTRIES && "entry chain index OOB" );
        if ( cur < 0 || cur >= MAX_CELL_ENTRIES )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid entry chain index out of bounds" );
        }
        if ( entries[cur].objectIndex == index )
        {
            ++frameStats.duplicateRejections;
            return;
        }
    }

    if ( entryPoolUsed >= MAX_CELL_ENTRIES )
    {
        assert( false && "SpatialGrid cell entry capacity exceeded" );
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid cell entry capacity exceeded" );
    }

    entries[entryPoolUsed].objectIndex = index;
    entries[entryPoolUsed].next = b.head;
    b.head = entryPoolUsed;
    ++entryPoolUsed;
    ++b.count;
    ++frameStats.entryWrites;
    frameStats.maxBucketOccupancy = (std::max)( frameStats.maxBucketOccupancy, b.count );
}


void SpatialGrid::BuildFullBucketLookup()
{
    // Invariant: at most 8,192 admitted buckets occupy a 16,384-slot index,
    // so linear probing always retains an empty sentinel for a missing key.
    std::fill_n( overflowStorage->lookup, FULL_LOOKUP_SLOT_COUNT, FULL_LOOKUP_EMPTY );
    for ( int active = 0; active < activeBucketCount; ++active )
    {
        const int bucketIndex = activeBuckets[active];
        int slot = static_cast<int>( static_cast<uint64_t>( buckets[bucketIndex].key ) & FULL_LOOKUP_SLOT_MASK );
        while ( overflowStorage->lookup[slot] != FULL_LOOKUP_EMPTY )
        {
            slot = ( slot + 1 ) & FULL_LOOKUP_SLOT_MASK;
        }
        overflowStorage->lookup[slot] = static_cast<uint16_t>( bucketIndex );
    }
    fullBucketLookupReady = true;
}


SpatialGrid::Bucket& SpatialGrid::BucketAt( int bucketIndex )
{
    return bucketIndex < TABLE_SIZE ? buckets[bucketIndex] : overflowStorage->buckets[bucketIndex - TABLE_SIZE];
}


const SpatialGrid::Bucket& SpatialGrid::BucketAt( int bucketIndex ) const
{
    return bucketIndex < TABLE_SIZE ? buckets[bucketIndex] : overflowStorage->buckets[bucketIndex - TABLE_SIZE];
}


int SpatialGrid::FindOrCreateFullBucket( int64_t key, int16_t cx, int16_t cy, int16_t cz )
{
    assert( fullBucketLookupReady && "saturated SpatialGrid lookup index missing" );
    int slot = static_cast<int>( static_cast<uint64_t>( key ) & FULL_LOOKUP_SLOT_MASK );
    for ( int probe = 0; probe < FULL_LOOKUP_SLOT_COUNT; ++probe )
    {
        const uint16_t compactBucketIndex = overflowStorage->lookup[slot];
        if ( compactBucketIndex == FULL_LOOKUP_EMPTY )
        {
            if ( overflowBucketCount >= OVERFLOW_BUCKET_COUNT )
            {
                // Lane F: dropping a cell could hide a real collision. The
                // fixed cold tier is the complete runtime admission budget.
                SB_FATAL( "Physics/SpatialGrid",
                          "SpatialGrid bucket capacity exceeded: capacity=%d active=%d primary=%d overflow=%d "
                          "phase=steady_gameplay.",
                          TOTAL_BUCKET_COUNT,
                          activeBucketCount,
                          TABLE_SIZE,
                          OVERFLOW_BUCKET_COUNT );
            }

            const int bucketIndex = TABLE_SIZE + overflowBucketCount++;
            Bucket& bucket = BucketAt( bucketIndex );
            bucket.key = key;
            bucket.generation = generation;
            bucket.head = -1;
            bucket.count = 0;
            bucket.ix = cx;
            bucket.iy = cy;
            bucket.iz = cz;
            overflowStorage->lookup[slot] = static_cast<uint16_t>( bucketIndex );
            ++activeBucketCount;
            return bucketIndex;
        }
        const int bucketIndex = static_cast<int>( compactBucketIndex );
        if ( BucketAt( bucketIndex ).key == key )
        {
            return bucketIndex;
        }
        slot = ( slot + 1 ) & FULL_LOOKUP_SLOT_MASK;
    }

    // Lane F: the table is capped at 50% load, so exhausting every probe is
    // impossible unless the lookup metadata was corrupted.
    SB_FATAL( "Physics/SpatialGrid", "SpatialGrid saturated lookup exhausted all probe slots" );
}


void SpatialGrid::InsertOverflowCellEntry( int index, int overflowIndex )
{
    // Why: overflow insertion is deliberately out of line so the common
    // primary path retains direct buckets[] access and its original branches.
    Bucket& b = overflowStorage->buckets[overflowIndex];
    for ( int cur = b.head; cur != -1; cur = entries[cur].next )
    {
        ++frameStats.bucketChainEntriesInspected;
        assert( cur >= 0 && cur < MAX_CELL_ENTRIES && "entry chain index OOB" );
        if ( cur < 0 || cur >= MAX_CELL_ENTRIES )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid overflow entry chain index out of bounds" );
        }
        if ( entries[cur].objectIndex == index )
        {
            ++frameStats.duplicateRejections;
            return;
        }
    }

    if ( entryPoolUsed >= MAX_CELL_ENTRIES )
    {
        assert( false && "SpatialGrid cell entry capacity exceeded" );
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid cell entry capacity exceeded" );
    }

    entries[entryPoolUsed].objectIndex = index;
    entries[entryPoolUsed].next = b.head;
    b.head = entryPoolUsed;
    ++entryPoolUsed;
    ++b.count;
    ++frameStats.entryWrites;
    frameStats.maxBucketOccupancy = (std::max)( frameStats.maxBucketOccupancy, b.count );
}


// Insert an object into all grid cells touched by an explicit AABB.
// Bounds are inclusive after conversion to grid coordinates.
void SpatialGrid::InsertBounds( int index, const Vector3& minBounds, const Vector3& maxBounds, bool sampledSweep )
{
    assert( index >= 0 && "Insert: negative object index" );
    if ( index < 0 || index >= SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid object index out of bounds" );
    }

    ValidateBroadphaseBounds( index, minBounds, maxBounds, inverseCellSize );

    if ( index >= objectCount )
    {
        objectCount = index + 1;
    }

    int minX = static_cast<int>( floorf( minBounds.x * inverseCellSize ) );
    int minY = static_cast<int>( floorf( minBounds.y * inverseCellSize ) );
    int minZ = static_cast<int>( floorf( minBounds.z * inverseCellSize ) );
    int maxX = static_cast<int>( floorf( maxBounds.x * inverseCellSize ) );
    int maxY = static_cast<int>( floorf( maxBounds.y * inverseCellSize ) );
    int maxZ = static_cast<int>( floorf( maxBounds.z * inverseCellSize ) );

    const int64_t cellCountX = int64_t( maxX ) - int64_t( minX ) + 1;
    const int64_t cellCountY = int64_t( maxY ) - int64_t( minY ) + 1;
    const int64_t cellCountZ = int64_t( maxZ ) - int64_t( minZ ) + 1;
    if ( cellCountX > MAX_CELL_ENTRIES || cellCountY > MAX_CELL_ENTRIES || cellCountZ > MAX_CELL_ENTRIES ||
         cellCountX * cellCountY > MAX_CELL_ENTRIES || cellCountX * cellCountY * cellCountZ > MAX_CELL_ENTRIES )
    {
        // Lane F: InsertBounds is the exact-AABB path. A span larger than the
        // fixed entry pool cannot succeed, so reject it before entering a
        // potentially enormous cell loop.
        SB_FATAL( "Physics/SpatialGrid",
                  "SpatialGrid bounds exceed cell-entry capacity: body=%d cells=(%lld,%lld,%lld) capacity=%d.",
                  index,
                  static_cast<long long>( cellCountX ),
                  static_cast<long long>( cellCountY ),
                  static_cast<long long>( cellCountZ ),
                  MAX_CELL_ENTRIES );
    }

    for ( int ix = minX; ix <= maxX; ++ix )
    {
        for ( int iy = minY; iy <= maxY; ++iy )
        {
            for ( int iz = minZ; iz <= maxZ; ++iz )
            {
                if ( sampledSweep )
                {
                    ++frameStats.sampledSweepCellVisits;
                }
                else
                {
                    ++frameStats.exactAabbCellVisits;
                }
                InsertCell( index, ix, iy, iz );
            }
        }
    }
}


// Insert an object into all grid cells it overlaps at its current position.
// A sphere at position P with radius R overlaps cells from:
//   min = floor((P - R) / cellSize)  to  max = floor((P + R) / cellSize)
void SpatialGrid::Insert( int index, const Vector3& position, float radius )
{
    ++frameStats.bodyInsertions;
    InsertBounds( index,
                  Vector3( position.x - radius, position.y - radius, position.z - radius ),
                  Vector3( position.x + radius, position.y + radius, position.z + radius ),
                  false );
}


// Insert a dynamic body over the world-space AABB swept by its center this tick.
// Narrowphase still computes the exact time-of-impact; this only prevents the
// broadphase from skipping a fast body that starts outside the target cell.
void SpatialGrid::InsertSwept( int index, const Vector3& position, const Vector3& displacement, float radius )
{
    ++frameStats.bodyInsertions;
    InsertSweptBounds( index, position, displacement, radius );
}


void SpatialGrid::InsertSweptBounds( int index, const Vector3& position, const Vector3& displacement, float radius )
{
    const Vector3 endPosition = position + displacement;
    const Vector3 minBounds( (std::min)( position.x, endPosition.x ) - radius,
                             (std::min)( position.y, endPosition.y ) - radius,
                             (std::min)( position.z, endPosition.z ) - radius );
    const Vector3 maxBounds( (std::max)( position.x, endPosition.x ) + radius,
                             (std::max)( position.y, endPosition.y ) + radius,
                             (std::max)( position.z, endPosition.z ) + radius );

    // InsertSwept performs its own cell-count conversion before delegating to
    // InsertBounds, so it must enforce the same Lane F boundary first.
    ValidateBroadphaseBounds( index, minBounds, maxBounds, inverseCellSize );

    const int minX = static_cast<int>( floorf( minBounds.x * inverseCellSize ) );
    const int minY = static_cast<int>( floorf( minBounds.y * inverseCellSize ) );
    const int minZ = static_cast<int>( floorf( minBounds.z * inverseCellSize ) );
    const int maxX = static_cast<int>( floorf( maxBounds.x * inverseCellSize ) );
    const int maxY = static_cast<int>( floorf( maxBounds.y * inverseCellSize ) );
    const int maxZ = static_cast<int>( floorf( maxBounds.z * inverseCellSize ) );
    const int64_t cellCountX = int64_t( maxX ) - int64_t( minX ) + 1;
    const int64_t cellCountY = int64_t( maxY ) - int64_t( minY ) + 1;
    const int64_t cellCountZ = int64_t( maxZ ) - int64_t( minZ ) + 1;
    const bool exactAabbFits = cellCountX <= MAX_SWEPT_AABB_CELLS && cellCountY <= MAX_SWEPT_AABB_CELLS &&
                               cellCountZ <= MAX_SWEPT_AABB_CELLS &&
                               cellCountX * cellCountY * cellCountZ <= MAX_SWEPT_AABB_CELLS;

    if ( exactAabbFits )
    {
        // For normal fast movers, the swept bounding box is still small enough
        // to insert exactly. That covers every cell touched between start and end.
        InsertBounds( index, minBounds, maxBounds, false );
        return;
    }

    const float distanceSq = displacement * displacement;
    if ( distanceSq <= TOLERANCE )
    {
        InsertBounds( index,
                      Vector3( position.x - radius, position.y - radius, position.z - radius ),
                      Vector3( position.x + radius, position.y + radius, position.z + radius ),
                      false );
        return;
    }

    auto cellFor = [&]( float value ) -> int { return static_cast<int>( floorf( value * inverseCellSize ) ); };

    int cx = cellFor( position.x );
    int cy = cellFor( position.y );
    int cz = cellFor( position.z );
    const int endX = cellFor( endPosition.x );
    const int endY = cellFor( endPosition.y );
    const int endZ = cellFor( endPosition.z );

    auto axisTraversal = [&]( float start, float delta, int cell, int& outStep, float& outTMax, float& outTDelta )
    {
        if ( fabsf( delta ) <= TOLERANCE )
        {
            outStep = 0;
            outTMax = FLT_MAX;
            outTDelta = FLT_MAX;
            return;
        }

        if ( delta > 0.0f )
        {
            outStep = 1;
            const float nextBoundary = static_cast<float>( cell + 1 ) * cellSize;
            outTMax = ( nextBoundary - start ) / delta;
            outTDelta = cellSize / delta;
        }
        else
        {
            outStep = -1;
            const float nextBoundary = static_cast<float>( cell ) * cellSize;
            outTMax = ( start - nextBoundary ) / -delta;
            outTDelta = cellSize / -delta;
        }

        if ( outTMax < 0.0f )
        {
            outTMax = 0.0f;
        }
    };

    int stepX = 0;
    int stepY = 0;
    int stepZ = 0;
    float tMaxX = FLT_MAX;
    float tMaxY = FLT_MAX;
    float tMaxZ = FLT_MAX;
    float tDeltaX = FLT_MAX;
    float tDeltaY = FLT_MAX;
    float tDeltaZ = FLT_MAX;
    axisTraversal( position.x, displacement.x, cx, stepX, tMaxX, tDeltaX );
    axisTraversal( position.y, displacement.y, cy, stepY, tMaxY, tDeltaY );
    axisTraversal( position.z, displacement.z, cz, stepZ, tMaxZ, tDeltaZ );

    InsertBounds( index,
                  Vector3( position.x - radius, position.y - radius, position.z - radius ),
                  Vector3( position.x + radius, position.y + radius, position.z + radius ),
                  true );
    int visitedCells = 0;
    while ( ( cx != endX || cy != endY || cz != endZ ) && visitedCells < MAX_SWEPT_TRAVERSED_CELLS )
    {
        const float nextT = (std::min)( tMaxX, (std::min)( tMaxY, tMaxZ ) );
        constexpr float AXIS_TIE_EPSILON = 1e-5f;
        if ( tMaxX <= nextT + AXIS_TIE_EPSILON )
        {
            cx += stepX;
            tMaxX += tDeltaX;
        }
        if ( tMaxY <= nextT + AXIS_TIE_EPSILON )
        {
            cy += stepY;
            tMaxY += tDeltaY;
        }
        if ( tMaxZ <= nextT + AXIS_TIE_EPSILON )
        {
            cz += stepZ;
            tMaxZ += tDeltaZ;
        }

        const float t = (std::max)( 0.0f, (std::min)( 1.0f, nextT ) );
        const Vector3 samplePosition = position + displacement * t;
        InsertBounds( index,
                      Vector3( samplePosition.x - radius, samplePosition.y - radius, samplePosition.z - radius ),
                      Vector3( samplePosition.x + radius, samplePosition.y + radius, samplePosition.z + radius ),
                      true );
        ++visitedCells;
    }

    InsertBounds( index,
                  Vector3( endPosition.x - radius, endPosition.y - radius, endPosition.z - radius ),
                  Vector3( endPosition.x + radius, endPosition.y + radius, endPosition.z + radius ),
                  true );
}


void SpatialGrid::InsertPreparedBounds( int index,
                                        const Vector3& position,
                                        const Vector3& displacement,
                                        float radius,
                                        const Vector3& minBounds,
                                        const Vector3& maxBounds,
                                        bool swept )
{
    ++frameStats.bodyInsertions;
    if ( swept )
    {
        // Why: most swept rows fit the exact-AABB budget and can consume the
        // vector-prepared bounds without repeating endpoint arithmetic. Only
        // an oversized sweep re-enters InsertSwept so its capped traversal and
        // tie policy remain owned here rather than leaking into the kernel.
        ValidateBroadphaseBounds( index, minBounds, maxBounds, inverseCellSize );
        const int minX = static_cast<int>( floorf( minBounds.x * inverseCellSize ) );
        const int minY = static_cast<int>( floorf( minBounds.y * inverseCellSize ) );
        const int minZ = static_cast<int>( floorf( minBounds.z * inverseCellSize ) );
        const int maxX = static_cast<int>( floorf( maxBounds.x * inverseCellSize ) );
        const int maxY = static_cast<int>( floorf( maxBounds.y * inverseCellSize ) );
        const int maxZ = static_cast<int>( floorf( maxBounds.z * inverseCellSize ) );
        const int64_t cellCountX = int64_t( maxX ) - int64_t( minX ) + 1;
        const int64_t cellCountY = int64_t( maxY ) - int64_t( minY ) + 1;
        const int64_t cellCountZ = int64_t( maxZ ) - int64_t( minZ ) + 1;
        const bool exactAabbFits = cellCountX <= MAX_SWEPT_AABB_CELLS && cellCountY <= MAX_SWEPT_AABB_CELLS &&
                                   cellCountZ <= MAX_SWEPT_AABB_CELLS &&
                                   cellCountX * cellCountY * cellCountZ <= MAX_SWEPT_AABB_CELLS;
        if ( !exactAabbFits )
        {
            InsertSweptBounds( index, position, displacement, radius );
            return;
        }
    }
    InsertBounds( index, minBounds, maxBounds, false );
}


// Collect all candidate collision pairs from the grid.
// For each bucket with 2+ objects, generate all (i,j) pairs from that cell.
// Uses a bitset to deduplicate (a ball in multiple cells would otherwise
// generate the same pair multiple times).
//
// Output: vector of (indexA, indexB) pairs where A < B.
// These pairs still need NARROW-PHASE testing (actual sphere overlap check).
// The optional typed filter is deterministic broadphase value logic applied
// before vector append; no callback or erased owner state enters the hot loop.
void SpatialGrid::GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs,
                                     const SkullbonezCore::Physics::BroadphaseCandidateFilterContext* filter )
{
    outPairs.clear();
    frameStats.rawPairCombinations = 0;
    frameStats.uniquePairs = 0;
    frameStats.filterRejections = 0;
    frameStats.emittedCandidates = 0;

    // Dedup bits are frame-local; stale bits would hide candidate pairs.
    assert( objectCount >= 0 && objectCount <= SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS && "objectCount OOB" );
    if ( objectCount < 0 || objectCount > SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid object count out of bounds" );
    }
    int pairBits = objectCount * ( objectCount - 1 ) / 2;
    int wordsNeeded = ( pairBits + 63 ) / 64;
    if ( wordsNeeded > PAIR_WORDS )
    {
        wordsNeeded = PAIR_WORDS;
    }
    memset( pairSeen, 0, wordsNeeded * sizeof( uint64_t ) );

    std::pair<int, int> pendingPairs[SkullbonezCore::Physics::Kernels::NARROWPHASE_PRUNE_LANE_COUNT] = {};
    int pendingPairCount = 0;
    auto appendPair = [&]( int a, int b )
    {
        assert( outPairs.size() < outPairs.capacity() && "SpatialGrid candidate pair reserve exhausted" );
        if ( outPairs.size() >= outPairs.capacity() )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid candidate pair reserve exhausted" );
        }
        outPairs.emplace_back( a, b );
        ++frameStats.emittedCandidates;
    };
    auto flushPendingPairs = [&]()
    {
        if ( pendingPairCount == 0 )
        {
            return;
        }
        const std::span<const std::pair<int, int>> pairs( pendingPairs, static_cast<size_t>( pendingPairCount ) );
        const uint32_t accepted = SkullbonezCore::Physics::Kernels::PruneNarrowphasePairsAvx2( filter->hotFields,
                                                                                               filter->colliderRecords,
                                                                                               pairs,
                                                                                               filter->modelCount,
                                                                                               filter->dt,
                                                                                               filter->contactSkin );
        for ( int lane = 0; lane < pendingPairCount; ++lane )
        {
            if ( ( accepted & ( 1u << lane ) ) != 0u )
            {
                appendPair( pendingPairs[lane].first, pendingPairs[lane].second );
            }
            else
            {
                ++frameStats.filterRejections;
            }
        }
        pendingPairCount = 0;
    };

    const auto emitBucketPairs = [&]( Bucket& b )
    {
        if ( b.generation != generation || b.count < 2 )
        {
            return;
        }

        // Collect cell indices into a local buffer for O(c^2) pair generation
        int cellIndices[SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS];
        int cellCount = 0;
        int cur = b.head;
        while ( cur != -1 )
        {
            assert( cur >= 0 && cur < MAX_CELL_ENTRIES && "entry chain index OOB" );
            if ( cur < 0 || cur >= MAX_CELL_ENTRIES )
            {
                SB_FATAL( "Physics/SpatialGrid", "SpatialGrid entry chain index out of bounds" );
            }
            int objIdx = entries[cur].objectIndex;
            assert( objIdx >= 0 && objIdx < SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS &&
                    "objectIndex OOB in entry chain" );
            if ( objIdx < 0 || objIdx >= SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS )
            {
                SB_FATAL( "Physics/SpatialGrid", "SpatialGrid object index out of bounds in entry chain" );
            }
            if ( cellCount < SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS )
            {
                cellIndices[cellCount++] = objIdx;
            }
            else
            {
                assert( false && "cell index staging overflow" );
                SB_FATAL( "Physics/SpatialGrid", "SpatialGrid cell index staging overflow" );
            }
            cur = entries[cur].next;
        }

        for ( int i = 0; i < cellCount - 1; ++i )
        {
            for ( int j = i + 1; j < cellCount; ++j )
            {
                ++frameStats.rawPairCombinations;
                int a = cellIndices[i];
                int bIdx = cellIndices[j];

                // Hash collisions can place the same object in a bucket twice — skip self-pairs
                if ( a == bIdx )
                {
                    continue;
                }

                if ( a > bIdx )
                {
                    int tmp = a;
                    a = bIdx;
                    bIdx = tmp;
                }

                // Triangular index: bIdx*(bIdx-1)/2 + a  (requires a < bIdx)
                assert( a < bIdx && "pair ordering violated: a must be less than bIdx" );
                if ( a >= bIdx )
                {
                    SB_FATAL( "Physics/SpatialGrid", "SpatialGrid pair ordering violated" );
                }
                int pairIdx = bIdx * ( bIdx - 1 ) / 2 + a;
                int word = pairIdx >> 6;
                assert( word >= 0 && word < PAIR_WORDS && "pairSeen word index OOB" );
                if ( word < 0 || word >= PAIR_WORDS )
                {
                    SB_FATAL( "Physics/SpatialGrid", "SpatialGrid pair dedup index out of bounds" );
                }
                uint64_t bit = uint64_t( 1 ) << ( pairIdx & 63 );

                if ( !( pairSeen[word] & bit ) )
                {
                    pairSeen[word] |= bit;
                    ++frameStats.uniquePairs;
                    if ( filter && filter->simdKernels )
                    {
                        pendingPairs[pendingPairCount++] = std::pair<int, int>( a, bIdx );
                        if ( pendingPairCount == SkullbonezCore::Physics::Kernels::NARROWPHASE_PRUNE_LANE_COUNT )
                        {
                            flushPendingPairs();
                        }
                        continue;
                    }
                    if ( filter && !SkullbonezCore::Physics::BroadphaseCandidateCanTouch( filter, a, bIdx ) )
                    {
                        ++frameStats.filterRejections;
                        continue;
                    }
                    appendPair( a, bIdx );
                }
            }
        }
    };

    // Why: keep the unsaturated traversal byte-shaped like the legacy loop.
    // The mirrored overflow lambda above is invoked only for cold rows.
    const int primaryActiveCount = (std::min)( activeBucketCount, TABLE_SIZE );
    for ( int activeIndex = 0; activeIndex < primaryActiveCount; ++activeIndex )
    {
        const int bi = activeBuckets[activeIndex];
        assert( bi >= 0 && bi < TABLE_SIZE && "primary active bucket index OOB" );
        if ( bi < 0 || bi >= TABLE_SIZE )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid primary active bucket index out of bounds" );
        }
        Bucket& b = buckets[bi];
        if ( b.generation != generation || b.count < 2 )
        {
            continue;
        }

        int cellIndices[SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS];
        int cellCount = 0;
        int cur = b.head;
        while ( cur != -1 )
        {
            assert( cur >= 0 && cur < MAX_CELL_ENTRIES && "entry chain index OOB" );
            if ( cur < 0 || cur >= MAX_CELL_ENTRIES )
            {
                SB_FATAL( "Physics/SpatialGrid", "SpatialGrid entry chain index out of bounds" );
            }
            const int objIdx = entries[cur].objectIndex;
            assert( objIdx >= 0 && objIdx < SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS &&
                    "objectIndex OOB in entry chain" );
            if ( objIdx < 0 || objIdx >= SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS )
            {
                SB_FATAL( "Physics/SpatialGrid", "SpatialGrid object index out of bounds in entry chain" );
            }
            if ( cellCount >= SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS )
            {
                assert( false && "cell index staging overflow" );
                SB_FATAL( "Physics/SpatialGrid", "SpatialGrid cell index staging overflow" );
            }
            cellIndices[cellCount++] = objIdx;
            cur = entries[cur].next;
        }

        for ( int i = 0; i < cellCount - 1; ++i )
        {
            for ( int j = i + 1; j < cellCount; ++j )
            {
                ++frameStats.rawPairCombinations;
                int a = cellIndices[i];
                int bIdx = cellIndices[j];
                if ( a == bIdx )
                {
                    continue;
                }
                if ( a > bIdx )
                {
                    const int tmp = a;
                    a = bIdx;
                    bIdx = tmp;
                }
                assert( a < bIdx && "pair ordering violated: a must be less than bIdx" );
                if ( a >= bIdx )
                {
                    SB_FATAL( "Physics/SpatialGrid", "SpatialGrid pair ordering violated" );
                }
                const int pairIdx = bIdx * ( bIdx - 1 ) / 2 + a;
                const int word = pairIdx >> 6;
                assert( word >= 0 && word < PAIR_WORDS && "pairSeen word index OOB" );
                if ( word < 0 || word >= PAIR_WORDS )
                {
                    SB_FATAL( "Physics/SpatialGrid", "SpatialGrid pair dedup index out of bounds" );
                }
                const uint64_t bit = uint64_t( 1 ) << ( pairIdx & 63 );
                if ( pairSeen[word] & bit )
                {
                    continue;
                }
                pairSeen[word] |= bit;
                ++frameStats.uniquePairs;
                if ( filter && filter->simdKernels )
                {
                    pendingPairs[pendingPairCount++] = std::pair<int, int>( a, bIdx );
                    if ( pendingPairCount == SkullbonezCore::Physics::Kernels::NARROWPHASE_PRUNE_LANE_COUNT )
                    {
                        flushPendingPairs();
                    }
                    continue;
                }
                if ( filter && !SkullbonezCore::Physics::BroadphaseCandidateCanTouch( filter, a, bIdx ) )
                {
                    ++frameStats.filterRejections;
                    continue;
                }
                appendPair( a, bIdx );
            }
        }
    }
    for ( int overflowIndex = 0; overflowIndex < overflowBucketCount; ++overflowIndex )
    {
        emitBucketPairs( overflowStorage->buckets[overflowIndex] );
    }
    // Invariant: the final partial block still runs the AVX2 masked-lane path;
    // it is flushed only after all grid buckets so candidate order is unchanged.
    if ( filter && filter->simdKernels )
    {
        flushPendingPairs();
    }
}


// Active cell info is written into the caller-provided array.
// Each entry contains the grid coordinate (ix, iy, iz) and object count.
void SpatialGrid::GetActiveCells( ActiveCell* outCells, int maxCells ) const
{
    int count = ( activeBucketCount < maxCells ) ? activeBucketCount : maxCells;
    for ( int i = 0; i < count; ++i )
    {
        const int bi = i < TABLE_SIZE ? activeBuckets[i] : i;
        const Bucket& b = BucketAt( bi );
        outCells[i].ix = b.ix;
        outCells[i].iy = b.iy;
        outCells[i].iz = b.iz;
        outCells[i].objectCount = b.count;
    }
}
