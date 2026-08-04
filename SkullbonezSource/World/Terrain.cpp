/*
File: SkullbonezSource/World/Terrain.cpp
Purpose:
  Owns heightfield posts, detached physics cells, terrain meshes, and terrain
  rendering resources.

Summary:
  RAW height samples become one world-X-major post grid. Collision queries,
  cached physics planes, render meshes, shadows, and debug geometry all derive
  from that same grid so they cannot describe different surfaces.

Glossary:
  Post: One sampled terrain vertex with a world-space position and smoothed
  normal.
  Quad: Four adjacent posts split into two authored collision/render triangles.
  Physics terrain cell: Detached pair of triangle planes for one quad, borrowed
  by Physics without retaining the World owner.
  Render relief: Shader-only height displacement that must not alter the CPU
  collision surface.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - A degenerate accumulated render normal falls back to world +Y; terrain
    mesh construction never publishes NaN normals.

Related:
  - SkullbonezSource/World/Terrain.h
  - Agentic/Reference/physics-overview.md
*/
#include "Terrain.h"
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
#include "../Assets/AssetKeys.h"
#include "../Assets/AssetSystem.h"
#endif
#include "../Core/FatalError.h"
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
#include "../Core/Profiler.h"
#endif
#include "../Core/SbResult.h"
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
#include "../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../Rendering/DX12/MeshDX12.h"
#include "../Rendering/DX12/ShaderDX12.h"
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>


using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using SkullbonezCore::Core::SbResult;

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


#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
Terrain::Terrain( int mapSize, int stepSize, int textureWrap, const SkullbonezCore::Core::EngineConfig& config,
                  SkullbonezCore::Assets::AssetSystem& assets, Dx12ResourceBuilder& resources )
    : Terrain( mapSize, stepSize, textureWrap, config, &assets, &resources )
{
}
#endif


Terrain::Terrain( PhysicsOnlyHeightMapTag, int mapSize, int stepSize, int textureWrap,
                  const SkullbonezCore::Core::EngineConfig& config )
    : Terrain( mapSize, stepSize, textureWrap, config, nullptr, nullptr )
{
}


Terrain::Terrain( int mapSize, int stepSize, int textureWrap, const SkullbonezCore::Core::EngineConfig& config,
                  SkullbonezCore::Assets::AssetSystem* assets, Dx12ResourceBuilder* resources )
{
    m_mapSize = mapSize;
    m_stepSize = stepSize;
    m_textureWrap = textureWrap;
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
    m_config = &config;
    m_assets = assets;
    m_resources = resources;
}


SkullbonezCore::Core::SbResult Terrain::TryCreatePhysicsFromHeightMap( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                       const char* fileName, int mapSize, int stepSize,
                                                                       int textureWrap,
                                                                       const SkullbonezCore::Core::EngineConfig& config,
                                                                       std::unique_ptr<Terrain>& outTerrain )
{
    outTerrain.reset();
    std::unique_ptr<Terrain> terrain = std::make_unique<Terrain>( PhysicsOnlyHeightMapTag {}, mapSize, stepSize, textureWrap,
                                                                  config );

    const SkullbonezCore::Core::SbResult loadResult = terrain->LoadTerrainData( diagnostics, fileName );

    if ( !loadResult.Ok() )
    {
        return loadResult;
    }

    terrain->BuildTerrain();
    terrain->m_terrainData.clear();
    terrain->m_terrainData.shrink_to_fit();
    outTerrain = std::move( terrain );
    return SkullbonezCore::Core::SbResult::Success();
}


#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
SkullbonezCore::Core::SbResult
Terrain::TryCreateFromHeightMap( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* fileName, int mapSize,
                                 int stepSize, int textureWrap, const SkullbonezCore::Core::EngineConfig& config,
                                 SkullbonezCore::Assets::AssetSystem& assets, Dx12ResourceBuilder& resources,
                                 std::unique_ptr<Terrain>& outTerrain )
{

    // Concept: RAW terrain files are external asset input. The factory keeps
    // a failed load out of the scene owner and reports Lane R instead of
    // letting constructor exceptions escape through scene startup.
    outTerrain.reset();
    std::unique_ptr<Terrain> terrain = std::make_unique<Terrain>( mapSize, stepSize, textureWrap, config, assets,
                                                                  resources );

    const SkullbonezCore::Core::SbResult loadResult = terrain->LoadTerrainData( diagnostics, fileName );

    if ( !loadResult.Ok() )
    {
        return loadResult;
    }

    terrain->BuildTerrain();
    terrain->BuildMesh();

    // Lifetime: collision and every render-resource rebuild use m_postData.
    // The source RAW bytes are no longer needed after the authoritative posts
    // exist, so release their cold-load storage before gameplay begins.
    terrain->m_terrainData.clear();
    terrain->m_terrainData.shrink_to_fit();
    terrain->InitialiseTerrainShader();
    outTerrain = std::move( terrain );
    return SkullbonezCore::Core::SbResult::Success();
}
#endif


#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
Terrain::Terrain( float slopeBaseY, float slopeX, float slopeZ, const SkullbonezCore::Core::EngineConfig& config,
                  SkullbonezCore::Assets::AssetSystem& assets, Dx12ResourceBuilder& resources )
    : Terrain( slopeBaseY, slopeX, slopeZ, config, &assets, &resources )
{
}
#endif


