// --- Includes ---
#include "SkullbonezSpatialGrid.h"


// --- Usings ---
using namespace SkullbonezCore::Math::CollisionDetection;


SpatialGrid::SpatialGrid( float fCellSize )
    : cellSize( fCellSize ), inverseCellSize( 1.0f / fCellSize ), generation( 0 ), indexPoolUsed( 0 ), objectCount( 0 )
{
    memset( buckets, 0, sizeof( buckets ) );
}


void SpatialGrid::Clear()
{
    ++generation;
    indexPoolUsed = 0;
    objectCount = 0;
}


int SpatialGrid::FindOrCreate( int64_t key )
{
    int idx = static_cast<int>( static_cast<uint64_t>( key ) & TABLE_MASK );

    for ( int probe = 0; probe < TABLE_SIZE; ++probe )
    {
        Bucket& b = buckets[idx];

        if ( b.generation != generation )
        {
            // Stale bucket — claim it
            b.key = key;
            b.generation = generation;
            b.start = static_cast<uint16_t>( indexPoolUsed );
            b.count = 0;
            return idx;
        }

        if ( b.key == key )
        {
            return idx;
        }

        idx = ( idx + 1 ) & TABLE_MASK;
    }

    // Table full — should never happen with TABLE_SIZE >> expected cells
    return 0;
}


void SpatialGrid::Insert( int index, const Vector3& position, float radius )
{
    if ( index >= MAX_OBJECTS )
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
                Bucket& b = buckets[bi];

                if ( indexPoolUsed < MAX_CELL_ENTRIES )
                {
                    // Entries must be contiguous: only append if this bucket is at the pool tail
                    if ( b.count == 0 || b.start + b.count == indexPoolUsed )
                    {
                        indexPool[indexPoolUsed++] = index;
                        ++b.count;
                    }
                }
            }
        }
    }
}


void SpatialGrid::GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs )
{
    outPairs.clear();

    // Clear only the bits we need: n*(n-1)/2 bits for objectCount objects
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

        int end = b.start + b.count;
        for ( int i = b.start; i < end - 1; ++i )
        {
            for ( int j = i + 1; j < end; ++j )
            {
                int a = indexPool[i];
                int bIdx = indexPool[j];

                // Ensure a < bIdx for canonical pair
                if ( a > bIdx )
                {
                    int tmp = a;
                    a = bIdx;
                    bIdx = tmp;
                }

                // Triangular index: bIdx*(bIdx-1)/2 + a
                int pairIdx = bIdx * ( bIdx - 1 ) / 2 + a;
                int word = pairIdx >> 6;
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
