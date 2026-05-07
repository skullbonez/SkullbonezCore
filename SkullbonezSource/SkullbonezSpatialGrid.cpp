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


// --- Usings ---
using namespace SkullbonezCore::Math::CollisionDetection;


SpatialGrid::SpatialGrid( float fCellSize )
    : cellSize( fCellSize ), inverseCellSize( 1.0f / fCellSize ), generation( 0 ), entryPoolUsed( 0 ), objectCount( 0 )
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
}


// Look up or create a bucket for the given hash key.
// Uses LINEAR PROBING: if the target slot is occupied by a different key,
// try the next slot, then the next, etc.
// Returns the bucket index for this key.
int SpatialGrid::FindOrCreate( int64_t key )
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
            return idx;
        }

        if ( b.key == key )
        {
            return idx;
        }

        idx = ( idx + 1 ) & TABLE_MASK;
    }

    return 0;
}


// Insert an object into all grid cells it overlaps.
// A sphere at position P with radius R overlaps cells from:
//   min = floor((P - R) / cellSize)  to  max = floor((P + R) / cellSize)
//
// For each overlapping cell, compute its hash key and add this object's
// index to that bucket's linked list (via the entries[] pool).
void SpatialGrid::Insert( int index, const Vector3& position, float radius )
{
    assert( index >= 0 && "Insert: negative object index" );
    if ( index >= MAX_GAME_MODELS )
    {
        return;
    }

    if ( index >= objectCount )
    {
        objectCount = index + 1;
    }

    int minX = static_cast<int>( floorf( ( position.x - radius ) * inverseCellSize ) );
    int minY = static_cast<int>( floorf( ( position.y - radius ) * inverseCellSize ) );
    int minZ = static_cast<int>( floorf( ( position.z - radius ) * inverseCellSize ) );
    int maxX = static_cast<int>( floorf( ( position.x + radius ) * inverseCellSize ) );
    int maxY = static_cast<int>( floorf( ( position.y + radius ) * inverseCellSize ) );
    int maxZ = static_cast<int>( floorf( ( position.z + radius ) * inverseCellSize ) );

    for ( int ix = minX; ix <= maxX; ++ix )
    {
        for ( int iy = minY; iy <= maxY; ++iy )
        {
            for ( int iz = minZ; iz <= maxZ; ++iz )
            {
                int64_t key = ( int64_t( ix ) * 73856093 ) ^ ( int64_t( iy ) * 19349663 ) ^ ( int64_t( iz ) * 83492791 );
                int bi = FindOrCreate( key );
                assert( bi >= 0 && bi < TABLE_SIZE && "Insert: bucket index OOB" );
                Bucket& b = buckets[bi];

                if ( entryPoolUsed < MAX_CELL_ENTRIES )
                {
                    entries[entryPoolUsed].objectIndex = index;
                    entries[entryPoolUsed].next = b.head;
                    b.head = entryPoolUsed;
                    ++entryPoolUsed;
                    ++b.count;
                }
            }
        }
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
    int pairBits = objectCount * ( objectCount - 1 ) / 2;
    int wordsNeeded = ( pairBits + 63 ) / 64;
    if ( wordsNeeded > PAIR_WORDS )
    {
        wordsNeeded = PAIR_WORDS;
    }
    memset( pairSeen, 0, wordsNeeded * sizeof( uint64_t ) );

    for ( int bi = 0; bi < TABLE_SIZE; ++bi )
    {
        Bucket& b = buckets[bi];
        if ( b.generation != generation || b.count < 2 )
        {
            continue;
        }

        // Collect cell indices into a local buffer for O(c^2) pair generation
        int cellIndices[64];
        int cellCount = 0;
        int cur = b.head;
        while ( cur != -1 && cellCount < 64 )
        {
            assert( cur >= 0 && cur < MAX_CELL_ENTRIES && "entry chain index OOB" );
            int objIdx = entries[cur].objectIndex;
            assert( objIdx >= 0 && objIdx < MAX_GAME_MODELS && "objectIndex OOB in entry chain" );
            cellIndices[cellCount++] = objIdx;
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
                int pairIdx = bIdx * ( bIdx - 1 ) / 2 + a;
                int word = pairIdx >> 6;
                assert( word >= 0 && word < PAIR_WORDS && "pairSeen word index OOB" );
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