Terrain::Terrain( float slopeBaseY, float slopeX, float slopeZ, const SkullbonezCore::Core::EngineConfig& config )
    : Terrain( slopeBaseY, slopeX, slopeZ, config, nullptr, nullptr )
{
}


Terrain::Terrain( float slopeBaseY, float slopeX, float slopeZ, const SkullbonezCore::Core::EngineConfig& config,
                  SkullbonezCore::Assets::AssetSystem* assets, Dx12ResourceBuilder* resources )
{
    m_mapSize = 0;
    m_stepSize = 0;
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
    m_config = &config;
    m_assets = assets;
    m_resources = resources;

    // Max height at the 4 corners of the flat slope play area
    float h00 = slopeBaseY;
    float h10 = slopeBaseY + slopeX * FLAT_SLOPE_EXTENT;
    float h01 = slopeBaseY + slopeZ * FLAT_SLOPE_EXTENT;
    float h11 = slopeBaseY + slopeX * FLAT_SLOPE_EXTENT + slopeZ * FLAT_SLOPE_EXTENT;
    m_maxTerrainHeight = (std::max)( (std::max)( h00, h10 ), (std::max)( h01, h11 ) );
    m_minTerrainHeight = (std::min)( (std::min)( h00, h10 ), (std::min)( h01, h11 ) );

#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )

    if ( m_resources && m_assets )
    {
        BuildFlatSlopeMesh();
        InitialiseTerrainShader();
    }
#endif
}


Terrain::~Terrain()
{
}


#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
void Terrain::BindRenderContexts( const SkullbonezCore::Core::EngineConfig& config,
                                  SkullbonezCore::Assets::AssetSystem& assets, Dx12ResourceBuilder& resources )
{

    // Lifetime: Terrain keeps these as rebuild-only borrows owned by Run and
    // refreshed by the render pass before lazy resource recreation.
    m_config = &config;
    m_assets = &assets;
    m_resources = &resources;
}


const SkullbonezCore::Core::EngineConfig& Terrain::Config() const
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
    m_terrainShader->SetVec4( "uLightAmbient", ordinary.skyAmbientR, ordinary.skyAmbientG, ordinary.skyAmbientB,
                              ordinary.ambientStrength );

    m_terrainShader->SetVec4( "uLightDiffuse", ordinary.sunColorR * ordinary.sunIntensity,
                              ordinary.sunColorG * ordinary.sunIntensity, ordinary.sunColorB * ordinary.sunIntensity, 1.0f );

    m_terrainShader->SetVec4( "uMaterialAmbient", ordinary.groundAmbientR, ordinary.groundAmbientG, ordinary.groundAmbientB,
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


void Terrain::EnsureRenderResources( const SkullbonezCore::Core::EngineConfig& config,
                                     SkullbonezCore::Assets::AssetSystem& assets, Dx12ResourceBuilder& resources )
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
#endif

#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
const SkullbonezCore::Core::EngineConfig& Terrain::Config() const
{
    assert( m_config );
    return *m_config;
}
#endif


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

    // Invariant: both cell loops stop at postsPerSide - 2. The target post is
    // the next-X/current-Z corner, so target, previous-X, previous-X/next-Z,
    // and next-Z all remain inside the BuildTerrain-sized post grid.

    for ( int worldXCell = 0; worldXCell < quadsPerSide; ++worldXCell )
    {

        for ( int worldZCell = 0; worldZCell < quadsPerSide; ++worldZCell )
        {
            int targetQuad = worldXCell * m_postsPerSide + worldZCell + m_postsPerSide;

            Triangle triA;
            triA.v1 = m_postData[targetQuad].vPosition;
            triA.v2 = m_postData[targetQuad - m_postsPerSide].vPosition;
            triA.v3 = m_postData[targetQuad - m_postsPerSide + 1].vPosition;

            Triangle triB;
            triB.v1 = m_postData[targetQuad].vPosition;
            triB.v2 = m_postData[targetQuad - m_postsPerSide + 1].vPosition;
            triB.v3 = m_postData[targetQuad + 1].vPosition;

            Physics::PhysicsTerrainCell& cached = m_cachedCollisionData[worldXCell * quadsPerSide + worldZCell];
            cached.triangleA = GeometricMath::ComputePlane( triA );
            cached.triangleB = GeometricMath::ComputePlane( triB );

            if ( cached.triangleA.m_normal.y < 0.0f )
            {
                cached.triangleA.m_normal = cached.triangleA.m_normal * -1.0f;
                cached.triangleA.m_distance *= -1.0f;
            }

            if ( cached.triangleB.m_normal.y < 0.0f )
            {
                cached.triangleB.m_normal = cached.triangleB.m_normal * -1.0f;
                cached.triangleB.m_distance *= -1.0f;
            }
        }
    }
}


