/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezSpatialGrid.h"

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math::CollisionDetection;

/* -- HASH CELL -------------------------------------------------------------------*/
int64_t SpatialGrid::HashCell( int ix, int iy, int iz )
{
    return ( int64_t( ix ) * 73856093 ) ^ ( int64_t( iy ) * 19349663 ) ^ ( int64_t( iz ) * 83492791 );
}

/* -- CONSTRUCTOR -----------------------------------------------------------------*/
SpatialGrid::SpatialGrid( float fCellSize )
    : cellSize( fCellSize ), inverseCellSize( 1.0f / fCellSize )
{
}

/* -- CLEAR -----------------------------------------------------------------------*/
void SpatialGrid::Clear()
{
    for ( auto& pair : cells )
    {
        pair.second.clear();
    }
    cells.clear();
}

/* -- INSERT ----------------------------------------------------------------------*/
void SpatialGrid::Insert( int index, const Vector3& m_position, float m_radius )
{
    int minX = static_cast<int>( floorf( ( m_position.x - m_radius ) * inverseCellSize ) );
    int minY = static_cast<int>( floorf( ( m_position.y - m_radius ) * inverseCellSize ) );
    int minZ = static_cast<int>( floorf( ( m_position.z - m_radius ) * inverseCellSize ) );
    int maxX = static_cast<int>( floorf( ( m_position.x + m_radius ) * inverseCellSize ) );
    int maxY = static_cast<int>( floorf( ( m_position.y + m_radius ) * inverseCellSize ) );
    int maxZ = static_cast<int>( floorf( ( m_position.z + m_radius ) * inverseCellSize ) );

    for ( int ix = minX; ix <= maxX; ++ix )
    {
        for ( int iy = minY; iy <= maxY; ++iy )
        {
            for ( int iz = minZ; iz <= maxZ; ++iz )
            {
                cells[HashCell( ix, iy, iz )].push_back( index );
            }
        }
    }
}

/* -- GET CANDIDATE PAIRS ---------------------------------------------------------*/
void SpatialGrid::GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs )
{
    outPairs.clear();
    std::unordered_set<int64_t> seen;

    for ( const auto& cell : cells )
    {
        const std::vector<int>& indices = cell.second;
        for ( int i = 0; i < static_cast<int>( indices.size() ) - 1; ++i )
        {
            for ( int j = i + 1; j < static_cast<int>( indices.size() ); ++j )
            {
                int a = ( std::min )( indices[i], indices[j] );
                int b = ( std::max )( indices[i], indices[j] );
                int64_t key = ( int64_t( a ) << 32 ) | int64_t( b );

                if ( seen.insert( key ).second )
                {
                    outPairs.push_back( std::make_pair( a, b ) );
                }
            }
        }
    }
}
