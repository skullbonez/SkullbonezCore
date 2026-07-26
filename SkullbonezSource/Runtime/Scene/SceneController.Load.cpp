/*
File: SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
Purpose:
  Loads, resets, and advances authored and generated scenes.

Summary:
  SceneController owns cold scene mutation and borrows only the owners required

  while scene storage is changing. SceneLoadTransaction owns the phase cursor
  and detached outputs, then sequences idempotent reactions at the excluded
  camera, input, interaction, tool, Replay, UI, and validation owners.

Glossary:
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Render setup owners: Exact DX12 frame-drain and cold resource-build
    capabilities plus the runtime renderer used during scene replacement.
  Lifecycle generation: Monotonic value identifying one post-preflight load
    attempt, even when it fails before activation.
  Lane R result: Recoverable scene-load or renderer-drain failure carrying
    owner/message diagnostics so load stops before unsafe resource replacement.
  Required scene contact: Authored pair gate that marks a scenario objective
    once two bodies have produced an exact contact.
  Authored projection: Cold-load, field-by-field copy from parser DTOs into an
    owning subsystem's runtime values.
  Capacity session: Per-scene live/high-water interval advanced only after the
    old stores are cleared and before new scene population.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
    surface.
  - DXR reflection setup may run only after RuntimeRenderer binds every concrete
    DX12 owner needed by scene population.
  - Scene/model/terrain destruction starts only after a successful GPU drain.
  - Load orchestration retains no caller pointer, callback, or mutable owner bag.
  - Tornado projection copies every authored field here so Gameplay never
    depends upward on Scene or parser vocabulary.
  - Capacity rows are logged before clear; the next capacity session begins
    after clear and before population.
Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SceneController.h"
#include "SceneLoadTransaction.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../../Core/WindowConstants.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../Interaction/OperatorCommandApplier.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Camera/AttachedCameraController.h"
#include "../Input/InputRouter.h"
#include "../Input/Input.h"
#include "../App/InputFrame.h"
#include "../App/ReplayRuntime.h"
#include "../App/RunStartupState.h"
#include "../App/RunTimerState.h"
#include "../App/Window.h"
#include "../Render/RuntimeRenderer.h"
#include "SceneRuntimeCoordinator.h"
#include "SceneRuntimeLoad.h"
#include "SceneSaveOperations.h"
#include "SceneRuntimeReset.h"
#include "SceneRuntimeStyle.h"
#include "SceneRuntimeUiOptions.h"
#include "../Editor/EditorTools.h"
#include "../Editor/EditorHullAssets.h"
#include "../../Physics/Ragdoll.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/SbResult.h"
#include "../../Core/WorkerPool.h"
#include "../../Rendering/DX12/Dx12FrameOwner.h"
#include "../../Rendering/DX12/RenderDeviceDX12.h"
#include "../../Rendering/RenderRaytracingTypes.h"
#include "../../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../../UI/UI.h"
#include "../../Scene/AuthoredScene.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"

#include <utility>

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayTimelineOperations;
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
using SkullbonezCore::Hardware::Input;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Rendering::MeshDX12;
using namespace SkullbonezCore::Runtime::RunInternal;
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

    // Boundary: Scene owns cold authored DTOs. Runtime performs the exhaustive
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

    const Quaternion xRotation( sinf( xHalf ), 0.0f, 0.0f, cosf( xHalf ) );
    const Quaternion yRotation( 0.0f, sinf( yHalf ), 0.0f, cosf( yHalf ) );
    const Quaternion zRotation( 0.0f, 0.0f, sinf( zHalf ), cosf( zHalf ) );

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
    const char* owner = result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "Runtime/Scene";
    const char* message = result.error.message[0] != '\0' ? result.error.message : "scene setup failed without a message";

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
        cinematic["basinMask"] = Json::array( { c.basinCenterX, c.basinCenterZ, c.basinRadiusX, c.basinRadiusZ, c.basinFeather } );
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

SceneAuthoredCameraContext BuildSceneAuthoredCameraContext( SceneWorld& sceneWorld )
{
    return SceneAuthoredCameraContext { sceneWorld };
}

SceneAuthoredModelContext BuildSceneAuthoredModelContext( SceneSessionState& sceneState, SceneWorld& sceneWorld,
                                                          SceneAutomationGateConfiguration& automationGates )
{
    return SceneAuthoredModelContext { sceneState, sceneWorld, automationGates };
}

SceneGeneratedCameraContext BuildSceneGeneratedCameraContext( SceneWorld& sceneWorld )
{
    return SceneGeneratedCameraContext { sceneWorld };
}

SceneGeneratedModelContext BuildSceneGeneratedModelContext( SceneSessionState& scene,
                                                            const SkullbonezCore::Core::EngineConfig& config,
                                                            SceneWorld& sceneWorld,
                                                            GeneratedObjectTypeOverride objectTypeOverride )
{
    return SceneGeneratedModelContext { scene, config, sceneWorld, objectTypeOverride };
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

SkullbonezCore::Core::SbResult UseDefaultTerrain( SceneWorld& sceneWorld, SkullbonezCore::Assets::AssetSystem& assets,
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
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/RunScene",
                                                        "Renderer resource factory unavailable for terrain load." );
    }

    if ( !terrainOwner.Get() || terrainOwner.IsFlatSlope() )
    {

        if ( renderFrame )
        {
            const SkullbonezCore::Core::SbResult flushResult = renderFrame->FlushGPU();

            if ( !flushResult.ok )
            {

                // Lane R: keep the currently owned terrain alive when its GPU
                // references cannot be proven drained.
                return flushResult;
            }
        }

        std::unique_ptr<Terrain> terrain;
        const SkullbonezCore::Core::SbResult terrainResult = Terrain::TryCreateFromHeightMap( terrainRawPath.c_str(), 256, 8,
                                                                                              15, config, assets,
                                                                                              *renderResources, terrain );

        if ( !terrainResult.ok )
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

SkullbonezCore::Core::SbResult UseFlatSlopeTerrain( SceneWorld& sceneWorld, SkullbonezCore::Assets::AssetSystem& assets,
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
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/RunScene",
                                                        "Renderer resource factory unavailable for flat terrain." );
    }

    if ( renderFrame )
    {
        const SkullbonezCore::Core::SbResult flushResult = renderFrame->FlushGPU();

        if ( !flushResult.ok )
        {

            // Lane R: assignment below destroys the old terrain. Leave it
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


void SceneLoadTransaction::Outputs::ResetForLoad()
{
    uiActivation = SceneUiActivation {};
    automationGates.Reset();
    navigation = SceneLoadNavigationState {};
    presentation = OverlayDebugState {};
    camera = CameraControlState {};
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

        // Lane F: accepting an out-of-order phase would expose partially
        // updated scene owners or publish presentation before reactions.
        SB_FATAL( "Runtime/SceneLoadTransaction", "Illegal phase transition. operation=%s current=%u next=%u", operation,
                  static_cast<unsigned int>( current ), static_cast<unsigned int>( next ) );
    }
}


void SceneLoadTransaction::FinishLoadPhase()
{

    if ( m_phase.Current() == SceneLoadPhaseCursor::Phase::Idle )
    {
        AdvanceOrFatal( SceneLoadPhaseCursor::Phase::Load, "FinishLoadPhase" );
        return;
    }

    if ( m_phase.Current() != SceneLoadPhaseCursor::Phase::Load )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Load phase finished from an invalid phase. current=%u",
                  static_cast<unsigned int>( m_phase.Current() ) );
    }
}


void SceneLoadTransaction::CaptureSubmittedState( const CameraControlState& camera,
                                                  const SceneLoadNavigationState& navigation, const OverlayDebugState& debug,
                                                  const char* rendererName, double sceneTimeSeconds )
{

    // Why: submitted navigation and presentation values own growable cold-load
    // storage. Attribute their copies to SceneLoad even when the transaction is
    // opened from the steady-gameplay frame boundary.
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope allocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );

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
    m_outputs.presentation = debug;
    sprintf_s( m_rendererName, "%s", rendererName ? rendererName : "unknown" );
    m_sceneTimeSeconds = sceneTimeSeconds;
}


SkullbonezCore::Core::SbResult
SceneLoadTransaction::Load( SceneController& sceneController, const SceneLoadRequest& request,
                            SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions,
                            const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender,
                            const RunStartupState& startup, Assets::AssetSystem& assets, Threading::WorkerPool& workerPool,
                            DiagnosticsRuntime& diagnosticsRuntime, Rendering::Dx12FrameOwner* renderFrame,
                            Rendering::Dx12ResourceBuilder* renderResources, RuntimeRenderer& renderer )
{
    AdvanceOrFatal( SceneLoadPhaseCursor::Phase::Load, "Load" );
    m_request = request;
    return sceneController.Load( request, config, launchOptions, defaultCinematicRender, startup, assets, workerPool,
                                 diagnosticsRuntime, renderFrame, renderResources, renderer, *this );
}


void SceneLoadTransaction::PreserveInactiveDevelopmentUi()
{

    if ( m_phase.Current() != SceneLoadPhaseCursor::Phase::Load )
    {
        SB_FATAL( "Runtime/SceneLoadTransaction", "Development UI policy changed outside the load phase. current=%u",
                  static_cast<unsigned int>( m_phase.Current() ) );
    }

    m_outputs.uiActivation.preserveUIState = true;
    m_outputs.uiActivation.forceVisible = false;
    m_outputs.uiActivation.forceUnminimized = false;
}


void SceneLoadTransaction::ApplyRuntimeReactions( const RunLaunchOptions& launchOptions, RunTimerState& timers,
                                                  RuntimeOverlayDiagnostics& overlays, SceneController& sceneController,
                                                  InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                                  CameraControlState& camera, AttachedCameraController& attachedCamera,
                                                  RuntimeTools& runtimeTools, ReplayRuntime& replayRuntime )
{
    AdvanceOrFatal( SceneLoadPhaseCursor::Phase::RuntimeReactions, "ApplyRuntimeReactions" );
    Outputs& outputs = m_outputs;

    // Lifetime: lifecycle identity stays owned by SceneController. Consumers
    // sample this reference synchronously and retain only their generation.
    const SceneLifecyclePacket& lifecycle = sceneController.LifecyclePacket();

    // Invariant: reactive owners consume the generation before external
    // UI/validation effects. This preserves their former end-of-Load ordering
    // without returning them to the transaction participant graph.
    timers.ObserveSceneLifecycle( lifecycle );
    overlays.ObserveSceneLifecycle( lifecycle, outputs.presentation );

    for ( std::size_t index = 0; index < outputs.completedWorldChangeCount; ++index )
    {
        const SceneLoadCompletedWorldChange& change = outputs.completedWorldChanges[index];
        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildWorldOverride( change.previousGravity, change.previousFluidHeight,
                                                                                     change.previousFluidDensity, change.gravity,
                                                                                     change.fluidHeight, change.fluidDensity ) );
    }

    runtimeTools.ObserveSceneLifecycle( lifecycle, sceneController.Scene(), inputRouter, interaction );
    attachedCamera.ObserveSceneLifecycle( lifecycle );
    replayRuntime.ObserveSceneLifecycleAfterClear( lifecycle, interaction, inputRouter );

    // Invariant: ResetForSceneLoad first selects Scene/Demo. Only authored
    // snapshot pause policy changes the detached result to Inspect, so the mode
    // is the authoritative activation value and no parallel boolean is needed.
    const bool enterInspectAfterActivation = outputs.camera.mode == RunCameraMode::Inspect;
    interaction.ObserveSceneLifecycle( lifecycle, enterInspectAfterActivation );
    camera.ObserveSceneLifecycle( lifecycle, outputs.camera );

    if ( inputRouter.ObserveSceneLifecycle( lifecycle, enterInspectAfterActivation ) )
    {
        Hardware::Input::ResetMouseLookDeltas();
    }

    const auto replayRestoreMode = [&]()
    {
        RunCameraMode restoreMode = replayRuntime.BuildInputView().restoreCameraMode;

        if ( sceneController.State().isSceneMode )
        {
            restoreMode = restoreMode == RunCameraMode::Demo ? RunCameraMode::Scene : restoreMode;
        }
        else if ( restoreMode == RunCameraMode::Scene )
        {
            restoreMode = sceneController.Scene().SceneEntityCount() > 0 ? RunCameraMode::Demo : RunCameraMode::Inspect;
        }
        else if ( restoreMode == RunCameraMode::Demo && sceneController.Scene().SceneEntityCount() <= 0 )
        {
            restoreMode = RunCameraMode::Inspect;
        }

        return restoreMode;
    };

    const ReplaySceneTimelineResetInput
        timelineReset = DescribeReplaySceneTimeline( sceneController, outputs.navigation.overrides, sceneController.State(),
                                                     sceneController.Scene().ActiveSceneObjectCapacity(),
                                                     static_cast<uint32_t>( launchOptions.generatedObjectTypeOverride ) );

    replayRuntime.ObserveSceneLifecycleAfterActivation( lifecycle, timelineReset, inputRouter, interaction,
                                                        &sceneController.Scene().Cameras(),
                                                        sceneController.Scene().Terrain().Get(), camera, replayRestoreMode(),
                                                        attachedCamera.State().activeFollow, camera.director.grabbed );

    if ( launchOptions.replayGuideArcsAtStartup && lifecycle.event == SceneRuntimeLifecycleEvent::AfterSceneActivated )
    {

        // Why: clear-phase processing restores the product's default-off state.
        // An explicit cold CLI request is reapplied only after activation.
        replayRuntime.SetGuideArcsEnabled( true );
    }

    // Invariant: only a successfully completed defaults write enters this
    // batch, so a failed Lane-R save cannot advance the editor clean cursor.

    for ( std::size_t index = 0; index < outputs.completedRequests.count; ++index )
    {

        if ( outputs.completedRequests.requests[index].type == SceneRequestType::SaveCurrentDefaults )
        {
            runtimeTools.Editor().history.MarkClean();
            break;
        }
    }

    for ( std::size_t index = 0; index < outputs.completedRequests.count; ++index )
    {
        const SceneRequest& request = outputs.completedRequests.requests[index];
        ReplayOwnerEventCode eventCode = ReplayOwnerEventCode::SceneLoadBrowserIndex;

        switch ( request.type )
        {
        case SceneRequestType::LoadBrowserIndex:
            eventCode = ReplayOwnerEventCode::SceneLoadBrowserIndex;
            break;
        case SceneRequestType::LoadDemoScene:
            eventCode = ReplayOwnerEventCode::SceneLoadDemo;
            break;
        case SceneRequestType::ResetCurrentScene:
            eventCode = ReplayOwnerEventCode::SceneReset;
            break;
        case SceneRequestType::CreateScene:
            eventCode = ReplayOwnerEventCode::SceneCreate;
            break;
        case SceneRequestType::SaveCurrentDefaults:
            eventCode = ReplayOwnerEventCode::SceneSaveDefaults;
            break;
        }

        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildCommand( ReplayEventKind::OwnerAction, 0, true,
                                                                               ReplaySceneRequestFlags( request ),
                                                                               static_cast<int32_t>( eventCode ),
                                                                               request.index, 0, 0, 0,
                                                                               request.type == SceneRequestType::CreateScene
                                                                                   ? request.text
                                                                                   : ReplayOwnerEventName( eventCode ) ) );
    }
}


void SceneLoadTransaction::ApplyPresentationOutputs( Window& window, UI::InGameUI& operatorUi,
                                                     RuntimeValidationHarness& validationHarness,
                                                     const RunLaunchOptions& launchOptions,
                                                     Rendering::Dx12RenderDevice* renderDevice, bool rendererVsyncEnabled,
                                                     SceneController& sceneController )
{
    AdvanceOrFatal( SceneLoadPhaseCursor::Phase::Presentation, "ApplyPresentationOutputs" );
    Outputs& outputs = m_outputs;
    const SceneLifecyclePacket& lifecycle = sceneController.LifecyclePacket();
    validationHarness.SceneGates().ObserveSceneLifecycle( lifecycle, std::move( outputs.automationGates ) );

    // Invariant: device swap policy commits only with a fully activated scene;
    // partial loads may publish clear/populate reactions but not presentation.

    if ( renderDevice && SceneLifecycleReached( lifecycle.event, SceneRuntimeLifecycleEvent::AfterSceneActivated ) )
    {
        renderDevice->SetVsyncEnabled( rendererVsyncEnabled );
    }

    if ( outputs.windowTitle[0] != '\0' )
    {
        window.SetTitleText( outputs.windowTitle );
    }

    if ( outputs.applyNavigation )
    {
        ApplySceneLoadNavigationState( operatorUi.SceneNavigation(), outputs.navigation );
    }

    if ( outputs.refreshSceneBrowser )
    {

        // Why: scene creation writes editor-authored IO inside the scene owner,
        // but UI keeps display names and stable c-string views. Rebuild those
        // views after the request batch returns to the UI boundary.
        RefreshSceneBrowserList( operatorUi.SceneNavigation().browser );
    }

    ApplySceneUiActivation( operatorUi, outputs.uiActivation );
    validationHarness.ObserveSceneLifecycle( lifecycle, launchOptions );
    AdvanceOrFatal( SceneLoadPhaseCursor::Phase::Complete, "CompletePresentation" );
}

SkullbonezCore::Core::SbResult SceneController::Load( const SceneLoadRequest& request, SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions,
                                                      const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender, const RunStartupState& startup,
                                                      Assets::AssetSystem& assets, Threading::WorkerPool& workerPool, DiagnosticsRuntime& diagnosticsRuntime,
                                                      Rendering::Dx12FrameOwner* renderFrame, Rendering::Dx12ResourceBuilder* renderResources, RuntimeRenderer& renderer,
                                                      SceneLoadTransaction& transaction )
{
    SceneLoadTransaction::Outputs& consumerOutputs = transaction.m_outputs;

    // Lifetime: these aliases make cold scene mutation readable without
    // recovering a retained context. They refer only to synchronously borrowed
    // phase inputs or transaction-owned outputs and die with this call.
    const char* rendererName = transaction.m_rendererName;
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::SceneLoad );
    SceneLoadNavigationState& sceneNavigation = consumerOutputs.navigation;
    OverlayDebugState& debug = consumerOutputs.presentation;
    CameraControlState& camera = consumerOutputs.camera;
    const auto recordCompletedWorldChange = [&]( const WorldOverrideChange& change )
    {

        if ( consumerOutputs.completedWorldChangeCount >= consumerOutputs.completedWorldChanges.size() )
        {
            SB_FATAL( "Runtime/SceneController", "Fixed completed world-change capacity exhausted." );
        }

        consumerOutputs.completedWorldChanges
            [consumerOutputs.completedWorldChangeCount++] = SceneLoadCompletedWorldChange { change.previousGravity,
                                                                                            change.previousFluidHeight,
                                                                                            change.previousFluidDensity,
                                                                                            change.gravity,
                                                                                            change.fluidHeight,
                                                                                            change.fluidDensity };
    };

    // Operator sleep policy is physics-owned and survives ordinary scene
    // changes. The scene reset snapshot restores the same owner explicitly.
    const bool retainedPhysicsSleepEnabled = Scene().Physics().IsSleepEnabled();

    if ( !request.accepted )
    {

        // Lane R: a rejected navigation value cannot identify a scene to load;
        // preserve the active scene and report the owner boundary violation.
        return SkullbonezCore::Core::SbResult::Failure( "SceneController",
                                                        "Rejected scene load request reached execution." );
    }

    if ( !request.HasLoad() )
    {

        if ( request.enterInteractiveSceneRun )
        {
            State().isInteractiveRun = true;
            State().isExitOnComplete = false;
            diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
        }

        return SkullbonezCore::Core::SbResult::Success();
    }

    const int index = request.index;
    const bool preserveUIState = request.preserveUIState;
    const bool suppressExitOnComplete = request.suppressExitOnComplete;
    const bool preserveRuntimeState = request.preserveRuntimeState;
    SceneController& sceneController = *this;
    const auto SceneState = [this]() -> SceneSessionState& { return State(); };

    SkullbonezCore::Core::SbResult lastSceneLoadResult = SkullbonezCore::Core::SbResult::Success();
    const SceneRuntimeLoadBeginResult loadBegin = PrepareSceneRuntimeLoad( sceneController, sceneNavigation.overrides,
                                                                           renderer, debug, camera, renderFrame,
                                                                           request.enterInteractiveSceneRun ||
                                                                               launchOptions.interactiveSceneRun,
                                                                           index, suppressExitOnComplete,
                                                                           preserveRuntimeState );

    if ( !loadBegin.status.ok )
    {

        // Lane R: preparation has not mutated or destroyed the old
        // scene after a failed GPU drain, so preserve that state and end load.
        lastSceneLoadResult = loadBegin.status;
        LogSceneLoadFailure( loadBegin.status, loadBegin.scenePath ? *loadBegin.scenePath : std::string {} );
        return lastSceneLoadResult;
    }

    if ( !loadBegin.shouldLoad )
    {
        return lastSceneLoadResult;
    }

    const SceneLifecycleBeginPolicy lifecyclePolicy { preserveUIState, preserveRuntimeState, suppressExitOnComplete,
                                                      request.enterInteractiveSceneRun, request.markManualReset };

    // Invariant: generation begins only after preflight and GPU drain succeed,
    // but before any transaction phase mutates the active scene. Every event
    // emitted below therefore belongs to this exact load attempt.
    sceneController.BeginLoadAttempt( index, lifecyclePolicy );
    SceneLifecycleConsumerMask beforeUnloadConsumers = SceneLifecycleConsumerBit( SceneLifecycleConsumer::RenderDrain );
    const std::string* unloadingScenePath = sceneController.CurrentPath();
    diagnosticsRuntime.BeforeSceneUnload( SceneState(), unloadingScenePath ? unloadingScenePath->c_str() : nullptr );
    beforeUnloadConsumers |= SceneLifecycleConsumerBit( SceneLifecycleConsumer::Diagnostics );
    sceneController.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeSceneUnload, beforeUnloadConsumers );
    CommitSceneRuntimeLoad( sceneController, sceneNavigation, loadBegin );
    consumerOutputs.applyNavigation = true;

    if ( request.markManualReset )
    {
        sceneController.MarkManualReset();
    }

    if ( request.enterInteractiveSceneRun )
    {
        State().isExitOnComplete = false;
        diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
    }

    const bool suppressAutomationExit = loadBegin.suppressAutomationExit;
    const bool shouldPreserveRuntimeState = loadBegin.shouldPreserveRuntimeState;
    const SceneRuntimeResetSnapshot& resetSnapshot = loadBegin.resetSnapshot;
    const std::string& scenePath = *loadBegin.scenePath;
    SceneLifecycleConsumerMask afterClearConsumers = 0;

    // Reset scene-local state; operator HUD preferences are restored below.
    SceneState().ResetForLoad( config.cinematicRender );
    diagnosticsRuntime.ResetForSceneLoad( m_perfPass + 1 );
    afterClearConsumers |= SceneLifecycleConsumerBit( SceneLifecycleConsumer::Diagnostics );
    renderer.ResetSceneRuntimePolicyFromConfig();

    sceneController.Scene().Cameras().Reset();
    sceneController.Scene().Clear();
    SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::BeginCapacitySession();

    camera.ResetForSceneLoad( !scenePath.empty() );
    debug.ResetForSceneLoad();

    // overlayMode intentionally preserved — the user's HUD state persists across scene reloads.

    // Reseed RNG. Unseeded reruns mix in the load/reset counters so quick repeated
    // Q resets do not collapse to the same time(nullptr) seed. Scene files and CLI
    // overrides can still pin this exactly for repro.
    unsigned int rngSeed = static_cast<unsigned int>( time( nullptr ) );
    rngSeed ^= static_cast<unsigned int>( SceneState().loadCount ) * 2654435761u;
    rngSeed ^= static_cast<unsigned int>( SceneState().manualResetCount ) * 2246822519u;

    if ( rngSeed == 0 )
    {
        rngSeed = 1;
    }

    bool hasSceneTornadoSystem = false;
    bool sceneMutualGravityEnabled = false;
    AuthoredTornadoSystemConfig sceneTornadoSystem;

    // Each bit is attached to its concrete call above. SceneRuntime rejects the
    // phase if a future edit drops an owner receipt without updating policy.
    sceneController.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneCleared, afterClearConsumers );
    sceneController.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeScenePopulate, 0 );

    // Branch on file-backed scene mode vs generated demo mode.

    if ( scenePath.empty() )
    {
        config.runtimeCapacity.sceneObjectCapacity = startup.sceneObjectCapacity;
        sceneController.Scene().ApplyRuntimeConfig( config );
        ApplySceneWorkerThreadSetting( config, workerPool, startup.workerThreads );

        if ( launchOptions.seedOverride > 0 )
        {
            rngSeed = launchOptions.seedOverride;
        }

        SceneState().rngSeed = rngSeed;
        SceneState().rngState = rngSeed;
        const SkullbonezCore::Core::SbResult
            terrainResult = UseDefaultTerrain( sceneController.Scene(), assets, config,
                                               assets.RegisterSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                                               "terrain.raw",
                                                                               config.assetPaths.terrainRaw.c_str() ),
                                               renderFrame, renderResources );

        if ( !terrainResult.ok )
        {
            lastSceneLoadResult = terrainResult;
            LogSceneLoadFailure( terrainResult, scenePath );
            return lastSceneLoadResult;
        }

        ApplyConfiguredWorldEnvironment( sceneController.Scene().Environment(), config,
                                         sceneController.Scene().Terrain().Get() );

        ApplyNoWaterOverride( sceneController.Scene().Environment(), sceneController.Scene().Terrain().Get(),
                              launchOptions.noWater );

        if ( shouldPreserveRuntimeState )
        {

            // Restore setup-affecting live controls before the generated model pool is rebuilt.
            // Other visual/debug controls are restored later after scene JSON has loaded.
            const WorldOverrideChange change = ApplyUIWorldOverride( sceneController.Scene().Environment(),
                                                                     resetSnapshot.worldGravity,
                                                                     resetSnapshot.worldFluidHeight,
                                                                     resetSnapshot.worldFluidDensity );

            recordCompletedWorldChange( change );
        }

        SceneState().isSceneMode = false;
        SceneGeneratedSetup::SetUpCameras( BuildSceneGeneratedCameraContext( sceneController.Scene() ) );
        const SceneGeneratedSetupResult generatedSetup = SceneGeneratedSetup::
            TrySetUpRequestedModels( BuildSceneGeneratedModelContext( SceneState(), config, sceneController.Scene(),
                                                                      launchOptions.generatedObjectTypeOverride ),
                                     SceneGeneratedPopulationRequest { sceneNavigation.overrides.modelCountOverride,
                                                                       sceneNavigation.overrides.solverBallCountOverride,
                                                                       sceneNavigation.overrides.solverBoxCountOverride, 0,
                                                                       0,
                                                                       SkullbonezCore::Scene::Capacity::
                                                                           DEFAULT_SCENE_OBJECTS },
                                     true );

        if ( !generatedSetup.status.ok )
        {
            lastSceneLoadResult = generatedSetup.status;
            LogSceneLoadFailure( generatedSetup.status, scenePath );
            return lastSceneLoadResult;
        }

        SkullbonezCore::UI::RunSceneBrowserState styleBrowser;
        styleBrowser.paths = sceneNavigation.browserPaths;
        styleBrowser.selectedCineModeSceneIndex = sceneNavigation.selectedCineModeSceneIndex;
        ApplyDemoHeroStyleOverride( SceneRuntimeStyleContext { launchOptions, SceneState(), styleBrowser, sceneController.Scene(), assets,
                                                               ActiveSceneCinematicConfig( SceneState(), config ), defaultCinematicRender } );

        sceneNavigation.selectedCineModeSceneIndex = styleBrowser.selectedCineModeSceneIndex;
        sprintf_s( consumerOutputs.windowTitle, "%s [%s]", TITLE_TEXT, rendererName );
    }
    else
    {
        SceneState().isSceneMode = true;
        AuthoredScene scene;
        const SkullbonezCore::Core::SbResult sceneLoad = AuthoredScene::TryLoadFromFile( scenePath.c_str(), assets, scene );

        if ( !sceneLoad.ok )
        {
            lastSceneLoadResult = sceneLoad;
            LogSceneLoadFailure( sceneLoad, scenePath );
            return lastSceneLoadResult;
        }

        hasSceneTornadoSystem = scene.HasTornadoSystem();
        sceneMutualGravityEnabled = scene.HasMutualGravityEnabled();

        if ( hasSceneTornadoSystem )
        {
            sceneTornadoSystem = scene.GetTornadoSystemConfig();
        }

        config.runtimeCapacity.sceneObjectCapacity = scene.HasModelCapacityOverride() ? scene.GetModelCapacity()
                                                                                      : startup.sceneObjectCapacity;

        // Invariant: authored capacity is resolved while the scene is empty and
        // applied before generated or authored population. Parsing an override
        // without activating it leaves the store at the startup default and
        // fails valid scenes at row 4,001.
        sceneController.Scene().ApplyRuntimeConfig( config );
        ApplySceneWorkerThreadSetting( config, workerPool,
                                       scene.HasWorkerThreadOverride() ? scene.GetWorkerThreads() : startup.workerThreads );

        SceneState().isScenePhysics = scene.IsPhysicsEnabled();
        SceneState().isSceneText = scene.IsTextEnabled();
        diagnosticsRuntime.ConfigurePerfLogFlush( scene.IsPerfLogFlushEnabled(), scene.GetPerfLogFlushInterval() );
        debug.physicsDebugFlags = scene.GetPhysicsDebugFlags();
        debug.isPhysicsDebugTransparent = scene.IsPhysicsDebugTransparent();
        debug.physicsDebugAlpha = scene.GetPhysicsDebugAlpha();
        debug.physicsDebugContactLinger = scene.GetPhysicsDebugContactLinger();

        if ( scene.HasVsyncOverride() )
        {
            renderer.SetVsyncEnabled( scene.IsVsyncEnabled() );
        }

        if ( scene.HasPipelineSyncOverride() )
        {
            renderer.SetPipelineSyncEnabled( scene.IsPipelineSyncEnabled() );
        }

        debug.isTextOnly = scene.IsTextOnly();
        SceneState().isEditableScene = scene.IsEditableScene();
        debug.isWaterHidden = scene.IsWaterHidden();
        debug.isTerrainHidden = scene.IsTerrainHidden();
        debug.isCollisionVisualizer = scene.IsCollisionVisualizerEnabled();
        debug.isBroadphaseOverlay = scene.IsBroadphaseOverlayEnabled();
        debug.isWaterFreezeDebug = scene.IsWaterFreezeDebugEnabled();
        debug.isWaterFlatDebug = scene.IsWaterFlatDebugEnabled();
        const int waterReflectionMode = std::clamp( scene.GetWaterReflectionMode(), 0, 2 );
        debug.isWaterRTReflect = waterReflectionMode == 1;
        debug.isWaterNoReflect = waterReflectionMode == 2;

        if ( debug.isWaterFreezeDebug )
        {
            debug.frozenWaterTime = static_cast<float>( transaction.m_sceneTimeSeconds );
        }

        SceneState().timeScale = scene.GetTimeScale();
        SceneState().isFixedStep = scene.IsFixedStep();

        // Start with engine.cfg defaults, then apply only the cinematic fields
        // that the .scene.json file explicitly authored.
        SceneState().hasCinematicRenderingOverride = scene.HasCinematicRenderingOverride();
        SceneState().isCinematicRenderingEnabled = scene.IsCinematicRenderingEnabled();
        SceneState().hasCinematicExposure = scene.HasCinematicExposure();
        SceneState().cinematicExposure = scene.GetCinematicExposure();
        SceneState().hasCinematicGamma = scene.HasCinematicGamma();
        SceneState().cinematicGamma = scene.GetCinematicGamma();
        SceneState().cinematicOverrideMask = scene.GetCinematicOverrideMask();
        SceneState().cinematicRender = config.cinematicRender;
        ApplyCinematicSceneOverrides( SceneState().cinematicRender, SceneState().cinematicOverrideMask,
                                      scene.GetCinematicRenderConfig() );

        const SceneUIOptions& UIOptions = scene.GetUIOptions();
        const double UINow = transaction.m_sceneTimeSeconds;
        bool isAutomationScene = scene.IsExitOnComplete() || scene.IsScreenshotAndExit() ||
                                 scene.GetScreenshotFrame() >= 0 || scene.GetScreenshotMs() >= 0 ||
                                 scene.GetScreenshotInterval() > 0 || scene.GetPerfLogPath()[0] != '\0';

#ifdef _DEBUG
        isAutomationScene = isAutomationScene || diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#endif
        PrepareSceneUiOptions( SceneRuntimeUiOptionsContext { diagnosticsRuntime, debug, consumerOutputs.uiActivation },
                               UIOptions, UINow, preserveUIState, isAutomationScene );

        SceneState().targetFrameCount = scene.GetFrameCount();
        SceneState().isExitOnComplete = suppressAutomationExit ? false : scene.IsExitOnComplete();
        diagnosticsRuntime.ApplySceneAutomationOptions( scene, suppressAutomationExit, m_perfPass );

        // Override RNG seed for deterministic scenes. CLI --seed wins so a launcher snapshot can
        // replay an unseeded/random scene or deliberately override a scene file seed.

        if ( scene.GetSeed() > 0 )
        {
            rngSeed = scene.GetSeed();
        }

        if ( launchOptions.seedOverride > 0 )
        {
            rngSeed = launchOptions.seedOverride;
        }

        SceneState().rngSeed = rngSeed;
        SceneState().rngState = rngSeed;

        // Scene terrain is authoritative.  A flat-slope test scene must not leak
        // its analytic terrain into the next height-map scene.

        if ( scene.HasFlatSlope() )
        {
            const SkullbonezCore::Core::SbResult terrainResult = UseFlatSlopeTerrain( sceneController.Scene(), assets,
                                                                                      config, scene.GetFlatBaseY(),
                                                                                      scene.GetFlatSlopeX(),
                                                                                      scene.GetFlatSlopeZ(), renderFrame,
                                                                                      renderResources );

            if ( !terrainResult.ok )
            {
                lastSceneLoadResult = terrainResult;
                LogSceneLoadFailure( terrainResult, scenePath );
                return lastSceneLoadResult;
            }

            SceneState().hasFlatSlope = true;
            SceneState().flatBaseY = scene.GetFlatBaseY();
            SceneState().flatSlopeX = scene.GetFlatSlopeX();
            SceneState().flatSlopeZ = scene.GetFlatSlopeZ();
        }
        else
        {
            const SkullbonezCore::Core::SbResult
                terrainResult = UseDefaultTerrain( sceneController.Scene(), assets, config,
                                                   assets
                                                       .RegisterSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                                                 "terrain.raw",
                                                                                 config.assetPaths.terrainRaw.c_str() ),
                                                   renderFrame, renderResources );

            if ( !terrainResult.ok )
            {
                lastSceneLoadResult = terrainResult;
                LogSceneLoadFailure( terrainResult, scenePath );
                return lastSceneLoadResult;
            }

            SceneState().hasFlatSlope = false;
        }

        ApplyConfiguredWorldEnvironment( sceneController.Scene().Environment(), config,
                                         sceneController.Scene().Terrain().Get() );

        // Override world environment if scene specifies world values

        if ( scene.HasWorldOverride() )
        {
            sceneController.Scene().Environment() = WorldEnvironment( scene.GetWorldFluidHeight(),
                                                                      scene.GetWorldFluidDensity(),
                                                                      config.worldForces.gasDensity,
                                                                      scene.GetWorldGravity() );

            sceneController.Scene().Environment().SetMutualGravitySettings( scene.GetWorldMutualGravitySettings() );
            sceneController.Scene().Environment().BindRuntimeConfig( config );
            UpdateWorldTerrainBounds( sceneController.Scene().Environment(), sceneController.Scene().Terrain().Get() );
        }

        ApplyNoWaterOverride( sceneController.Scene().Environment(), sceneController.Scene().Terrain().Get(),
                              launchOptions.noWater );

        if ( shouldPreserveRuntimeState )
        {

            // World sliders/keyboard water edits are part of the live scene controls.
            // Restore them after terrain/world JSON and --no-water have resolved,
            // so a plain reset keeps the operator's current environment.
            const WorldOverrideChange change = ApplyUIWorldOverride( sceneController.Scene().Environment(),
                                                                     resetSnapshot.worldGravity,
                                                                     resetSnapshot.worldFluidHeight,
                                                                     resetSnapshot.worldFluidDensity );

            recordCompletedWorldChange( change );
        }

        SceneAuthoredSetup::SetUpCameras( BuildSceneAuthoredCameraContext( sceneController.Scene() ), scene );

        const SceneGeneratedSetupResult generatedModels = SceneGeneratedSetup::
            TrySetUpRequestedModels( BuildSceneGeneratedModelContext( SceneState(), config, sceneController.Scene(),
                                                                      launchOptions.generatedObjectTypeOverride ),
                                     SceneGeneratedPopulationRequest { sceneNavigation.overrides.modelCountOverride,
                                                                       sceneNavigation.overrides.solverBallCountOverride,
                                                                       sceneNavigation.overrides.solverBoxCountOverride,
                                                                       scene.GetSolverBallCount(), scene.GetSolverBoxCount(),
                                                                       0 },
                                     false );

        if ( !generatedModels.status.ok )
        {
            lastSceneLoadResult = generatedModels.status;
            LogSceneLoadFailure( generatedModels.status, scenePath );
            return lastSceneLoadResult;
        }

        if ( !generatedModels.applied )
        {
            const SkullbonezCore::Core::SbResult authoredSetup = SceneAuthoredSetup::
                SetUpSceneEntities( BuildSceneAuthoredModelContext( SceneState(), sceneController.Scene(),
                                                                    consumerOutputs.automationGates ),
                                    scene );

            if ( !authoredSetup.ok )
            {
                lastSceneLoadResult = authoredSetup;
                LogSceneLoadFailure( authoredSetup, scenePath );
                return lastSceneLoadResult;
            }
        }

        // Physics regression log: current-solver per-frame CSV enabled only by command line.
#ifdef _DEBUG
        sceneController.Scene().Physics().SetPhysicsRegressionLogPath( diagnosticsRuntime.PerfLog().physicsRegressionLogOverride );

        sceneController.Scene().Physics().SetPhysicsCollisionTimeLogPath( diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride );

        if ( diagnosticsRuntime.PhysicsDiagnostics().isEnabled )
        {
            sceneController.Scene().Physics().SetPhysicsDiagnosticsPath( diagnosticsRuntime.PhysicsDiagnostics().path );
        }
#endif

        // Ball-tracking camera: enabled when scene specifies a positive track_height

        if ( scene.GetTrackHeight() > 0.0f )
        {
            camera.trackHeight = scene.GetTrackHeight();
            camera.trackBallRow.value = 0;
            camera.autoCycleInterval = scene.GetAutoCycleInterval(); // -1 if not specified = disabled
        }

        sprintf_s( consumerOutputs.windowTitle, "%s [SCENE MODE] [%s]", TITLE_TEXT, rendererName );

        // Snapshot scenes start paused in Inspect by default; authored live scenes
        // may opt out when body-state entries are just stable initial poses.
        const bool hasSnapshotState = scene.GetBallStateCount() > 0 || scene.GetBoxStateCount() > 0 ||
                                      scene.GetConvexHullStateCount() > 0;

#ifdef _DEBUG
        const bool shouldPauseSnapshotState = hasSnapshotState && scene.ShouldPauseSnapshotState() &&
                                              !diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
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
            uint32_t activeCam = sceneController.Scene().Cameras().GetSelectedCameraName();
            sceneController.Scene().Cameras().SetCameraXZBounds( activeCam, unbounded );
            camera.input.xMove = 0;
            camera.input.yMove = 0;
            camera.hasMouseLookLastClient = false;
            camera.needsMouseLookReset = true;
        }
    }

    sceneController.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterScenePopulate, 0 );

    if ( shouldPreserveRuntimeState )
    {
        RestoreSceneRuntimeResetSnapshot( sceneController, sceneNavigation.overrides, renderer, debug, camera, resetSnapshot,
                                          suppressExitOnComplete );
    }

    // CLI --time-scale and --fixed-step override anything the scene file sets.

    if ( launchOptions.timeScaleOverride > 0.0f )
    {
        SceneState().timeScale = launchOptions.timeScaleOverride;
    }

    if ( sceneNavigation.overrides.timeScaleOverride > 0.0f )
    {
        SceneState().timeScale = sceneNavigation.overrides.timeScaleOverride;
    }

    if ( launchOptions.fixedStep )
    {
        SceneState().isFixedStep = true;
    }

    if ( !shouldPreserveRuntimeState )
    {
        Gameplay::TornadoFieldConfig tornadoField;
        Gameplay::TornadoSystemConfig tornadoSystem;
        ApplyTornadoDefaultsForActiveScene( tornadoField, sceneController.Scene().Environment(),
                                            ActiveSceneCinematicConfig( SceneState(), config ) );

        if ( hasSceneTornadoSystem )
        {
            tornadoSystem = ProjectAuthoredTornadoSystem( sceneTornadoSystem );
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

        // Why: n-body space scenes have no contacts to wake quiet bodies later;
        // authored mutual gravity owns sleep policy for the duration of setup.
        sceneController.Scene().Physics().SetSleepEnabled( false );
    }
    else if ( !shouldPreserveRuntimeState )
    {
        sceneController.Scene().Physics().SetSleepEnabled( retainedPhysicsSleepEnabled );
    }

    if ( launchOptions.frameCountOverride > 0 )
    {
        SceneState().targetFrameCount = launchOptions.frameCountOverride;
        SceneState().isExitOnComplete = true;
    }

    if ( launchOptions.uiStress )
    {
        diagnosticsRuntime.UIStress().enabled = true;
        diagnosticsRuntime.UIStress().randomState = launchOptions.uiStressSeed;
        diagnosticsRuntime.UIStress().actionsPerFrame = launchOptions.uiStressActions;
        consumerOutputs.uiActivation.nowSeconds = transaction.m_sceneTimeSeconds;
        consumerOutputs.uiActivation.forceVisible = true;
        consumerOutputs.uiActivation.forceUnminimized = true;
    }

    if ( launchOptions.graphicsStress )
    {

        // Invariant: scene reloads reset authored scene automation, but a
        // graphics-stress run is operator-owned and must keep running until the
        // launcher or timeout stops the process.
        SceneState().isInteractiveRun = true;
        SceneState().targetFrameCount = 0;
        SceneState().isTestComplete = false;
        SceneState().isExitOnComplete = false;
        diagnosticsRuntime.Capture().ResetScreenshot();
        diagnosticsRuntime.ClosePerfLog();
        diagnosticsRuntime.ResetPerfLogForSceneLoad();
        consumerOutputs.uiActivation.nowSeconds = transaction.m_sceneTimeSeconds;
        consumerOutputs.uiActivation.forceVisible = true;
        consumerOutputs.uiActivation.forceUnminimized = true;
    }

    if ( launchOptions.hasCinematicShadowsOverride )
    {
        ActiveSceneCinematicConfig( SceneState(), config ).shadow.enabled = launchOptions.cinematicShadows;
        SceneState().cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    }

    if ( launchOptions.hasPhysicsDebugFlagsOverride )
    {
        debug.physicsDebugFlags = launchOptions.physicsDebugFlagsOverride;
    }

    if ( launchOptions.hasPhysicsDebugTransparentOverride )
    {
        debug.isPhysicsDebugTransparent = launchOptions.physicsDebugTransparentOverride;
    }

    if ( launchOptions.hasPhysicsDebugAlphaOverride )
    {
        debug.physicsDebugAlpha = launchOptions.physicsDebugAlphaOverride;
    }

    if ( launchOptions.hasPhysicsDebugContactLingerOverride )
    {
        debug.physicsDebugContactLinger = launchOptions.physicsDebugContactLingerOverride;
    }

#ifdef _DEBUG
    SkullbonezCore::Core::Log()
        .WriteEventf( "scene_started index=%d load=%d path=\"%s\" renderer=\"%s\" target_frames=%d seed=%u "
                      "fixed_step=%d physics=%d text=%d models=%d",
                      SceneState().currentSceneIndex, SceneState().loadCount,
                      scenePath.empty() ? "generated" : scenePath.c_str(), rendererName, SceneState().targetFrameCount,
                      SceneState().rngSeed, SceneState().isFixedStep ? 1 : 0, SceneState().isScenePhysics ? 1 : 0,
                      SceneState().isSceneText ? 1 : 0, SceneState().modelCount );
#endif

#ifdef _DEBUG
    diagnosticsRuntime.BeginPhysicsDiagnosticsRun( sceneController.Scene().Physics(), SceneState(), config,
                                                   scenePath.c_str(), rendererName );
#endif

    const SkullbonezCore::Core::SbResult rayTracingResult = renderer.ResourceLifecycle().InitialiseSceneRayTracing( SkullbonezCore::Core::ActiveSceneObjectCapacity( config ) );

    if ( !rayTracingResult.ok )
    {
        lastSceneLoadResult = rayTracingResult;
        return lastSceneLoadResult;
    }

    sceneController.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneActivated, 0 );
    lastSceneLoadResult = SkullbonezCore::Core::SbResult::Success();
    return lastSceneLoadResult;
}


// Concept: each save owner publishes only its own persisted fields. The
// composed request is synchronous, is never retained, and does not let the
// writer recover Run or collection-order identity.
SkullbonezCore::Core::SbResult SceneController::SaveCurrentDefaults( const SceneDefaultsSaveView& view ) const
{
    const std::string* scenePath = CurrentPath();

    if ( !State().isSceneMode || !scenePath || scenePath->empty() )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/SceneController",
                                                        "No authored scene is active for defaults save" );
    }

    if ( State().isEditableScene )
    {
        const SkullbonezCore::Core::SbResult saveResult = SaveEditableSceneBeforeReplacement( scenePath->c_str(),
                                                                                              Scene().GetSaveState(),
                                                                                              State().GetSaveState(),
                                                                                              view.debug.GetSaveState() );

        return saveResult;
    }

    std::ifstream input( *scenePath );

    if ( !input )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/SceneController",
                                                        "Could not read active scene defaults file: %s",
                                                        scenePath->c_str() );
    }

    Json root = Json::parse( input, nullptr, false );

    if ( root.is_discarded() )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/SceneController",
                                                        "Active scene defaults file is not valid JSON: %s",
                                                        scenePath->c_str() );
    }

    if ( !root.is_object() )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/SceneController",
                                                        "Active scene defaults root is not an object: %s",
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
    simulation["textOnly"] = view.debug.isTextOnly;
    runtime["vsync"] = view.renderer.VsyncEnabled();
    runtime["pipelineSync"] = view.renderer.PipelineSyncEnabled();
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

    physicsDebug["axes"] = ( view.debug.physicsDebugFlags & PHYSICS_DEBUG_AXES ) != 0;
    physicsDebug["contacts"] = ( view.debug.physicsDebugFlags & PHYSICS_DEBUG_CONTACTS ) != 0;
    physicsDebug["sleep"] = ( view.debug.physicsDebugFlags & PHYSICS_DEBUG_SLEEP ) != 0;
    physicsDebug["pipeline"] = ( view.debug.physicsDebugFlags & PHYSICS_DEBUG_PIPELINE ) != 0;
    physicsDebug["terrainContact"] = ( view.debug.physicsDebugFlags & PHYSICS_DEBUG_TERRAIN_CONTACT ) != 0;
    physicsDebug["transparent"] = view.debug.isPhysicsDebugTransparent;
    physicsDebug["alpha"] = view.debug.physicsDebugAlpha;
    physicsDebug["contactLinger"] = view.debug.physicsDebugContactLinger;

    debug["collisionVisualizer"] = view.debug.isCollisionVisualizer;
    debug["broadphaseOverlay"] = view.debug.isBroadphaseOverlay;
    debug["waterFreeze"] = view.debug.isWaterFreezeDebug;
    debug["waterFlat"] = view.debug.isWaterFlatDebug;
    debug["waterHidden"] = view.debug.isWaterHidden;
    debug["terrainHidden"] = view.debug.isTerrainHidden;
    debug["waterReflection"] = WaterReflectionJsonValue( view.debug.isWaterNoReflect, view.debug.isWaterRTReflect );

    if ( view.camera.trackBallRow.IsValid() && view.camera.trackHeight > 0.0f )
    {
        playback["trackHeight"] = view.camera.trackHeight;
    }
    else
    {
        playback.erase( "trackHeight" );
    }

    if ( view.camera.autoCycleInterval > 0.0f )
    {
        playback["autoCycleInterval"] = view.camera.autoCycleInterval;
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

    if ( view.uiOverrides.modelCountOverride >= 0 )
    {
        simulation["solverBalls"] = view.uiOverrides.modelCountOverride;
        simulation.erase( "solverBoxes" );
    }
    else if ( State().solverBallCount > 0 || State().solverBoxCount > 0 || view.uiOverrides.solverBallCountOverride >= 0 ||
              view.uiOverrides.solverBoxCountOverride >= 0 )
    {
        simulation["solverBalls"] = State().solverBallCount;
        simulation["solverBoxes"] = State().solverBoxCount;
    }

    std::ofstream output( *scenePath, std::ios::trunc );

    if ( !output )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/SceneController",
                                                        "Could not open active scene defaults for write: %s",
                                                        scenePath->c_str() );
    }

    output << root.dump( 2 ) << '\n';

    if ( !output.good() )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/SceneController",
                                                        "Could not write active scene defaults: %s", scenePath->c_str() );
    }

    return SkullbonezCore::Core::SbResult::Success();
}