SkullbonezCore::Physics::PhysicsTerrainView Terrain::PhysicsView() const noexcept
{
    SkullbonezCore::Physics::PhysicsTerrainView view;
    view.cells = m_cachedCollisionData;
    view.quadsPerSide = m_postsPerSide - 1;
    view.scaledStepSize = m_isFlatSlope ? 0.0f : m_stepSize * Config().terrainGeometry.scale;
    view.worldExtent = m_isFlatSlope ? 0.0f : m_terrainSizeWorldCoords * Config().terrainGeometry.scale;
    view.maxHeight = m_maxTerrainHeight;
    view.flatSlope = m_isFlatSlope;
    view.flatSlopeExtent = FLAT_SLOPE_EXTENT;
    view.slopeBaseY = m_slopeBaseY;
    view.slopeX = m_slopeX;
    view.slopeZ = m_slopeZ;
    view.flatSlopePlane = m_flatSlopePlane;
    return view;
}


void Terrain::QueryCollisionData( float xPosition, float zPosition, float& outHeight, Vector3* outNormal, Plane* outPlane )
{

    if ( !PhysicsView().IsInBounds( xPosition, zPosition ) )
    {
        SB_FATAL( "Terrain", "Coordinates out of terrain bounds in QueryCollisionData: x=%.3f z=%.3f.", xPosition,
                  zPosition );
    }

    QueryCollisionDataUnchecked( xPosition, zPosition, outHeight, outNormal, outPlane );
}


void Terrain::QueryCollisionDataUnchecked( float xPosition, float zPosition, float& outHeight, Vector3* outNormal,
                                           Plane* outPlane )
{

    // This is the main physics terrain lookup. It returns the Y height and, if
    // requested, the contact normal or full plane at a given X/Z point. Callers
    // that already checked bounds use this unchecked version in hot paths.
    Plane plane;
    PhysicsView().HeightAndPlaneAt( xPosition, zPosition, outHeight, plane );

    if ( outNormal )
    {
        *outNormal = plane.m_normal;
    }

    if ( outPlane )
    {
        *outPlane = plane;
    }
}


int Terrain::GetPixelHeightAt( int worldXCoordinate, int worldZCoordinate )
{
    return m_terrainData[worldXCoordinate + worldZCoordinate * m_mapSize];
}


