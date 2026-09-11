// Terrain editing preserves one grid for visible geometry and collision.
#include "Terrain.h"
#include "../Core/AtomicTextFileWriter.h"
#include "../Core/SbDiagnosticStore.h"
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
#include "../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../Rendering/DX12/MeshDX12.h"
#endif

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace SkullbonezCore::Geometry
{
void Terrain::PrepareEditing()
{
    if ( m_editingPrepared )
    {
        return;
    }
    // Lifetime: scene loading reserves all posts, collision cells and upload data.
    // An untouched analytic plane stays analytic until the first successful stroke.
    if ( m_isFlatSlope )
    {
        m_postsPerSide = 129;
        m_postCount = static_cast<size_t>( m_postsPerSide ) * m_postsPerSide;
        m_quadCount = static_cast<size_t>( m_postsPerSide - 1 ) * ( m_postsPerSide - 1 );
        m_gridSpacing = FLAT_SLOPE_EXTENT / ( m_postsPerSide - 1 );
        m_textureWrap = 8;
        m_postData.resize( m_postCount );
        for ( int x = 0; x < m_postsPerSide; ++x )
        {
            for ( int z = 0; z < m_postsPerSide; ++z )
            {
                auto& post = m_postData[x * m_postsPerSide + z];
                const float worldX = x * m_gridSpacing;
                const float worldZ = z * m_gridSpacing;
                post.vPosition = Math::Vector::Vector3( worldX, m_slopeBaseY + m_slopeX * worldX + m_slopeZ * worldZ, worldZ );
                post.vNormal = m_flatSlopeNormal;
            }
        }
    }
    m_cachedCollisionData.reserve( m_quadCount );
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
    FillRenderVertexData( m_renderVertexData );
    if ( m_resources )
    {
        BuildMesh();
    }
#endif
    m_editingPrepared = true;
}

void Terrain::RefreshEditedGrid()
{
    m_minTerrainHeight = ( std::numeric_limits<float>::max )();
    m_maxTerrainHeight = std::numeric_limits<float>::lowest();
    for ( const auto& post : m_postData )
    {
        m_minTerrainHeight = (std::min)( m_minTerrainHeight, post.vPosition.y );
        m_maxTerrainHeight = (std::max)( m_maxTerrainHeight, post.vPosition.y );
    }
    BuildCollisionCache();
    GenerateNormals();
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
    FillRenderVertexData( m_renderVertexData );
#endif
}

bool Terrain::Sculpt( const Math::Vector::Vector3& center, float radius, float heightDelta )
{
    if ( !m_editingPrepared || !std::isfinite( center.x ) || !std::isfinite( center.z ) || !std::isfinite( radius ) || radius <= 0.0f || !std::isfinite( heightDelta ) || heightDelta == 0.0f )
    {
        return false;
    }
    bool changed = false;
    for ( auto& post : m_postData )
    {
        const float dx = post.vPosition.x - center.x;
        const float dz = post.vPosition.z - center.z;
        const float distanceSquared = dx * dx + dz * dz;
        if ( distanceSquared >= radius * radius )
        {
            continue;
        }
        // Why: a smooth falloff leaves the edge stationary and avoids a hard ridge.
        const float falloff = 1.0f - distanceSquared / ( radius * radius );
        const float next = std::clamp( post.vPosition.y + heightDelta * falloff * falloff, -10000.0f, 10000.0f );
        changed |= next != post.vPosition.y;
        post.vPosition.y = next;
    }
    if ( changed )
    {
        m_isFlatSlope = false;
        m_edited = true;
        ++m_editRevision;
        RefreshEditedGrid();
    }
    return changed;
}

#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
bool Terrain::UploadEditedMesh()
{
    return m_resources && m_terrainMesh && m_resources->UpdateMesh( *m_terrainMesh, m_renderVertexData.data(), static_cast<int>( m_renderVertexData.size() / 8 ) );
}
#endif

Core::SbResult Terrain::LoadSavedHeightMap( Core::SbDiagnosticStore& diagnostics, const char* path, const Core::EngineConfig& config, std::unique_ptr<Terrain>& result )
{
    if ( !path || !*path )
    {
        return diagnostics.Failure( "World/Terrain", "Height map path is empty." );
    }
    const std::filesystem::path source( path );
    std::string extension = source.extension().string();
    std::transform( extension.begin(), extension.end(), extension.begin(), []( unsigned char value ) { return static_cast<char>( std::tolower( value ) ); } );
    if ( extension == ".raw" )
    {
        const auto loaded = TryCreatePhysicsFromHeightMap( diagnostics, path, 256, 8, 15, config, result );
        if ( loaded.Ok() )
        {
            result->m_heightMapSource = std::filesystem::absolute( source ).lexically_normal().generic_string();
        }
        return loaded;
    }
    std::ifstream input( source );
    input.imbue( std::locale::classic() );
    char magic[32] = {};
    int version = 0;
    int side = 0;
    float spacing = 0;
    int wrap = 0;
    if ( !( input >> std::setw( sizeof( magic ) ) >> magic >> version >> side >> spacing >> wrap ) || std::strcmp( magic, "SKULLBONEZ_HEIGHTMAP" ) != 0 || version != 1 || side < 2 ||
         side > MAX_SAVED_POSTS_PER_SIDE || !std::isfinite( spacing ) || spacing < MIN_SAVED_GRID_SPACING || spacing * ( side - 1 ) > 100000.0f || wrap < 1 || wrap > 1024 )
    {
        return diagnostics.Failure( "World/Terrain", "Invalid height map header: %s", path );
    }
    ValidatedHeightMapGeometry geometry;
    const auto checked = TryValidateHeightMapDimensions( diagnostics, side, 1, wrap, geometry );
    if ( !checked.Ok() )
    {
        return checked;
    }
    auto loaded = std::make_unique<Terrain>( geometry, config, nullptr, nullptr );
    loaded->m_gridSpacing = spacing;
    loaded->m_postData.resize( geometry.postCount );
    for ( int x = 0; x < side; ++x )
    {
        for ( int z = 0; z < side; ++z )
        {
            float height = 0;
            if ( !( input >> height ) || !std::isfinite( height ) || height < -10000.0f || height > 10000.0f )
            {
                return diagnostics.Failure( "World/Terrain", "Invalid height sample in %s", path );
            }
            loaded->m_postData[x * side + z].vPosition = Math::Vector::Vector3( x * spacing, height, z * spacing );
        }
    }
    input >> std::ws;
    if ( !input.eof() )
    {
        return diagnostics.Failure( "World/Terrain", "Unexpected trailing height map data in %s", path );
    }
    loaded->RefreshEditedGrid();
    loaded->m_heightMapSource = std::filesystem::absolute( source ).lexically_normal().generic_string();
    result = std::move( loaded );
    return Core::SbResult::Success();
}

Core::SbResult Terrain::SaveHeightMapForScene( Core::SbDiagnosticStore& diagnostics, const char* scenePath, std::string& reference ) const
{
    reference = m_heightMapSource;
    if ( !m_edited )
    {
        return Core::SbResult::Success();
    }
    if ( !scenePath || !*scenePath )
    {
        return diagnostics.Failure( "World/Terrain", "Cannot save height map without a level path." );
    }
    std::ostringstream stream;
    stream.imbue( std::locale::classic() );
    stream << std::setprecision( std::numeric_limits<float>::max_digits10 );
    stream << "SKULLBONEZ_HEIGHTMAP 1\n" << m_postsPerSide << ' ' << m_gridSpacing << ' ' << m_textureWrap << '\n';
    for ( const auto& post : m_postData )
    {
        stream << post.vPosition.y << '\n';
    }
    const std::string contents = stream.str();
    uint64_t hash = 14695981039346656037ull;
    for ( unsigned char byte : contents )
    {
        hash = ( hash ^ byte ) * 1099511628211ull;
    }
    // Why: immutable content-named siblings preserve the previous level on failed
    // saves and let repeated saves reuse exactly the same authored map.
    const std::filesystem::path level( scenePath );
    const std::filesystem::path map = level.parent_path() / ( level.stem().string() + ".terrain-" + std::to_string( hash ) + ".heightmap" );
    std::error_code error;
    if ( std::filesystem::exists( map, error ) )
    {
        std::ifstream existing( map, std::ios::binary );
        const std::string previous( ( std::istreambuf_iterator<char>( existing ) ), std::istreambuf_iterator<char>() );
        if ( previous != contents )
        {
            return diagnostics.Failure( "World/Terrain", "Height map filename conflicts with different data: %s", map.string().c_str() );
        }
    }
    else
    {
        const auto saved = Core::WriteTextFileAtomic( diagnostics, "World/Terrain", map.string().c_str(), contents );
        if ( !saved.Ok() )
        {
            return saved;
        }
    }
    reference = map.filename().generic_string();
    return Core::SbResult::Success();
}
} // namespace SkullbonezCore::Geometry
