/*
File: SkullbonezSource/World/Terrain.cpp
Purpose:
  Stores terrain mesh, height queries, and terrain rendering resources.

Summary:
  Terrain.cpp stores terrain mesh, height queries, and terrain rendering
  resources. As an implementation unit, keep edits anchored on world-state
  ownership, terrain/environment data, and physics/render handoff and on the
  glossary/invariants below.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/World/Terrain.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Terrain.h"
#include "../Assets/AssetKeys.h"
#include "../Assets/AssetSystem.h"
#include "../Core/FatalError.h"
#include "../Core/Profiler.h"
#include "../Core/SbResult.h"
#include "../Rendering/IRenderResourceFactory.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <memory>


using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using SkullbonezCore::Basics::SbResult;

namespace
{
struct FileCloser
{
    void operator()( FILE* file ) const
    {
        if ( file )
        {
            fclose( file );
        }
    }
};

using FileHandle = std::unique_ptr<FILE, FileCloser>;
} // namespace


Terrain::Terrain( int iMapSize,
                  int iStepSize,
                  int iTextureWrap,
                  const SkullbonezCore::Basics::EngineConfig& config,
                  SkullbonezCore::Assets::AssetSystem& assets,
                  IRenderResourceFactory& resources )
{
    m_mapSize = iMapSize;
    m_stepSize = iStepSize;
    m_renderStepSize = iStepSize;
    m_renderPostsPerSide = 0;
    m_textureWrap = iTextureWrap;
    m_isFlatSlope = false;
    m_slopeBaseY = 0.0f;
    m_slopeX = 0.0f;
    m_slopeZ = 0.0f;
    m_flatSlopeNormal = Vector3( 0.0f, 1.0f, 0.0f );
    m_flatSlopePlane.m_normal = m_flatSlopeNormal;
    m_flatSlopePlane.m_distance = 0.0f;
    m_maxTerrainHeight = 0.0f;
    m_minTerrainHeight = 0.0f;

    m_terrainSizeWorldCoords = ( ( m_mapSize - m_stepSize ) / m_stepSize ) * m_stepSize;

    m_postsPerSide = m_mapSize / m_stepSize;
    BindRenderContexts( config, assets, resources );
    ConfigureRenderStepSize();
}


SbResult Terrain::TryCreateFromHeightMap( const char* sFileName,
                                          int iMapSize,
                                          int iStepSize,
                                          int iTextureWrap,
                                          const SkullbonezCore::Basics::EngineConfig& config,
                                          SkullbonezCore::Assets::AssetSystem& assets,
                                          IRenderResourceFactory& resources,
                                          std::unique_ptr<Terrain>& outTerrain )
{
    // Concept: RAW terrain files are external asset input. The factory keeps
    // a failed load out of the scene owner and reports Lane R instead of
    // letting constructor exceptions escape through scene startup.
    outTerrain.reset();
    std::unique_ptr<Terrain> terrain =
        std::make_unique<Terrain>( iMapSize, iStepSize, iTextureWrap, config, assets, resources );
    const SbResult loadResult = terrain->LoadTerrainData( sFileName );
    if ( !loadResult.ok )
    {
        return loadResult;
    }

    terrain->BuildTerrain();
    terrain->BuildMesh();
    terrain->InitialiseTerrainShader();
    outTerrain = std::move( terrain );
    return SbResult::Success();
}


Terrain::Terrain( float slopeBaseY,
                  float slopeX,
                  float slopeZ,
                  const SkullbonezCore::Basics::EngineConfig& config,
                  SkullbonezCore::Assets::AssetSystem& assets,
                  IRenderResourceFactory& resources )
{
    m_mapSize = 0;
    m_stepSize = 0;
    m_renderStepSize = 0;
    m_renderPostsPerSide = 0;
    m_textureWrap = 0;
    m_postsPerSide = 0;
    m_terrainSizeWorldCoords = 0;
    m_isFlatSlope = true;
    m_slopeBaseY = slopeBaseY;
    m_slopeX = slopeX;
    m_slopeZ = slopeZ;
    m_flatSlopeNormal = Vector3( -m_slopeX, 1.0f, -m_slopeZ );
    m_flatSlopeNormal.Normalise();
    if ( m_flatSlopeNormal.y < 0.0f )
    {
        m_flatSlopeNormal = m_flatSlopeNormal * -1.0f;
    }
    m_flatSlopePlane.m_normal = m_flatSlopeNormal;
    m_flatSlopePlane.m_distance = m_flatSlopeNormal.y * m_slopeBaseY;
    BindRenderContexts( config, assets, resources );

    // Max height at the 4 corners of the flat slope play area
    float h00 = slopeBaseY;
    float h10 = slopeBaseY + slopeX * FLAT_SLOPE_EXTENT;
    float h01 = slopeBaseY + slopeZ * FLAT_SLOPE_EXTENT;
    float h11 = slopeBaseY + slopeX * FLAT_SLOPE_EXTENT + slopeZ * FLAT_SLOPE_EXTENT;
    m_maxTerrainHeight = (std::max)( (std::max)( h00, h10 ), (std::max)( h01, h11 ) );
    m_minTerrainHeight = (std::min)( (std::min)( h00, h10 ), (std::min)( h01, h11 ) );

    BuildFlatSlopeMesh();
    InitialiseTerrainShader();
}


Terrain::~Terrain()
{
}


void Terrain::BindRenderContexts( const SkullbonezCore::Basics::EngineConfig& config,
                                  SkullbonezCore::Assets::AssetSystem& assets,
                                  IRenderResourceFactory& resources )
{
    // Lifetime: Terrain keeps these as rebuild-only borrows owned by Run and
    // refreshed by the render pass before lazy resource recreation.
    m_config = &config;
    m_assets = &assets;
    m_resources = &resources;
}


const SkullbonezCore::Basics::EngineConfig& Terrain::Config() const
{
    assert( m_config );
    return *m_config;
}


void Terrain::InitialiseTerrainShader()
{
    assert( m_assets );
    assert( m_resources );
    m_terrainShader = m_assets->CreateShader( *m_resources, "shader.lit_textured" );
    if ( !m_terrainShader )
    {
        return;
    }
    m_terrainShader->Use();
    const auto& ordinary = Config().ordinaryRender;
    m_terrainShader->SetVec4( "uLightAmbient",
                              ordinary.skyAmbientR,
                              ordinary.skyAmbientG,
                              ordinary.skyAmbientB,
                              ordinary.ambientStrength );
    m_terrainShader->SetVec4( "uLightDiffuse",
                              ordinary.sunColorR * ordinary.sunIntensity,
                              ordinary.sunColorG * ordinary.sunIntensity,
                              ordinary.sunColorB * ordinary.sunIntensity,
                              1.0f );
    m_terrainShader->SetVec4( "uMaterialAmbient",
                              ordinary.groundAmbientR,
                              ordinary.groundAmbientG,
                              ordinary.groundAmbientB,
                              1.0f );
    m_terrainShader->SetVec4( "uMaterialDiffuse", 1.0f, 1.0f, 1.0f, 1.0f );
    m_terrainShader->SetVec4( "uCinematicTerrain", 0.0f, 0.0f, 0.0f, 0.0f );
    m_terrainShader->SetVec4( "uCinematicBasin", 620.0f, 615.0f, 285.0f, 205.0f );
    m_terrainShader->SetVec4( "uStyleModes", 0.0f, 0.0f, 0.0f, 1.0f );
    m_terrainShader->SetVec4( "uTerrainTint", 0.78f, 0.60f, 0.38f, 1.0f );
    m_terrainShader->SetVec4( "uTerrainAccent", 0.20f, 0.09f, 0.02f, 0.0f );
    m_terrainShader->SetVec4( "uTerrainGrid", 46.0f, 0.0f, 0.0f, 0.0f );
    m_terrainShader->SetInt( "uTexture", 0 );
}


void Terrain::ResetRenderResources()
{
    m_terrainMesh.reset();
    m_terrainShader.reset();
    m_shadowDepthShader.reset();

    if ( m_isFlatSlope )
    {
        BuildFlatSlopeMesh();
    }
    else
    {
        BuildMesh();
    }

    InitialiseTerrainShader();
}


void Terrain::EnsureRenderResources( const SkullbonezCore::Basics::EngineConfig& config,
                                     SkullbonezCore::Assets::AssetSystem& assets,
                                     IRenderResourceFactory& resources )
{
    BindRenderContexts( config, assets, resources );
    if ( !m_terrainMesh || !m_terrainShader )
    {
        ResetRenderResources();
    }
}


void Terrain::EnsureShadowDepthResources()
{
    if ( m_shadowDepthShader )
    {
        return;
    }

    // Runtime allocation policy: terrain's non-instanced shadow caster shader is
    // a backend resource. Create it during the explicit shadow backend-init step
    // so the first live shadow draw does not compile HLSL in the render phase.
    assert( m_assets );
    assert( m_resources );
    m_shadowDepthShader = m_assets->CreateShader( *m_resources, "shader.shadow_depth" );
}


void Terrain::ReleaseRenderResources()
{
    m_terrainMesh.reset();
    m_terrainShader.reset();
    m_shadowDepthShader.reset();
}


void Terrain::ConfigureRenderStepSize()
{
    int requestedStep = Config().terrainGeometry.renderStepSize;
    requestedStep = (std::max)( 1, (std::min)( requestedStep, m_stepSize ) );

    const int renderRawExtent = m_mapSize - m_stepSize;
    int selectedStep = requestedStep;
    if ( renderRawExtent > 0 && ( renderRawExtent % selectedStep ) != 0 )
    {
        int bestStep = m_stepSize;
        int bestDelta = INT_MAX;
        for ( int candidate = 1; candidate <= m_stepSize; ++candidate )
        {
            if ( ( renderRawExtent % candidate ) != 0 )
            {
                continue;
            }

            const int delta = abs( candidate - requestedStep );
            if ( delta < bestDelta || ( delta == bestDelta && candidate < bestStep ) )
            {
                bestStep = candidate;
                bestDelta = delta;
            }
        }

        selectedStep = bestStep;
        fprintf( stderr,
                 "[terrain] terrain_render_step_size=%d adjusted to %d so the render mesh fits the physics terrain "
                 "extent.\n",
                 requestedStep,
                 selectedStep );
    }

    m_renderStepSize = selectedStep;
    m_renderPostsPerSide = renderRawExtent > 0 ? ( renderRawExtent / m_renderStepSize ) + 1 : 0;
}


void Terrain::BuildTerrain()
{
    int terrainPostCount = ( m_mapSize / m_stepSize ) * ( m_mapSize / m_stepSize );

    m_postData.resize( terrainPostCount );

    TranslatePostings();

    // Track the authored height range for diagnostics and normalization.
    m_maxTerrainHeight = -FLT_MAX;
    m_minTerrainHeight = FLT_MAX;
    for ( const auto& post : m_postData )
    {
        if ( post.vPosition.y > m_maxTerrainHeight )
        {
            m_maxTerrainHeight = post.vPosition.y;
        }
        if ( post.vPosition.y < m_minTerrainHeight )
        {
            m_minTerrainHeight = post.vPosition.y;
        }
    }

    BuildCollisionCache();
    GenerateNormals();
}


void Terrain::BuildCollisionCache()
{
    // Precompute the two triangle planes for every terrain quad. At runtime, a
    // physics query only needs to find the quad and choose triangle A or B, then
    // read its plane/normal directly. This turns repeated terrain collision
    // lookups into cheap table reads instead of geometry rebuilds.
    if ( m_isFlatSlope )
    {
        m_cachedCollisionData.clear();
        return;
    }

    int quadsPerSide = m_postsPerSide - 1;
    if ( quadsPerSide <= 0 )
    {
        m_cachedCollisionData.clear();
        return;
    }

    m_cachedCollisionData.resize( quadsPerSide * quadsPerSide );

    for ( int zPosting = 0; zPosting < quadsPerSide; ++zPosting )
    {
        for ( int xPosting = 0; xPosting < quadsPerSide; ++xPosting )
        {
            int targetQuadric = zPosting * m_postsPerSide + xPosting + m_postsPerSide;

            Triangle triA;
            triA.v1 = m_postData[targetQuadric].vPosition;
            triA.v2 = m_postData[targetQuadric - m_postsPerSide].vPosition;
            triA.v3 = m_postData[targetQuadric - m_postsPerSide + 1].vPosition;

            Triangle triB;
            triB.v1 = m_postData[targetQuadric].vPosition;
            triB.v2 = m_postData[targetQuadric - m_postsPerSide + 1].vPosition;
            triB.v3 = m_postData[targetQuadric + 1].vPosition;

            CachedQuadData& cached = m_cachedCollisionData[zPosting * quadsPerSide + xPosting];
            cached.m_triangleA.m_plane = GeometricMath::ComputePlane( triA );
            cached.m_triangleB.m_plane = GeometricMath::ComputePlane( triB );

            if ( cached.m_triangleA.m_plane.m_normal.y < 0.0f )
            {
                cached.m_triangleA.m_plane.m_normal = cached.m_triangleA.m_plane.m_normal * -1.0f;
                cached.m_triangleA.m_plane.m_distance *= -1.0f;
            }
            if ( cached.m_triangleB.m_plane.m_normal.y < 0.0f )
            {
                cached.m_triangleB.m_plane.m_normal = cached.m_triangleB.m_plane.m_normal * -1.0f;
                cached.m_triangleB.m_plane.m_distance *= -1.0f;
            }

            cached.m_triangleA.m_normal = cached.m_triangleA.m_plane.m_normal;
            cached.m_triangleB.m_normal = cached.m_triangleB.m_plane.m_normal;
        }
    }
}


int Terrain::GetQuadCacheIndex( float xPosition, float zPosition, bool& isTriangleA )
{
    float scaledStepSize = m_stepSize * Config().terrainGeometry.scale;
    int xPosting = static_cast<int>( floorf( zPosition / scaledStepSize ) );
    int zPosting = static_cast<int>( floorf( xPosition / scaledStepSize ) );
    int quadsPerSide = m_postsPerSide - 1;

    if ( xPosting < 0 || zPosting < 0 || xPosting >= quadsPerSide || zPosting >= quadsPerSide )
    {
        SB_FATAL( "Terrain",
                  "Coordinates out of terrain bounds in GetQuadCacheIndex: x=%.3f z=%.3f xPosting=%d "
                  "zPosting=%d quadsPerSide=%d.",
                  xPosition,
                  zPosition,
                  xPosting,
                  zPosting,
                  quadsPerSide );
    }

    float localZ = zPosition - ( xPosting * scaledStepSize );
    float localX = xPosition - ( zPosting * scaledStepSize );

    // Same split as LocatePolygon: triangle A when above the quad diagonal, or
    // exactly on the axis where the old gradient test was infinite.
    isTriangleA = ( localX <= TOLERANCE ) || ( ( scaledStepSize - localZ ) > localX );

    return zPosting * quadsPerSide + xPosting;
}


void Terrain::QueryCollisionData( float xPosition,
                                  float zPosition,
                                  float& outHeight,
                                  Vector3* outNormal,
                                  Plane* outPlane )
{
    if ( !IsInBounds( xPosition, zPosition ) )
    {
        SB_FATAL( "Terrain",
                  "Coordinates out of terrain bounds in QueryCollisionData: x=%.3f z=%.3f.",
                  xPosition,
                  zPosition );
    }

    QueryCollisionDataUnchecked( xPosition, zPosition, outHeight, outNormal, outPlane );
}


void Terrain::QueryCollisionDataUnchecked( float xPosition,
                                           float zPosition,
                                           float& outHeight,
                                           Vector3* outNormal,
                                           Plane* outPlane )
{
    // This is the main physics terrain lookup. It returns the Y height and, if
    // requested, the contact normal or full plane at a given X/Z point. Callers
    // that already checked bounds use this unchecked version in hot paths.
    if ( m_isFlatSlope )
    {
        outHeight = m_slopeBaseY + m_slopeX * xPosition + m_slopeZ * zPosition;
        if ( outNormal )
        {
            *outNormal = m_flatSlopeNormal;
        }
        if ( outPlane )
        {
            *outPlane = m_flatSlopePlane;
        }
        return;
    }

    bool isTriangleA = false;
    int cacheIndex = GetQuadCacheIndex( xPosition, zPosition, isTriangleA );
    const CachedTriangleData& cachedTriangle =
        isTriangleA ? m_cachedCollisionData[cacheIndex].m_triangleA : m_cachedCollisionData[cacheIndex].m_triangleB;

    const Plane& plane = cachedTriangle.m_plane;
    outHeight = ( plane.m_distance - plane.m_normal.x * xPosition - plane.m_normal.z * zPosition ) / plane.m_normal.y;

    if ( outNormal )
    {
        *outNormal = cachedTriangle.m_normal;
    }
    if ( outPlane )
    {
        *outPlane = plane;
    }
}


int Terrain::GetPixelHeightAt( int xCoord, int yCoord )
{
    return m_terrainData[xCoord + yCoord * m_mapSize];
}


float Terrain::SampleRenderHeightRaw( float rawX, float rawZ ) const
{
    if ( m_terrainData.empty() || m_mapSize <= 0 )
    {
        return 0.0f;
    }

    const float maxRawCoord = static_cast<float>( m_mapSize - 1 );
    rawX = (std::max)( 0.0f, (std::min)( rawX, maxRawCoord ) );
    rawZ = (std::max)( 0.0f, (std::min)( rawZ, maxRawCoord ) );

    const int x0 = static_cast<int>( floorf( rawX ) );
    const int z0 = static_cast<int>( floorf( rawZ ) );
    const int x1 = (std::min)( x0 + 1, m_mapSize - 1 );
    const int z1 = (std::min)( z0 + 1, m_mapSize - 1 );
    const float tx = rawX - static_cast<float>( x0 );
    const float tz = rawZ - static_cast<float>( z0 );

    const float h00 = static_cast<float>( m_terrainData[x0 + z0 * m_mapSize] );
    const float h10 = static_cast<float>( m_terrainData[x1 + z0 * m_mapSize] );
    const float h01 = static_cast<float>( m_terrainData[x0 + z1 * m_mapSize] );
    const float h11 = static_cast<float>( m_terrainData[x1 + z1 * m_mapSize] );

    const float h0 = h00 + ( h10 - h00 ) * tx;
    const float h1 = h01 + ( h11 - h01 ) * tx;
    return ( h0 + ( h1 - h0 ) * tz ) * Config().terrainGeometry.heightScale * Config().terrainGeometry.scale;
}


Vector3 Terrain::SampleRenderNormalRaw( float rawX, float rawZ ) const
{
    const float rawExtent = static_cast<float>( m_mapSize - m_stepSize );
    const float sampleStep = static_cast<float>( (std::max)( 1, m_renderStepSize ) );
    const float left = (std::max)( 0.0f, rawX - sampleStep );
    const float right = (std::min)( rawExtent, rawX + sampleStep );
    const float top = (std::max)( 0.0f, rawZ - sampleStep );
    const float bottom = (std::min)( rawExtent, rawZ + sampleStep );

    const float terrainScale = Config().terrainGeometry.scale;
    const float hLeft = SampleRenderHeightRaw( left, rawZ );
    const float hRight = SampleRenderHeightRaw( right, rawZ );
    const float hTop = SampleRenderHeightRaw( rawX, top );
    const float hBottom = SampleRenderHeightRaw( rawX, bottom );

    const Vector3 dx( ( right - left ) * terrainScale, hRight - hLeft, 0.0f );
    const Vector3 dz( 0.0f, hBottom - hTop, ( bottom - top ) * terrainScale );
    Vector3 normal = CrossProduct( dz, dx );

    if ( VectorMagSquared( normal ) <= TOLERANCE * TOLERANCE )
    {
        return Vector3( 0.0f, 1.0f, 0.0f );
    }

    normal.Normalise();
    if ( normal.y < 0.0f )
    {
        normal *= -1.0f;
    }
    return normal;
}


TerrainPost Terrain::BuildRenderPost( float rawX, float rawZ ) const
{
    TerrainPost post;
    const float terrainScale = Config().terrainGeometry.scale;
    post.vPosition.SetAll( rawX * terrainScale, SampleRenderHeightRaw( rawX, rawZ ), rawZ * terrainScale );
    post.vNormal = SampleRenderNormalRaw( rawX, rawZ );
    return post;
}


SbResult Terrain::LoadTerrainData( const char* sFileName )
{
    // Lane R: height-map files are config/scene-selected assets. Missing or
    // truncated bytes report a recoverable load failure at the scene boundary.
    if ( !sFileName || sFileName[0] == '\0' )
    {
        return SbResult::Failure( "World/Terrain", "Height map file path is empty." );
    }

    FILE* rawFile = nullptr;
    fopen_s( &rawFile, sFileName, "rb" );

    if ( !rawFile )
    {
        return SbResult::Failure( "World/Terrain", "Height map file not found: %s", sFileName );
    }
    FileHandle file( rawFile );

    m_terrainData.resize( m_mapSize * m_mapSize );

    const std::size_t expectedBytes = m_terrainData.size();
    const std::size_t bytesRead = fread( m_terrainData.data(), 1, m_terrainData.size(), file.get() );

    if ( bytesRead != expectedBytes || ferror( file.get() ) )
    {
        m_terrainData.clear();
        return SbResult::Failure( "World/Terrain",
                                  "Failed to read height map '%s' (%zu/%zu bytes).",
                                  sFileName,
                                  bytesRead,
                                  expectedBytes );
    }

    return SbResult::Success();
}


void Terrain::Render( const Matrix4& view,
                      const Matrix4& projection,
                      IRenderCommandContext& commands,
                      const float* lightPosition,
                      const float* clipPlane,
                      const SkullbonezCore::Basics::CinematicRenderConfig* cinematicOverride,
                      const ShadowFrameData* shadow,
                      const ShadowFrameData* detailShadow )
{
    if ( !m_terrainShader || !m_terrainMesh )
    {
        return;
    }

    m_terrainShader->Use();

    // Model matrix is identity (m_terrain vertices are in world space)
    Matrix4 model;
    m_terrainShader->SetMat4( "uModel", model );
    m_terrainShader->SetMat4( "uView", view );
    m_terrainShader->SetMat4( "uProjection", projection );
    assert( clipPlane );
    m_terrainShader->SetVec4( "uClipPlane", clipPlane[0], clipPlane[1], clipPlane[2], clipPlane[3] );

    // Transform light position to view space
    float lx = view.m[0] * lightPosition[0] + view.m[4] * lightPosition[1] + view.m[8] * lightPosition[2] +
               view.m[12] * lightPosition[3];
    float ly = view.m[1] * lightPosition[0] + view.m[5] * lightPosition[1] + view.m[9] * lightPosition[2] +
               view.m[13] * lightPosition[3];
    float lz = view.m[2] * lightPosition[0] + view.m[6] * lightPosition[1] + view.m[10] * lightPosition[2] +
               view.m[14] * lightPosition[3];
    const bool cinematicMode = cinematicOverride != nullptr;
    if ( cinematicMode )
    {
        const SkullbonezCore::Basics::CinematicRenderConfig& cinematic = *cinematicOverride;
        m_terrainShader->SetVec4( "uLightAmbient", 0.20f, 0.11f, 0.055f, 1.0f );
        m_terrainShader->SetVec4( "uLightDiffuse",
                                  cinematic.sunColorR * 1.45f,
                                  cinematic.sunColorG * 1.45f,
                                  cinematic.sunColorB * 1.45f,
                                  1.0f );
        m_terrainShader->SetVec4( "uMaterialAmbient", 0.34f, 0.28f, 0.20f, 1.0f );
        m_terrainShader->SetVec4( "uMaterialDiffuse", 0.74f, 0.62f, 0.42f, 1.0f );

        // These uniforms are read by the terrain vertex/fragment shaders. The
        // relief value is a visual morph slider only: it changes rendered vertex
        // height and lighting normals on the GPU, but it does not move the CPU
        // collision terrain or the balls sitting on it.
        m_terrainShader->SetVec4( "uCinematicTerrain",
                                  cinematic.terrainReliefEnabled ? 1.0f : 0.0f,
                                  cinematic.terrainRelief,
                                  cinematic.basinDepth,
                                  cinematic.basinRimLift );
        m_terrainShader->SetVec4( "uCinematicBasin",
                                  cinematic.basinCenterX,
                                  cinematic.basinCenterZ,
                                  cinematic.basinRadiusX + 80.0f,
                                  cinematic.basinRadiusZ + 60.0f );
        m_terrainShader->SetVec4( "uStyleModes",
                                  1.0f,
                                  static_cast<float>( cinematic.terrainMode ),
                                  static_cast<float>( cinematic.objectStyle ),
                                  static_cast<float>( cinematic.waterMode ) );
        m_terrainShader->SetVec4( "uTerrainTint",
                                  cinematic.terrainTintR,
                                  cinematic.terrainTintG,
                                  cinematic.terrainTintB,
                                  1.0f );
        m_terrainShader->SetVec4( "uTerrainAccent",
                                  cinematic.terrainAccentR,
                                  cinematic.terrainAccentG,
                                  cinematic.terrainAccentB,
                                  1.0f );
        m_terrainShader->SetVec4( "uTerrainGrid",
                                  cinematic.terrainGridScale,
                                  cinematic.terrainGridStrength,
                                  0.0f,
                                  0.0f );
    }
    else
    {
        const auto& ordinary = Config().ordinaryRender;
        m_terrainShader->SetVec4( "uLightAmbient",
                                  ordinary.skyAmbientR,
                                  ordinary.skyAmbientG,
                                  ordinary.skyAmbientB,
                                  ordinary.ambientStrength );
        m_terrainShader->SetVec4( "uLightDiffuse",
                                  ordinary.sunColorR * ordinary.sunIntensity,
                                  ordinary.sunColorG * ordinary.sunIntensity,
                                  ordinary.sunColorB * ordinary.sunIntensity,
                                  1.0f );
        m_terrainShader->SetVec4( "uMaterialAmbient",
                                  ordinary.groundAmbientR,
                                  ordinary.groundAmbientG,
                                  ordinary.groundAmbientB,
                                  1.0f );
        m_terrainShader->SetVec4( "uMaterialDiffuse", 1.0f, 1.0f, 1.0f, 1.0f );
        m_terrainShader->SetVec4( "uCinematicTerrain", 0.0f, 0.0f, 0.0f, 0.0f );
        m_terrainShader->SetVec4( "uCinematicBasin", 620.0f, 615.0f, 285.0f, 205.0f );
        m_terrainShader->SetVec4( "uStyleModes", 0.0f, 0.0f, 0.0f, 1.0f );
        m_terrainShader->SetVec4( "uTerrainTint", 0.78f, 0.60f, 0.38f, 1.0f );
        m_terrainShader->SetVec4( "uTerrainAccent", 0.20f, 0.09f, 0.02f, 0.0f );
        m_terrainShader->SetVec4( "uTerrainGrid", 46.0f, 0.0f, 0.0f, 0.0f );
    }
    m_terrainShader->SetVec4( "uLightPosition", lx, ly, lz, lightPosition[3] );
    ApplyShadowReceiverUniforms( *m_terrainShader, commands, shadow, shadow ? shadow->terrainReceives : false );
    ApplyDetailShadowReceiverUniforms( *m_terrainShader,
                                       commands,
                                       detailShadow,
                                       detailShadow ? detailShadow->objectsReceive : false );

    m_terrainMesh->Draw();
}


void Terrain::RenderShadowDepth( const Matrix4& lightView,
                                 const Matrix4& lightProjection,
                                 const SkullbonezCore::Basics::CinematicRenderConfig* cinematicOverride )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/TerrainCasters/DepthDraw" );

    if ( !m_shadowDepthShader )
    {
        EnsureShadowDepthResources();
    }
    if ( !m_shadowDepthShader || !m_terrainMesh )
    {
        return;
    }

    m_shadowDepthShader->Use();

    // Terrain vertices are already stored in world coordinates, so the model
    // matrix remains identity. The supplied view/projection are the light-space
    // matrices built by Run::BuildShadowFrameData.
    Matrix4 model;
    m_shadowDepthShader->SetMat4( "uModel", model );
    m_shadowDepthShader->SetMat4( "uView", lightView );
    m_shadowDepthShader->SetMat4( "uProjection", lightProjection );
    m_shadowDepthShader->SetVec4( "uClipPlane", 0.0f, 1.0f, 0.0f, 1.0e9f );

    if ( cinematicOverride )
    {
        // If the visible terrain is using render-only cinematic relief, the
        // shadow caster must apply the same vertex offset. Otherwise shadow edges
        // would be produced by the flat CPU terrain while the visible terrain is
        // displaced in the vertex shader.
        const SkullbonezCore::Basics::CinematicRenderConfig& cinematic = *cinematicOverride;
        m_shadowDepthShader->SetVec4( "uCinematicTerrain",
                                      cinematic.terrainReliefEnabled ? 1.0f : 0.0f,
                                      cinematic.terrainRelief,
                                      cinematic.basinDepth,
                                      cinematic.basinRimLift );
        m_shadowDepthShader->SetVec4( "uCinematicBasin",
                                      cinematic.basinCenterX,
                                      cinematic.basinCenterZ,
                                      cinematic.basinRadiusX + 80.0f,
                                      cinematic.basinRadiusZ + 60.0f );
    }
    else
    {
        // Normal render mode has no visual terrain relief, but the shader still
        // receives deterministic defaults so no stale uniforms leak in from a
        // prior cinematic scene.
        m_shadowDepthShader->SetVec4( "uCinematicTerrain", 0.0f, 0.0f, 0.0f, 0.0f );
        m_shadowDepthShader->SetVec4( "uCinematicBasin", 620.0f, 615.0f, 285.0f, 205.0f );
    }

    m_terrainMesh->Draw();
}


float Terrain::GetTerrainHeightAt( float xPosition, float zPosition, bool isFluidMin )
{
    float terrainHeight = 0.0f;
    QueryCollisionData( xPosition, zPosition, terrainHeight, nullptr, nullptr );

    if ( isFluidMin )
    {
        return ( terrainHeight < Config().worldForces.fluidHeight ) ? Config().worldForces.fluidHeight : terrainHeight;
    }
    else
    {
        return terrainHeight;
    }
}


Vector3 Terrain::GetTerrainNormalAt( float xPosition, float zPosition )
{
    float terrainHeight = 0.0f;
    Vector3 normal;
    QueryCollisionData( xPosition, zPosition, terrainHeight, &normal, nullptr );
    return normal;
}


void Terrain::GetTerrainHeightAndNormalAt( float xPosition, float zPosition, float& outHeight, Vector3& outNormal )
{
    QueryCollisionData( xPosition, zPosition, outHeight, &outNormal, nullptr );
}


void Terrain::GetTerrainHeightAndPlaneAt( float xPosition, float zPosition, float& outHeight, Plane& outPlane )
{
    QueryCollisionDataUnchecked( xPosition, zPosition, outHeight, nullptr, &outPlane );
}


bool Terrain::IsInBounds( float xPosition, float zPosition )
{
    if ( m_isFlatSlope )
    {
        return ( xPosition >= 0.0f && xPosition < FLAT_SLOPE_EXTENT && zPosition >= 0.0f &&
                 zPosition < FLAT_SLOPE_EXTENT );
    }

    /*
        Justification for not allowing coordinates to the absolute outer bound:
        -----------------------------------------------------------------------
        It is arguable that a point would be in bounds of the m_terrain if it was
        equal to the scaled terrain extent. This may be true on
        a physical level, however, this can cause major problems for the
        Terrain::LocatePolygon method as it uses:
        floor(xPosition / scaledStepSize) and
        floor(zPosition / scaledStepSize)
        to determine which m_terrain quadric the point is in - you can only imagine
        what happens when the xPosition or the zPosition are equal to the upper
        bound of the m_terrain - the quadric is set to something that does not exist
        and all hell breaks loose (i.e. hours of debugging).

        So, who cares if you cant move to the absolute outer bound of the m_terrain,
        just move to the abolsute outer bound minus the smallest possible fraction
        of a float possible instead.
    */

    return ( ( xPosition >= 0.0f ) && ( zPosition >= 0.0f ) &&
             ( xPosition < m_terrainSizeWorldCoords * Config().terrainGeometry.scale ) &&
             ( zPosition < m_terrainSizeWorldCoords * Config().terrainGeometry.scale ) );
}