SkullbonezCore::Core::SbResult Terrain::LoadTerrainData( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                         const char* fileName )
{

    // Lane R: height-map files are config/scene-selected assets. Missing or
    // truncated bytes report a recoverable load failure at the scene boundary.

    if ( !fileName || fileName[0] == '\0' )
    {
        return diagnostics.Failure( "World/Terrain", "Height map file path is empty." );
    }

    FILE* rawFile = nullptr;
    fopen_s( &rawFile, fileName, "rb" );

    if ( !rawFile )
    {
        return diagnostics.Failure( "World/Terrain", "Height map file not found: %s", fileName );
    }

    FileHandle file( rawFile );

    m_terrainData.resize( m_mapSize * m_mapSize );

    const std::size_t expectedBytes = m_terrainData.size();
    const std::size_t bytesRead = fread( m_terrainData.data(), 1, m_terrainData.size(), file.get() );

    if ( bytesRead != expectedBytes || ferror( file.get() ) )
    {
        m_terrainData.clear();
        return diagnostics.Failure( "World/Terrain", "Failed to read height map '%s' (%zu/%zu bytes).", fileName, bytesRead,
                                    expectedBytes );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
void Terrain::Render( const Matrix4& view, const Matrix4& projection, Dx12TextureOwner& textures, const float* lightPosition,
                      const float* clipPlane, const Rendering::PassRasterStateBucket& rasterState,
                      const SkullbonezCore::Core::CinematicRenderConfig* cinematicOverride, const ShadowFrameData* shadow,
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
        const SkullbonezCore::Core::CinematicRenderConfig& cinematic = *cinematicOverride;
        m_terrainShader->SetVec4( "uLightAmbient", 0.20f, 0.11f, 0.055f, 1.0f );
        m_terrainShader->SetVec4( "uLightDiffuse", cinematic.sunColorR * 1.45f, cinematic.sunColorG * 1.45f,
                                  cinematic.sunColorB * 1.45f, 1.0f );

        m_terrainShader->SetVec4( "uMaterialAmbient", 0.34f, 0.28f, 0.20f, 1.0f );
        m_terrainShader->SetVec4( "uMaterialDiffuse", 0.74f, 0.62f, 0.42f, 1.0f );

        // These uniforms are read by the terrain vertex/fragment shaders. The
        // relief value is a visual morph slider only: it changes rendered vertex
        // height and lighting normals on the GPU, but it does not move the CPU
        // collision terrain or the balls sitting on it.
        m_terrainShader->SetVec4( "uCinematicTerrain", cinematic.terrainReliefEnabled ? 1.0f : 0.0f, cinematic.terrainRelief,
                                  cinematic.basinDepth, cinematic.basinRimLift );

        m_terrainShader->SetVec4( "uCinematicBasin", cinematic.basinCenterX, cinematic.basinCenterZ,
                                  cinematic.basinRadiusX + 80.0f, cinematic.basinRadiusZ + 60.0f );

        m_terrainShader->SetVec4( "uStyleModes", 1.0f, static_cast<float>( cinematic.terrainMode ),
                                  static_cast<float>( cinematic.objectStyle ), static_cast<float>( cinematic.waterMode ) );

        m_terrainShader->SetVec4( "uTerrainTint", cinematic.terrainTintR, cinematic.terrainTintG, cinematic.terrainTintB,
                                  1.0f );

        m_terrainShader->SetVec4( "uTerrainAccent", cinematic.terrainAccentR, cinematic.terrainAccentG,
                                  cinematic.terrainAccentB, 1.0f );

        m_terrainShader->SetVec4( "uTerrainGrid", cinematic.terrainGridScale, cinematic.terrainGridStrength, 0.0f, 0.0f );
    }
    else
    {
        const auto& ordinary = Config().ordinaryRender;
        m_terrainShader->SetVec4( "uLightAmbient", ordinary.skyAmbientR, ordinary.skyAmbientG, ordinary.skyAmbientB,
                                  ordinary.ambientStrength );

        m_terrainShader->SetVec4( "uLightDiffuse", ordinary.sunColorR * ordinary.sunIntensity,
                                  ordinary.sunColorG * ordinary.sunIntensity, ordinary.sunColorB * ordinary.sunIntensity,
                                  1.0f );

        m_terrainShader->SetVec4( "uMaterialAmbient", ordinary.groundAmbientR, ordinary.groundAmbientG,
                                  ordinary.groundAmbientB, 1.0f );

        m_terrainShader->SetVec4( "uMaterialDiffuse", 1.0f, 1.0f, 1.0f, 1.0f );
        m_terrainShader->SetVec4( "uCinematicTerrain", 0.0f, 0.0f, 0.0f, 0.0f );
        m_terrainShader->SetVec4( "uCinematicBasin", 620.0f, 615.0f, 285.0f, 205.0f );
        m_terrainShader->SetVec4( "uStyleModes", 0.0f, 0.0f, 0.0f, 1.0f );
        m_terrainShader->SetVec4( "uTerrainTint", 0.78f, 0.60f, 0.38f, 1.0f );
        m_terrainShader->SetVec4( "uTerrainAccent", 0.20f, 0.09f, 0.02f, 0.0f );
        m_terrainShader->SetVec4( "uTerrainGrid", 46.0f, 0.0f, 0.0f, 0.0f );
    }

    m_terrainShader->SetVec4( "uLightPosition", lx, ly, lz, lightPosition[3] );
    ApplyShadowReceiverUniforms( *m_terrainShader, textures, shadow, shadow ? shadow->terrainReceives : false );
    ApplyDetailShadowReceiverUniforms( *m_terrainShader, textures, detailShadow,
                                       detailShadow ? detailShadow->objectsReceive : false );

    m_terrainMesh->Draw( rasterState );
}


void Terrain::RenderShadowDepth( Core::Profiler*, const Matrix4& lightView, const Matrix4& lightProjection,
                                 const Rendering::PassRasterStateBucket& rasterState,
                                 const SkullbonezCore::Core::CinematicRenderConfig* cinematicOverride )
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
        const SkullbonezCore::Core::CinematicRenderConfig& cinematic = *cinematicOverride;
        m_shadowDepthShader->SetVec4( "uCinematicTerrain", cinematic.terrainReliefEnabled ? 1.0f : 0.0f,
                                      cinematic.terrainRelief, cinematic.basinDepth, cinematic.basinRimLift );

        m_shadowDepthShader->SetVec4( "uCinematicBasin", cinematic.basinCenterX, cinematic.basinCenterZ,
                                      cinematic.basinRadiusX + 80.0f, cinematic.basinRadiusZ + 60.0f );
    }
    else
    {

        // Normal render mode has no visual terrain relief, but the shader still
        // receives deterministic defaults so no stale uniforms leak in from a
        // prior cinematic scene.
        m_shadowDepthShader->SetVec4( "uCinematicTerrain", 0.0f, 0.0f, 0.0f, 0.0f );
        m_shadowDepthShader->SetVec4( "uCinematicBasin", 620.0f, 615.0f, 285.0f, 205.0f );
    }

    m_terrainMesh->Draw( rasterState );
}
#endif


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
    return PhysicsView().IsInBounds( xPosition, zPosition );
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

    if ( m_isFlatSlope )
    {

        if ( !IsInBounds( xPosition, zPosition ) )
        {
            SB_FATAL( "Terrain", "Coordinates out of terrain bounds in LocatePolygon: x=%.3f z=%.3f.", xPosition,
                      zPosition );
        }

        // Concept: analytic flat-slope terrain returns three points on the plane
        // y = m_slopeBaseY + m_slopeX*x + m_slopeZ*z.
        // Invariant: +Z then +X is counter-clockwise from above, so
        // ComputePlane produces an upward-facing normal.
        Triangle tri;
        float y0 = m_slopeBaseY + m_slopeX * xPosition + m_slopeZ * zPosition;
        float y2 = m_slopeBaseY + m_slopeX * xPosition + m_slopeZ * ( zPosition + 100.0f );
        float y1 = m_slopeBaseY + m_slopeX * ( xPosition + 100.0f ) + m_slopeZ * zPosition;
        tri.v1 = Vector3( xPosition, y0, zPosition );
        tri.v2 = Vector3( xPosition, y2, zPosition + 100.0f ); // +Z first
        tri.v3 = Vector3( xPosition + 100.0f, y1, zPosition ); // +X second

        return tri;
    }

    const float scaledStepSize = m_stepSize * Config().terrainGeometry.scale;

    if ( !std::isfinite( xPosition ) || !std::isfinite( zPosition ) || !std::isfinite( scaledStepSize ) )
    {
        SB_FATAL( "Terrain", "Terrain polygon query is not finite: x=%.3f z=%.3f scaledStepSize=%.3f.", xPosition, zPosition,
                  scaledStepSize );
    }

    if ( m_postsPerSide < 2 || scaledStepSize <= 0.0f )
    {
        SB_FATAL( "Terrain", "Terrain polygon grid is invalid: postsPerSide=%d scaledStepSize=%.3f.", m_postsPerSide,
                  scaledStepSize );
    }

    // Terrain posts are stored world-X-major, matching TranslatePostings.
    const float worldZCellFloat = floorf( zPosition / scaledStepSize );
    const float worldXCellFloat = floorf( xPosition / scaledStepSize );

    if ( !std::isfinite( worldXCellFloat ) || !std::isfinite( worldZCellFloat ) ||
         worldXCellFloat < static_cast<float>( ( std::numeric_limits<int>::min )() ) ||
         worldZCellFloat < static_cast<float>( ( std::numeric_limits<int>::min )() ) ||
         worldXCellFloat >= static_cast<float>( ( std::numeric_limits<int>::max )() ) ||
         worldZCellFloat >= static_cast<float>( ( std::numeric_limits<int>::max )() ) )
    {
        SB_FATAL( "Terrain", "Terrain polygon cell is not representable: x=%.3f z=%.3f.", xPosition, zPosition );
    }

    const int worldZCell = static_cast<int>( worldZCellFloat );
    const int worldXCell = static_cast<int>( worldXCellFloat );
    const int quadsPerSide = m_postsPerSide - 1;

    if ( worldXCell < 0 || worldZCell < 0 || worldXCell >= quadsPerSide || worldZCell >= quadsPerSide )
    {
        SB_FATAL( "Terrain", "Terrain polygon cell out of range: x=%.3f z=%.3f worldXCell=%d worldZCell=%d quadsPerSide=%d.",
                  xPosition, zPosition, worldXCell, worldZCell, quadsPerSide );
    }

    // Invariant: the strict cell guard caps both cells at postsPerSide - 2.
    // Therefore the four named indices span [0, postsPerSide^2 - 1]. The
    // storage-size guard pins that derivation to the actual backing vector.
    const std::size_t postsPerSide = static_cast<std::size_t>( m_postsPerSide );

    if ( postsPerSide > ( std::numeric_limits<std::size_t>::max )() / postsPerSide )
    {
        SB_FATAL( "Terrain", "Terrain polygon post-grid size overflow: postsPerSide=%zu.", postsPerSide );
    }

    const std::size_t worldXCellIndex = static_cast<std::size_t>( worldXCell );
    const std::size_t worldZCellIndex = static_cast<std::size_t>( worldZCell );
    const std::size_t targetPostIndex = worldXCellIndex * postsPerSide + worldZCellIndex + postsPerSide;
    const std::size_t previousXPostIndex = targetPostIndex - postsPerSide;
    const std::size_t previousXNextZPostIndex = previousXPostIndex + 1u;
    const std::size_t targetNextZPostIndex = targetPostIndex + 1u;

    if ( targetNextZPostIndex >= m_postData.size() )
    {
        SB_FATAL( "Terrain", "Terrain polygon post window out of range: first=%zu target=%zu last=%zu postCount=%zu.",
                  previousXPostIndex, targetPostIndex, targetNextZPostIndex, m_postData.size() );
    }

    // Express the query relative to the target quad's next-X/current-Z post.
    // The Z component points back into the quad and is therefore negative.
    float negativeLocalZ = -( scaledStepSize - ( fmodf( zPosition, scaledStepSize ) ) );
    float localX = fmodf( xPosition, scaledStepSize );

    // Why: the authored diagonal split is expressed as a local X/Z gradient.
    // Keep the vertical case explicit so the comparison never divides by zero.
    float gradient = 0.0f;
    bool isGradientInfinite = false;

    if ( localX == 0.0f )
    {
        isGradientInfinite = true;
    }

    if ( !isGradientInfinite )
    {
        gradient = negativeLocalZ / localX;
    }

    Triangle targetPolygon {};

    // Invariant: gradient -1 is the quad diagonal. The strict comparison and
    // vertical-case choice preserve the same triangle on the diagonal as the
    // cached PhysicsTerrainView lookup.

    if ( isGradientInfinite || gradient < -1.0f )
    {

        // TRIANGLE A
        targetPolygon.v1 = m_postData[targetPostIndex].vPosition;
        targetPolygon.v2 = m_postData[previousXPostIndex].vPosition;
        targetPolygon.v3 = m_postData[previousXNextZPostIndex].vPosition;
    }
    else
    {

        // TRIANGLE B
        targetPolygon.v1 = m_postData[targetPostIndex].vPosition;
        targetPolygon.v2 = m_postData[previousXNextZPostIndex].vPosition;
        targetPolygon.v3 = m_postData[targetNextZPostIndex].vPosition;
    }

    return targetPolygon;
}


