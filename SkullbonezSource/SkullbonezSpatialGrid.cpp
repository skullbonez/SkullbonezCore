// =============================================================================
// SPATIAL HASH GRID — Broadphase Collision Detection (SkullbonezSpatialGrid.cpp)
// =============================================================================
//
// PURPOSE: Dramatically reduce the number of collision checks needed each frame.
// Without this, checking N objects against each other requires N×(N-1)/2 tests.
// With 300 balls, that's 44,850 pair checks per frame — far too many.
//
// --- The Problem: O(N²) is Too Slow ---
//
//  Naive approach:
//  For each ball A:
//    For each ball B (B ≠ A):
//      Check if A overlaps B  →  N² checks
//
//  With spatial hashing:
//  For each ball: insert into grid cells it overlaps
//  Only check pairs that share at least one cell  →  Near O(N) in practice
//
// --- How Spatial Hashing Works ---
//
//  1. Divide the world into a uniform 3D grid of cells (cellSize ≈ diameter of objects)
//
//     +---+---+---+---+
//     |   | ● |   |   |     ● = ball occupies this cell
//     +---+---+---+---+
//     |   | ● | ● |   |     Only balls in the SAME cell can possibly collide
//     +---+---+---+---+
//     |   |   | ● |   |
//     +---+---+---+---+
//
//  2. For each ball, compute which cells it overlaps (based on position ± radius)
//  3. Insert the ball's index into each overlapping cell
//  4. For each cell with 2+ balls, those balls are "candidate pairs" — test them
//
// --- Hash Function ---
//
//  Instead of a giant 3D array, we use a HASH TABLE.
//  Cell coordinate (ix, iy, iz) → hash key via:
//    key = ix×73856093 ⊕ iy×19349663 ⊕ iz×83492791
//
//  These large primes create a good distribution with minimal clustering.
//  The hash maps to a fixed-size bucket array (TABLE_SIZE = power of 2).
//
// --- Generation Counter (Lazy Clearing) ---
//
//  Instead of zeroing the entire hash table each frame (expensive),
//  we increment a "generation" counter. Buckets with old generation are
//  treated as empty — effectively a free O(1) clear operation.
//
// --- Pair Deduplication ---
//
//  A ball spanning multiple cells will create duplicate pairs. We use a
//  compact bitset (triangular index) to ensure each pair is reported exactly once.
//
// =============================================================================


// --- Includes ---
#include "SkullbonezSpatialGrid.h"
#include <algorithm>
#include <stdexcept>


// --- Usings ---
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Vector;


SpatialGrid::SpatialGrid( float fCellSize )
    : cellSize( fCellSize ), inverseCellSize( 1.0f / fCellSize ), generation( 0 ), entryPoolUsed( 0 ), objectCount( 0 ), activeBucketCount( 0 )
{
    memset( buckets, 0, sizeof( buckets ) );
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
// Returns the bucket index for this key.
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
                throw std::runtime_error( "SpatialGrid active bucket capacity exceeded" );
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
            throw std::runtime_error( "SpatialGrid entry chain index out of bounds" );
        }
        if ( entries[cur].objectIndex == index )
        {
            return;
        }
    }

    if ( entryPoolUsed < MAX_CELL_ENTRIES )
    {
        entries[entryPoolUsed].objectIndex = index;
        entries[entryPoolUsed].next = b.head;
        b.head = entryPoolUsed;
        ++entryPoolUsed;
        ++b.count;
    }
}


// Insert an object into all grid cells touched by an explicit AABB.
// Bounds are inclusive after conversion to grid coordinates.
void SpatialGrid::InsertBounds( int index, const Vector3& minBounds, const Vector3& maxBounds )
{
    assert( index >= 0 && "Insert: negative object index" );
    if ( index < 0 || index >= MAX_GAME_MODELS )
    {
        throw std::runtime_error( "SpatialGrid object index out of bounds" );
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

    const float distance = sqrtf( distanceSq );
    const float stepLength = (std::max)( cellSize * 0.5f, 0.01f );
    int steps = static_cast<int>( ceilf( distance / stepLength ) );
    steps = (std::max)( 1, (std::min)( steps, MAX_SWEPT_SAMPLE_STEPS ) );

    // If the swept AABB would flood the fixed entry pool, sample along the path
    // instead. This is conservative for projectiles without letting one extreme
    // move consume the entire broadphase grid.
    for ( int sample = 0; sample <= steps; ++sample )
    {
        const float t = static_cast<float>( sample ) / static_cast<float>( steps );
        Insert( index, position + displacement * t, radius );
    }
}


// Collect all candidate collision pairs from the grid.
// For each bucket with 2+ objects, generate all (i,j) pairs from that cell.
// Uses a bitset to deduplicate (a ball in multiple cells would otherwise
// generate the same pair multiple times).
//
// Output: vector of (indexA, indexB) pairs where A < B.
// These pairs still need NARROW-PHASE testing (actual sphere overlap check).
void SpatialGrid::GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs )
{
    outPairs.clear();

    // Clear pair dedup bits
    assert( objectCount >= 0 && objectCount <= MAX_GAME_MODELS && "objectCount OOB" );
    if ( objectCount < 0 || objectCount > MAX_GAME_MODELS )
    {
        throw std::runtime_error( "SpatialGrid object count out of bounds" );
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
            throw std::runtime_error( "SpatialGrid active bucket index out of bounds" );
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
                throw std::runtime_error( "SpatialGrid entry chain index out of bounds" );
            }
            int objIdx = entries[cur].objectIndex;
            assert( objIdx >= 0 && objIdx < MAX_GAME_MODELS && "objectIndex OOB in entry chain" );
            if ( objIdx < 0 || objIdx >= MAX_GAME_MODELS )
            {
                throw std::runtime_error( "SpatialGrid object index out of bounds in entry chain" );
            }
            if ( cellCount < MAX_GAME_MODELS )
            {
                cellIndices[cellCount++] = objIdx;
            }
            else
            {
                assert( false && "cell index staging overflow" );
                throw std::runtime_error( "SpatialGrid cell index staging overflow" );
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
                    throw std::runtime_error( "SpatialGrid pair ordering violated" );
                }
                int pairIdx = bIdx * ( bIdx - 1 ) / 2 + a;
                int word = pairIdx >> 6;
                assert( word >= 0 && word < PAIR_WORDS && "pairSeen word index OOB" );
                if ( word < 0 || word >= PAIR_WORDS )
                {
                    throw std::runtime_error( "SpatialGrid pair dedup index out of bounds" );
                }
                uint64_t bit = uint64_t( 1 ) << ( pairIdx & 63 );

                if ( !( pairSeen[word] & bit ) )
                {
                    pairSeen[word] |= bit;
                    outPairs.emplace_back( a, bIdx );
                }
            }
        }
    }
}


// Copies active cell info into the caller-provided array.
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