XZBounds Terrain::GetXZBounds()
{
    XZBounds bounds;

    if ( m_isFlatSlope )
    {
        bounds.m_xMin = 0.0f;
        bounds.m_zMin = 0.0f;
        bounds.m_xMax = FLAT_SLOPE_EXTENT;
        bounds.m_zMax = FLAT_SLOPE_EXTENT;
        return bounds;
    }

    bounds.m_xMin = 0.0f;
    bounds.m_zMin = 0.0f;
    bounds.m_xMax = m_terrainSizeWorldCoords * Config().terrainGeometry.scale;
    bounds.m_zMax = m_terrainSizeWorldCoords * Config().terrainGeometry.scale;

    return bounds;
}


Triangle Terrain::LocatePolygon( float xPosition, float zPosition )
{
    if ( !IsInBounds( xPosition, zPosition ) )
    {
        SB_FATAL( "Terrain",
                  "Coordinates out of terrain bounds in LocatePolygon: x=%.3f z=%.3f.",
                  xPosition,
                  zPosition );
    }

    if ( m_isFlatSlope )
    {
        // Analytic flat-slope terrain returns three points on the plane
        // y = m_slopeBaseY + m_slopeX*x + m_slopeZ*z.
        // Winding order: CCW from above so ComputePlane produces an upward-facing normal (n.y > 0)
        Triangle tri;
        float y0 = m_slopeBaseY + m_slopeX * xPosition + m_slopeZ * zPosition;
        float y2 = m_slopeBaseY + m_slopeX * xPosition + m_slopeZ * ( zPosition + 100.0f );
        float y1 = m_slopeBaseY + m_slopeX * ( xPosition + 100.0f ) + m_slopeZ * zPosition;
        tri.v1 = Vector3( xPosition, y0, zPosition );
        tri.v2 = Vector3( xPosition, y2, zPosition + 100.0f ); // +Z first
        tri.v3 = Vector3( xPosition + 100.0f, y1, zPosition ); // +X second
        return tri;
    }

    // NOTE:  X and Z params are switched in this method to account for world
    // co-ordinate space find which quadric we are in (treat m_terrain as orthagonal
    // XZ projection to locate the quadric)
    int xPosting = static_cast<int>( floorf( zPosition / ( m_stepSize * Config().terrainGeometry.scale ) ) );
    int zPosting = static_cast<int>( floorf( xPosition / ( m_stepSize * Config().terrainGeometry.scale ) ) );

    // Use the bottom-right post as the target quad so the A/B split below can
    // work in one local coordinate frame.
    int targetQuadric = zPosting * m_postsPerSide + xPosting + m_postsPerSide;

    float scaledStepSize = m_stepSize * Config().terrainGeometry.scale;

    // NOTE:  X and Z params are switched in this method to account for world
    // co-ordinate space make our X and Z positions relative to the target quadric
    // (as we are essentially generating a 2d vector relative to the bottom right
    // post of the target quadric, the xRelativePosition needs to be negated)
    float xRelativePosition = -( scaledStepSize - ( fmodf( zPosition, scaledStepSize ) ) );
    float zRelativePosition = fmodf( xPosition, scaledStepSize );

    // vars to help safely determine the gradient of the vector expressed by
    // xRelativePosition and zRelativePosition
    float gradient = 0.0f;
    bool isGradientInfinite = false;

    // test to see if rise is infinitely greater than run
    if ( !zRelativePosition )
    {
        isGradientInfinite = true;
    }

    // avoid a division by zero
    if ( !isGradientInfinite )
    {
        gradient = xRelativePosition / zRelativePosition;
    }

    // triangle structure for the target polygon
    Triangle targetPolygon;
    ZeroMemory( &targetPolygon, sizeof( targetPolygon ) );

    /*
        The following test checks to see if triangle A or B has been hit

        |\---|						 \										  |
        | \ A|						  \  <- grad = -1   ------- <- grad = 0   | <- grad = infinite
        |B \ |						   \									  |
        |---\|<- object space origin    \									  |

        (NOTE: The gradient of the cross section is equal to -1)
    */
    if ( isGradientInfinite || gradient < -1.0f )
    {
        // TRIANGLE A
        targetPolygon.v1 = m_postData[targetQuadric].vPosition;
        targetPolygon.v2 = m_postData[targetQuadric - m_postsPerSide].vPosition;
        targetPolygon.v3 = m_postData[targetQuadric - m_postsPerSide + 1].vPosition;
    }
    else
    {
        // TRIANGLE B
        targetPolygon.v1 = m_postData[targetQuadric].vPosition;
        targetPolygon.v2 = m_postData[targetQuadric - m_postsPerSide + 1].vPosition;
        targetPolygon.v3 = m_postData[targetQuadric + 1].vPosition;
    }

    return targetPolygon;
}


