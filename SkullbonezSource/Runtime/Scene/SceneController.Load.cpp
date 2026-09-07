/*
File: SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
Purpose:
  Loads, resets, and advances authored and generated scenes.

Summary:
  SceneLoadTransaction owns cold-load orchestration, the phase cursor, and
  detached outputs. It mutates SceneController's public active-world/session
  surface synchronously; App applies Render activation before later reactions.

Glossary:
  Render setup values: Exact DX12 frame-drain/resource-build borrows plus the
    detached policy and activation request returned to App.
  Required scene contact: Authored pair gate that marks a scenario objective
    once two bodies have produced an exact contact.
  Authored projection: Cold-load, field-by-field copy from parser DTOs into an
    owning subsystem's runtime values.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
    surface.
  - Scene activation is published only after App completes Render-owned DXR setup.
  - Scene/model/terrain destruction starts only after a successful GPU drain.
  - Load orchestration retains no caller pointer, callback, friend backdoor, or
    mutable owner bag.
  - Tornado projection copies every authored field here so Gameplay never
    depends upward on Scene or parser vocabulary.
  - Capacity rows are logged before clear; the next capacity session begins
    after clear and before population.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneController.h"
#include "SceneLoadTransaction.h"


#include "../../Core/WindowConstants.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"


#include "../Startup/RunStartupState.h"


#include "../Simulation/SimulationSystem.h"
#include "SceneLoadRequest.h"
#include "SceneLoadPreparation.h"
#include "SceneSaveOperations.h"
#include "SceneCinematicPolicy.h"
#include "../../Assets/EditorHullAssets.h"
#include "SceneGeneratedSetup.h"
#include "../../Physics/Ragdoll.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/SbResult.h"
#include "../../Core/WorkerPool.h"
#include "../../Rendering/DX12/Dx12FrameOwner.h"
#include "../../Rendering/RenderRaytracingTypes.h"
#include "../../Rendering/DX12/Dx12ResourceBuilder.h"
#include "SceneNavigationModel.h"
#include "../../Scene/AuthoredScene.h"
#include "../../Scene/SceneSnapshotWriter.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"

#include <fstream>
#include <utility>

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Assets::ResolveEditorHullAssetPath;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Environment::WorldEnvironment;
using SkullbonezCore::Gameplay::TornadoFieldConfig;
using SkullbonezCore::Gameplay::TornadoSystemConfig;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Geometry::XZBounds;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Rendering::MeshDX12;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{
using Json = nlohmann::ordered_json;
constexpr float NO_WATER_TERRAIN_CLEARANCE = 100.0f;

TornadoFieldConfig ProjectAuthoredTornadoField( const AuthoredTornadoFieldConfig& authored )
{
    TornadoFieldConfig projected;
    projected.enabled = authored.enabled;
    projected.visualizeVelocityField = authored.visualizeVelocityField;
    projected.center = authored.center;
    projected.radius = authored.radius;
    projected.height = authored.height;
    projected.inwardAcceleration = authored.inwardAcceleration;
    projected.swirlAcceleration = authored.swirlAcceleration;
    projected.liftAcceleration = authored.liftAcceleration;
    projected.ejectAcceleration = authored.ejectAcceleration;
    projected.ejectUpAcceleration = authored.ejectUpAcceleration;
    projected.ejectBand = authored.ejectBand;
    projected.minCaptureSeconds = authored.minCaptureSeconds;
    projected.ejectCooldownSeconds = authored.ejectCooldownSeconds;
    projected.maxDeltaVelocity = authored.maxDeltaVelocity;
    return projected;
}

TornadoSystemConfig ProjectAuthoredTornadoSystem( const AuthoredTornadoSystemConfig& authored )
{
    // Owner: Scene owns cold authored DTOs. Runtime performs the exhaustive
    // copy so Gameplay never depends upward on Scene or parser vocabulary.
    TornadoSystemConfig projected;
    projected.enabled = authored.enabled;
    projected.visualizeVelocityField = authored.visualizeVelocityField;
    projected.vortices.reserve( authored.vortices.size() );

    for ( const AuthoredTornadoVortexConfig& authoredVortex : authored.vortices )
    {
        SkullbonezCore::Gameplay::TornadoVortexConfig vortex;
        vortex.field = ProjectAuthoredTornadoField( authoredVortex.field );
        vortex.spawnSeconds = authoredVortex.spawnSeconds;
        vortex.timeToLiveSeconds = authoredVortex.timeToLiveSeconds;
        vortex.growSeconds = authoredVortex.growSeconds;
        vortex.shrinkSeconds = authoredVortex.shrinkSeconds;
        vortex.driftRadius = authoredVortex.driftRadius;
        vortex.driftSpeed = authoredVortex.driftSpeed;
        vortex.driftPhase = authoredVortex.driftPhase;
        vortex.repulsionRadius = authoredVortex.repulsionRadius;
        vortex.repulsionStrength = authoredVortex.repulsionStrength;
        projected.vortices.push_back( vortex );
    }

    return projected;
}

void ApplySceneWorkerThreadSetting( SkullbonezCore::Core::EngineConfig& config,
                                    SkullbonezCore::Threading::WorkerPool& workerPool, int requestedWorkerThreads )
{
    const int clampedWorkerThreads = std::clamp( requestedWorkerThreads, -1,
                                                 SkullbonezCore::Threading::WorkerPool::MaxThreadCount() );

    const int resolvedWorkerThreads = SkullbonezCore::Threading::WorkerPool::ResolveThreadCount( clampedWorkerThreads );
    config.runtimeCapacity.workerThreads = clampedWorkerThreads;

    if ( workerPool.GetThreadCount() != resolvedWorkerThreads )
    {
        workerPool.Initialise( clampedWorkerThreads );
    }
}

Quaternion MakeSceneEulerQuaternion( float eulerXDeg, float eulerYDeg, float eulerZDeg )
{
    static constexpr float DEG2RAD = 3.14159265f / 180.0f;
    const float xHalf = eulerXDeg * DEG2RAD * 0.5f;
    const float yHalf = eulerYDeg * DEG2RAD * 0.5f;
    const float zHalf = eulerZDeg * DEG2RAD * 0.5f;

    // Invariant: scene Euler degrees preserve their established world-space
    // meaning across the canonical Hamilton representation change.
    const Quaternion xRotation( -sinf( xHalf ), 0.0f, 0.0f, cosf( xHalf ) );
    const Quaternion yRotation( 0.0f, -sinf( yHalf ), 0.0f, cosf( yHalf ) );
    const Quaternion zRotation( 0.0f, 0.0f, -sinf( zHalf ), cosf( zHalf ) );

    Quaternion orientation;
    orientation *= xRotation * yRotation * zRotation;
    orientation.Normalise();
    return orientation;
}


void LogSceneLoadFailure( const SkullbonezCore::Core::SbResult& result, const std::string& scenePath )
{
    // Why: scene setup is a recoverable load boundary. Logging the owner keeps
    // automation and operators on a concrete failing subsystem without treating
    // malformed scene/generated input as an engine invariant failure.
    const char* owner = result.ErrorOwner() && result.ErrorOwner()[0] != '\0' ? result.ErrorOwner() : "Runtime/Scene";
    const char* message = result.ErrorMessage()[0] != '\0' ? result.ErrorMessage() : "scene setup failed without a message";

    fprintf( stderr, "[scene] scene_load_failed owner=%s path=\"%s\" reason=\"%s\"\n", owner,
             scenePath.empty() ? "<generated>" : scenePath.c_str(), message );
}

bool IsCineScenePath( const std::string& path )
{
    const char* name = SceneFileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 || strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr || strstr( name, "cine_" ) == name;
}

Json& EnsureJsonObject( Json& parent, const char* key )
{
    Json& child = parent[key];

    if ( !child.is_object() )
    {
        child = Json::object();
    }

    return child;
}

void SetTouchedCinematicSceneProperties( Json& root, uint64_t touchedMask,
                                         const SkullbonezCore::Core::CinematicRenderConfig& c )
{
    // Concept: save only values the UI actually touched.
    //
    // Scene JSON can include reusable style files plus a few local overrides.
    // The touched mask prevents "Save Defaults" from expanding every engine.cfg
    // or style default into the scene file.
    if ( touchedMask == 0 )
    {
        return;
    }

    Json& cinematic = EnsureJsonObject( root, "cinematic" );
    const auto writeBool = [&]( uint64_t bit, const char* key, bool value )
    {
        if ( ( touchedMask & bit ) != 0 )
        {
            cinematic[key] = value;
        }
    };

    const auto writeFloat = [&]( uint64_t bit, const char* key, float value )
    {
        if ( ( touchedMask & bit ) != 0 )
        {
            cinematic[key] = value;
        }
    };

    const auto writeInt = [&]( uint64_t bit, const char* key, int value )
    {
        if ( ( touchedMask & bit ) != 0 )
        {
            cinematic[key] = value;
        }
    };

    writeBool( SCENE_CINE_RENDERING, "rendering", c.enabled );
    writeBool( SCENE_CINE_SKY_ATMOSPHERE, "skyAtmosphere", c.skyAtmosphereEnabled );
    writeBool( SCENE_CINE_CLOUDS, "clouds", c.cloudsEnabled );
    writeBool( SCENE_CINE_GOD_RAYS, "godRays", c.godRaysEnabled );
    writeBool( SCENE_CINE_VOLUMETRIC_LIGHTING, "volumetricLighting", c.volumetricLightingEnabled );
    writeBool( SCENE_CINE_BLOOM, "bloom", c.bloomEnabled );
    writeBool( SCENE_CINE_FOG, "fog", c.fogEnabled );
    writeBool( SCENE_CINE_TERRAIN_RELIEF_ENABLED, "terrainReliefEnabled", c.terrainReliefEnabled );

    writeFloat( SCENE_CINE_EXPOSURE, "exposure", c.exposure );
    writeFloat( SCENE_CINE_GAMMA, "gamma", c.gamma );

    // Compatibility: saved scenes keep the established JSON keys even though
    // the in-memory fields now describe world-sky azimuth and elevation.
    writeFloat( SCENE_CINE_SUN_AZIMUTH, "sunScreenX", c.sunAzimuth );
    writeFloat( SCENE_CINE_SUN_ELEVATION, "sunScreenY", c.sunElevation );
    writeFloat( SCENE_CINE_SUN_COLOR_R, "sunColorR", c.sunColorR );
    writeFloat( SCENE_CINE_SUN_COLOR_G, "sunColorG", c.sunColorG );
    writeFloat( SCENE_CINE_SUN_COLOR_B, "sunColorB", c.sunColorB );
    writeFloat( SCENE_CINE_SUN_INTENSITY, "sunIntensity", c.sunIntensity );
    writeFloat( SCENE_CINE_SKY_HORIZON_R, "skyHorizonR", c.skyHorizonR );
    writeFloat( SCENE_CINE_SKY_HORIZON_G, "skyHorizonG", c.skyHorizonG );
    writeFloat( SCENE_CINE_SKY_HORIZON_B, "skyHorizonB", c.skyHorizonB );
    writeFloat( SCENE_CINE_SKY_ZENITH_R, "skyZenithR", c.skyZenithR );
    writeFloat( SCENE_CINE_SKY_ZENITH_G, "skyZenithG", c.skyZenithG );
    writeFloat( SCENE_CINE_SKY_ZENITH_B, "skyZenithB", c.skyZenithB );
    writeFloat( SCENE_CINE_SKY_GLOW_STRENGTH, "skyGlowStrength", c.skyGlowStrength );
    writeFloat( SCENE_CINE_CLOUD_COVERAGE, "cloudCoverage", c.cloudCoverage );
    writeFloat( SCENE_CINE_CLOUD_SOFTNESS, "cloudSoftness", c.cloudSoftness );
    writeFloat( SCENE_CINE_CLOUD_SCALE, "cloudScale", c.cloudScale );
    writeFloat( SCENE_CINE_CLOUD_INTENSITY, "cloudIntensity", c.cloudIntensity );
    writeFloat( SCENE_CINE_SUN_SHAFT_STRENGTH, "sunShaftStrength", c.sunShaftStrength );
    writeFloat( SCENE_CINE_SUN_SHAFT_FALLOFF, "sunShaftFalloff", c.sunShaftFalloff );
    writeFloat( SCENE_CINE_VOLUMETRIC_STRENGTH, "volumetricStrength", c.volumetricStrength );
    writeFloat( SCENE_CINE_VOLUMETRIC_DENSITY, "volumetricDensity", c.volumetricDensity );
    writeFloat( SCENE_CINE_VOLUMETRIC_DECAY, "volumetricDecay", c.volumetricDecay );
    writeFloat( SCENE_CINE_BLOOM_THRESHOLD, "bloomThreshold", c.bloomThreshold );
    writeFloat( SCENE_CINE_BLOOM_KNEE, "bloomKnee", c.bloomKnee );
    writeFloat( SCENE_CINE_BLOOM_STRENGTH, "bloomStrength", c.bloomStrength );
    writeFloat( SCENE_CINE_BLOOM_RADIUS, "bloomRadius", c.bloomRadius );
    writeFloat( SCENE_CINE_TERRAIN_RELIEF, "terrainRelief", c.terrainRelief );
    writeFloat( SCENE_CINE_BASIN_DEPTH, "basinDepth", c.basinDepth );
    writeFloat( SCENE_CINE_BASIN_RIM_LIFT, "basinRimLift", c.basinRimLift );
    writeBool( SCENE_CINE_SHADOWS, "shadows", c.shadow.enabled );
    writeInt( SCENE_CINE_SHADOW_MAP_SIZE, "shadowMapSize", c.shadow.mapSize );
    writeInt( SCENE_CINE_SHADOW_PCF_RADIUS, "shadowPcfRadius", c.shadow.pcfRadius );
    writeFloat( SCENE_CINE_SHADOW_STRENGTH, "shadowStrength", c.shadow.strength );
    writeFloat( SCENE_CINE_SHADOW_SOFTNESS, "shadowSoftness", c.shadow.softness );
    writeFloat( SCENE_CINE_SHADOW_DEPTH_BIAS, "shadowDepthBias", c.shadow.depthBias );
    writeFloat( SCENE_CINE_SHADOW_SLOPE_BIAS, "shadowSlopeBias", c.shadow.slopeBias );
    writeFloat( SCENE_CINE_SHADOW_MAX_DISTANCE, "shadowMaxDistance", c.shadow.maxDistance );

    if ( ( touchedMask & SCENE_CINE_SHADOW_PARTICIPATION ) != 0 )
    {
        cinematic["shadowParticipation"] = Json::array(
            { c.shadow.terrainCasts, c.shadow.objectsCast, c.shadow.terrainReceives, c.shadow.objectsReceive } );
    }

    writeFloat( SCENE_CINE_FOG_COLOR_R, "fogColorR", c.fogColorR );
    writeFloat( SCENE_CINE_FOG_COLOR_G, "fogColorG", c.fogColorG );
    writeFloat( SCENE_CINE_FOG_COLOR_B, "fogColorB", c.fogColorB );
    writeFloat( SCENE_CINE_FOG_START, "fogStart", c.fogStart );
    writeFloat( SCENE_CINE_FOG_END, "fogEnd", c.fogEnd );
    writeFloat( SCENE_CINE_FOG_DENSITY, "fogDensity", c.fogDensity );
    writeFloat( SCENE_CINE_FOG_MAX_OPACITY, "fogMaxOpacity", c.fogMaxOpacity );

    if ( ( touchedMask & SCENE_CINE_STYLE_MODES ) != 0 )
    {
        cinematic["styleModes"] = Json::array( { c.skyMode, c.terrainMode, c.objectStyle, c.waterMode } );
    }

    if ( ( touchedMask & SCENE_CINE_STYLE_GRADE ) != 0 )
    {
        cinematic["styleGrade"] = Json::array( { c.styleSaturation, c.styleContrast, c.styleVignette } );
    }

    if ( ( touchedMask & SCENE_CINE_TERRAIN_TINT ) != 0 )
    {
        cinematic["terrainTint"] = Json::array( { c.terrainTintR, c.terrainTintG, c.terrainTintB } );
    }

    if ( ( touchedMask & SCENE_CINE_TERRAIN_ACCENT ) != 0 )
    {
        cinematic["terrainAccent"] = Json::array( { c.terrainAccentR, c.terrainAccentG, c.terrainAccentB } );
    }

    if ( ( touchedMask & SCENE_CINE_TERRAIN_GRID ) != 0 )
    {
        cinematic["terrainGrid"] = Json::array( { c.terrainGridScale, c.terrainGridStrength } );
    }

    if ( ( touchedMask & SCENE_CINE_WATER_TINT ) != 0 )
    {
        cinematic["waterTint"] = Json::array( { c.waterTintR, c.waterTintG, c.waterTintB } );
    }

    if ( ( touchedMask & SCENE_CINE_WATER_PROFILE ) != 0 )
    {
        cinematic["waterProfile"] = Json::array( { c.waterAlpha, c.waterReflectionStrength, c.waterGlintStrength } );
    }

    if ( ( touchedMask & SCENE_CINE_BASIN_MASK ) != 0 )
    {
        cinematic["basinMask"] = Json::array(
            { c.basinCenterX, c.basinCenterZ, c.basinRadiusX, c.basinRadiusZ, c.basinFeather } );
    }
}

const char* WaterReflectionJsonValue( bool noReflect, bool rtReflect )
{
    if ( noReflect )
    {
        return "none";
    }

    return rtReflect ? "dxr" : "fbo";
}

void UpdateWorldTerrainBounds( WorldEnvironment& world, Terrain* terrain )
{
    if ( !terrain )
    {
        return;
    }

    XZBounds tb = terrain->GetXZBounds();
    world.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );
}

void ApplyConfiguredWorldEnvironment( WorldEnvironment& world, const SkullbonezCore::Core::EngineConfig& cfg,
                                      Terrain* terrain )
{
    world = WorldEnvironment( cfg.worldForces.fluidHeight, cfg.worldForces.fluidDensity, cfg.worldForces.gasDensity,
                              cfg.worldForces.gravity );

    world.BindRuntimeConfig( cfg );
    UpdateWorldTerrainBounds( world, terrain );
}

void ApplyNoWaterOverride( WorldEnvironment& world, Terrain* terrain, bool noWater )
{
    if ( !noWater || !terrain )
    {
        return;
    }

    world.SetFluidSurfaceHeight( terrain->GetMinHeight() - NO_WATER_TERRAIN_CLEARANCE );
}

SkullbonezCore::Core::SbResult UseDefaultTerrain( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                  SceneWorld& sceneWorld, SkullbonezCore::Assets::AssetSystem& assets,
                                                  const SkullbonezCore::Core::EngineConfig& config,
                                                  const std::string& terrainRawPath,
                                                  SkullbonezCore::Rendering::Dx12FrameOwner* renderFrame,
                                                  SkullbonezCore::Rendering::Dx12ResourceBuilder* renderResources )
{
    SceneTerrain& terrainOwner = sceneWorld.Terrain();
    WorldEnvironment& world = sceneWorld.Environment();
    assert( renderResources );

    if ( !renderResources )
    {
        return resultDiagnostics.Failure( "Runtime/RunScene", "Renderer resource factory unavailable for terrain load." );
    }

    if ( !terrainOwner.Get() || terrainOwner.IsFlatSlope() )
    {
        if ( renderFrame )
        {
            const SkullbonezCore::Core::SbResult flushResult = renderFrame->FlushGPU();

            if ( !flushResult.Ok() )
            {
                // Recoverable error: keep the currently owned terrain alive when its GPU
                // references cannot be proven drained.
                return flushResult;
            }
        }

        std::unique_ptr<Terrain> terrain;
        const SkullbonezCore::Core::SbResult terrainResult = Terrain::TryCreateFromHeightMap( resultDiagnostics,
                                                                                              terrainRawPath.c_str(), 256, 8,
                                                                                              15, config, assets,
                                                                                              *renderResources, terrain );

        if ( !terrainResult.Ok() )
        {
            // Why: RAW terrain is external scene/config input. Report the load
            // failure before replacing the currently owned terrain.
            return terrainResult;
        }

        sceneWorld.ReplaceTerrain( std::move( terrain ), false );
    }
    else
    {
        terrainOwner.Get()->BindRenderContexts( config, assets, *renderResources );
    }

    UpdateWorldTerrainBounds( world, terrainOwner.Get() );
    return SkullbonezCore::Core::SbResult::Success();
}

SkullbonezCore::Core::SbResult UseFlatSlopeTerrain( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                    SceneWorld& sceneWorld, SkullbonezCore::Assets::AssetSystem& assets,
                                                    const SkullbonezCore::Core::EngineConfig& config, float baseY,
                                                    float slopeX, float slopeZ,
                                                    SkullbonezCore::Rendering::Dx12FrameOwner* renderFrame,
                                                    SkullbonezCore::Rendering::Dx12ResourceBuilder* renderResources )
{
    SceneTerrain& terrainOwner = sceneWorld.Terrain();
    WorldEnvironment& world = sceneWorld.Environment();
    assert( renderResources );

    if ( !renderResources )
    {
        return resultDiagnostics.Failure( "Runtime/RunScene", "Renderer resource factory unavailable for flat terrain." );
    }

    if ( renderFrame )
    {
        const SkullbonezCore::Core::SbResult flushResult = renderFrame->FlushGPU();

        if ( !flushResult.Ok() )
        {
            // Recoverable error: assignment below destroys the old terrain. Leave it
            // untouched unless the GPU drain and command-list reopen succeeded.
            return flushResult;
        }
    }

    auto terrain = std::make_unique<Terrain>( baseY, slopeX, slopeZ, config, assets, *renderResources );
    sceneWorld.ReplaceTerrain( std::move( terrain ), true );

    UpdateWorldTerrainBounds( world, terrainOwner.Get() );
    return SkullbonezCore::Core::SbResult::Success();
}

void ApplyTornadoDefaultsForActiveScene( TornadoFieldConfig& field, WorldEnvironment& world,
                                         const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    const float basinRadius = (std::max)( cinematic.basinRadiusX, cinematic.basinRadiusZ );

    field.center = Vector3( cinematic.basinCenterX, world.GetFluidSurfaceHeight(), cinematic.basinCenterZ );
    field.radius = std::clamp( basinRadius * 1.28f, 180.0f, 340.0f );
    field.height = (std::max)( 130.0f, field.radius * 0.66f );
    field.inwardAcceleration = 150.0f;
    field.swirlAcceleration = 185.0f;
    field.liftAcceleration = 64.0f;
}
} // namespace


void SceneLoadResult::ResetForLoad()
{
    uiActivation = SceneUiActivation {};
    automationGates.Reset();
    navigation = SceneLoadNavigationState {};
    presentation = ScenePresentationValues {};
    camera = CameraControlState {};
    renderPolicy = SceneRenderPolicyState {};
    renderActivationSceneObjectCapacity = 0;
    renderActivationPending = false;
    captureReactions = SceneCaptureReactionBatch {};
    diagnosticsReactions = SceneDiagnosticsReactionBatch {};
    completedWorldChanges = {};
    completedWorldChangeCount = 0;
    completedRequests = SceneRequestBatch {};
    windowTitle[0] = '\0';
    applyNavigation = false;
    refreshSceneBrowser = false;
}


void SceneLoadTransaction::AdvanceOrFatal( SceneLoadPhaseCursor::Phase next, const char* operation )
{
    const SceneLoadPhaseCursor::Phase current = m_phase.Current();

    if ( !m_phase.TryAdvance( next ) )
    {
        // Fatal invariant: accepting an out-of-order phase would expose partially
        // updated scene owners or publish presentation before reactions.
        SB_FATAL( "Runtime/SceneLoadTransaction", "Illegal phase transition. operation=%s current=%u next=%u", operation,
                  static_cast<unsigned int>( current ), static_cast<unsigned int>( next ) );
    }
}


void SceneLoadTransaction::FinishRequestBatch()
{
    if ( m_phase.Current() == SceneLoadPhaseCursor::Phase::Idle )
    {
        AdvanceOrFatal( SceneLoadPhaseCursor::Phase::Load, "FinishRequestBatch" );
        return;
    }

    if ( m_phase.Current() != SceneLoadPhaseCursor::Phase::Load )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Load phase finished from an invalid phase. current=%u",
                  static_cast<unsigned int>( m_phase.Current() ) );
    }
}


void SceneLoadTransaction::CaptureSubmittedState( const CameraControlState& camera,
                                                  const SceneLoadNavigationState& navigation,
                                                  const ScenePresentationValues& presentation,
                                                  SceneRenderPolicyState renderPolicy, const char* rendererName,
                                                  double sceneTimeSeconds )
{
    // Why: submitted navigation and presentation values own growable cold-load
    // storage. Attribute their copies to SceneLoad even when the transaction is
    // opened from the steady-gameplay frame boundary.
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope allocationScope(
        SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );

    if ( m_phase.Current() != SceneLoadPhaseCursor::Phase::Idle )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction",
                  "Submitted scene-load values changed after the load phase began. current=%u",
                  static_cast<unsigned int>( m_phase.Current() ) );
    }

    m_request = SceneLoadRequest::None();
    m_outputs.ResetForLoad();
    m_outputs.camera = camera;
    m_outputs.navigation = navigation;
    m_outputs.presentation = presentation;
    m_outputs.renderPolicy = renderPolicy;
    sprintf_s( m_rendererName, "%s", rendererName ? rendererName : "unknown" );
    m_sceneTimeSeconds = sceneTimeSeconds;
}


const SceneLoadBeginResult& SceneLoadTransaction::Prepare( SceneController& sceneController, const SceneLoadRequest& request,
                                                           Rendering::Dx12FrameOwner* renderFrame,
                                                           bool interactiveSceneRunRequested )
{
    if ( m_phase.Current() != SceneLoadPhaseCursor::Phase::Idle || m_hasPreparedLoad )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Scene load prepared after its load phase began." );
    }

    m_request = request;
    m_preparedLoad = PrepareLoad( sceneController, m_outputs.navigation.overrides, m_outputs.renderPolicy,
                                  m_outputs.presentation, m_outputs.camera, renderFrame, interactiveSceneRunRequested,
                                  request.index, request.suppressExitOnComplete, request.preserveRuntimeState );
    m_hasPreparedLoad = true;
    return m_preparedLoad;
}


void SceneLoadTransaction::CompleteBeforeUnloadDiagnostics()
{
    if ( !m_hasPreparedLoad || !m_preparedLoad.shouldLoad || !m_preparedLoad.status.Ok() )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Before-unload diagnostics completed without a prepared load." );
    }

    m_beforeUnloadDiagnosticsComplete = true;
}


void SceneLoadTransaction::CaptureDiagnosticsLoad( bool physicsDiagnosticsEnabled, const char* physicsDiagnosticsPath,
                                                   const char* physicsRegressionLogPath,
                                                   const char* physicsCollisionTimeLogPath )
{
    m_physicsDiagnosticsEnabled = physicsDiagnosticsEnabled;
    strcpy_s( m_physicsDiagnosticsPath, physicsDiagnosticsPath ? physicsDiagnosticsPath : "" );
    strcpy_s( m_physicsRegressionLogPath, physicsRegressionLogPath ? physicsRegressionLogPath : "" );
    strcpy_s( m_physicsCollisionTimeLogPath, physicsCollisionTimeLogPath ? physicsCollisionTimeLogPath : "" );
}


void SceneLoadTransaction::CompleteRenderActivation( SceneController& sceneController )
{
    if ( !m_outputs.renderActivationPending || m_phase.Current() != SceneLoadPhaseCursor::Phase::Load )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Render activation completed without a pending Scene request." );
    }

    m_outputs.renderActivationPending = false;
    sceneController.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneActivated, 0 );
}


void SceneLoadTransaction::RecordCompletedRequest( const SceneRequest& request )
{
    if ( m_outputs.completedRequests.count >= SCENE_REQUEST_QUEUE_CAPACITY )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Fixed completed scene-request capacity exhausted." );
    }

    m_outputs.completedRequests.requests[m_outputs.completedRequests.count++] = request;
}


const SceneLoadResult& SceneLoadTransaction::BeginRuntimeReactions()
{
    AdvanceOrFatal( SceneLoadPhaseCursor::Phase::RuntimeReactions, "BeginRuntimeReactions" );
    return m_outputs;
}


const SceneLoadResult& SceneLoadTransaction::BeginPresentation()
{
    AdvanceOrFatal( SceneLoadPhaseCursor::Phase::Presentation, "BeginPresentation" );
    return m_outputs;
}


SceneAutomationGateConfiguration SceneLoadTransaction::TakeAutomationGates()
{
    if ( m_phase.Current() != SceneLoadPhaseCursor::Phase::Presentation )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Automation gates taken outside presentation. current=%u",
                  static_cast<unsigned int>( m_phase.Current() ) );
    }

    return std::move( m_outputs.automationGates );
}


void SceneLoadTransaction::CompletePresentation()
{
    AdvanceOrFatal( SceneLoadPhaseCursor::Phase::Complete, "CompletePresentation" );
}


void SceneLoadTransaction::ApplyAuthoredValues( SceneController& sceneController, SkullbonezCore::Core::EngineConfig& config,
                                                RunLaunchOptions& launchOptions, const AuthoredScene& scene,
                                                unsigned int rngSeed )
{
    SceneSessionState& sceneState = sceneController.State();
    ScenePresentationValues& presentation = m_outputs.presentation;
    sceneState.isScenePhysics = scene.IsPhysicsEnabled();
    sceneState.isSceneText = scene.IsTextEnabled();
    sceneState.predictionAllBodiesSpaceSeed = scene.PredictionShowsAllBodies();
    SceneDiagnosticsReaction perfFlush;
    perfFlush.kind = SceneDiagnosticsReactionKind::ConfigurePerfLogFlush;
    perfFlush.enabled = scene.IsPerfLogFlushEnabled();
    perfFlush.value = scene.GetPerfLogFlushInterval();
    AppendDiagnosticsReaction( perfFlush );
    presentation.physicsDebugFlags = scene.GetPhysicsDebugFlags();
    presentation.physicsDebugTransparent = scene.IsPhysicsDebugTransparent();
    presentation.physicsDebugAlpha = scene.GetPhysicsDebugAlpha();
    presentation.physicsDebugContactLinger = scene.GetPhysicsDebugContactLinger();

    if ( scene.HasVsyncOverride() )
    {
        m_outputs.renderPolicy.vsyncEnabled = scene.IsVsyncEnabled();
    }

    if ( scene.HasPipelineSyncOverride() )
    {
        m_outputs.renderPolicy.pipelineSyncEnabled = scene.IsPipelineSyncEnabled();
    }

    presentation.textOnly = scene.IsTextOnly();
    sceneState.isEditableScene = scene.IsEditableScene();
    presentation.waterHidden = scene.IsWaterHidden();
    presentation.terrainHidden = scene.IsTerrainHidden();
    presentation.collisionVisualizer = scene.IsCollisionVisualizerEnabled();
    presentation.broadphaseOverlay = scene.IsBroadphaseOverlayEnabled();
    presentation.waterFreeze = scene.IsWaterFreezeDebugEnabled();
    presentation.waterFlat = scene.IsWaterFlatDebugEnabled();
    const int waterReflectionMode = std::clamp( scene.GetWaterReflectionMode(), 0, 2 );
    presentation.waterRtReflect = waterReflectionMode == 1;
    presentation.waterNoReflect = waterReflectionMode == 2;

    if ( presentation.waterFreeze )
    {
        presentation.frozenWaterTime = static_cast<float>( m_sceneTimeSeconds );
    }

    sceneState.timeScale = scene.GetTimeScale();
    sceneState.isFixedStep = scene.IsFixedStep();
    sceneState.hasCinematicRenderingOverride = scene.HasCinematicRenderingOverride();
    sceneState.isCinematicRenderingEnabled = scene.IsCinematicRenderingEnabled();
    sceneState.hasCinematicExposure = scene.HasCinematicExposure();
    sceneState.cinematicExposure = scene.GetCinematicExposure();
    sceneState.hasCinematicGamma = scene.HasCinematicGamma();
    sceneState.cinematicGamma = scene.GetCinematicGamma();
    sceneState.cinematicOverrideMask = scene.GetCinematicOverrideMask();
    sceneState.cinematicRender = config.cinematicRender;
    ApplyCinematicSceneOverrides( sceneState.cinematicRender, sceneState.cinematicOverrideMask,
                                  scene.GetCinematicRenderConfig() );

    bool isAutomationScene = scene.IsExitOnComplete() || scene.IsScreenshotAndExit() || scene.GetScreenshotFrame() >= 0 ||
                             scene.GetScreenshotMs() >= 0 || scene.GetScreenshotInterval() > 0 ||
                             scene.GetPerfLogPath()[0] != '\0';
#ifdef _DEBUG
    isAutomationScene = isAutomationScene || PhysicsDiagnosticsEnabled();
#endif
    PrepareUiOptions( presentation, m_outputs.uiActivation, scene.GetUIOptions(), m_sceneTimeSeconds,
                      m_request.preserveUIState, isAutomationScene );
    sceneState.targetFrameCount = scene.GetFrameCount();
    sceneState.isExitOnComplete = m_preparedLoad.suppressAutomationExit ? false : scene.IsExitOnComplete();

    SceneCaptureReaction captureReaction;
    captureReaction.kind = SceneCaptureReactionKind::ApplyAutomation;
    captureReaction.automation.screenshotFrame = scene.GetScreenshotFrame();
    captureReaction.automation.screenshotMs = scene.GetScreenshotMs();
    captureReaction.automation.screenshotAndExit = m_preparedLoad.suppressAutomationExit ? false
                                                                                         : scene.IsScreenshotAndExit();
    captureReaction.automation.screenshotInterval = scene.GetScreenshotInterval();
    strcpy_s( captureReaction.automation.screenshotPath, scene.GetScreenshotPath() );
    strcpy_s( captureReaction.automation.screenshotDirectory, scene.GetScreenshotDir() );
    AppendCaptureReaction( captureReaction );

    SceneDiagnosticsReaction perfLog;
    perfLog.kind = SceneDiagnosticsReactionKind::ApplyScenePerfLog;
    perfLog.value = sceneController.PerfPass();
    strcpy_s( perfLog.path, scene.GetPerfLogPath() );
    AppendDiagnosticsReaction( perfLog );

    if ( scene.GetSeed() > 0 )
    {
        rngSeed = scene.GetSeed();
    }

    if ( launchOptions.seedOverride > 0 )
    {
        rngSeed = launchOptions.seedOverride;
    }

    sceneState.rngSeed = rngSeed;
    sceneState.rngState = rngSeed;
}


SkullbonezCore::Core::SbResult SceneLoadTransaction::BuildAuthoredTerrain(
    SceneController& sceneController, SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
    SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions, Assets::AssetSystem& assets,
    const AuthoredScene& scene, Rendering::Dx12FrameOwner* renderFrame, Rendering::Dx12ResourceBuilder* renderResources )
{
    SkullbonezCore::Core::SbResult terrainResult = SkullbonezCore::Core::SbResult::Success();

    if ( scene.HasFlatSlope() )
    {
        terrainResult = UseFlatSlopeTerrain( diagnostics, sceneController.Scene(), assets, config, scene.GetFlatBaseY(),
                                             scene.GetFlatSlopeX(), scene.GetFlatSlopeZ(), renderFrame, renderResources );

        if ( terrainResult.Ok() )
        {
            sceneController.State().hasFlatSlope = true;
            sceneController.State().flatBaseY = scene.GetFlatBaseY();
            sceneController.State().flatSlopeX = scene.GetFlatSlopeX();
            sceneController.State().flatSlopeZ = scene.GetFlatSlopeZ();
        }
    }
    else
    {
        terrainResult = UseDefaultTerrain( diagnostics, sceneController.Scene(), assets, config,
                                           assets.RegisterSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                                           "terrain.raw",
                                                                           config.assetPaths.terrainRaw.c_str() ),
                                           renderFrame, renderResources );
    }

    if ( !terrainResult.Ok() )
    {
        LogSceneLoadFailure( terrainResult, *m_preparedLoad.scenePath );
        return terrainResult;
    }

    if ( !scene.HasFlatSlope() )
    {
        sceneController.State().hasFlatSlope = false;
    }

    ApplyConfiguredWorldEnvironment( sceneController.Scene().Environment(), config,
                                     sceneController.Scene().Terrain().Get() );

    if ( scene.HasWorldOverride() )
    {
        sceneController.Scene().Environment() = WorldEnvironment( scene.GetWorldFluidHeight(), scene.GetWorldFluidDensity(),
                                                                  config.worldForces.gasDensity, scene.GetWorldGravity() );
        sceneController.Scene().Environment().SetMutualGravitySettings( scene.GetWorldMutualGravitySettings() );
        sceneController.Scene().Environment().BindRuntimeConfig( config );
        UpdateWorldTerrainBounds( sceneController.Scene().Environment(), sceneController.Scene().Terrain().Get() );
    }

    ApplyNoWaterOverride( sceneController.Scene().Environment(), sceneController.Scene().Terrain().Get(),
                          launchOptions.noWater );

    if ( m_preparedLoad.shouldPreserveRuntimeState )
    {
        const SceneResetPreservationSnapshot& reset = m_preparedLoad.resetSnapshot;
        const Environment::WorldOverrideChange
            change = sceneController.Scene().Environment().ApplyOverride( reset.worldGravity, reset.worldFluidHeight,
                                                                          reset.worldFluidDensity );

        if ( m_outputs.completedWorldChangeCount >= m_outputs.completedWorldChanges.size() )
        {
            SB_FATAL( "Runtime/SceneLoadTransaction", "Fixed completed world-change capacity exhausted." );
        }

        m_outputs.completedWorldChanges[m_outputs.completedWorldChangeCount++] = { change.previousGravity,
                                                                                   change.previousFluidHeight,
                                                                                   change.previousFluidDensity,
                                                                                   change.gravity,
                                                                                   change.fluidHeight,
                                                                                   change.fluidDensity };
    }

    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult SceneLoadTransaction::PopulateAuthoredEntities(
    SceneController& sceneController, SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
    SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions, const AuthoredScene& scene )
{
    SceneAuthoredSetup::SetUpCameras( sceneController.Scene(), scene );
    const SceneLoadNavigationState& navigation = m_outputs.navigation;
    const bool hasUiSolverCount = navigation.overrides.solverBallCountOverride >= 0 ||
                                  navigation.overrides.solverBoxCountOverride >= 0;
    const bool hasUiModelCount = navigation.overrides.modelCountOverride >= 0;
    const bool hasSceneSolverCount = scene.GetSolverBallCount() > 0 || scene.GetSolverBoxCount() > 0;
    const GeneratedPopulationMode generatedMode = hasUiSolverCount
                                                      ? GeneratedPopulationMode::Solver
                                                      : ( hasUiModelCount
                                                              ? GeneratedPopulationMode::Models
                                                              : ( hasSceneSolverCount ? GeneratedPopulationMode::Solver
                                                                                      : GeneratedPopulationMode::None ) );
    const int generatedBalls = hasUiSolverCount ? navigation.overrides.solverBallCountOverride : scene.GetSolverBallCount();
    const int generatedBoxes = hasUiSolverCount ? navigation.overrides.solverBoxCountOverride : scene.GetSolverBoxCount();
    const SceneGeneratedSetupResult
        generatedModels = SceneGeneratedSetup::TrySetUpRequestedModels( sceneController.State(), config,
                                                                        sceneController.Scene(),
                                                                        launchOptions.generatedObjectTypeOverride,
                                                                        generatedMode,
                                                                        navigation.overrides.modelCountOverride,
                                                                        generatedBalls, generatedBoxes );

    if ( !generatedModels.status.Ok() )
    {
        LogSceneLoadFailure( generatedModels.status, *m_preparedLoad.scenePath );
        return generatedModels.status;
    }

    if ( !generatedModels.applied )
    {
        const SkullbonezCore::Core::SbResult
            authoredSetup = SceneAuthoredSetup::SetUpSceneEntities( diagnostics, sceneController.State(),
                                                                    sceneController.Scene(), m_outputs.automationGates,
                                                                    scene );

        if ( !authoredSetup.Ok() )
        {
            LogSceneLoadFailure( authoredSetup, *m_preparedLoad.scenePath );
            return authoredSetup;
        }
    }

#ifdef _DEBUG
    sceneController.Scene().Physics().SetPhysicsRegressionLogPath( PhysicsRegressionLogPath() );
    sceneController.Scene().Physics().SetPhysicsCollisionTimeLogPath( PhysicsCollisionTimeLogPath() );

    if ( PhysicsDiagnosticsEnabled() )
    {
        sceneController.Scene().Physics().SetPhysicsDiagnosticsPath( PhysicsDiagnosticsPath() );
    }
#endif

    CameraControlState& camera = m_outputs.camera;

    if ( scene.GetTrackHeight() > 0.0f )
    {
        camera.trackHeight = scene.GetTrackHeight();
        camera.trackBallRow.value = 0;
        camera.autoCycleInterval = scene.GetAutoCycleInterval();
    }

    sprintf_s( m_outputs.windowTitle, "%s [SCENE MODE] [%s]", TITLE_TEXT, m_rendererName );

    const bool hasSnapshotState = scene.GetBallStateCount() > 0 || scene.GetBoxStateCount() > 0 ||
                                  scene.GetConvexHullStateCount() > 0;
#ifdef _DEBUG
    const bool shouldPauseSnapshotState = hasSnapshotState && scene.ShouldPauseSnapshotState() &&
                                          !PhysicsDiagnosticsEnabled();
#else
    const bool shouldPauseSnapshotState = hasSnapshotState && scene.ShouldPauseSnapshotState();
#endif

    if ( shouldPauseSnapshotState )
    {
        camera.mode = RunCameraMode::Inspect;
        camera.cameraTime = 0.0f;
        XZBounds unbounded;
        unbounded.m_xMin = -99999.9f;
        unbounded.m_xMax = 99999.9f;
        unbounded.m_zMin = -99999.9f;
        unbounded.m_zMax = 99999.9f;
        const uint32_t activeCam = sceneController.Scene().Cameras().GetSelectedCameraName();
        sceneController.Scene().Cameras().SetCameraXZBounds( activeCam, unbounded );
        camera.inputXMove = 0;
        camera.inputYMove = 0;
        camera.hasMouseLookLastClient = false;
        camera.needsMouseLookReset = true;
    }

    return SkullbonezCore::Core::SbResult::Success();
}


void SceneLoadTransaction::FinalizeLoadedScene( SceneController& sceneController, SkullbonezCore::Core::EngineConfig& config,
                                                RunLaunchOptions& launchOptions, bool retainedPhysicsSleepEnabled,
                                                bool sceneMutualGravityEnabled,
                                                const AuthoredTornadoSystemConfig* sceneTornadoSystem )
{
    SceneSessionState& sceneState = sceneController.State();
    SceneLoadNavigationState& navigation = m_outputs.navigation;
    ScenePresentationValues& presentation = m_outputs.presentation;
    sceneController.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterScenePopulate, 0 );

    if ( m_preparedLoad.shouldPreserveRuntimeState )
    {
        RestoreResetSnapshot( sceneController, navigation.overrides, m_outputs.renderPolicy, presentation, m_outputs.camera,
                              m_preparedLoad.resetSnapshot, m_request.suppressExitOnComplete );
    }

    if ( launchOptions.timeScaleOverride > 0.0f )
    {
        sceneState.timeScale = launchOptions.timeScaleOverride;
    }

    if ( navigation.overrides.timeScaleOverride > 0.0f )
    {
        sceneState.timeScale = navigation.overrides.timeScaleOverride;
    }

    if ( launchOptions.fixedStep )
    {
        sceneState.isFixedStep = true;
    }

    if ( !m_preparedLoad.shouldPreserveRuntimeState )
    {
        Gameplay::TornadoFieldConfig tornadoField;
        Gameplay::TornadoSystemConfig tornadoSystem;
        ApplyTornadoDefaultsForActiveScene( tornadoField, sceneController.Scene().Environment(),
                                            ActiveSceneCinematicConfig( sceneState, config ) );

        if ( sceneTornadoSystem )
        {
            tornadoSystem = ProjectAuthoredTornadoSystem( *sceneTornadoSystem );
            tornadoField.enabled = false;
            sceneController.Scene().Tornado().SetVisualEnabled( true );
        }

        sceneController.Scene().Tornado().SetFieldConfig( tornadoField );
        sceneController.Scene().Tornado().SetSystemConfig( tornadoSystem );
    }

    Gameplay::TornadoFieldConfig tornadoField = sceneController.Scene().Tornado().GetFieldConfig();
    Gameplay::TornadoSystemConfig tornadoSystem = sceneController.Scene().Tornado().GetSystemConfig();

    if ( launchOptions.hasTornadoOverride )
    {
        if ( tornadoSystem.enabled || !tornadoSystem.vortices.empty() )
        {
            tornadoSystem.enabled = launchOptions.tornadoEnabled;
            tornadoField.enabled = false;
        }
        else
        {
            tornadoField.enabled = launchOptions.tornadoEnabled;
        }

        if ( sceneController.Scene().Tornado().VisualAutoEnableWithTornado() )
        {
            sceneController.Scene().Tornado().SetVisualEnabled( launchOptions.tornadoEnabled );
        }
    }

    if ( launchOptions.tornadoVectors )
    {
        tornadoField.visualizeVelocityField = true;
        tornadoSystem.visualizeVelocityField = true;
    }

    sceneController.Scene().Tornado().SetFieldConfig( tornadoField );
    sceneController.Scene().Tornado().SetSystemConfig( tornadoSystem );

    if ( sceneMutualGravityEnabled )
    {
        sceneController.Scene().Physics().SetSleepEnabled( false );
    }
    else if ( !m_preparedLoad.shouldPreserveRuntimeState )
    {
        sceneController.Scene().Physics().SetSleepEnabled( retainedPhysicsSleepEnabled );
    }

    if ( launchOptions.frameCountOverride > 0 )
    {
        sceneState.targetFrameCount = launchOptions.frameCountOverride;
        sceneState.isExitOnComplete = true;
    }

    if ( launchOptions.uiStress )
    {
        SceneDiagnosticsReaction uiStress;
        uiStress.kind = SceneDiagnosticsReactionKind::ConfigureUiStress;
        uiStress.enabled = true;
        uiStress.value = static_cast<int>( launchOptions.uiStressSeed );
        uiStress.secondaryValue = launchOptions.uiStressActions;
        AppendDiagnosticsReaction( uiStress );
        m_outputs.uiActivation.nowSeconds = m_sceneTimeSeconds;
        m_outputs.uiActivation.forceVisible = true;
        m_outputs.uiActivation.forceUnminimized = true;
    }

    if ( launchOptions.graphicsStress )
    {
        sceneState.isInteractiveRun = true;
        sceneState.targetFrameCount = 0;
        sceneState.isTestComplete = false;
        sceneState.isExitOnComplete = false;
        AppendCaptureReaction( { SceneCaptureReactionKind::ResetScreenshot } );
        SceneDiagnosticsReaction closePerfLog;
        closePerfLog.kind = SceneDiagnosticsReactionKind::ClosePerfLog;
        AppendDiagnosticsReaction( closePerfLog );
        SceneDiagnosticsReaction resetPerfLog;
        resetPerfLog.kind = SceneDiagnosticsReactionKind::ResetPerfLogForSceneLoad;
        AppendDiagnosticsReaction( resetPerfLog );
        m_outputs.uiActivation.nowSeconds = m_sceneTimeSeconds;
        m_outputs.uiActivation.forceVisible = true;
        m_outputs.uiActivation.forceUnminimized = true;
    }

    if ( launchOptions.hasCinematicShadowsOverride )
    {
        ActiveSceneCinematicConfig( sceneState, config ).shadow.enabled = launchOptions.cinematicShadows;
        sceneState.cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    }

    if ( launchOptions.hasPhysicsDebugFlagsOverride )
    {
        presentation.physicsDebugFlags = launchOptions.physicsDebugFlagsOverride;
    }

    if ( launchOptions.hasPhysicsDebugTransparentOverride )
    {
        presentation.physicsDebugTransparent = launchOptions.physicsDebugTransparentOverride;
    }

    if ( launchOptions.hasPhysicsDebugAlphaOverride )
    {
        presentation.physicsDebugAlpha = launchOptions.physicsDebugAlphaOverride;
    }

    if ( launchOptions.hasPhysicsDebugContactLingerOverride )
    {
        presentation.physicsDebugContactLinger = launchOptions.physicsDebugContactLingerOverride;
    }
}


SkullbonezCore::Core::SbResult SceneLoadTransaction::LoadAuthoredScene(
    SceneController& sceneController, SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
    SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions, const RunStartupState& startup,
    Assets::AssetSystem& assets, Threading::WorkerPool& workerPool, Rendering::Dx12FrameOwner* renderFrame,
    Rendering::Dx12ResourceBuilder* renderResources, bool retainedPhysicsSleepEnabled )
{
    SceneSessionState& sceneState = sceneController.State();
    sceneState.isSceneMode = true;
    AuthoredScene scene;
    const std::string& scenePath = *m_preparedLoad.scenePath;
    const SkullbonezCore::Core::SbResult sceneLoad = AuthoredScene::TryLoadFromFile( diagnostics, scenePath.c_str(), assets,
                                                                                     scene );

    if ( !sceneLoad.Ok() )
    {
        LogSceneLoadFailure( sceneLoad, scenePath );
        return sceneLoad;
    }

    config.runtimeCapacity.sceneObjectCapacity = scene.HasModelCapacityOverride() ? scene.GetModelCapacity()
                                                                                  : startup.sceneObjectCapacity;
    sceneController.Scene().ApplyRuntimeConfig( config );
    ApplySceneWorkerThreadSetting( config, workerPool,
                                   scene.HasWorkerThreadOverride() ? scene.GetWorkerThreads() : startup.workerThreads );

    unsigned int rngSeed = static_cast<unsigned int>( time( nullptr ) );
    rngSeed ^= static_cast<unsigned int>( sceneState.loadCount ) * 2654435761u;
    rngSeed ^= static_cast<unsigned int>( sceneState.manualResetCount ) * 2246822519u;
    rngSeed = rngSeed == 0 ? 1 : rngSeed;
    ApplyAuthoredValues( sceneController, config, launchOptions, scene, rngSeed );

    SkullbonezCore::Core::SbResult result = BuildAuthoredTerrain( sceneController, diagnostics, config, launchOptions,
                                                                  assets, scene, renderFrame, renderResources );

    if ( !result.Ok() )
    {
        return result;
    }

    result = PopulateAuthoredEntities( sceneController, diagnostics, config, launchOptions, scene );

    if ( !result.Ok() )
    {
        return result;
    }

    AuthoredTornadoSystemConfig authoredTornadoValue;
    const AuthoredTornadoSystemConfig* authoredTornado = nullptr;

    if ( scene.HasTornadoSystem() )
    {
        authoredTornadoValue = scene.GetTornadoSystemConfig();
        authoredTornado = &authoredTornadoValue;
    }

    FinalizeLoadedScene( sceneController, config, launchOptions, retainedPhysicsSleepEnabled,
                         scene.HasMutualGravityEnabled(), authoredTornado );
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult SceneLoadTransaction::LoadGeneratedScene(
    SceneController& sceneController, SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
    SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions,
    const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender, const RunStartupState& startup,
    Assets::AssetSystem& assets, Threading::WorkerPool& workerPool, Rendering::Dx12FrameOwner* renderFrame,
    Rendering::Dx12ResourceBuilder* renderResources )
{
    SceneSessionState& sceneState = sceneController.State();
    SceneLoadNavigationState& navigation = m_outputs.navigation;
    unsigned int rngSeed = static_cast<unsigned int>( time( nullptr ) );
    rngSeed ^= static_cast<unsigned int>( sceneState.loadCount ) * 2654435761u;
    rngSeed ^= static_cast<unsigned int>( sceneState.manualResetCount ) * 2246822519u;
    rngSeed = rngSeed == 0 ? 1 : rngSeed;

    config.runtimeCapacity.sceneObjectCapacity = startup.sceneObjectCapacity;
    sceneController.Scene().ApplyRuntimeConfig( config );
    ApplySceneWorkerThreadSetting( config, workerPool, startup.workerThreads );

    if ( launchOptions.seedOverride > 0 )
    {
        rngSeed = launchOptions.seedOverride;
    }

    sceneState.rngSeed = rngSeed;
    sceneState.rngState = rngSeed;
    const SkullbonezCore::Core::SbResult
        terrainResult = UseDefaultTerrain( diagnostics, sceneController.Scene(), assets, config,
                                           assets.RegisterSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                                           "terrain.raw",
                                                                           config.assetPaths.terrainRaw.c_str() ),
                                           renderFrame, renderResources );

    if ( !terrainResult.Ok() )
    {
        LogSceneLoadFailure( terrainResult, {} );
        return terrainResult;
    }

    ApplyConfiguredWorldEnvironment( sceneController.Scene().Environment(), config,
                                     sceneController.Scene().Terrain().Get() );
    ApplyNoWaterOverride( sceneController.Scene().Environment(), sceneController.Scene().Terrain().Get(),
                          launchOptions.noWater );

    if ( m_preparedLoad.shouldPreserveRuntimeState )
    {
        const SceneResetPreservationSnapshot& reset = m_preparedLoad.resetSnapshot;
        const Environment::WorldOverrideChange
            change = sceneController.Scene().Environment().ApplyOverride( reset.worldGravity, reset.worldFluidHeight,
                                                                          reset.worldFluidDensity );

        if ( m_outputs.completedWorldChangeCount >= m_outputs.completedWorldChanges.size() )
        {
            SB_FATAL( "Runtime/SceneLoadTransaction", "Fixed completed world-change capacity exhausted." );
        }

        m_outputs.completedWorldChanges[m_outputs.completedWorldChangeCount++] = { change.previousGravity,
                                                                                   change.previousFluidHeight,
                                                                                   change.previousFluidDensity,
                                                                                   change.gravity,
                                                                                   change.fluidHeight,
                                                                                   change.fluidDensity };
    }

    sceneState.isSceneMode = false;
    SceneGeneratedSetup::SetUpCameras( sceneController.Scene() );
    const bool hasUiSolverCount = navigation.overrides.solverBallCountOverride >= 0 ||
                                  navigation.overrides.solverBoxCountOverride >= 0;
    const GeneratedPopulationMode generatedMode = hasUiSolverCount ? GeneratedPopulationMode::Solver
                                                                   : GeneratedPopulationMode::Models;
    const int generatedModelCount = navigation.overrides.modelCountOverride >= 0
                                        ? navigation.overrides.modelCountOverride
                                        : SkullbonezCore::Scene::Capacity::DEFAULT_SCENE_OBJECTS;
    const SceneGeneratedSetupResult
        generatedSetup = SceneGeneratedSetup::TrySetUpRequestedModels( sceneState, config, sceneController.Scene(),
                                                                       launchOptions.generatedObjectTypeOverride,
                                                                       generatedMode, generatedModelCount,
                                                                       navigation.overrides.solverBallCountOverride,
                                                                       navigation.overrides.solverBoxCountOverride );

    if ( !generatedSetup.status.Ok() )
    {
        LogSceneLoadFailure( generatedSetup.status, {} );
        return generatedSetup.status;
    }

    SkullbonezCore::UI::RunSceneBrowserState styleBrowser;
    styleBrowser.paths = navigation.browserPaths;
    styleBrowser.selectedCineModeSceneIndex = navigation.selectedCineModeSceneIndex;
    sceneController.ApplyDemoHeroStyle( launchOptions, styleBrowser, assets,
                                        ActiveSceneCinematicConfig( sceneState, config ), defaultCinematicRender );
    navigation.selectedCineModeSceneIndex = styleBrowser.selectedCineModeSceneIndex;
    sprintf_s( m_outputs.windowTitle, "%s [%s]", TITLE_TEXT, m_rendererName );
    return SkullbonezCore::Core::SbResult::Success();
}

SkullbonezCore::Core::SbResult
SceneLoadTransaction::Load( SceneController& sceneController, const SceneLoadRequest& request,
                            SkullbonezCore::Core::SbDiagnosticStore& diagnostics, SkullbonezCore::Core::EngineConfig& config,
                            RunLaunchOptions& launchOptions,
                            const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender,
                            const RunStartupState& startup, Assets::AssetSystem& assets, Threading::WorkerPool& workerPool,
                            Rendering::Dx12FrameOwner* renderFrame, Rendering::Dx12ResourceBuilder* renderResources )
{
    if ( !m_hasPreparedLoad )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Scene load executed without preparation." );
    }

    AdvanceOrFatal( SceneLoadPhaseCursor::Phase::Load, "Load" );
    m_request = request;
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::SceneLoad );

    // Operator sleep policy is physics-owned and survives ordinary scene
    // changes. The scene reset snapshot restores the same owner explicitly.
    const bool retainedPhysicsSleepEnabled = sceneController.Scene().Physics().IsSleepEnabled();

    if ( !request.accepted )
    {
        // Recoverable error: a rejected navigation value cannot identify a scene to load;
        // preserve the active scene and report the owner boundary violation.
        return diagnostics.Failure( "SceneController", "Rejected scene load request reached execution." );
    }

    if ( !request.HasLoad() )
    {
        if ( request.enterInteractiveSceneRun )
        {
            sceneController.State().isInteractiveRun = true;
            sceneController.State().isExitOnComplete = false;
            AppendCaptureReaction( { SceneCaptureReactionKind::DisableAutomationExit } );
        }

        return SkullbonezCore::Core::SbResult::Success();
    }

    SkullbonezCore::Core::SbResult lastSceneLoadResult = SkullbonezCore::Core::SbResult::Success();
    const SceneLoadBeginResult& loadBegin = m_preparedLoad;

    if ( !loadBegin.status.Ok() )
    {
        // Recoverable error: preparation has not mutated or destroyed the old
        // scene after a failed GPU drain, so preserve that state and end load.
        lastSceneLoadResult = loadBegin.status;
        LogSceneLoadFailure( loadBegin.status, loadBegin.scenePath ? *loadBegin.scenePath : std::string {} );
        return lastSceneLoadResult;
    }

    if ( !loadBegin.shouldLoad )
    {
        return lastSceneLoadResult;
    }

    const SceneLifecycleBeginPolicy lifecyclePolicy { request.preserveUIState, request.preserveRuntimeState,
                                                      request.suppressExitOnComplete, request.enterInteractiveSceneRun,
                                                      request.markManualReset };

    // Invariant: generation begins only after preflight and GPU drain succeed,
    // but before any transaction phase mutates the active scene. Every event
    // emitted below therefore belongs to this exact load attempt.
    sceneController.BeginLoadAttempt( request.index, lifecyclePolicy );
    SceneLifecycleConsumerMask beforeUnloadConsumers = SceneLifecycleConsumerBit( SceneLifecycleConsumer::RenderDrain );

    if ( !m_beforeUnloadDiagnosticsComplete )
    {
        SB_FATAL( "Runtime/SceneController", "Scene mutation began before App completed diagnostics unload." );
    }

    beforeUnloadConsumers |= SceneLifecycleConsumerBit( SceneLifecycleConsumer::Diagnostics );
    sceneController.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeSceneUnload, beforeUnloadConsumers );
    CommitLoad( sceneController, m_outputs.navigation, loadBegin );
    m_outputs.applyNavigation = true;

    if ( request.markManualReset )
    {
        sceneController.MarkManualReset();
    }

    if ( request.enterInteractiveSceneRun )
    {
        sceneController.State().isExitOnComplete = false;
        AppendCaptureReaction( { SceneCaptureReactionKind::DisableAutomationExit } );
    }

    const std::string& scenePath = *loadBegin.scenePath;

    // Reset scene-local state; operator HUD preferences are restored below.
    sceneController.State().ResetForLoad( config.cinematicRender );
    SceneDiagnosticsReaction resetDiagnostics;
    resetDiagnostics.kind = SceneDiagnosticsReactionKind::ResetForSceneLoad;
    resetDiagnostics.value = sceneController.PerfPass() + 1;
    AppendDiagnosticsReaction( resetDiagnostics );
    AppendCaptureReaction( { SceneCaptureReactionKind::ResetScreenshot } );
    m_outputs.renderPolicy = { config.runtimeRender.vsyncEnabled, config.runtimeRender.forcePipelineSync };

    sceneController.Scene().Cameras().Reset();
    sceneController.Scene().Clear();
    SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::BeginCapacitySession();

    m_outputs.camera.ResetForSceneLoad( !scenePath.empty() );
    m_outputs.presentation.ResetForSceneLoad();

    // overlayMode intentionally preserved — the user's HUD state persists across scene reloads.

    // Diagnostics reset is a detached reaction applied by App after Load returns,
    // so no synchronous owner receipt belongs to the cleared commit edge.
    sceneController.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneCleared, 0 );
    sceneController.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeScenePopulate, 0 );

    // Branch on file-backed scene mode vs generated demo mode.
    if ( scenePath.empty() )
    {
        lastSceneLoadResult = LoadGeneratedScene( sceneController, diagnostics, config, launchOptions,
                                                  defaultCinematicRender, startup, assets, workerPool, renderFrame,
                                                  renderResources );

        if ( !lastSceneLoadResult.Ok() )
        {
            return lastSceneLoadResult;
        }

        FinalizeLoadedScene( sceneController, config, launchOptions, retainedPhysicsSleepEnabled, false, nullptr );
    }
    else
    {
        lastSceneLoadResult = LoadAuthoredScene( sceneController, diagnostics, config, launchOptions, startup, assets,
                                                 workerPool, renderFrame, renderResources, retainedPhysicsSleepEnabled );

        if ( !lastSceneLoadResult.Ok() )
        {
            return lastSceneLoadResult;
        }
    }

#ifdef _DEBUG
    const SimulationPacingPolicy
        diagnosticsPacingPolicy = ResolveSimulationPacingPolicy( launchOptions.fixedStep,
                                                                 sceneController.State().isFixedStep,
                                                                 sceneController.State().targetFrameCount,
                                                                 sceneController.State().isInteractiveRun );
    const bool effectiveRenderFrameLockstep = diagnosticsPacingPolicy == SimulationPacingPolicy::RenderFrameLockstep;

    // Compatibility: fixed_step remains in the event stream for existing
    // parsers; adjacent fields state its request sources and resolved policy.
    SkullbonezCore::Core::Log()
        .WriteEventf( "scene_started index=%d load=%d path=\"%s\" renderer=\"%s\" target_frames=%d seed=%u "
                      "fixed_step=%d scene_session_render_frame_lockstep_requested=%d explicit_render_frame_lockstep=%d "
                      "effective_render_frame_lockstep=%d physics=%d text=%d models=%d",
                      sceneController.State().currentSceneIndex, sceneController.State().loadCount,
                      scenePath.empty() ? "generated" : scenePath.c_str(), m_rendererName,
                      sceneController.State().targetFrameCount, sceneController.State().rngSeed,
                      sceneController.State().isFixedStep ? 1 : 0, sceneController.State().isFixedStep ? 1 : 0,
                      launchOptions.fixedStep ? 1 : 0, effectiveRenderFrameLockstep ? 1 : 0,
                      sceneController.State().isScenePhysics ? 1 : 0, sceneController.State().isSceneText ? 1 : 0,
                      sceneController.State().modelCount );

    SceneDiagnosticsReaction beginDiagnostics;
    beginDiagnostics.kind = SceneDiagnosticsReactionKind::BeginPhysicsDiagnostics;
    strcpy_s( beginDiagnostics.path, scenePath.c_str() );
    strcpy_s( beginDiagnostics.rendererName, m_rendererName );
    beginDiagnostics.explicitRenderFrameLockstep = launchOptions.fixedStep;
    beginDiagnostics.effectiveRenderFrameLockstep = effectiveRenderFrameLockstep;
    AppendDiagnosticsReaction( beginDiagnostics );
#endif

    // App applies Render-owned cold activation synchronously, then calls
    // CompleteRenderActivation before the request may be recorded or followed by a save.
    m_outputs.renderActivationSceneObjectCapacity = SkullbonezCore::Core::ActiveSceneObjectCapacity( config );
    m_outputs.renderActivationPending = true;
    lastSceneLoadResult = SkullbonezCore::Core::SbResult::Success();
    return lastSceneLoadResult;
}


// Concept: each save owner publishes only its own persisted fields. The
// composed request is synchronous, is never retained, and does not let the
// writer recover Run or collection-order identity.
SkullbonezCore::Core::SbResult SceneController::SaveCurrentDefaults( const SceneDefaultsSaveSnapshot& snapshot ) const
{
    const std::string* scenePath = CurrentPath();

    if ( !State().isSceneMode || !scenePath || scenePath->empty() )
    {
        return m_resultDiagnostics.Failure( "Runtime/SceneController", "No authored scene is active for defaults save" );
    }

    if ( State().isEditableScene )
    {
        const SkullbonezCore::Core::SbResult
            saveResult = SaveEditableSceneBeforeReplacement( m_resultDiagnostics, scenePath->c_str(), Scene().GetSaveState(),
                                                             State().GetSaveState(),
                                                             GameObjects::PresentationSaveState { snapshot.presentation
                                                                                                      .waterHidden,
                                                                                                  snapshot.presentation
                                                                                                      .terrainHidden } );

        return saveResult;
    }

    std::ifstream input( *scenePath );

    if ( !input )
    {
        return m_resultDiagnostics.Failure( "Runtime/SceneController", "Could not read active scene defaults file: %s",
                                            scenePath->c_str() );
    }

    Json root = Json::parse( input, nullptr, false );

    if ( root.is_discarded() )
    {
        return m_resultDiagnostics.Failure( "Runtime/SceneController", "Active scene defaults file is not valid JSON: %s",
                                            scenePath->c_str() );
    }

    if ( !root.is_object() )
    {
        return m_resultDiagnostics.Failure( "Runtime/SceneController", "Active scene defaults root is not an object: %s",
                                            scenePath->c_str() );
    }

    root["format"] = "skullbonez.scene.json";
    root["version"] = 1;
    Json& simulation = EnsureJsonObject( root, "simulation" );
    Json& playback = EnsureJsonObject( root, "playback" );
    Json& runtime = EnsureJsonObject( root, "runtime" );
    Json& debug = EnsureJsonObject( root, "debug" );
    Json& physicsDebug = EnsureJsonObject( debug, "physics" );
    Json& world = EnsureJsonObject( simulation, "world" );

    simulation["physics"] = State().isScenePhysics;
    simulation["text"] = State().isSceneText;
    simulation["textOnly"] = snapshot.presentation.textOnly;
    runtime["vsync"] = snapshot.renderPolicy.vsyncEnabled;
    runtime["pipelineSync"] = snapshot.renderPolicy.pipelineSyncEnabled;
    playback["fixedStep"] = State().isFixedStep;

    if ( State().targetFrameCount > 0 )
    {
        playback["frames"] = State().targetFrameCount;
    }
    else
    {
        playback["frames"] = "unlimited";
    }

    simulation["seed"] = (std::max)( 1u, State().rngSeed );
    simulation["timeScale"] = State().timeScale;
    playback["exitOnComplete"] = State().isExitOnComplete;

    physicsDebug["axes"] = ( snapshot.presentation.physicsDebugFlags & PHYSICS_DEBUG_AXES ) != 0;
    physicsDebug["contacts"] = ( snapshot.presentation.physicsDebugFlags & PHYSICS_DEBUG_CONTACTS ) != 0;
    physicsDebug["sleep"] = ( snapshot.presentation.physicsDebugFlags & PHYSICS_DEBUG_SLEEP ) != 0;
    physicsDebug["pipeline"] = ( snapshot.presentation.physicsDebugFlags & PHYSICS_DEBUG_PIPELINE ) != 0;
    physicsDebug["terrainContact"] = ( snapshot.presentation.physicsDebugFlags & PHYSICS_DEBUG_TERRAIN_CONTACT ) != 0;
    physicsDebug["transparent"] = snapshot.presentation.physicsDebugTransparent;
    physicsDebug["alpha"] = snapshot.presentation.physicsDebugAlpha;
    physicsDebug["contactLinger"] = snapshot.presentation.physicsDebugContactLinger;

    debug["collisionVisualizer"] = snapshot.presentation.collisionVisualizer;
    debug["broadphaseOverlay"] = snapshot.presentation.broadphaseOverlay;
    debug["waterFreeze"] = snapshot.presentation.waterFreeze;
    debug["waterFlat"] = snapshot.presentation.waterFlat;
    debug["waterHidden"] = snapshot.presentation.waterHidden;
    debug["terrainHidden"] = snapshot.presentation.terrainHidden;
    debug["waterReflection"] = WaterReflectionJsonValue( snapshot.presentation.waterNoReflect,
                                                         snapshot.presentation.waterRtReflect );

    if ( snapshot.camera.writeTrackHeight )
    {
        playback["trackHeight"] = snapshot.camera.trackHeight;
    }
    else
    {
        playback.erase( "trackHeight" );
    }

    if ( snapshot.camera.writeAutoCycleInterval )
    {
        playback["autoCycleInterval"] = snapshot.camera.autoCycleInterval;
    }
    else
    {
        playback.erase( "autoCycleInterval" );
    }

    world["gravity"] = Scene().Environment().GetGravity();
    world["fluidHeight"] = Scene().Environment().GetFluidSurfaceHeight();
    world["fluidDensity"] = Scene().Environment().GetFluidDensity();
    const MutualGravitySettings& mutualGravity = Scene().Environment().GetMutualGravitySettings();

    if ( mutualGravity.enabled )
    {
        world["mutualGravity"] = {
            { "enabled", true },
            { "gravitationalConstant", mutualGravity.gravitationalConstant },
            { "softeningLength", mutualGravity.softeningLength },
            { "elasticCollisions", mutualGravity.elasticCollisions },
        };
    }
    else
    {
        world.erase( "mutualGravity" );
    }

    SetTouchedCinematicSceneProperties( root, State().uiCinematicOverrideMask, State().cinematicRender );

    if ( snapshot.generatedCounts.modelCount >= 0 )
    {
        simulation["solverBalls"] = snapshot.generatedCounts.modelCount;
        simulation.erase( "solverBoxes" );
    }
    else if ( State().solverBallCount > 0 || State().solverBoxCount > 0 || snapshot.generatedCounts.solverBallCount >= 0 ||
              snapshot.generatedCounts.solverBoxCount >= 0 )
    {
        simulation["solverBalls"] = State().solverBallCount;
        simulation["solverBoxes"] = State().solverBoxCount;
    }

    std::ofstream output( *scenePath, std::ios::trunc );

    if ( !output )
    {
        return m_resultDiagnostics.Failure( "Runtime/SceneController", "Could not open active scene defaults for write: %s",
                                            scenePath->c_str() );
    }

    output << root.dump( 2 ) << '\n';

    if ( !output.good() )
    {
        return m_resultDiagnostics.Failure( "Runtime/SceneController", "Could not write active scene defaults: %s",
                                            scenePath->c_str() );
    }

    return SkullbonezCore::Core::SbResult::Success();
}
