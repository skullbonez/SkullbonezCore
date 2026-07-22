/*
File: SkullbonezSource/World/WorldEnvironment.cpp
Purpose:
  Stores world forces, fluid parameters, and water rendering resources.

Summary:
  WorldEnvironment.cpp stores world forces, fluid parameters, and water
  rendering resources. As an implementation unit, keep edits anchored on
  world-state ownership, terrain/environment data, and physics/render handoff
  and on the glossary/invariants below.

Glossary:
  Buoyancy: Upward force from displaced water, applied through the center of
  buoyancy instead of the model origin.
  Center of buoyancy: World-space average location of displaced water. Its
  offset from the model origin creates roll/pitch torque.
  Wet sample: Fixed point inside a box-like body used to add angular water
    damping without allocating per-frame data.
  Fluid surface adjustment: Signed meters-per-second command applied over one
    simulation interval without exposing input-device semantics.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - World-setting commands arrive in domain units and retain no input owner.

Related:
  - SkullbonezSource/World/WorldEnvironment.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "WorldEnvironment.h"
#include "../Assets/AssetSystem.h"
#include "../Rendering/RenderCommandTypes.h"
#include "../Rendering/DX12/RenderBackendDX12.h"
#include "../Rendering/DX12/Dx12ResourceBuilder.h"
#include <vector>


using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;

namespace
{
WaterMode WaterModeFromConfigValue( int value )
{
    switch ( value )
    {
    case 0:
        return WaterMode::Off;
    case 1:
        return WaterMode::Basin;
    case 2:
        return WaterMode::Ocean;
    case 3:
        return WaterMode::WetFloor;
    case 4:
        return WaterMode::StylizedBasin;
    default:
        return WaterMode::Ocean;
    }
}

int WaterModeUniformValue( WaterMode mode )
{
    return static_cast<int>( mode );
}

bool WaterModeIncludesOuterOcean( WaterMode mode )
{
    return mode == WaterMode::Ocean;
}
} // namespace


WorldEnvironment::WorldEnvironment()
    : m_fluidSurfaceHeight( 0.0f ), m_fluidDensity( 0.0f ), m_gasDensity( 0.0f ), m_gravity( 0.0f )
{
}


WorldEnvironment::WorldEnvironment( float fFluidSurfaceHeight, float fFluidDensity, float fGasDensity, float fGravity )
    : m_fluidSurfaceHeight( fFluidSurfaceHeight ), m_fluidDensity( fFluidDensity ), m_gasDensity( fGasDensity ),
      m_gravity( fGravity )
{
}


WorldEnvironment::~WorldEnvironment()
{
}


void WorldEnvironment::BindRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    ApplyWaterAndFluidSettings( config );
}


void WorldEnvironment::BindRenderContexts( const SkullbonezCore::Core::EngineConfig& config,
                                           SkullbonezCore::Assets::AssetSystem& assets,
                                           Dx12ResourceBuilder& resources )
{
    // Lifetime: water keeps rebuild-only borrows owned by Run and refreshed by
    // WaterPass before lazy resource recreation.
    ApplyWaterAndFluidSettings( config );
    m_assets = &assets;
    m_resources = &resources;
}


void WorldEnvironment::ApplyWaterAndFluidSettings( const SkullbonezCore::Core::EngineConfig& config )
{
    m_waterStyle.ordinary = config.ordinaryRender;
    m_waterStyle.cinematicFallback = config.cinematicRender;
    m_waterStyle.oceanWaveHeight = config.waterRenderStyle.oceanWaveHeight;
    m_waterStyle.oceanPerturbStrength = config.waterRenderStyle.oceanPerturbStrength;
    m_waterMeshBuild.frustumFar = config.camera.frustumFar;
    m_fluidForces.angularDragMultiplier = config.worldForces.fluidAngularDragMultiplier;
}


void WorldEnvironment::SetTerrainBounds( float xMin, float xMax, float zMin, float zMax )
{
    m_terrainXMin = xMin;
    m_terrainXMax = xMax;
    m_terrainZMin = zMin;
    m_terrainZMax = zMax;
}


WaterStyleParams
WorldEnvironment::BuildCalmWaterStyle( bool cinematic,
                                       const SkullbonezCore::Core::CinematicRenderConfig& cinematicStyle ) const
{
    WaterStyleParams style;
    style.cinematic = cinematic;
    style.sunR = cinematicStyle.sunColorR;
    style.sunG = cinematicStyle.sunColorG;
    style.sunB = cinematicStyle.sunColorB;
    style.mode = cinematic ? WaterModeFromConfigValue( cinematicStyle.waterMode ) : WaterMode::Ocean;
    style.waveHeight = m_waterStyle.oceanWaveHeight;
    style.perturbStrength = m_waterStyle.oceanPerturbStrength;
    style.basinCenterX = cinematicStyle.basinCenterX;
    style.basinCenterZ = cinematicStyle.basinCenterZ;
    style.basinRadiusX = cinematicStyle.basinRadiusX;
    style.basinRadiusZ = cinematicStyle.basinRadiusZ;
    style.basinFeather = cinematic ? cinematicStyle.basinFeather : 1.0f;

    if ( cinematic )
    {
        style.tintR = cinematicStyle.waterTintR;
        style.tintG = cinematicStyle.waterTintG;
        style.tintB = cinematicStyle.waterTintB;
        style.alpha = cinematicStyle.waterAlpha;
        style.reflectionStrength = cinematicStyle.waterReflectionStrength;
        style.glintStrength = cinematicStyle.waterGlintStrength;
    }
    else
    {
        const SkullbonezCore::Core::OrdinaryRenderConfig& ordinary = m_waterStyle.ordinary;
        style.tintR = ordinary.waterTintR;
        style.tintG = ordinary.waterTintG;
        style.tintB = ordinary.waterTintB;
        style.alpha = ordinary.waterAlpha;
        style.reflectionStrength = ordinary.waterReflectionStrength;
        style.fresnelF0 = ordinary.waterFresnelF0;
        style.sunR = ordinary.sunColorR;
        style.sunG = ordinary.sunColorG;
        style.sunB = ordinary.sunColorB;
    }

    return style;
}


WaterStyleParams
WorldEnvironment::BuildOceanWaterStyle( bool cinematic,
                                        const SkullbonezCore::Core::CinematicRenderConfig& cinematicStyle ) const
{
    WaterStyleParams style;
    style.cinematic = cinematic;
    style.sunR = cinematicStyle.sunColorR;
    style.sunG = cinematicStyle.sunColorG;
    style.sunB = cinematicStyle.sunColorB;
    style.mode = cinematic ? WaterModeFromConfigValue( cinematicStyle.waterMode ) : WaterMode::Ocean;
    style.waveHeight = m_waterStyle.oceanWaveHeight;
    style.perturbStrength = m_waterStyle.oceanPerturbStrength;

    if ( cinematic )
    {
        style.tintR = cinematicStyle.waterTintR;
        style.tintG = cinematicStyle.waterTintG;
        style.tintB = cinematicStyle.waterTintB;
        style.alpha = cinematicStyle.waterAlpha;
        style.reflectionStrength = cinematicStyle.waterReflectionStrength;
        style.glintStrength = cinematicStyle.waterGlintStrength;
    }
    else
    {
        const SkullbonezCore::Core::OrdinaryRenderConfig& ordinary = m_waterStyle.ordinary;
        style.tintR = ordinary.waterTintR;
        style.tintG = ordinary.waterTintG;
        style.tintB = ordinary.waterTintB;
        style.alpha = ordinary.waterAlpha;
        style.reflectionStrength = ordinary.waterReflectionStrength;
        style.fresnelF0 = ordinary.waterFresnelF0;
        style.sunR = ordinary.sunColorR;
        style.sunG = ordinary.sunColorG;
        style.sunB = ordinary.sunColorB;
    }

    return style;
}


void WorldEnvironment::BindCommonWaterStyle( Rendering::ShaderDX12& shader,
                                             const WaterStyleParams& style,
                                             const Vector3& cameraWorld,
                                             const WaterReflectionInput& reflection ) const
{
    shader.SetMat4( "uModel", Matrix4::Translate( 0.0f, m_fluidSurfaceHeight, 0.0f ) );
    shader.SetMat4( "uReflectVP", reflection.sampleViewProjection );
    shader.SetVec4( "uColorTint", style.tintR, style.tintG, style.tintB, style.alpha );
    shader.SetFloat( "uReflectionStrength", style.reflectionStrength );
    shader.SetFloat( "uWaterFresnelF0", style.fresnelF0 );
    shader.SetVec3( "uCameraWorld", cameraWorld.x, cameraWorld.y, cameraWorld.z );
    shader.SetInt( "uNoReflect", reflection.noReflection ? 1 : 0 );
    shader.SetFloat( "uCinematicMode", style.cinematic ? 1.0f : 0.0f );
    shader.SetVec3( "uSunColor", style.sunR, style.sunG, style.sunB );
    shader.SetFloat( "uSunGlintStrength", style.glintStrength );
}


void WorldEnvironment::BindCalmWaterStyle( Rendering::ShaderDX12& shader, const WaterStyleParams& style ) const
{
    shader.SetInt( "uWaterMode", WaterModeUniformValue( style.mode ) );
    shader.SetVec4( "uBasinMask", style.basinCenterX, style.basinCenterZ, style.basinRadiusX, style.basinRadiusZ );
    shader.SetFloat( "uBasinMaskFeather", style.basinFeather );
}


void WorldEnvironment::BindOceanWaterStyle( Rendering::ShaderDX12& shader,
                                            const WaterStyleParams& style,
                                            float time,
                                            bool flatWater ) const
{
    shader.SetFloat( "uTime", time );
    shader.SetFloat( "uWaveHeight", style.waveHeight );
    shader.SetFloat( "uPerturbStrength", style.perturbStrength );
    shader.SetInt( "uFlatWater", flatWater ? 1 : 0 );
}


void WorldEnvironment::RenderFluid( const Matrix4& view,
                                    const Matrix4& proj,
                                    const Vector3& cameraWorld,
                                    Dx12TextureOwner& textures,
                                    const WaterReflectionInput& reflection,
                                    const PassRasterStateBucket& rasterState,
                                    float time,
                                    bool flatWater,
                                    bool cinematic,
                                    const SkullbonezCore::Core::CinematicRenderConfig* cinematicConfig )
{
    if ( !m_calmMesh || !m_oceanMesh || !m_calmShader || !m_oceanShader )
    {
        ResetRenderResources();
    }
    if ( !m_calmMesh || !m_oceanMesh || !m_calmShader || !m_oceanShader )
    {
        return;
    }
    const SkullbonezCore::Core::CinematicRenderConfig& cinematicStyle =
        cinematicConfig ? *cinematicConfig : m_waterStyle.cinematicFallback;
    const WaterMode waterMode = cinematic ? WaterModeFromConfigValue( cinematicStyle.waterMode ) : WaterMode::Ocean;
    if ( cinematic && waterMode == WaterMode::Off )
    {
        return;
    }

    textures.BindTexture( reflection.textureHandle, 1 );

    const WaterStyleParams calmStyle = BuildCalmWaterStyle( cinematic, cinematicStyle );
    const WaterStyleParams oceanStyle = BuildOceanWaterStyle( cinematic, cinematicStyle );

    // --- calm (inner) pass: flat, reflective unless disabled ---
    m_calmShader->Use();
    m_calmShader->SetMat4( "uView", view );
    m_calmShader->SetMat4( "uProjection", proj );
    BindCommonWaterStyle( *m_calmShader, calmStyle, cameraWorld, reflection );
    BindCalmWaterStyle( *m_calmShader, calmStyle );
    m_calmMesh->Draw( rasterState );

    if ( cinematic && !WaterModeIncludesOuterOcean( waterMode ) )
    {
        // Cinematic preview stops after the calm basin pool. Skipping the outer
        // ocean avoids a giant water sheet behind the shot and keeps attention on
        // the terrain bowl, balls, sunset, and fog.
        return;
    }

    // --- ocean (outer) pass: vertex displacement + UV perturbation ---
    m_oceanShader->Use();
    m_oceanShader->SetMat4( "uView", view );
    m_oceanShader->SetMat4( "uProjection", proj );
    BindCommonWaterStyle( *m_oceanShader, oceanStyle, cameraWorld, reflection );
    BindOceanWaterStyle( *m_oceanShader, oceanStyle, time, flatWater );
    m_oceanMesh->Draw( rasterState );
}


void WorldEnvironment::BuildFluidMesh()
{
    assert( m_assets );
    assert( m_resources );
    float f = m_waterMeshBuild.frustumFar;

    const int N = 64;
    const float step = 2.0f * f / static_cast<float>( N );

    // Calm region: half the terrain footprint, centered on the terrain
    float cxMid = ( m_terrainXMin + m_terrainXMax ) * 0.5f;
    float czMid = ( m_terrainZMin + m_terrainZMax ) * 0.5f;
    float cxHalf = ( m_terrainXMax - m_terrainXMin ) * 0.25f;
    float czHalf = ( m_terrainZMax - m_terrainZMin ) * 0.25f;
    float calmXMin = cxMid - cxHalf;
    float calmXMax = cxMid + cxHalf;
    float calmZMin = czMid - czHalf;
    float calmZMax = czMid + czHalf;

    std::vector<float> calmVerts;
    std::vector<float> oceanVerts;
    constexpr int CALM_N = 128;
    calmVerts.reserve( CALM_N * CALM_N * 6 * 3 );
    oceanVerts.reserve( N * N * 6 * 3 );

    const float calmStepX = ( calmXMax - calmXMin ) / static_cast<float>( CALM_N );
    const float calmStepZ = ( calmZMax - calmZMin ) / static_cast<float>( CALM_N );
    for ( int row = 0; row < CALM_N; ++row )
    {
        for ( int col = 0; col < CALM_N; ++col )
        {
            float x0 = calmXMin + static_cast<float>( col ) * calmStepX;
            float x1 = x0 + calmStepX;
            float z0 = calmZMin + static_cast<float>( row ) * calmStepZ;
            float z1 = z0 + calmStepZ;

            calmVerts.push_back( x0 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z0 );
            calmVerts.push_back( x0 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z1 );
            calmVerts.push_back( x1 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z1 );

            calmVerts.push_back( x0 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z0 );
            calmVerts.push_back( x1 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z1 );
            calmVerts.push_back( x1 );
            calmVerts.push_back( 0.0f );
            calmVerts.push_back( z0 );
        }
    }

    for ( int row = 0; row < N; ++row )
    {
        for ( int col = 0; col < N; ++col )
        {
            float x0 = -f + static_cast<float>( col ) * step;
            float x1 = x0 + step;
            float z0 = -f + static_cast<float>( row ) * step;
            float z1 = z0 + step;

            // A quad belongs to the calm mesh only if it lies fully inside the calm region
            bool isCalm = ( x0 >= calmXMin && x1 <= calmXMax && z0 >= calmZMin && z1 <= calmZMax );
            if ( isCalm )
            {
                continue;
            }
            std::vector<float>& v = oceanVerts;

            v.push_back( x0 );
            v.push_back( 0.0f );
            v.push_back( z0 );
            v.push_back( x0 );
            v.push_back( 0.0f );
            v.push_back( z1 );
            v.push_back( x1 );
            v.push_back( 0.0f );
            v.push_back( z1 );

            v.push_back( x0 );
            v.push_back( 0.0f );
            v.push_back( z0 );
            v.push_back( x1 );
            v.push_back( 0.0f );
            v.push_back( z1 );
            v.push_back( x1 );
            v.push_back( 0.0f );
            v.push_back( z0 );
        }
    }

    int calmCount = static_cast<int>( calmVerts.size() ) / 3;
    int oceanCount = static_cast<int>( oceanVerts.size() ) / 3;

    m_calmMesh = m_resources->CreateMesh( calmVerts.data(), calmCount, false, false );
    m_oceanMesh = m_resources->CreateMesh( oceanVerts.data(), oceanCount, false, false );

    m_calmShader = m_assets->CreateShader( *m_resources, "shader.water_calm" );
    if ( !m_calmShader )
    {
        return;
    }
    m_calmShader->Use();
    m_calmShader->SetMat4( "uModel", Matrix4() );
    m_calmShader->SetVec4( "uColorTint", 0.05f, 0.15f, 0.42f, 0.65f );
    m_calmShader->SetFloat( "uReflectionStrength", 0.35f );
    m_calmShader->SetFloat( "uWaterFresnelF0", m_waterStyle.ordinary.waterFresnelF0 );
    m_calmShader->SetVec3( "uCameraWorld", 0.0f, 0.0f, 0.0f );
    m_calmShader->SetInt( "uReflectionTex", 1 );
    m_calmShader->SetFloat( "uCinematicMode", 0.0f );
    m_calmShader->SetInt( "uWaterMode", WaterModeUniformValue( WaterMode::Ocean ) );
    m_calmShader->SetVec3( "uSunColor",
                           m_waterStyle.cinematicFallback.sunColorR,
                           m_waterStyle.cinematicFallback.sunColorG,
                           m_waterStyle.cinematicFallback.sunColorB );
    m_calmShader->SetFloat( "uSunGlintStrength", 0.0f );
    m_calmShader->SetVec4( "uBasinMask", 620.0f, 615.0f, 205.0f, 145.0f );
    m_calmShader->SetFloat( "uBasinMaskFeather", 1.0f );

    m_oceanShader = m_assets->CreateShader( *m_resources, "shader.water_ocean" );
    if ( !m_oceanShader )
    {
        return;
    }
    m_oceanShader->Use();
    m_oceanShader->SetMat4( "uModel", Matrix4() );
    m_oceanShader->SetVec4( "uColorTint", 0.02f, 0.10f, 0.35f, 0.72f );
    m_oceanShader->SetFloat( "uWaveHeight", m_waterStyle.oceanWaveHeight );
    m_oceanShader->SetFloat( "uPerturbStrength", m_waterStyle.oceanPerturbStrength );
    m_oceanShader->SetFloat( "uReflectionStrength", 0.25f );
    m_oceanShader->SetFloat( "uWaterFresnelF0", m_waterStyle.ordinary.waterFresnelF0 );
    m_oceanShader->SetVec3( "uCameraWorld", 0.0f, 0.0f, 0.0f );
    m_oceanShader->SetInt( "uReflectionTex", 1 );
    m_oceanShader->SetFloat( "uCinematicMode", 0.0f );
    m_oceanShader->SetVec3( "uSunColor",
                            m_waterStyle.cinematicFallback.sunColorR,
                            m_waterStyle.cinematicFallback.sunColorG,
                            m_waterStyle.cinematicFallback.sunColorB );
    m_oceanShader->SetFloat( "uSunGlintStrength", 0.0f );
}


void WorldEnvironment::ResetRenderResources()
{
    ReleaseRenderResources();
    BuildFluidMesh();
}


void WorldEnvironment::EnsureRenderResources( const SkullbonezCore::Core::EngineConfig& config,
                                              SkullbonezCore::Assets::AssetSystem& assets,
                                              Dx12ResourceBuilder& resources )
{
    BindRenderContexts( config, assets, resources );
    if ( !m_calmMesh || !m_oceanMesh || !m_calmShader || !m_oceanShader )
    {
        ResetRenderResources();
    }
}


void WorldEnvironment::ReleaseRenderResources()
{
    m_calmMesh.reset();
    m_calmShader.reset();
    m_oceanMesh.reset();
    m_oceanShader.reset();
}


float WorldEnvironment::GetFluidSurfaceHeight() const
{
    return m_fluidSurfaceHeight;
}


SkullbonezCore::Physics::PhysicsWorldForces WorldEnvironment::GetPhysicsWorldForces() const
{
    SkullbonezCore::Physics::PhysicsWorldForces forces;
    forces.fluidSurfaceHeight = m_fluidSurfaceHeight;
    forces.fluidDensity = m_fluidDensity;
    forces.gasDensity = m_gasDensity;
    forces.gravity = m_gravity;
    forces.angularDragMultiplier = m_fluidForces.angularDragMultiplier;
    forces.mutualGravity = m_mutualGravity;
    return forces;
}


void WorldEnvironment::SetFluidSurfaceHeight( float height )
{
    m_fluidSurfaceHeight = height;
}


void WorldEnvironment::ApplyFluidSurfaceAdjustment( const FluidSurfaceAdjustment& adjustment, float deltaSeconds )
{
    // Invariant: input has already resolved device semantics. This world owner
    // consumes only signed meters-per-second over the simulation interval.
    m_fluidSurfaceHeight += adjustment.DeltaMeters( deltaSeconds );
}


float WorldEnvironment::GetGravity() const
{
    return m_gravity;
}


void WorldEnvironment::SetGravity( float gravity )
{
    m_gravity = gravity;
}


float WorldEnvironment::GetFluidDensity() const
{
    return m_fluidDensity;
}


void WorldEnvironment::SetFluidDensity( float density )
{
    m_fluidDensity = density;
}


const SkullbonezCore::Physics::MutualGravitySettings& WorldEnvironment::GetMutualGravitySettings() const
{
    return m_mutualGravity;
}


void WorldEnvironment::SetMutualGravitySettings( const SkullbonezCore::Physics::MutualGravitySettings& settings )
{
    m_mutualGravity = settings;
}