void Terrain::TranslatePostings()
{
    int indexCounter = 0;

    // Invariant: the loaded heightfield contract makes map size an exact
    // multiple of step size, so these world-X/world-Z loops write exactly
    // postsPerSide * postsPerSide entries into the BuildTerrain-sized post vector.

    for ( int worldXCoordinate = 0; worldXCoordinate < m_mapSize; worldXCoordinate += m_stepSize )
    {

        for ( int worldZCoordinate = 0; worldZCoordinate < m_mapSize; worldZCoordinate += m_stepSize )
        {
            m_postData[indexCounter]
                .vPosition.SetAll( static_cast<float>( worldXCoordinate ) * Config().terrainGeometry.scale,
                                   static_cast<float>( GetPixelHeightAt( worldXCoordinate, worldZCoordinate ) ) *
                                       Config().terrainGeometry.heightScale * Config().terrainGeometry.scale,
                                   static_cast<float>( worldZCoordinate ) * Config().terrainGeometry.scale );

            ++indexCounter;
        }
    }
}


void Terrain::GenerateNormals()
{

    // Concept: each post normal is the weighted sum of the adjacent triangle
    // face normals. Corners, edges, and interior posts have different valid
    // neighborhoods.
    // Invariant: the outer loop advances world X, the inner loop advances world
    // Z, and postIndex = worldXPost * m_postsPerSide + worldZPost. The four
    // boundary flags admit only neighboring-post reads that exist in that
    // world-X-major grid.
    bool isFirstXPost = true;
    bool isFinalXPost = false;
    bool isFirstZPost = true;
    bool isFinalZPost = false;

    for ( int worldXPost = 0; worldXPost < m_postsPerSide; ++worldXPost )
    {

        if ( worldXPost > 0 )
        {
            isFirstXPost = false;
        }

        if ( worldXPost == m_postsPerSide - 1 )
        {
            isFinalXPost = true;
        }

        for ( int worldZPost = 0; worldZPost < m_postsPerSide; ++worldZPost )
        {
            const int postIndex = worldXPost * m_postsPerSide + worldZPost;

            m_postData[postIndex].vNormal.Zero();

            if ( worldZPost == 0 )
            {
                isFirstZPost = true;
            }
            else
            {
                isFirstZPost = false;
            }

            if ( worldZPost == m_postsPerSide - 1 )
            {
                isFinalZPost = true;
            }
            else
            {
                isFinalZPost = false;
            }

            if ( isFirstZPost )
            {

                // x 0 0 0  - we are an 'x'
                // x 0 0 0
                // x 0 0 0
                // x 0 0 0

                if ( isFirstXPost )
                {

                    // x 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 0

                    Vector3 nextZPost = m_postData[postIndex + 1].vPosition;
                    Vector3 nextXPost = m_postData[postIndex + m_postsPerSide].vPosition;

                    // Translate neighboring posts into vectors from the target post.
                    nextZPost -= m_postData[postIndex].vPosition;
                    nextXPost -= m_postData[postIndex].vPosition;

                    // +Z/+X face normal (1/4 weight)
                    m_postData[postIndex].vNormal += 0.25f * CrossProduct( nextZPost, nextXPost );
                }
                else if ( isFinalXPost )
                {

                    // 0 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // x 0 0 0

                    Vector3 previousXPost = m_postData[postIndex - m_postsPerSide].vPosition;
                    Vector3 previousXNextZPost = m_postData[postIndex - m_postsPerSide + 1].vPosition;
                    Vector3 nextZPost = m_postData[postIndex + 1].vPosition;

                    // Translate neighboring posts into vectors from the target post.
                    previousXPost -= m_postData[postIndex].vPosition;
                    previousXNextZPost -= m_postData[postIndex].vPosition;
                    nextZPost -= m_postData[postIndex].vPosition;

                    // -X/-X+Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( previousXPost, previousXNextZPost );

                    // -X+Z/+Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( previousXNextZPost, nextZPost );
                }
                else
                {

                    // 0 0 0 0  - we are an 'x'
                    // x 0 0 0
                    // x 0 0 0
                    // 0 0 0 0

                    Vector3 previousXPost = m_postData[postIndex - m_postsPerSide].vPosition;
                    Vector3 previousXNextZPost = m_postData[postIndex - m_postsPerSide + 1].vPosition;
                    Vector3 nextZPost = m_postData[postIndex + 1].vPosition;
                    Vector3 nextXPost = m_postData[postIndex + m_postsPerSide].vPosition;

                    // Translate neighboring posts into vectors from the target post.
                    previousXPost -= m_postData[postIndex].vPosition;
                    previousXNextZPost -= m_postData[postIndex].vPosition;
                    nextZPost -= m_postData[postIndex].vPosition;
                    nextXPost -= m_postData[postIndex].vPosition;

                    // -X/-X+Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( previousXPost, previousXNextZPost );

                    // -X+Z/+Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( previousXNextZPost, nextZPost );

                    // +Z/+X face normal (1/4 weight)
                    m_postData[postIndex].vNormal += 0.25f * CrossProduct( nextZPost, nextXPost );
                }
            }
            else if ( isFinalZPost )
            {

                // 0 0 0 x  - we are an 'x'
                // 0 0 0 x
                // 0 0 0 x
                // 0 0 0 x

                if ( isFirstXPost )
                {

                    // 0 0 0 x  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 0

                    Vector3 previousZPost = m_postData[postIndex - 1].vPosition;
                    Vector3 nextXPost = m_postData[postIndex + m_postsPerSide].vPosition;
                    Vector3 nextXPreviousZPost = m_postData[postIndex + m_postsPerSide - 1].vPosition;

                    // Translate neighboring posts into vectors from the target post.
                    previousZPost -= m_postData[postIndex].vPosition;
                    nextXPost -= m_postData[postIndex].vPosition;
                    nextXPreviousZPost -= m_postData[postIndex].vPosition;

                    // +X/+X-Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( nextXPost, nextXPreviousZPost );

                    // +X-Z/-Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( nextXPreviousZPost, previousZPost );
                }
                else if ( isFinalXPost )
                {

                    // 0 0 0 0  - we are 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 x

                    Vector3 previousZPost = m_postData[postIndex - 1].vPosition;
                    Vector3 previousXPost = m_postData[postIndex - m_postsPerSide].vPosition;

                    // Translate neighboring posts into vectors from the target post.
                    previousZPost -= m_postData[postIndex].vPosition;
                    previousXPost -= m_postData[postIndex].vPosition;

                    // -Z/-X face normal (1/4 weight)
                    m_postData[postIndex].vNormal += 0.25f * CrossProduct( previousZPost, previousXPost );
                }
                else
                {

                    // 0 0 0 0  - we are an 'x'
                    // 0 0 0 x
                    // 0 0 0 x
                    // 0 0 0 0

                    Vector3 previousZPost = m_postData[postIndex - 1].vPosition;
                    Vector3 previousXPost = m_postData[postIndex - m_postsPerSide].vPosition;
                    Vector3 nextXPost = m_postData[postIndex + m_postsPerSide].vPosition;
                    Vector3 nextXPreviousZPost = m_postData[postIndex + m_postsPerSide - 1].vPosition;

                    // Translate neighboring posts into vectors from the target post.
                    previousZPost -= m_postData[postIndex].vPosition;
                    previousXPost -= m_postData[postIndex].vPosition;
                    nextXPost -= m_postData[postIndex].vPosition;
                    nextXPreviousZPost -= m_postData[postIndex].vPosition;

                    // -Z/-X face normal (1/4 weight)
                    m_postData[postIndex].vNormal += 0.25f * CrossProduct( previousZPost, previousXPost );

                    // +X/+X-Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( nextXPost, nextXPreviousZPost );

                    // +X-Z/-Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( nextXPreviousZPost, previousZPost );
                }
            }
            else
            {

                // 0 x x 0  - we are an 'x'
                // 0 x x 0
                // 0 x x 0
                // 0 x x 0

                if ( isFirstXPost )
                {

                    // 0 x x 0  - we are an 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 0 0 0

                    Vector3 previousZPost = m_postData[postIndex - 1].vPosition;
                    Vector3 nextZPost = m_postData[postIndex + 1].vPosition;
                    Vector3 nextXPost = m_postData[postIndex + m_postsPerSide].vPosition;
                    Vector3 nextXPreviousZPost = m_postData[postIndex + m_postsPerSide - 1].vPosition;

                    // Translate neighboring posts into vectors from the target post.
                    previousZPost -= m_postData[postIndex].vPosition;
                    nextZPost -= m_postData[postIndex].vPosition;
                    nextXPost -= m_postData[postIndex].vPosition;
                    nextXPreviousZPost -= m_postData[postIndex].vPosition;

                    // +Z/+X face normal (1/4 weight)
                    m_postData[postIndex].vNormal += 0.25f * CrossProduct( nextZPost, nextXPost );

                    // +X/+X-Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( nextXPost, nextXPreviousZPost );

                    // +X-Z/-Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( nextXPreviousZPost, previousZPost );
                }
                else if ( isFinalXPost )
                {

                    // 0 0 0 0  - we are an 'x'
                    // 0 0 0 0
                    // 0 0 0 0
                    // 0 x x 0

                    Vector3 previousZPost = m_postData[postIndex - 1].vPosition;
                    Vector3 previousXPost = m_postData[postIndex - m_postsPerSide].vPosition;
                    Vector3 previousXNextZPost = m_postData[postIndex - m_postsPerSide + 1].vPosition;
                    Vector3 nextZPost = m_postData[postIndex + 1].vPosition;

                    // Translate neighboring posts into vectors from the target post.
                    previousZPost -= m_postData[postIndex].vPosition;
                    previousXPost -= m_postData[postIndex].vPosition;
                    previousXNextZPost -= m_postData[postIndex].vPosition;
                    nextZPost -= m_postData[postIndex].vPosition;

                    // -Z/-X face normal (1/4 weight)
                    m_postData[postIndex].vNormal += 0.25f * CrossProduct( previousZPost, previousXPost );

                    // -X/-X+Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( previousXPost, previousXNextZPost );

                    // -X+Z/+Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( previousXNextZPost, nextZPost );
                }
                else
                {

                    // 0 0 0 0  - we are an 'x'
                    // 0 x x 0
                    // 0 x x 0
                    // 0 0 0 0

                    Vector3 previousZPost = m_postData[postIndex - 1].vPosition;
                    Vector3 previousXPost = m_postData[postIndex - m_postsPerSide].vPosition;
                    Vector3 previousXNextZPost = m_postData[postIndex - m_postsPerSide + 1].vPosition;
                    Vector3 nextZPost = m_postData[postIndex + 1].vPosition;
                    Vector3 nextXPost = m_postData[postIndex + m_postsPerSide].vPosition;
                    Vector3 nextXPreviousZPost = m_postData[postIndex + m_postsPerSide - 1].vPosition;

                    // Translate neighboring posts into vectors from the target post.
                    previousZPost -= m_postData[postIndex].vPosition;
                    previousXPost -= m_postData[postIndex].vPosition;
                    previousXNextZPost -= m_postData[postIndex].vPosition;
                    nextZPost -= m_postData[postIndex].vPosition;
                    nextXPost -= m_postData[postIndex].vPosition;
                    nextXPreviousZPost -= m_postData[postIndex].vPosition;

                    // -Z/-X face normal (1/4 weight)
                    m_postData[postIndex].vNormal += 0.25f * CrossProduct( previousZPost, previousXPost );

                    // -X/-X+Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( previousXPost, previousXNextZPost );

                    // -X+Z/+Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( previousXNextZPost, nextZPost );

                    // +Z/+X face normal (1/4 weight)
                    m_postData[postIndex].vNormal += 0.25f * CrossProduct( nextZPost, nextXPost );

                    // +X/+X-Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( nextXPost, nextXPreviousZPost );

                    // +X-Z/-Z face normal (1/8 weight)
                    m_postData[postIndex].vNormal += 0.125f * CrossProduct( nextXPreviousZPost, previousZPost );
                }
            }

            // Hazard: a fully degenerate neighborhood can cancel every
            // weighted face; publish +Y instead of a NaN render normal.

            if ( !m_postData[postIndex].vNormal.TryNormalise() )
            {
                m_postData[postIndex].vNormal = Vector3( 0.0f, 1.0f, 0.0f );
            }
        }
    }
}


