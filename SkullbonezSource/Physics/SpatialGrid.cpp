/*
File: SkullbonezSource/Physics/SpatialGrid.cpp
Purpose:
  Partitions space into broadphase cells so physics can test nearby objects cheaply.

Mental model:
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
#include "../Core/FatalError.h"
#include <algorithm>
#include <cfloat>


using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Vector;


SpatialGrid::SpatialGrid( float fCellSize )
    : cellSize( fCellSize ), inverseCellSize( 1.0f / fCellSize ), generation( 0 ), entryPoolUsed( 0 ), objectCount( 0 ),
      activeBucketCount( 0 )
{
    memset( buckets, 0, sizeof( buckets ) );
}


void SpatialGrid::SetCellSize( float fCellSize )
{
    if ( fCellSize <= TOLERANCE || !std::isfinite( fCellSize ) )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid cell size must be finite and positive" );
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
}


// Look up or create a bucket for the given hash key.
// Uses LINEAR PROBING: if the target slot is occupied by a different key,
// try the next slot, then the next, etc.
// Output is the bucket index for this key.
int SpatialGrid::FindOrCreate( int64_t key, int16_t cx, int16_t cy, int16_t cz )
{
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
            if ( activeBucketCount >= TABLE_SIZE )
            {
                SB_FATAL( "Physics/SpatialGrid", "SpatialGrid active bucket capacity exceeded" );
            }
            activeBuckets[activeBucketCount++] = idx;
            return idx;
        }

        if ( b.key == key )
        {
            return idx;
        }

        idx = ( idx + 1 ) & TABLE_MASK;
    }

    return -1;
}


void SpatialGrid::InsertCell( int index, int ix, int iy, int iz )
{
    // The same object can overlap a cell through multiple sampled bounds during
    // a swept insert. Before appending, scan this bucket's linked list so one
    // object contributes at most once to a cell's candidate-pair list.
    const int64_t key = ( int64_t( ix ) * 73856093 ) ^ ( int64_t( iy ) * 19349663 ) ^ ( int64_t( iz ) * 83492791 );
    const int bi = FindOrCreate( key, (int16_t)ix, (int16_t)iy, (int16_t)iz );
    if ( bi < 0 || bi >= TABLE_SIZE )
    {
        return;
    }

    Bucket& b = buckets[bi];
    for ( int cur = b.head; cur != -1; cur = entries[cur].next )
    {
        assert( cur >= 0 && cur < MAX_CELL_ENTRIES && "entry chain index OOB" );
        if ( cur < 0 || cur >= MAX_CELL_ENTRIES )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid entry chain index out of bounds" );
        }
        if ( entries[cur].objectIndex == index )
        {
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
}


// Insert an object into all grid cells touched by an explicit AABB.
// Bounds are inclusive after conversion to grid coordinates.
void SpatialGrid::InsertBounds( int index, const Vector3& minBounds, const Vector3& maxBounds )
{
    assert( index >= 0 && "Insert: negative object index" );
    if ( index < 0 || index >= MAX_GAME_MODELS )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid object index out of bounds" );
    }

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

    for ( int ix = minX; ix <= maxX; ++ix )
    {
        for ( int iy = minY; iy <= maxY; ++iy )
        {
            for ( int iz = minZ; iz <= maxZ; ++iz )
            {
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
    InsertBounds( index,
                  Vector3( position.x - radius, position.y - radius, position.z - radius ),
                  Vector3( position.x + radius, position.y + radius, position.z + radius ) );
}


// Insert a dynamic body over the world-space AABB swept by its center this tick.
// Narrowphase still computes the exact time-of-impact; this only prevents the
// broadphase from skipping a fast body that starts outside the target cell.
void SpatialGrid::InsertSwept( int index, const Vector3& position, const Vector3& displacement, float radius )
{
    const Vector3 endPosition = position + displacement;
    const Vector3 minBounds( (std::min)( position.x, endPosition.x ) - radius,
                             (std::min)( position.y, endPosition.y ) - radius,
                             (std::min)( position.z, endPosition.z ) - radius );
    const Vector3 maxBounds( (std::max)( position.x, endPosition.x ) + radius,
                             (std::max)( position.y, endPosition.y ) + radius,
                             (std::max)( position.z, endPosition.z ) + radius );

    const int minX = static_cast<int>( floorf( minBounds.x * inverseCellSize ) );
    const int minY = static_cast<int>( floorf( minBounds.y * inverseCellSize ) );
    const int minZ = static_cast<int>( floorf( minBounds.z * inverseCellSize ) );
    const int maxX = static_cast<int>( floorf( maxBounds.x * inverseCellSize ) );
    const int maxY = static_cast<int>( floorf( maxBounds.y * inverseCellSize ) );
    const int maxZ = static_cast<int>( floorf( maxBounds.z * inverseCellSize ) );
    const int64_t cellCount = int64_t( maxX - minX + 1 ) * int64_t( maxY - minY + 1 ) * int64_t( maxZ - minZ + 1 );

    if ( cellCount <= MAX_SWEPT_AABB_CELLS )
    {
        // For normal fast movers, the swept bounding box is still small enough
        // to insert exactly. That covers every cell touched between start and end.
        InsertBounds( index, minBounds, maxBounds );
        return;
    }

    const float distanceSq = displacement * displacement;
    if ( distanceSq <= TOLERANCE )
    {
        Insert( index, position, radius );
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

    Insert( index, position, radius );
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
        Insert( index, position + displacement * t, radius );
        ++visitedCells;
    }

    Insert( index, endPosition, radius );
}


// Collect all candidate collision pairs from the grid.
// For each bucket with 2+ objects, generate all (i,j) pairs from that cell.
// Uses a bitset to deduplicate (a ball in multiple cells would otherwise
// generate the same pair multiple times).
//
// Output: vector of (indexA, indexB) pairs where A < B.
// These pairs still need NARROW-PHASE testing (actual sphere overlap check).
// The optional filter is only a deterministic broadphase reject before vector append.
void SpatialGrid::GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs,
                                     CandidatePairFilter filter,
                                     const void* filterUserData )
{
    outPairs.clear();

    // Dedup bits are frame-local; stale bits would hide candidate pairs.
    assert( objectCount >= 0 && objectCount <= MAX_GAME_MODELS && "objectCount OOB" );
    if ( objectCount < 0 || objectCount > MAX_GAME_MODELS )
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

    // Iterate only buckets that were actually touched this frame.
    for ( int activeIndex = 0; activeIndex < activeBucketCount; ++activeIndex )
    {
        int bi = activeBuckets[activeIndex];
        assert( bi >= 0 && bi < TABLE_SIZE && "active bucket index OOB" );
        if ( bi < 0 || bi >= TABLE_SIZE )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid active bucket index out of bounds" );
        }
        Bucket& b = buckets[bi];
        if ( b.generation != generation || b.count < 2 )
        {
            continue;
        }

        // Collect cell indices into a local buffer for O(c^2) pair generation
        int cellIndices[MAX_GAME_MODELS];
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
            assert( objIdx >= 0 && objIdx < MAX_GAME_MODELS && "objectIndex OOB in entry chain" );
            if ( objIdx < 0 || objIdx >= MAX_GAME_MODELS )
            {
                SB_FATAL( "Physics/SpatialGrid", "SpatialGrid object index out of bounds in entry chain" );
            }
            if ( cellCount < MAX_GAME_MODELS )
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
                    if ( filter && !filter( filterUserData, a, bIdx ) )
                    {
                        continue;
                    }
                    assert( outPairs.size() < outPairs.capacity() && "SpatialGrid candidate pair reserve exhausted" );
                    if ( outPairs.size() >= outPairs.capacity() )
                    {
                        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid candidate pair reserve exhausted" );
                    }
                    outPairs.emplace_back( a, bIdx );
                }
            }
        }
    }
}


// Active cell info is written into the caller-provided array.
// Each entry contains the grid coordinate (ix, iy, iz) and object count.
void SpatialGrid::GetActiveCells( ActiveCell* outCells, int maxCells ) const
{
    int count = ( activeBucketCount < maxCells ) ? activeBucketCount : maxCells;
    for ( int i = 0; i < count; ++i )
    {
        int bi = activeBuckets[i];
        const Bucket& b = buckets[bi];
        outCells[i].ix = b.ix;
        outCells[i].iy = b.iy;
        outCells[i].iz = b.iz;
        outCells[i].objectCount = b.count;
    }
}