void Terrain::TranslatePostings()
{
    int indexCounter = 0;

    for ( int X = 0; X < m_mapSize; X += m_stepSize )
    {
        for ( int Z = 0; Z < m_mapSize; Z += m_stepSize )
        {
            m_postData[indexCounter].vPosition.SetAll( static_cast<float>( X ) * Config().terrainGeometry.scale,
                                                       static_cast<float>( GetPixelHeightAt( X, Z ) ) *
                                                           Config().terrainGeometry.heightScale *
                                                           Config().terrainGeometry.scale,
                                                       static_cast<float>( Z ) * Config().terrainGeometry.scale );

            ++indexCounter;
        }
    }
}


void Terrain::GenerateNormals()
{
    // flags to indicate special cases
    bool isFirstRow = true;
    bool isFinalRow = false;
    bool isFirstCol = true;
    bool isFinalCol = false;

    for ( int row = 0; row < m_postsPerSide; ++row )
    {
        if ( row > 0 )
        {
            isFirstRow = false;
        }

        if ( row == m_postsPerSide - 1 )
        {
            isFinalRow = true;
        }

        for ( int col = 0; col < m_postsPerSide; ++col )
        {
            int postingIndex = row * m_postsPerSide + col;

            m_postData[postingIndex].vNormal.Zero();

            if ( col == 0 )
            {
                isFirstCol = true;
            }
            else
            {
                isFirstCol = false;
            }

            if ( col == m_postsPerSide - 1 )
            {
                isFinalCol = true;
            }
            else
            {
                isFinalCol = false;
            }

            if ( isFirstCol )
            {
                // x 0 0 0  - we are an 'x'
                // x 0 0 0
                // x 0 0 0
                // x 0 0 0

                if ( isFirstRow )
                {
                    // x 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 0

                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    rightPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;

                    // right-down m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );
                }
                else if ( isFinalRow )
                {
                    // 0 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // x 0 0 0

                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;
                    Vector3 topRightPost = m_postData[postingIndex - m_postsPerSide + 1].vPosition;
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    topPost -= m_postData[postingIndex].vPosition;
                    topRightPost -= m_postData[postingIndex].vPosition;
                    rightPost -= m_postData[postingIndex].vPosition;

                    // top-top-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );
                }
                else
                {
                    // 0 0 0 0  - we are an 'x'
                    // x 0 0 0
                    // x 0 0 0
                    // 0 0 0 0

                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;
                    Vector3 topRightPost = m_postData[postingIndex - m_postsPerSide + 1].vPosition;
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    topPost -= m_postData[postingIndex].vPosition;
                    topRightPost -= m_postData[postingIndex].vPosition;
                    rightPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;

                    // top-top-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );

                    // right-down m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );
                }
            }
            else if ( isFinalCol )
            {
                // 0 0 0 x  - we are an 'x'
                // 0 0 0 x
                // 0 0 0 x
                // 0 0 0 x

                if ( isFirstRow )
                {
                    // 0 0 0 x  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 0

                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;
                    Vector3 downLeftPost = m_postData[postingIndex + m_postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;
                    downLeftPost -= m_postData[postingIndex].vPosition;

                    // down-down-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
                else if ( isFinalRow )
                {
                    // 0 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 x

                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    topPost -= m_postData[postingIndex].vPosition;

                    // top-left m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );
                }
                else
                {
                    // 0 0 0 0  - we are an 'x'
                    // 0 0 0 x
                    // 0 0 0 x
                    // 0 0 0 0

                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;
                    Vector3 downLeftPost = m_postData[postingIndex + m_postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    topPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;
                    downLeftPost -= m_postData[postingIndex].vPosition;

                    // top-left m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );

                    // down-down-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
            }
            else
            {
                // 0 x x 0  - we are an 'x'
                // 0 x x 0
                // 0 x x 0
                // 0 x x 0

                if ( isFirstRow )
                {
                    // 0 x x 0  - we are an 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 0

                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;
                    Vector3 downLeftPost = m_postData[postingIndex + m_postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    rightPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;
                    downLeftPost -= m_postData[postingIndex].vPosition;

                    // right-down m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );

                    // down-down-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
                else if ( isFinalRow )
                {
                    // 0 0 0 0  - we are an 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 x x 0

                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;
                    Vector3 topRightPost = m_postData[postingIndex - m_postsPerSide + 1].vPosition;
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    topPost -= m_postData[postingIndex].vPosition;
                    topRightPost -= m_postData[postingIndex].vPosition;
                    rightPost -= m_postData[postingIndex].vPosition;

                    // top-left m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );

                    // top-top-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );
                }
                else
                {
                    // 0 0 0 0  - we are an 'x'
                    // 0 x x 0
                    // 0 x x 0
                    // 0 0 0 0

                    Vector3 leftPost = m_postData[postingIndex - 1].vPosition;
                    Vector3 topPost = m_postData[postingIndex - m_postsPerSide].vPosition;
                    Vector3 topRightPost = m_postData[postingIndex - m_postsPerSide + 1].vPosition;
                    Vector3 rightPost = m_postData[postingIndex + 1].vPosition;
                    Vector3 downPost = m_postData[postingIndex + m_postsPerSide].vPosition;
                    Vector3 downLeftPost = m_postData[postingIndex + m_postsPerSide - 1].vPosition;

                    // make neighbours relative to target post (conversion to polar coordinates)
                    leftPost -= m_postData[postingIndex].vPosition;
                    topPost -= m_postData[postingIndex].vPosition;
                    topRightPost -= m_postData[postingIndex].vPosition;
                    rightPost -= m_postData[postingIndex].vPosition;
                    downPost -= m_postData[postingIndex].vPosition;
                    downLeftPost -= m_postData[postingIndex].vPosition;

                    // top-left m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( leftPost, topPost );

                    // top-top-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topPost, topRightPost );

                    // top-right-right m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( topRightPost, rightPost );

                    // right-down m_normal (1/4 weight)
                    m_postData[postingIndex].vNormal += 0.25f * CrossProduct( rightPost, downPost );

                    // down-down-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downPost, downLeftPost );

                    // down-left-left m_normal (1/8 weight)
                    m_postData[postingIndex].vNormal += 0.125f * CrossProduct( downLeftPost, leftPost );
                }
            }

            // finally, normalise the m_normal
            m_postData[postingIndex].vNormal.Normalise();
        }
    }
}


void Terrain::BuildMesh()
{
    // 2 triangles per quad, 6 vertices each, 8 floats per vertex (pos3 + normal3 + texcoord2)
    int quadsPerSide = m_renderPostsPerSide - 1;
    int totalQuads = quadsPerSide * quadsPerSide;
    int totalVerts = totalQuads * 6;

    std::vector<float> vertexData;
    vertexData.reserve( static_cast<size_t>( totalVerts ) * 8 );

    fprintf( stdout,
             "Terrain render mesh: step=%d raw pixels, posts=%d x %d, vertices=%d\n",
             m_renderStepSize,
             m_renderPostsPerSide,
             m_renderPostsPerSide,
             totalVerts );

    for ( int row = 0; row < quadsPerSide; ++row )
    {
        for ( int col = 0; col < quadsPerSide; ++col )
        {
            const float rawX0 = static_cast<float>( row * m_renderStepSize );
            const float rawX1 = static_cast<float>( ( row + 1 ) * m_renderStepSize );
            const float rawZ0 = static_cast<float>( col * m_renderStepSize );
            const float rawZ1 = static_cast<float>( ( col + 1 ) * m_renderStepSize );

            float texCoordS = ( rawZ0 / static_cast<float>( m_mapSize ) ) * m_textureWrap;
            float texCoordT = ( rawX0 / static_cast<float>( m_mapSize ) ) * m_textureWrap;
            float texCoordSP1 = ( rawZ1 / static_cast<float>( m_mapSize ) ) * m_textureWrap;
            float texCoordTP1 = ( rawX1 / static_cast<float>( m_mapSize ) ) * m_textureWrap;

            const TerrainPost p00 = BuildRenderPost( rawX0, rawZ0 );
            const TerrainPost p10 = BuildRenderPost( rawX0, rawZ1 );
            const TerrainPost p01 = BuildRenderPost( rawX1, rawZ0 );
            const TerrainPost p11 = BuildRenderPost( rawX1, rawZ1 );

            // Helper lambda: push the same floating-point world position that
            // the render-only height sampler produces. Physics still queries
            // the coarse cached collision grid; this denser mesh is visual.
            auto pushVertex = [&]( const TerrainPost& p, float s, float t )
            {
                vertexData.push_back( p.vPosition.x );
                vertexData.push_back( p.vPosition.y );
                vertexData.push_back( p.vPosition.z );
                vertexData.push_back( p.vNormal.x );
                vertexData.push_back( p.vNormal.y );
                vertexData.push_back( p.vNormal.z );
                vertexData.push_back( s );
                vertexData.push_back( t );
            };

            // Triangle 1 (upper-left): v1, v2, v3
            pushVertex( p00, texCoordS, texCoordT );
            pushVertex( p10, texCoordSP1, texCoordT );
            pushVertex( p01, texCoordS, texCoordTP1 );

            // Triangle 2 (lower-right): v3, v2, v5
            pushVertex( p01, texCoordS, texCoordTP1 );
            pushVertex( p10, texCoordSP1, texCoordT );
            pushVertex( p11, texCoordSP1, texCoordTP1 );
        }
    }

    assert( m_resources );
    m_terrainMesh = m_resources->CreateMesh( vertexData.data(),
                                             totalVerts,
                                             true, // hasNormals
                                             true  // hasTexCoords
    );
}


void Terrain::BuildFlatSlopeMesh()
{
    // Generate a 40x40 quad grid over [0,1000] x [0,1000]
    // Height at each point: y = m_slopeBaseY + m_slopeX*x + m_slopeZ*z
    // Constant normal:       normalize(-m_slopeX, 1.0f, -m_slopeZ)

    const int gridN = 40;
    const float gridMax = FLAT_SLOPE_EXTENT;
    const float step = gridMax / static_cast<float>( gridN );
    const float textureWrap = 8.0f;

    float nLen = sqrtf( m_slopeX * m_slopeX + 1.0f + m_slopeZ * m_slopeZ );
    float nx = -m_slopeX / nLen;
    float ny = 1.0f / nLen;
    float nz = -m_slopeZ / nLen;

    int totalVerts = gridN * gridN * 6;
    std::vector<float> vertexData;
    vertexData.reserve( static_cast<size_t>( totalVerts ) * 8 );

    auto pushVert = [&]( float x, float z )
    {
        float y = m_slopeBaseY + m_slopeX * x + m_slopeZ * z;
        vertexData.push_back( x );
        vertexData.push_back( y );
        vertexData.push_back( z );
        vertexData.push_back( nx );
        vertexData.push_back( ny );
        vertexData.push_back( nz );
        vertexData.push_back( ( x / gridMax ) * textureWrap );
        vertexData.push_back( ( z / gridMax ) * textureWrap );
    };

    for ( int row = 0; row < gridN; ++row )
    {
        for ( int col = 0; col < gridN; ++col )
        {
            float x0 = col * step;
            float x1 = x0 + step;
            float z0 = row * step;
            float z1 = z0 + step;

            // Triangle 1 — CCW from above (+Y), front face up
            pushVert( x0, z0 );
            pushVert( x0, z1 );
            pushVert( x1, z0 );

            // Triangle 2 — CCW from above (+Y), front face up
            pushVert( x0, z1 );
            pushVert( x1, z1 );
            pushVert( x1, z0 );
        }
    }

    assert( m_resources );
    m_terrainMesh = m_resources->CreateMesh( vertexData.data(), totalVerts, true, true );
}
