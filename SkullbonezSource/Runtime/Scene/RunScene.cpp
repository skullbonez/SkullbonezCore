/*
File: SkullbonezSource/Runtime/Scene/RunScene.cpp
Purpose:
  Loads, resets, and advances authored and generated scenes.

Summary:
  SceneController owns the cold load transaction and borrows only the owners
  required while scene storage is changing. It returns detached values and a
  lifecycle generation; composition then sequences idempotent reactions at the
  excluded camera, input, interaction, tool, Replay, UI, and validation owners.

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

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
    surface.
  - DXR reflection setup may run only after RuntimeRenderer binds every concrete
    DX12 owner needed by scene population.
  - Scene/model/terrain destruction starts only after a successful GPU drain.
  - Load orchestration retains no caller pointer, callback, or mutable owner bag.
  - Tornado projection copies every authored field here so Gameplay never
    depends upward on Scene or parser vocabulary.
Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SceneController.h"
#include "../RuntimeOverlayDiagnostics.h"
#include "../RuntimeValidationHarness.h"
#include "../../Core/WindowConstants.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../OperatorCommandApplier.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../AttachedCameraController.h"
#include "../InputRouter.h"
#include "../Input.h"
#include "../InputFrame.h"
#include "../Replay/ReplayRuntime.h"
#include "../RunStartupState.h"
#include "../RunTimerState.h"
#include "../Window.h"
#include "../Render/RuntimeRenderer.h"
#include "SceneRuntimeCoordinator.h"
#include "SceneRuntimeLoad.h"
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
#include "../../Scene/SceneSnapshotWriter.h"
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
using SkullbonezCore::GameObjects::SceneSaveRequest;
using SkullbonezCore::GameObjects::SceneSaveView;
using SkullbonezCore::GameObjects::SceneSnapshotWriter;
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
                                    SkullbonezCore::Threading::WorkerPool& workerPool,
                                    int requestedWorkerThreads )
{
    const int clampedWorkerThreads =
        std::clamp( requestedWorkerThreads, -1, SkullbonezCore::Threading::WorkerPool::MaxThreadCount() );
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
    const char* message =
        result.error.message[0] != '\0' ? result.error.message : "scene setup failed without a message";
    fprintf( stderr,
             "[scene] scene_load_failed owner=%s path=\"%s\" reason=\"%s\"\n",
             owner,
             scenePath.empty() ? "<generated>" : scenePath.c_str(),
             message );
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

void SetTouchedCinematicSceneProperties( Json& root,
                                         uint64_t touchedMask,
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
        cinematic["basinMask"] =
            Json::array( { c.basinCenterX, c.basinCenterZ, c.basinRadiusX, c.basinRadiusZ, c.basinFeather } );
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
    return SceneAuthoredCameraContext{ sceneWorld };
}

SceneAuthoredModelContext BuildSceneAuthoredModelContext( RunSceneState& sceneState,
                                                          SceneWorld& sceneWorld,
                                                          SceneAutomationGateConfiguration& automationGates )
{
    return SceneAuthoredModelContext{ sceneState, sceneWorld, automationGates };
}

SceneGeneratedCameraContext BuildSceneGeneratedCameraContext( SceneWorld& sceneWorld )
{
    return SceneGeneratedCameraContext{ sceneWorld };
}

SceneGeneratedModelContext BuildSceneGeneratedModelContext( RunSceneState& scene,
                                                            const SkullbonezCore::Core::EngineConfig& config,
                                                            SceneWorld& sceneWorld,
                                                            GeneratedObjectTypeOverride objectTypeOverride )
{
    return SceneGeneratedModelContext{ scene, config, sceneWorld, objectTypeOverride };
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

void ApplyConfiguredWorldEnvironment( WorldEnvironment& world,
                                      const SkullbonezCore::Core::EngineConfig& cfg,
                                      Terrain* terrain )
{
    world = WorldEnvironment( cfg.worldForces.fluidHeight,
                              cfg.worldForces.fluidDensity,
                              cfg.worldForces.gasDensity,
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

SkullbonezCore::Core::SbResult UseDefaultTerrain( SceneTerrain& terrainOwner,
                                                  SkullbonezCore::Assets::AssetSystem& assets,
                                                  WorldEnvironment& world,
                                                  const SkullbonezCore::Core::EngineConfig& config,
                                                  const std::string& terrainRawPath,
                                                  SkullbonezCore::Rendering::Dx12FrameOwner* renderFrame,
                                                  SkullbonezCore::Rendering::Dx12ResourceBuilder* renderResources )
{
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
        const SkullbonezCore::Core::SbResult terrainResult = Terrain::TryCreateFromHeightMap( terrainRawPath.c_str(),
                                                                                              256,
                                                                                              8,
                                                                                              15,
                                                                                              config,
                                                                                              assets,
                                                                                              *renderResources,
                                                                                              terrain );
        if ( !terrainResult.ok )
        {
            // Why: RAW terrain is external scene/config input. Report the load
            // failure before replacing the currently owned terrain.
            return terrainResult;
        }
        terrainOwner.Replace( std::move( terrain ), false );
    }
    else
    {
        terrainOwner.Get()->BindRenderContexts( config, assets, *renderResources );
    }

    UpdateWorldTerrainBounds( world, terrainOwner.Get() );
    return SkullbonezCore::Core::SbResult::Success();
}

SkullbonezCore::Core::SbResult UseFlatSlopeTerrain( SceneTerrain& terrainOwner,
                                                    SkullbonezCore::Assets::AssetSystem& assets,
                                                    WorldEnvironment& world,
                                                    const SkullbonezCore::Core::EngineConfig& config,
                                                    float baseY,
                                                    float slopeX,
                                                    float slopeZ,
                                                    SkullbonezCore::Rendering::Dx12FrameOwner* renderFrame,
                                                    SkullbonezCore::Rendering::Dx12ResourceBuilder* renderResources )
{
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
    terrainOwner.Replace( std::move( terrain ), true );

    UpdateWorldTerrainBounds( world, terrainOwner.Get() );
    return SkullbonezCore::Core::SbResult::Success();
}

SkullbonezCore::Core::SbResult SaveCurrentEditableSceneSnapshot( const std::string& scenePath,
                                                                 const RunSceneState& sceneState,
                                                                 const SceneWorld& sceneWorld,
                                                                 bool waterHidden,
                                                                 bool terrainHidden )
{
    // Lifetime: editable persistence borrows the concrete scene world only for
    // this synchronous write; scene reload may replace its stores afterward.
    const WorldEnvironment& world = sceneWorld.Environment();
    const CameraCollection& cameras = sceneWorld.Cameras();
    const auto& joints = SkullbonezCore::Physics::PhysicsEngine::ReadPointJointConstraints( sceneWorld.Physics() );
    const SceneSaveView saveView{ sceneWorld.Entities(),
                                  sceneWorld.BodyStore(),
                                  sceneWorld.Colliders(),
                                  joints.data(),
                                  static_cast<int>( joints.size() ),
                                  world.GetGravity(),
                                  world.GetFluidSurfaceHeight(),
                                  world.GetFluidDensity(),
                                  world.GetMutualGravitySettings() };
    const SceneSaveRequest request{ scenePath.c_str(),
                                    cameras.GetCameraTranslation(),
                                    cameras.GetCameraView(),
                                    cameras.GetCameraUp(),
                                    sceneState.isScenePhysics,
                                    sceneState.isSceneText,
                                    true,
                                    sceneState.isFixedStep,
                                    waterHidden,
                                    terrainHidden,
                                    sceneState.hasFlatSlope,
                                    sceneState.flatBaseY,
                                    sceneState.flatSlopeX,
                                    sceneState.flatSlopeZ };
    return SceneSnapshotWriter::Save( saveView, request );
}

void ApplyTornadoDefaultsForActiveScene( TornadoFieldConfig& field,
                                         WorldEnvironment& world,
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


void SceneLoadConsumerOutputs::ResetForLoad()
{
    uiActivation = SceneUiActivation{};
    automationGates.Reset();
    navigation = SceneLoadNavigationState{};
    presentation = RunDebugState{};
    camera = RunCameraState{};
    completedWorldChanges = {};
    completedWorldChangeCount = 0;
    completedRequests = SceneRequestBatch{};
    windowTitle[0] = '\0';
    applyNavigation = false;
    refreshSceneBrowser = false;
}


void SkullbonezCore::Runtime::ApplySceneLoadConsumerOutputs( SceneLoadConsumerOutputs& outputs,
                                                             Window& window,
                                                             UI::InGameUI& operatorUi,
                                                             RuntimeValidationHarness& validationHarness,
                                                             const RunLaunchOptions& launchOptions,
                                                             Rendering::Dx12RenderDevice* renderDevice,
                                                             bool rendererVsyncEnabled,
                                                             RunTimerState& timers,
                                                             RuntimeOverlayDiagnostics& overlays,
                                                             SceneController& sceneController,
                                                             InputRouter& inputRouter,
                                                             RuntimeInteractionController& interaction,
                                                             RunCameraState& camera,
                                                             AttachedCameraController& attachedCamera,
                                                             RuntimeTools& runtimeTools,
                                                             ReplayRuntime& replayRuntime )
{
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
        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildWorldOverride( change.previousGravity,
                                                                                     change.previousFluidHeight,
                                                                                     change.previousFluidDensity,
                                                                                     change.gravity,
                                                                                     change.fluidHeight,
                                                                                     change.fluidDensity ) );
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
    const auto replayOwners = [&]( RunCameraState& cameraOwner )
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
        return ReplaySceneTimelineResetOwners{ inputRouter,
                                               interaction,
                                               &sceneController.Scene().Cameras(),
                                               sceneController.Scene().Terrain().Get(),
                                               cameraOwner,
                                               restoreMode,
                                               attachedCamera.State().activeFollow,
                                               cameraOwner.director.grabbed };
    };
    const ReplaySceneTimelineResetInput timelineReset =
        DescribeReplaySceneTimeline( sceneController,
                                     outputs.navigation.overrides,
                                     sceneController.State(),
                                     sceneController.Scene().ActiveSceneObjectCapacity(),
                                     static_cast<uint32_t>( launchOptions.generatedObjectTypeOverride ) );
    ReplaySceneTimelineResetOwners activationOwners = replayOwners( camera );
    replayRuntime.ObserveSceneLifecycleAfterActivation( lifecycle, timelineReset, activationOwners );
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
        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildCommand(
            ReplayEventKind::OwnerAction,
            0,
            true,
            ReplaySceneRequestFlags( request ),
            static_cast<int32_t>( eventCode ),
            request.index,
            0,
            0,
            0,
            request.type == SceneRequestType::CreateScene ? request.text : ReplayOwnerEventName( eventCode ) ) );
    }
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
}

SkullbonezCore::Core::SbResult SceneController::Load( const SceneLoadRequest& request,
                                                      const SceneLoadPolicyInputs& policy,
                                                      const SceneLoadInteractionParticipants& interactionParticipants,
                                                      const SceneLoadPresentationParticipants& presentation,
                                                      SceneLoadConsumerOutputs& consumerOutputs )
{
    // Lifetime: these aliases make the long load transaction readable without
    // recovering a retained context. They refer only to the four caller-owned
    // phase values and die with this synchronous call.
    SkullbonezCore::Core::EngineConfig& config = policy.config;
    RunLaunchOptions& launchOptions = policy.launchOptions;
    const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender = policy.defaultCinematicRender;
    const RunStartupState& startup = policy.startup;
    SkullbonezCore::Assets::AssetSystem& assets = policy.assets;
    Threading::WorkerPool& workerPool = policy.workerPool;
    DiagnosticsRuntime& diagnosticsRuntime = policy.diagnosticsRuntime;
    const char* rendererName = policy.rendererName ? policy.rendererName : "unknown";
    Rendering::Dx12FrameOwner* renderFrame = presentation.renderFrame;
    Rendering::Dx12ResourceBuilder* renderResources = presentation.renderResources;
    RuntimeRenderer& renderer = presentation.renderer;

    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::SceneLoad );
    consumerOutputs.ResetForLoad();
    consumerOutputs.navigation = interactionParticipants.navigation;
    consumerOutputs.presentation = presentation.debug;
    consumerOutputs.camera = interactionParticipants.camera;
    SceneLoadNavigationState& sceneNavigation = consumerOutputs.navigation;
    RunDebugState& m_debug = consumerOutputs.presentation;
    RunCameraState& camera = consumerOutputs.camera;
    const auto recordCompletedWorldChange = [&]( const WorldOverrideChange& change )
    {
        if ( consumerOutputs.completedWorldChangeCount >= consumerOutputs.completedWorldChanges.size() )
        {
            SB_FATAL( "Runtime/SceneController", "Fixed completed world-change capacity exhausted." );
        }
        consumerOutputs.completedWorldChanges[consumerOutputs.completedWorldChangeCount++] =
            SceneLoadCompletedWorldChange{ change.previousGravity,
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
    SceneController& m_sceneController = *this;
    const auto SceneState = [this]() -> RunSceneState& { return State(); };
    SkullbonezCore::Core::SbResult m_lastSceneLoadResult = SkullbonezCore::Core::SbResult::Success();
    SceneController& runtime = m_sceneController;
    const SceneRuntimeLoadBeginResult loadBegin =
        PrepareSceneRuntimeLoad( runtime,
                                 sceneNavigation.overrides,
                                 renderer,
                                 m_debug,
                                 camera,
                                 renderFrame,
                                 request.enterInteractiveSceneRun || launchOptions.interactiveSceneRun,
                                 index,
                                 suppressExitOnComplete,
                                 preserveRuntimeState );
    if ( !loadBegin.status.ok )
    {
        // Lane R: preparation has not mutated or destroyed the old
        // scene after a failed GPU drain, so preserve that state and end load.
        m_lastSceneLoadResult = loadBegin.status;
        LogSceneLoadFailure( loadBegin.status, loadBegin.scenePath ? *loadBegin.scenePath : std::string{} );
        return m_lastSceneLoadResult;
    }
    if ( !loadBegin.shouldLoad )
    {
        return m_lastSceneLoadResult;
    }
    const SceneLifecycleBeginPolicy lifecyclePolicy{ preserveUIState,
                                                     preserveRuntimeState,
                                                     suppressExitOnComplete,
                                                     request.enterInteractiveSceneRun,
                                                     request.markManualReset };
    // Invariant: generation begins only after preflight and GPU drain succeed,
    // but before any transaction phase mutates the active scene. Every event
    // emitted below therefore belongs to this exact load attempt.
    runtime.BeginLoadAttempt( index, lifecyclePolicy );
    SceneLifecycleConsumerMask beforeUnloadConsumers = SceneLifecycleConsumerBit( SceneLifecycleConsumer::RenderDrain );
    diagnosticsRuntime.BeforeSceneUnload( SceneState() );
    beforeUnloadConsumers |= SceneLifecycleConsumerBit( SceneLifecycleConsumer::Diagnostics );
    runtime.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeSceneUnload, beforeUnloadConsumers );
    CommitSceneRuntimeLoad( runtime, sceneNavigation, loadBegin );
    consumerOutputs.applyNavigation = true;
    if ( request.markManualReset )
    {
        runtime.MarkManualReset();
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

    m_sceneController.Scene().Cameras().Reset();
    m_sceneController.Scene().Clear();

    camera.ResetForSceneLoad( !scenePath.empty() );
    m_debug.ResetForSceneLoad();
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
    runtime.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneCleared, afterClearConsumers );
    runtime.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeScenePopulate, 0 );

    // Branch on file-backed scene mode vs generated demo mode.
    if ( scenePath.empty() )
    {
        config.runtimeCapacity.sceneObjectCapacity = startup.sceneObjectCapacity;
        m_sceneController.Scene().ApplyRuntimeConfig( config );
        ApplySceneWorkerThreadSetting( config, workerPool, startup.workerThreads );
        if ( launchOptions.seedOverride > 0 )
        {
            rngSeed = launchOptions.seedOverride;
        }
        SceneState().rngSeed = rngSeed;
        SceneState().rngState = rngSeed;
        const SkullbonezCore::Core::SbResult terrainResult =
            UseDefaultTerrain( m_sceneController.Scene().Terrain(),
                               assets,
                               m_sceneController.Scene().Environment(),
                               config,
                               assets.RegisterSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                               "terrain.raw",
                                                               config.assetPaths.terrainRaw.c_str() ),
                               renderFrame,
                               renderResources );
        if ( !terrainResult.ok )
        {
            m_lastSceneLoadResult = terrainResult;
            LogSceneLoadFailure( terrainResult, scenePath );
            return m_lastSceneLoadResult;
        }
        ApplyConfiguredWorldEnvironment( m_sceneController.Scene().Environment(),
                                         config,
                                         m_sceneController.Scene().Terrain().Get() );
        ApplyNoWaterOverride( m_sceneController.Scene().Environment(),
                              m_sceneController.Scene().Terrain().Get(),
                              launchOptions.noWater );
        if ( shouldPreserveRuntimeState )
        {
            // Restore setup-affecting live controls before the generated model pool is rebuilt.
            // Other visual/debug controls are restored later after scene JSON has loaded.
            const WorldOverrideChange change = ApplyUIWorldOverride( m_sceneController.Scene().Environment(),
                                                                     resetSnapshot.worldGravity,
                                                                     resetSnapshot.worldFluidHeight,
                                                                     resetSnapshot.worldFluidDensity );
            recordCompletedWorldChange( change );
        }

        SceneState().isSceneMode = false;
        SceneGeneratedSetup::SetUpCameras( BuildSceneGeneratedCameraContext( m_sceneController.Scene() ) );
        const SceneGeneratedSetupResult generatedSetup = SceneGeneratedSetup::TrySetUpRequestedModels(
            BuildSceneGeneratedModelContext( SceneState(),
                                             config,
                                             m_sceneController.Scene(),
                                             launchOptions.generatedObjectTypeOverride ),
            SceneGeneratedPopulationRequest{ sceneNavigation.overrides.modelCountOverride,
                                             sceneNavigation.overrides.solverBallCountOverride,
                                             sceneNavigation.overrides.solverBoxCountOverride,
                                             0,
                                             0,
                                             SkullbonezCore::Scene::Capacity::DEFAULT_SCENE_OBJECTS },
            true );
        if ( !generatedSetup.status.ok )
        {
            m_lastSceneLoadResult = generatedSetup.status;
            LogSceneLoadFailure( generatedSetup.status, scenePath );
            return m_lastSceneLoadResult;
        }
        RunSceneBrowserState styleBrowser;
        styleBrowser.paths = sceneNavigation.browserPaths;
        styleBrowser.selectedCineModeSceneIndex = sceneNavigation.selectedCineModeSceneIndex;
        ApplyDemoHeroStyleOverride( SceneRuntimeStyleContext{ launchOptions,
                                                              SceneState(),
                                                              styleBrowser,
                                                              m_sceneController.Scene(),
                                                              assets,
                                                              ActiveSceneCinematicConfig( SceneState(), config ),
                                                              defaultCinematicRender } );
        sceneNavigation.selectedCineModeSceneIndex = styleBrowser.selectedCineModeSceneIndex;
        sprintf_s( consumerOutputs.windowTitle, "%s [%s]", TITLE_TEXT, rendererName );
    }
    else
    {
        SceneState().isSceneMode = true;
        AuthoredScene scene;
        const SkullbonezCore::Core::SbResult sceneLoad =
            AuthoredScene::TryLoadFromFile( scenePath.c_str(), assets, scene );
        if ( !sceneLoad.ok )
        {
            m_lastSceneLoadResult = sceneLoad;
            LogSceneLoadFailure( sceneLoad, scenePath );
            return m_lastSceneLoadResult;
        }
        hasSceneTornadoSystem = scene.HasTornadoSystem();
        sceneMutualGravityEnabled = scene.HasMutualGravityEnabled();
        if ( hasSceneTornadoSystem )
        {
            sceneTornadoSystem = scene.GetTornadoSystemConfig();
        }
        config.runtimeCapacity.sceneObjectCapacity =
            scene.HasModelCapacityOverride() ? scene.GetModelCapacity() : startup.sceneObjectCapacity;
        // Invariant: authored capacity is resolved while the scene is empty and
        // applied before generated or authored population. Parsing an override
        // without activating it leaves the store at the startup default and
        // fails valid scenes at row 4,001.
        m_sceneController.Scene().ApplyRuntimeConfig( config );
        ApplySceneWorkerThreadSetting(
            config,
            workerPool,
            scene.HasWorkerThreadOverride() ? scene.GetWorkerThreads() : startup.workerThreads );
        SceneState().isScenePhysics = scene.IsPhysicsEnabled();
        SceneState().isSceneText = scene.IsTextEnabled();
        diagnosticsRuntime.ConfigurePerfLogFlush( scene.IsPerfLogFlushEnabled(), scene.GetPerfLogFlushInterval() );
        m_debug.physicsDebugFlags = scene.GetPhysicsDebugFlags();
        m_debug.isPhysicsDebugTransparent = scene.IsPhysicsDebugTransparent();
        m_debug.physicsDebugAlpha = scene.GetPhysicsDebugAlpha();
        m_debug.physicsDebugContactLinger = scene.GetPhysicsDebugContactLinger();
        if ( scene.HasVsyncOverride() )
        {
            renderer.SetVsyncEnabled( scene.IsVsyncEnabled() );
        }
        if ( scene.HasPipelineSyncOverride() )
        {
            renderer.SetPipelineSyncEnabled( scene.IsPipelineSyncEnabled() );
        }
        m_debug.isTextOnly = scene.IsTextOnly();
        SceneState().isEditableScene = scene.IsEditableScene();
        m_debug.isWaterHidden = scene.IsWaterHidden();
        m_debug.isTerrainHidden = scene.IsTerrainHidden();
        m_debug.isCollisionVisualizer = scene.IsCollisionVisualizerEnabled();
        m_debug.isBroadphaseOverlay = scene.IsBroadphaseOverlayEnabled();
        m_debug.isWaterFreezeDebug = scene.IsWaterFreezeDebugEnabled();
        m_debug.isWaterFlatDebug = scene.IsWaterFlatDebugEnabled();
        const int waterReflectionMode = std::clamp( scene.GetWaterReflectionMode(), 0, 2 );
        m_debug.isWaterRTReflect = waterReflectionMode == 1;
        m_debug.isWaterNoReflect = waterReflectionMode == 2;
        if ( m_debug.isWaterFreezeDebug )
        {
            m_debug.frozenWaterTime = static_cast<float>( policy.sceneTimeSeconds );
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
        ApplyCinematicSceneOverrides( SceneState().cinematicRender,
                                      SceneState().cinematicOverrideMask,
                                      scene.GetCinematicRenderConfig() );

        const SceneUIOptions& UIOptions = scene.GetUIOptions();
        const double UINow = policy.sceneTimeSeconds;
        bool isAutomationScene = scene.IsExitOnComplete() || scene.IsScreenshotAndExit() ||
                                 scene.GetScreenshotFrame() >= 0 || scene.GetScreenshotMs() >= 0 ||
                                 scene.GetScreenshotInterval() > 0 || scene.GetPerfLogPath()[0] != '\0';
#ifdef _DEBUG
        isAutomationScene = isAutomationScene || diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#endif
        PrepareSceneUiOptions(
            SceneRuntimeUiOptionsContext{ diagnosticsRuntime, m_debug, consumerOutputs.uiActivation },
            UIOptions,
            UINow,
            preserveUIState,
            isAutomationScene );
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
            const SkullbonezCore::Core::SbResult terrainResult =
                UseFlatSlopeTerrain( m_sceneController.Scene().Terrain(),
                                     assets,
                                     m_sceneController.Scene().Environment(),
                                     config,
                                     scene.GetFlatBaseY(),
                                     scene.GetFlatSlopeX(),
                                     scene.GetFlatSlopeZ(),
                                     renderFrame,
                                     renderResources );
            if ( !terrainResult.ok )
            {
                m_lastSceneLoadResult = terrainResult;
                LogSceneLoadFailure( terrainResult, scenePath );
                return m_lastSceneLoadResult;
            }
            SceneState().hasFlatSlope = true;
            SceneState().flatBaseY = scene.GetFlatBaseY();
            SceneState().flatSlopeX = scene.GetFlatSlopeX();
            SceneState().flatSlopeZ = scene.GetFlatSlopeZ();
        }
        else
        {
            const SkullbonezCore::Core::SbResult terrainResult =
                UseDefaultTerrain( m_sceneController.Scene().Terrain(),
                                   assets,
                                   m_sceneController.Scene().Environment(),
                                   config,
                                   assets.RegisterSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                                   "terrain.raw",
                                                                   config.assetPaths.terrainRaw.c_str() ),
                                   renderFrame,
                                   renderResources );
            if ( !terrainResult.ok )
            {
                m_lastSceneLoadResult = terrainResult;
                LogSceneLoadFailure( terrainResult, scenePath );
                return m_lastSceneLoadResult;
            }
            SceneState().hasFlatSlope = false;
        }

        ApplyConfiguredWorldEnvironment( m_sceneController.Scene().Environment(),
                                         config,
                                         m_sceneController.Scene().Terrain().Get() );
        // Override world environment if scene specifies world values
        if ( scene.HasWorldOverride() )
        {
            m_sceneController.Scene().Environment() = WorldEnvironment( scene.GetWorldFluidHeight(),
                                                                        scene.GetWorldFluidDensity(),
                                                                        config.worldForces.gasDensity,
                                                                        scene.GetWorldGravity() );
            m_sceneController.Scene().Environment().SetMutualGravitySettings( scene.GetWorldMutualGravitySettings() );
            m_sceneController.Scene().Environment().BindRuntimeConfig( config );
            UpdateWorldTerrainBounds( m_sceneController.Scene().Environment(),
                                      m_sceneController.Scene().Terrain().Get() );
        }
        ApplyNoWaterOverride( m_sceneController.Scene().Environment(),
                              m_sceneController.Scene().Terrain().Get(),
                              launchOptions.noWater );
        if ( shouldPreserveRuntimeState )
        {
            // World sliders/keyboard water edits are part of the live scene controls.
            // Restore them after terrain/world JSON and --no-water have resolved,
            // so a plain reset keeps the operator's current environment.
            const WorldOverrideChange change = ApplyUIWorldOverride( m_sceneController.Scene().Environment(),
                                                                     resetSnapshot.worldGravity,
                                                                     resetSnapshot.worldFluidHeight,
                                                                     resetSnapshot.worldFluidDensity );
            recordCompletedWorldChange( change );
        }

        SceneAuthoredSetup::SetUpCameras( BuildSceneAuthoredCameraContext( m_sceneController.Scene() ), scene );

        const SceneGeneratedSetupResult generatedModels = SceneGeneratedSetup::TrySetUpRequestedModels(
            BuildSceneGeneratedModelContext( SceneState(),
                                             config,
                                             m_sceneController.Scene(),
                                             launchOptions.generatedObjectTypeOverride ),
            SceneGeneratedPopulationRequest{ sceneNavigation.overrides.modelCountOverride,
                                             sceneNavigation.overrides.solverBallCountOverride,
                                             sceneNavigation.overrides.solverBoxCountOverride,
                                             scene.GetSolverBallCount(),
                                             scene.GetSolverBoxCount(),
                                             0 },
            false );
        if ( !generatedModels.status.ok )
        {
            m_lastSceneLoadResult = generatedModels.status;
            LogSceneLoadFailure( generatedModels.status, scenePath );
            return m_lastSceneLoadResult;
        }
        if ( !generatedModels.applied )
        {
            const SkullbonezCore::Core::SbResult authoredSetup = SceneAuthoredSetup::SetUpSceneEntities(
                BuildSceneAuthoredModelContext( SceneState(),
                                                m_sceneController.Scene(),
                                                consumerOutputs.automationGates ),
                scene );
            if ( !authoredSetup.ok )
            {
                m_lastSceneLoadResult = authoredSetup;
                LogSceneLoadFailure( authoredSetup, scenePath );
                return m_lastSceneLoadResult;
            }
        }
        // Physics regression log: current-solver per-frame CSV enabled only by command line.
#ifdef _DEBUG
        m_sceneController.Scene().Physics().SetPhysicsRegressionLogPath(
            diagnosticsRuntime.PerfLog().physicsRegressionLogOverride );
        m_sceneController.Scene().Physics().SetPhysicsCollisionTimeLogPath(
            diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride );
        if ( diagnosticsRuntime.PhysicsDiagnostics().isEnabled )
        {
            m_sceneController.Scene().Physics().SetPhysicsDiagnosticsPath(
                diagnosticsRuntime.PhysicsDiagnostics().path );
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
        const bool hasSnapshotState =
            scene.GetBallStateCount() > 0 || scene.GetBoxStateCount() > 0 || scene.GetConvexHullStateCount() > 0;
#ifdef _DEBUG
        const bool shouldPauseSnapshotState =
            hasSnapshotState && scene.ShouldPauseSnapshotState() && !diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
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
            uint32_t activeCam = m_sceneController.Scene().Cameras().GetSelectedCameraName();
            m_sceneController.Scene().Cameras().SetCameraXZBounds( activeCam, unbounded );
            camera.input.xMove = 0;
            camera.input.yMove = 0;
            camera.hasMouseLookLastClient = false;
            camera.needsMouseLookReset = true;
        }
    }

    runtime.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterScenePopulate, 0 );
    if ( shouldPreserveRuntimeState )
    {
        RestoreSceneRuntimeResetSnapshot( m_sceneController,
                                          sceneNavigation.overrides,
                                          renderer,
                                          m_debug,
                                          camera,
                                          resetSnapshot,
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
        ApplyTornadoDefaultsForActiveScene( tornadoField,
                                            m_sceneController.Scene().Environment(),
                                            ActiveSceneCinematicConfig( SceneState(), config ) );
        if ( hasSceneTornadoSystem )
        {
            tornadoSystem = ProjectAuthoredTornadoSystem( sceneTornadoSystem );
            tornadoField.enabled = false;
            m_sceneController.Scene().Tornado().SetVisualEnabled( true );
        }
        m_sceneController.Scene().Tornado().SetFieldConfig( tornadoField );
        m_sceneController.Scene().Tornado().SetSystemConfig( tornadoSystem );
    }
    Gameplay::TornadoFieldConfig tornadoField = m_sceneController.Scene().Tornado().GetFieldConfig();
    Gameplay::TornadoSystemConfig tornadoSystem = m_sceneController.Scene().Tornado().GetSystemConfig();
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
        if ( m_sceneController.Scene().Tornado().VisualAutoEnableWithTornado() )
        {
            m_sceneController.Scene().Tornado().SetVisualEnabled( launchOptions.tornadoEnabled );
        }
    }
    if ( launchOptions.tornadoVectors )
    {
        tornadoField.visualizeVelocityField = true;
        tornadoSystem.visualizeVelocityField = true;
    }
    m_sceneController.Scene().Tornado().SetFieldConfig( tornadoField );
    m_sceneController.Scene().Tornado().SetSystemConfig( tornadoSystem );
    if ( sceneMutualGravityEnabled )
    {
        // Why: n-body space scenes have no contacts to wake quiet bodies later;
        // authored mutual gravity owns sleep policy for the duration of setup.
        m_sceneController.Scene().Physics().SetSleepEnabled( false );
    }
    else if ( !shouldPreserveRuntimeState )
    {
        m_sceneController.Scene().Physics().SetSleepEnabled( retainedPhysicsSleepEnabled );
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
        consumerOutputs.uiActivation.nowSeconds = policy.sceneTimeSeconds;
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
        consumerOutputs.uiActivation.nowSeconds = policy.sceneTimeSeconds;
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
        m_debug.physicsDebugFlags = launchOptions.physicsDebugFlagsOverride;
    }
    if ( launchOptions.hasPhysicsDebugTransparentOverride )
    {
        m_debug.isPhysicsDebugTransparent = launchOptions.physicsDebugTransparentOverride;
    }
    if ( launchOptions.hasPhysicsDebugAlphaOverride )
    {
        m_debug.physicsDebugAlpha = launchOptions.physicsDebugAlphaOverride;
    }
    if ( launchOptions.hasPhysicsDebugContactLingerOverride )
    {
        m_debug.physicsDebugContactLinger = launchOptions.physicsDebugContactLingerOverride;
    }

#ifdef _DEBUG
    SkullbonezCore::Core::Log().WriteEventf(
        "scene_started index=%d load=%d path=\"%s\" renderer=\"%s\" target_frames=%d seed=%u "
        "fixed_step=%d physics=%d text=%d models=%d",
        SceneState().currentSceneIndex,
        SceneState().loadCount,
        scenePath.empty() ? "generated" : scenePath.c_str(),
        rendererName,
        SceneState().targetFrameCount,
        SceneState().rngSeed,
        SceneState().isFixedStep ? 1 : 0,
        SceneState().isScenePhysics ? 1 : 0,
        SceneState().isSceneText ? 1 : 0,
        SceneState().modelCount );
#endif

#ifdef _DEBUG
    diagnosticsRuntime.BeginPhysicsDiagnosticsRun( m_sceneController.Scene().Physics(),
                                                   SceneState(),
                                                   config,
                                                   scenePath.c_str(),
                                                   rendererName );
#endif

    const SkullbonezCore::Core::SbResult rayTracingResult =
        renderer.InitialiseSceneRayTracing( SkullbonezCore::Core::ActiveSceneObjectCapacity( config ) );
    if ( !rayTracingResult.ok )
    {
        m_lastSceneLoadResult = rayTracingResult;
        return m_lastSceneLoadResult;
    }
    runtime.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneActivated, 0 );
    m_lastSceneLoadResult = SkullbonezCore::Core::SbResult::Success();
    return m_lastSceneLoadResult;
}


// Concept: save authority belongs to the scene owner. The view is a synchronous
// read-only join over concrete world/presentation owners; it is never retained
// and does not allow the writer to recover Run or collection-order identity.
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
        const SkullbonezCore::Core::SbResult saveResult =
            SaveCurrentEditableSceneSnapshot( *scenePath,
                                              State(),
                                              Scene(),
                                              view.debug.isWaterHidden,
                                              view.debug.isTerrainHidden );
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
    else if ( State().solverBallCount > 0 || State().solverBoxCount > 0 ||
              view.uiOverrides.solverBallCountOverride >= 0 || view.uiOverrides.solverBoxCountOverride >= 0 )
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
                                                        "Could not write active scene defaults: %s",
                                                        scenePath->c_str() );
    }
    return SkullbonezCore::Core::SbResult::Success();
}