#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
void Terrain::BuildMesh()
{

    // Two triangles per quad, three vertices per triangle, and eight floats per
    // vertex (position 3 + normal 3 + texture coordinate 2).
    int quadsPerSide = m_postsPerSide - 1;
    int totalQuads = quadsPerSide * quadsPerSide;
    int totalVerts = totalQuads * 6;

    std::vector<float> vertexData = BuildRenderVertexData();

    assert( m_resources );
    m_terrainMesh = m_resources->CreateMesh( vertexData.data(), totalVerts, true, true );
}
#endif


std::vector<float> Terrain::BuildRenderVertexData() const
{
    const int quadsPerSide = m_postsPerSide - 1;
    const int totalQuads = quadsPerSide * quadsPerSide;
    const int totalVerts = totalQuads * 6;
    std::vector<float> vertexData;
    vertexData.reserve( static_cast<size_t>( totalVerts ) * 8 );

    // Invariant: both cell loops stop at postsPerSide - 2, so the four named
    // post indices below remain in [0, postsPerSide * postsPerSide - 1].

    for ( int worldXCell = 0; worldXCell < quadsPerSide; ++worldXCell )
    {

        for ( int worldZCell = 0; worldZCell < quadsPerSide; ++worldZCell )
        {
            float texCoordS = ( static_cast<float>( worldZCell ) / static_cast<float>( m_postsPerSide ) ) * m_textureWrap;
            float texCoordT = ( static_cast<float>( worldXCell ) / static_cast<float>( m_postsPerSide ) ) * m_textureWrap;
            float texCoordSP1 = ( static_cast<float>( worldZCell + 1 ) / static_cast<float>( m_postsPerSide ) ) *
                                m_textureWrap;

            float texCoordTP1 = ( static_cast<float>( worldXCell + 1 ) / static_cast<float>( m_postsPerSide ) ) *
                                m_textureWrap;

            const int firstPostIndex = worldXCell * m_postsPerSide + worldZCell;
            const TerrainPost& postX0Z0 = m_postData[firstPostIndex];
            const TerrainPost& postX0Z1 = m_postData[firstPostIndex + 1];
            const TerrainPost& postX1Z0 = m_postData[firstPostIndex + m_postsPerSide];
            const TerrainPost& postX1Z1 = m_postData[firstPostIndex + m_postsPerSide + 1];

            // Helper lambda: push the same floating-point world position that
            // collision queries use. Render, shadows, DXR, and physics now share
            // one post grid, so collision visualization cannot expose a second
            // interpolated terrain surface.
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

            // Triangle A: current X/current Z, current X/next Z, next X/current Z.
            pushVertex( postX0Z0, texCoordS, texCoordT );
            pushVertex( postX0Z1, texCoordSP1, texCoordT );
            pushVertex( postX1Z0, texCoordS, texCoordTP1 );

            // Triangle B: next X/current Z, current X/next Z, next X/next Z.
            pushVertex( postX1Z0, texCoordS, texCoordTP1 );
            pushVertex( postX0Z1, texCoordSP1, texCoordT );
            pushVertex( postX1Z1, texCoordSP1, texCoordTP1 );
        }
    }

    return vertexData;
}


#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )
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

    for ( int worldZCell = 0; worldZCell < gridN; ++worldZCell )
    {

        for ( int worldXCell = 0; worldXCell < gridN; ++worldXCell )
        {
            float x0 = worldXCell * step;
            float x1 = x0 + step;
            float z0 = worldZCell * step;
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
#endif
