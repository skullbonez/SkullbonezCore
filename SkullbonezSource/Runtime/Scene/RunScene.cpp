/*
File: SkullbonezSource/Runtime/Scene/RunScene.cpp
Purpose:
  Loads, resets, and advances authored and generated scenes.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Lane R result: Recoverable scene-load failure carrying owner/message
    diagnostics while the runtime stays alive.
  Required scene contact: Authored pair gate that marks a scenario objective
    once two bodies have produced an exact contact.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
    surface.
  - Required contact checks read PhysicsBodyStore and ColliderStore snapshots;
    they must not require the post-step GameModel body mirror to be fresh.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../RunInternal.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../RuntimeTuning.h"
#include "SceneRuntimeLoad.h"
#include "SceneRuntimeReset.h"
#include "SceneRuntimeStyle.h"
#include "SceneRuntimeUiOptions.h"
#include "../Editor/EditorHullAssets.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/ObjectContactManifold.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/Ragdoll.h"
#include "../../Core/Log.h"
#include "../../Core/SbResult.h"
#include "../../Core/WorkerPool.h"
#include "../../Rendering/IRenderDeviceLifecycle.h"
#include "../../Rendering/IRenderRayTracing.h"
#include "../../Rendering/IRenderResourceFactory.h"

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Assets::ResolveEditorHullAssetPath;
using namespace SkullbonezCore::Basics::RunInternal;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
using Json = nlohmann::ordered_json;
constexpr float SCENE_EDITOR_TEXTURE_MODE_INVERTED = -2.0f;

ObjectContactBodyView SceneContactBodyView( const PhysicsBodyRecord& body )
{
    ObjectContactBodyView view;
    view.position = body.position;
    view.orientation = body.orientation;
    return view;
}

void ApplySceneWorkerThreadSetting( EngineConfig& config,
                                    SkullbonezCore::Threading::WorkerPool& workerPool,
                                    int requestedWorkerThreads )
{
    const int clampedWorkerThreads =
        std::clamp( requestedWorkerThreads, -1, SkullbonezCore::Threading::WorkerPool::MaxThreadCount() );
    const int resolvedWorkerThreads = SkullbonezCore::Threading::WorkerPool::ResolveThreadCount( clampedWorkerThreads );
    config.workerThreads = clampedWorkerThreads;
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


bool SceneNameStartsWith( const char* name, const char* prefix )
{
    return name && strncmp( name, prefix, strlen( prefix ) ) == 0;
}

bool IsEditorPlacedSphereName( const char* name )
{
    return SceneNameStartsWith( name, "static_ball_" ) || SceneNameStartsWith( name, "dynamic_ball_" ) ||
           SceneNameStartsWith( name, "sleeping_ball_" ) || SceneNameStartsWith( name, "static_sphere_" ) ||
           SceneNameStartsWith( name, "dynamic_sphere_" ) || SceneNameStartsWith( name, "sleeping_sphere_" );
}

void ApplyEditorPlacedSphereMaterial( GameModel& model )
{
    if ( IsEditorPlacedSphereName( model.GetName() ) )
    {
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, SCENE_EDITOR_TEXTURE_MODE_INVERTED );
    }
}

void LogSceneLoadFailure( const SbResult& result, const std::string& scenePath )
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
    const char* name = FileNameFromPath( path.c_str() );
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

void SetTouchedCinematicSceneProperties( Json& root, uint64_t touchedMask, const CinematicRenderConfig& c )
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
    writeFloat( SCENE_CINE_SUN_SCREEN_X, "sunScreenX", c.sunScreenX );
    writeFloat( SCENE_CINE_SUN_SCREEN_Y, "sunScreenY", c.sunScreenY );
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
    writeBool( SCENE_CINE_SHADOWS, "shadows", c.shadowsEnabled );
    writeInt( SCENE_CINE_SHADOW_MAP_SIZE, "shadowMapSize", c.shadowMapSize );
    writeInt( SCENE_CINE_SHADOW_PCF_RADIUS, "shadowPcfRadius", c.shadowPcfRadius );
    writeFloat( SCENE_CINE_SHADOW_STRENGTH, "shadowStrength", c.shadowStrength );
    writeFloat( SCENE_CINE_SHADOW_SOFTNESS, "shadowSoftness", c.shadowSoftness );
    writeFloat( SCENE_CINE_SHADOW_DEPTH_BIAS, "shadowDepthBias", c.shadowDepthBias );
    writeFloat( SCENE_CINE_SHADOW_SLOPE_BIAS, "shadowSlopeBias", c.shadowSlopeBias );
    writeFloat( SCENE_CINE_SHADOW_MAX_DISTANCE, "shadowMaxDistance", c.shadowMaxDistance );
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

SceneAuthoredCameraContext BuildSceneAuthoredCameraContext( SkullbonezCore::Environment::CameraCollection& cameras,
                                                            SkullbonezCore::Geometry::Terrain& terrain )
{
    return SceneAuthoredCameraContext{ cameras, terrain };
}

SceneAuthoredModelContext
BuildSceneAuthoredModelContext( RunSceneState& sceneState,
                                SkullbonezCore::Environment::WorldEnvironment& world,
                                SkullbonezCore::Geometry::Terrain* terrain,
                                SkullbonezCore::GameObjects::GameModelCollection& models,
                                SkullbonezCore::Physics::PhysicsEngine& physics,
                                std::vector<RunRequiredContactState>& requiredContacts,
                                std::vector<RunRequiredBroadphaseXCellsState>& requiredBroadphaseXCells )
{
    return SceneAuthoredModelContext{ sceneState,
                                      world,
                                      terrain,
                                      models,
                                      physics,
                                      requiredContacts,
                                      requiredBroadphaseXCells };
}

SceneGeneratedCameraContext BuildSceneGeneratedCameraContext( SkullbonezCore::Environment::CameraCollection& cameras,
                                                              SkullbonezCore::Geometry::Terrain& terrain )
{
    return SceneGeneratedCameraContext{ cameras, terrain };
}

SceneGeneratedModelContext BuildSceneGeneratedModelContext( RunSceneState& scene,
                                                            const EngineConfig& config,
                                                            SkullbonezCore::Environment::WorldEnvironment& world,
                                                            SkullbonezCore::Geometry::Terrain* terrain,
                                                            SkullbonezCore::GameObjects::GameModelCollection& models,
                                                            SkullbonezCore::Physics::PhysicsEngine& physics,
                                                            GeneratedObjectTypeOverride objectTypeOverride )
{
    return SceneGeneratedModelContext{ scene, config, world, terrain, models, physics, objectTypeOverride };
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

void ApplyConfiguredWorldEnvironment( WorldEnvironment& world, const EngineConfig& cfg, Terrain* terrain )
{
    world = WorldEnvironment( cfg.fluidHeight, cfg.fluidDensity, cfg.gasDensity, cfg.gravity );
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

SbResult UseDefaultTerrain( RunSubsystemState& systems,
                            WorldEnvironment& world,
                            const EngineConfig& config,
                            const std::string& terrainRawPath,
                            SkullbonezCore::Rendering::IRenderDeviceLifecycle* renderLifecycle,
                            SkullbonezCore::Rendering::IRenderResourceFactory* renderResources )
{
    assert( renderResources );
    if ( !renderResources )
    {
        return SbResult::Failure( "Runtime/RunScene", "Renderer resource factory unavailable for terrain load." );
    }
    if ( !systems.terrain || systems.isFlatSlopeTerrain )
    {
        if ( renderLifecycle )
        {
            renderLifecycle->FlushGPU();
        }
        std::unique_ptr<Terrain> terrain;
        const SbResult terrainResult = Terrain::TryCreateFromHeightMap( terrainRawPath.c_str(),
                                                                        256,
                                                                        8,
                                                                        15,
                                                                        config,
                                                                        systems.assets,
                                                                        *renderResources,
                                                                        terrain );
        if ( !terrainResult.ok )
        {
            // Why: RAW terrain is external scene/config input. Report the load
            // failure before replacing the currently owned terrain.
            return terrainResult;
        }
        systems.terrain = std::move( terrain );
        systems.isFlatSlopeTerrain = false;
    }
    else
    {
        systems.terrain->BindRenderContexts( config, systems.assets, *renderResources );
    }

    UpdateWorldTerrainBounds( world, systems.terrain.get() );
    return SbResult::Success();
}

void UseFlatSlopeTerrain( RunSubsystemState& systems,
                          WorldEnvironment& world,
                          const EngineConfig& config,
                          float baseY,
                          float slopeX,
                          float slopeZ,
                          SkullbonezCore::Rendering::IRenderDeviceLifecycle* renderLifecycle,
                          SkullbonezCore::Rendering::IRenderResourceFactory* renderResources )
{
    assert( renderResources );
    if ( !renderResources )
    {
        return;
    }
    if ( renderLifecycle )
    {
        renderLifecycle->FlushGPU();
    }
    systems.terrain = std::make_unique<Terrain>( baseY, slopeX, slopeZ, config, systems.assets, *renderResources );
    systems.isFlatSlopeTerrain = true;

    UpdateWorldTerrainBounds( world, systems.terrain.get() );
}

bool SaveCurrentEditableSceneSnapshot( const std::string& scenePath,
                                       const RunSceneState& sceneState,
                                       SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                       WorldEnvironment& world,
                                       CameraCollection& cameras,
                                       bool waterHidden,
                                       bool terrainHidden )
{
    return modelCollection.SaveSceneSnapshot( scenePath.c_str(),
                                              sceneState.isScenePhysics,
                                              sceneState.isSceneText,
                                              world,
                                              cameras.GetCameraTranslation(),
                                              cameras.GetCameraView(),
                                              cameras.GetCameraUp(),
                                              true,
                                              sceneState.isFixedStep,
                                              waterHidden,
                                              terrainHidden,
                                              sceneState.hasFlatSlope,
                                              sceneState.flatBaseY,
                                              sceneState.flatSlopeX,
                                              sceneState.flatSlopeZ );
}

void ApplyTornadoDefaultsForActiveScene( RunRuntimeSettings& runtimeSettings,
                                         WorldEnvironment& world,
                                         const CinematicRenderConfig& cinematic )
{
    TornadoFieldConfig field = runtimeSettings.tornadoField;
    const float basinRadius = (std::max)( cinematic.basinRadiusX, cinematic.basinRadiusZ );

    field.center = Vector3( cinematic.basinCenterX, world.GetFluidSurfaceHeight(), cinematic.basinCenterZ );
    field.radius = std::clamp( basinRadius * 1.28f, 180.0f, 340.0f );
    field.height = (std::max)( 130.0f, field.radius * 0.66f );
    field.inwardAcceleration = 150.0f;
    field.swirlAcceleration = 185.0f;
    field.liftAcceleration = 64.0f;
    runtimeSettings.tornadoField = field;
}
} // namespace

void Run::UpdateRequiredSceneContacts()
{
    if ( m_requiredSceneContacts.empty() )
    {
        return;
    }

    const PhysicsBodyStore& bodyStore = m_cGameModelCollection.GetPhysicsEngine().BodyStore();
    const ColliderStore& colliderStore = m_cGameModelCollection.GetPhysicsEngine().Colliders();
    const auto& bodyRecords = bodyStore.Records();
    const auto& colliderRecords = colliderStore.Records();
    const int contactModelCount =
        (std::min)( bodyStore.Count(), static_cast<int>( (std::min)( bodyRecords.size(), colliderRecords.size() ) ) );
    for ( RunRequiredContactState& required : m_requiredSceneContacts )
    {
        if ( required.touched || required.bodyA < 0 || required.bodyB < 0 || required.bodyA >= contactModelCount ||
             required.bodyB >= contactModelCount )
        {
            continue;
        }

        const PhysicsBodyRecord& bodyA = bodyRecords[static_cast<size_t>( required.bodyA )];
        const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<size_t>( required.bodyB )];
        const ColliderRecord& colliderA = colliderRecords[static_cast<size_t>( required.bodyA )];
        const ColliderRecord& colliderB = colliderRecords[static_cast<size_t>( required.bodyB )];
        ObjectContactManifold manifold;
        if ( BuildObjectContactManifold( SceneContactBodyView( bodyA ),
                                         colliderA.shape,
                                         SceneContactBodyView( bodyB ),
                                         colliderB.shape,
                                         required.bodyA,
                                         required.bodyB,
                                         m_config.contactEpsilon + 0.25f,
                                         manifold ) )
        {
            required.touched = true;
        }
    }

    const std::vector<PhysicsDebugContact>& contacts = m_cGameModelCollection.GetPhysicsDebugContacts();
    for ( const PhysicsDebugContact& contact : contacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 )
        {
            continue;
        }
        for ( RunRequiredContactState& required : m_requiredSceneContacts )
        {
            if ( required.touched || required.bodyA < 0 || required.bodyB < 0 )
            {
                continue;
            }
            const bool sameOrder = contact.bodyA == required.bodyA && contact.bodyB == required.bodyB;
            const bool swappedOrder = contact.bodyA == required.bodyB && contact.bodyB == required.bodyA;
            if ( sameOrder || swappedOrder )
            {
                required.touched = true;
                break;
            }
        }
    }
}


bool Run::RequiredSceneContactsComplete() const
{
    for ( const RunRequiredContactState& contact : m_requiredSceneContacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
        {
            return false;
        }
    }
    return true;
}


void Run::UpdateRequiredSceneBroadphaseXCells( const SpatialGrid::ActiveCell* activeCells, int activeCellCount )
{
    if ( m_requiredBroadphaseXCells.empty() || !activeCells || activeCellCount <= 0 )
    {
        return;
    }

    for ( RunRequiredBroadphaseXCellsState& required : m_requiredBroadphaseXCells )
    {
        if ( required.activated )
        {
            continue;
        }

        required.lastActiveCellCount = activeCellCount;
        required.lastMissingCellX = -1;
        required.hasObservedXRange = false;
        for ( int i = 0; i < activeCellCount; ++i )
        {
            const SpatialGrid::ActiveCell& active = activeCells[i];
            if ( active.iy == required.cellY && active.iz == required.cellZ )
            {
                if ( !required.hasObservedXRange )
                {
                    required.lastObservedMinX = active.ix;
                    required.lastObservedMaxX = active.ix;
                    required.hasObservedXRange = true;
                }
                else
                {
                    required.lastObservedMinX = (std::min)( required.lastObservedMinX, static_cast<int>( active.ix ) );
                    required.lastObservedMaxX = (std::max)( required.lastObservedMaxX, static_cast<int>( active.ix ) );
                }
            }
        }

        bool allActive = true;
        for ( int x = required.minCellX; x <= required.maxCellX; ++x )
        {
            bool found = false;
            for ( int i = 0; i < activeCellCount; ++i )
            {
                const SpatialGrid::ActiveCell& active = activeCells[i];
                if ( active.ix == x && active.iy == required.cellY && active.iz == required.cellZ )
                {
                    found = true;
                    break;
                }
            }

            if ( !found )
            {
                allActive = false;
                required.lastMissingCellX = x;
                break;
            }
        }

        if ( allActive )
        {
            required.activated = true;
        }
    }
}


bool Run::RequiredSceneBroadphaseXCellsComplete() const
{
    for ( const RunRequiredBroadphaseXCellsState& required : m_requiredBroadphaseXCells )
    {
        if ( !required.activated )
        {
            return false;
        }
    }
    return true;
}


SbResult Run::LoadScene( int index, bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
{
    m_lastSceneLoadResult = SbResult::Success();
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::SceneLoad );
    SceneController& runtime = m_sceneController;
    SceneRuntimeResetContext resetContext{ m_runtimeSettings,
                                           m_debug,
                                           SceneState(),
                                           m_sceneController.UIOverrides(),
                                           m_camera,
                                           m_cWorldEnvironment,
                                           m_physicsDebugVisualizer };
    SceneRuntimeLoadBeginContext loadBeginContext{ runtime,
                                                   resetContext,
                                                   m_sceneController.Browser(),
                                                   m_renderBackendView.deviceLifecycle,
                                                   m_launchOptions.interactiveSceneRun };
#ifdef _DEBUG
    EndPhysicsDiagnosticsRun( "scene_reload" );
#endif
    runtime.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeSceneUnload );
    const SceneRuntimeLoadBeginResult loadBegin =
        BeginSceneRuntimeLoad( loadBeginContext, index, suppressExitOnComplete, preserveRuntimeState );
    if ( !loadBegin.shouldLoad )
    {
        return m_lastSceneLoadResult;
    }

    const bool suppressAutomationExit = loadBegin.suppressAutomationExit;
    const bool shouldPreserveRuntimeState = loadBegin.shouldPreserveRuntimeState;
    const SceneRuntimeResetSnapshot& resetSnapshot = loadBegin.resetSnapshot;
    const std::string& scenePath = *loadBegin.scenePath;

    m_diagnosticsRuntime.ClosePerfLogWithMemoryCheckpoint( sPerfPass + 1, "end" );

    // Reset scene-local state; operator HUD preferences are restored below.
    SceneState().ResetForLoad( m_config.cinematicRender );
    m_diagnosticsRuntime.ResetPerfLogForSceneLoad();
    m_simulation.Reset();
    m_diagnosticsRuntime.Capture().ResetScreenshot();
    m_contactAudio.ResetSimpleLinearHistory();
    m_runtimeSettings.isVsyncEnabled = m_config.runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = m_config.runtimeRender.forcePipelineSync;
    m_diagnosticsRuntime.UIStress() = DiagnosticsRuntime::UIStressState{};
    m_requiredSceneContacts.clear();

    m_systems.cameras->Reset();
    m_cGameModelCollection.Clear();

    CancelMousePickup();
    ResetAttachedCamera();
    {
        const RuntimeInteractionTransition transition = m_interaction.ResetForScene( InteractionExitReason::LoadScene );
        ClearRuntimeInteractionStateForTransition( transition );
        m_interaction.ResetForScene( InteractionExitReason::LoadScene );
    }
    SetCameraModeLabelAfterInteractionTransition( scenePath.empty() ? RunCameraMode::Demo : RunCameraMode::Scene );
    m_runtimeTools.ClearRayCastTestLines();
    m_debug.isWaterFreezeDebug = false;
    m_debug.isWaterNoReflect = false;
    m_debug.isWaterRTReflect = false;
    m_debug.isWaterFlatDebug = false;
    m_debug.isTerrainHidden = false;
    m_debug.isWaterHidden = false;
    m_debug.isTextOnly = false;
    m_debug.isUITestPattern = false;
    m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
    m_debug.isPhysicsDebugTransparent = false;
    m_debug.physicsDebugAlpha = 0.28f;
    m_debug.physicsDebugContactLinger = 0.45f;
    m_debug.physicsDebugPipelineStageCursor = 0;
    m_physicsDebugVisualizer.SetFlags( PHYSICS_DEBUG_NONE );
#ifdef _DEBUG
    m_debug.reproSnapshotMessage[0] = '\0';
    m_debug.reproSnapshotMessageUntil = 0.0;
#endif
    m_debug.frozenWaterTime = 0.0f;
    m_camera.trackBallIndex = -1;
    m_camera.trackHeight = 300.0f;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
    m_camera.autoCycleShotsTaken = 0;
    m_camera.input = {};
    // overlayMode intentionally preserved — the user's HUD state persists across scene reloads.
    m_camera.selectedCamera = 0;

    m_timers.timeSinceLastRender = 0.0f;
    m_timers.renderTime = 0.0f;
    m_camera.cameraTime = 0.0f;
    m_timers.rollingRenderTime = 0.0f;
    m_timers.physicsTime = 0.0f;
    m_timers.rollingPhysicsTime = 0.0f;
    m_timers.rollingFpsTime = 0.0f;
    m_timers.rollingSceneEnergy = 0.0f;
    m_timers.cpuFrameWorkMs = 0.0f;
    m_timers.gpuFrameWorkMs = 0.0f;
    m_timers.sceneEnergyAccumulator = 0.0;
    m_timers.sceneEnergySampleCount = 0;
    m_timers.lastUIDrawCalls = 0;

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
    TornadoSystemConfig sceneTornadoSystem;

    runtime.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneCleared );
    runtime.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeScenePopulate );

    // Branch on file-backed scene mode vs generated demo mode.
    if ( scenePath.empty() )
    {
        m_config.gameModelCapacity = m_startup.gameModelCapacity;
        ApplySceneWorkerThreadSetting( m_config, *m_systems.workerPool, m_startup.workerThreads );
        if ( m_launchOptions.seedOverride > 0 )
        {
            rngSeed = m_launchOptions.seedOverride;
        }
        SceneState().rngSeed = rngSeed;
        SceneState().rngState = rngSeed;
        const SbResult terrainResult = UseDefaultTerrain(
            m_systems,
            m_cWorldEnvironment,
            m_config,
            ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain, "terrain.raw", m_config.terrainRaw ),
            m_renderBackendView.deviceLifecycle,
            m_renderBackendView.renderResources );
        if ( !terrainResult.ok )
        {
            m_lastSceneLoadResult = terrainResult;
            LogSceneLoadFailure( terrainResult, scenePath );
            return m_lastSceneLoadResult;
        }
        ApplyConfiguredWorldEnvironment( m_cWorldEnvironment, m_config, m_systems.terrain.get() );
        ApplyNoWaterOverride( m_cWorldEnvironment, m_systems.terrain.get(), m_launchOptions.noWater );
        if ( shouldPreserveRuntimeState )
        {
            // Restore setup-affecting live controls before the generated model pool is rebuilt.
            // Other visual/debug controls are restored later after scene JSON has loaded.
            ApplyUIWorldOverride( m_cWorldEnvironment,
                                  m_replayRuntime,
                                  resetSnapshot.worldGravity,
                                  resetSnapshot.worldFluidHeight,
                                  resetSnapshot.worldFluidDensity );
        }

        SceneState().isSceneMode = false;
        SceneGeneratedSetup::SetUpCameras( BuildSceneGeneratedCameraContext( *m_systems.cameras, *m_systems.terrain ) );
        const SceneGeneratedSetupResult generatedSetup = SceneGeneratedSetup::TrySetUpRequestedModels(
            BuildSceneGeneratedModelContext( SceneState(),
                                             m_config,
                                             m_cWorldEnvironment,
                                             m_systems.terrain.get(),
                                             m_cGameModelCollection,
                                             m_cGameModelCollection.GetPhysicsEngine(),
                                             m_launchOptions.generatedObjectTypeOverride ),
            SceneGeneratedPopulationRequest{ m_sceneController.UIOverrides().modelCountOverride,
                                             m_sceneController.UIOverrides().solverBallCountOverride,
                                             m_sceneController.UIOverrides().solverBoxCountOverride,
                                             0,
                                             0,
                                             DEFAULT_GAME_MODELS },
            true );
        if ( !generatedSetup.status.ok )
        {
            m_lastSceneLoadResult = generatedSetup.status;
            LogSceneLoadFailure( generatedSetup.status, scenePath );
            return m_lastSceneLoadResult;
        }
        ApplyDemoHeroStyleOverride( SceneRuntimeStyleContext{ m_launchOptions,
                                                              SceneState(),
                                                              m_sceneController.Browser(),
                                                              m_cGameModelCollection,
                                                              m_systems.assets,
                                                              RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                                              m_defaultCinematicRender } );
        const char* rendererName = m_renderBackendView.renderDiagnostics
                                       ? m_renderBackendView.renderDiagnostics->GetRendererName()
                                       : "unknown";
        char titleText[256];
        sprintf_s( titleText, "%s [%s]", TITLE_TEXT, rendererName );
        m_systems.window->SetTitleText( titleText );
    }
    else
    {
        SceneState().isSceneMode = true;
        TestScene scene;
        const SbResult sceneLoad = TestScene::TryLoadFromFile( scenePath.c_str(), m_systems.assets, scene );
        if ( !sceneLoad.ok )
        {
            m_lastSceneLoadResult = sceneLoad;
            LogSceneLoadFailure( sceneLoad, scenePath );
            return m_lastSceneLoadResult;
        }
        hasSceneTornadoSystem = scene.HasTornadoSystem();
        if ( hasSceneTornadoSystem )
        {
            sceneTornadoSystem = scene.GetTornadoSystemConfig();
        }
        m_config.gameModelCapacity =
            scene.HasModelCapacityOverride() ? scene.GetModelCapacity() : m_startup.gameModelCapacity;
        ApplySceneWorkerThreadSetting(
            m_config,
            *m_systems.workerPool,
            scene.HasWorkerThreadOverride() ? scene.GetWorkerThreads() : m_startup.workerThreads );
        SceneState().isScenePhysics = scene.IsPhysicsEnabled();
        SceneState().isSceneText = scene.IsTextEnabled();
        m_diagnosticsRuntime.ConfigurePerfLogFlush( scene.IsPerfLogFlushEnabled(), scene.GetPerfLogFlushInterval() );
        m_debug.physicsDebugFlags = scene.GetPhysicsDebugFlags();
        m_debug.isPhysicsDebugTransparent = scene.IsPhysicsDebugTransparent();
        m_debug.physicsDebugAlpha = scene.GetPhysicsDebugAlpha();
        m_debug.physicsDebugContactLinger = scene.GetPhysicsDebugContactLinger();
        if ( scene.HasVsyncOverride() )
        {
            m_runtimeSettings.isVsyncEnabled = scene.IsVsyncEnabled();
        }
        if ( scene.HasPipelineSyncOverride() )
        {
            m_runtimeSettings.isPipelineSyncEnabled = scene.IsPipelineSyncEnabled();
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
            m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
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
        SceneState().cinematicRender = m_config.cinematicRender;
        ApplyCinematicSceneOverrides( SceneState().cinematicRender,
                                      SceneState().cinematicOverrideMask,
                                      scene.GetCinematicRenderConfig() );

        const SceneUIOptions& UIOptions = scene.GetUIOptions();
        const double UINow = m_timers.simulationTimer.GetTotalTime();
        bool isAutomationScene = scene.IsExitOnComplete() || scene.IsScreenshotAndExit() ||
                                 scene.GetScreenshotFrame() >= 0 || scene.GetScreenshotMs() >= 0 ||
                                 scene.GetScreenshotInterval() > 0 || scene.GetPerfLogPath()[0] != '\0';
#ifdef _DEBUG
        isAutomationScene = isAutomationScene || m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#endif
        ApplySceneRuntimeUiOptions( SceneRuntimeUiOptionsContext{ m_UI,
                                                                  m_diagnosticsRuntime,
                                                                  m_debug,
                                                                  UINow,
                                                                  preserveUIState,
                                                                  isAutomationScene },
                                    UIOptions );
        SceneState().targetFrameCount = scene.GetFrameCount();
        SceneState().isExitOnComplete = suppressAutomationExit ? false : scene.IsExitOnComplete();
        m_diagnosticsRuntime.ApplySceneAutomationOptions( scene, suppressAutomationExit, sPerfPass );

        // Override RNG seed for deterministic scenes. CLI --seed wins so a launcher snapshot can
        // replay an unseeded/random scene or deliberately override a scene file seed.
        if ( scene.GetSeed() > 0 )
        {
            rngSeed = scene.GetSeed();
        }
        if ( m_launchOptions.seedOverride > 0 )
        {
            rngSeed = m_launchOptions.seedOverride;
        }
        SceneState().rngSeed = rngSeed;
        SceneState().rngState = rngSeed;

        // Scene terrain is authoritative.  A flat-slope test scene must not leak
        // its analytic terrain into the next height-map scene.
        if ( scene.HasFlatSlope() )
        {
            SceneState().hasFlatSlope = true;
            SceneState().flatBaseY = scene.GetFlatBaseY();
            SceneState().flatSlopeX = scene.GetFlatSlopeX();
            SceneState().flatSlopeZ = scene.GetFlatSlopeZ();
            UseFlatSlopeTerrain( m_systems,
                                 m_cWorldEnvironment,
                                 m_config,
                                 scene.GetFlatBaseY(),
                                 scene.GetFlatSlopeX(),
                                 scene.GetFlatSlopeZ(),
                                 m_renderBackendView.deviceLifecycle,
                                 m_renderBackendView.renderResources );
        }
        else
        {
            SceneState().hasFlatSlope = false;
            const SbResult terrainResult =
                UseDefaultTerrain( m_systems,
                                   m_cWorldEnvironment,
                                   m_config,
                                   ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                           "terrain.raw",
                                                           m_config.terrainRaw ),
                                   m_renderBackendView.deviceLifecycle,
                                   m_renderBackendView.renderResources );
            if ( !terrainResult.ok )
            {
                m_lastSceneLoadResult = terrainResult;
                LogSceneLoadFailure( terrainResult, scenePath );
                return m_lastSceneLoadResult;
            }
        }

        ApplyConfiguredWorldEnvironment( m_cWorldEnvironment, m_config, m_systems.terrain.get() );
        // Override world environment if scene specifies world values
        if ( scene.HasWorldOverride() )
        {
            m_cWorldEnvironment = WorldEnvironment( scene.GetWorldFluidHeight(),
                                                    scene.GetWorldFluidDensity(),
                                                    m_config.gasDensity,
                                                    scene.GetWorldGravity() );
            m_cWorldEnvironment.BindRuntimeConfig( m_config );
            UpdateWorldTerrainBounds( m_cWorldEnvironment, m_systems.terrain.get() );
        }
        ApplyNoWaterOverride( m_cWorldEnvironment, m_systems.terrain.get(), m_launchOptions.noWater );
        if ( shouldPreserveRuntimeState )
        {
            // World sliders/keyboard water edits are part of the live scene controls.
            // Restore them after terrain/world JSON and --no-water have resolved,
            // so a plain reset keeps the operator's current environment.
            ApplyUIWorldOverride( m_cWorldEnvironment,
                                  m_replayRuntime,
                                  resetSnapshot.worldGravity,
                                  resetSnapshot.worldFluidHeight,
                                  resetSnapshot.worldFluidDensity );
        }

        SceneAuthoredSetup::SetUpCameras( BuildSceneAuthoredCameraContext( *m_systems.cameras, *m_systems.terrain ),
                                          scene );

        const SceneGeneratedSetupResult generatedModels = SceneGeneratedSetup::TrySetUpRequestedModels(
            BuildSceneGeneratedModelContext( SceneState(),
                                             m_config,
                                             m_cWorldEnvironment,
                                             m_systems.terrain.get(),
                                             m_cGameModelCollection,
                                             m_cGameModelCollection.GetPhysicsEngine(),
                                             m_launchOptions.generatedObjectTypeOverride ),
            SceneGeneratedPopulationRequest{ m_sceneController.UIOverrides().modelCountOverride,
                                             m_sceneController.UIOverrides().solverBallCountOverride,
                                             m_sceneController.UIOverrides().solverBoxCountOverride,
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
            const SbResult authoredSetup = SceneAuthoredSetup::SetUpGameModels(
                BuildSceneAuthoredModelContext( SceneState(),
                                                m_cWorldEnvironment,
                                                m_systems.terrain.get(),
                                                m_cGameModelCollection,
                                                m_cGameModelCollection.GetPhysicsEngine(),
                                                m_requiredSceneContacts,
                                                m_requiredBroadphaseXCells ),
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
        m_cGameModelCollection.SetPhysicsRegressionLogPath(
            m_diagnosticsRuntime.PerfLog().physicsRegressionLogOverride );
        m_cGameModelCollection.SetPhysicsCollisionTimeLogPath(
            m_diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride );
        if ( m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled )
        {
            m_cGameModelCollection.SetPhysicsDiagnosticsPath( m_diagnosticsRuntime.PhysicsDiagnostics().path );
        }
#endif

        // Ball-tracking camera: enabled when scene specifies a positive track_height
        if ( scene.GetTrackHeight() > 0.0f )
        {
            m_camera.trackHeight = scene.GetTrackHeight();
            m_camera.trackBallIndex = 0;
            m_camera.autoCycleInterval = scene.GetAutoCycleInterval(); // -1 if not specified = disabled
        }
        const char* rendererName = m_renderBackendView.renderDiagnostics
                                       ? m_renderBackendView.renderDiagnostics->GetRendererName()
                                       : "unknown";
        char titleText[256];
        sprintf_s( titleText, "%s [SCENE MODE] [%s]", TITLE_TEXT, rendererName );
        m_systems.window->SetTitleText( titleText );

        // Snapshot scenes start paused in Inspect by default; authored live scenes
        // may opt out when body-state entries are just stable initial poses.
        const bool hasSnapshotState =
            scene.GetBallStateCount() > 0 || scene.GetBoxStateCount() > 0 || scene.GetConvexHullStateCount() > 0;
#ifdef _DEBUG
        const bool shouldPauseSnapshotState = hasSnapshotState && scene.ShouldPauseSnapshotState() &&
                                              !m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#else
        const bool shouldPauseSnapshotState = hasSnapshotState && scene.ShouldPauseSnapshotState();
#endif
        if ( shouldPauseSnapshotState )
        {
            const RuntimeInteractionTransition transition = m_interaction.EnterInspect();
            ApplyRuntimeInteractionTransitionCleanup( transition );
            SetCameraModeLabelAfterInteractionTransition( RunCameraMode::Inspect );
            m_camera.cameraTime = 0.0f;
            XZBounds unbounded;
            unbounded.m_xMin = -99999.9f;
            unbounded.m_xMax = 99999.9f;
            unbounded.m_zMin = -99999.9f;
            unbounded.m_zMax = 99999.9f;
            uint32_t activeCam = m_systems.cameras->GetSelectedCameraName();
            m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
            Input::SetSystemCursorVisible( false );
            m_camera.input.xMove = 0;
            m_camera.input.yMove = 0;
            m_camera.hasMouseLookLastClient = false;
            m_camera.needsMouseLookReset = true;
            Input::ResetMouseLookDeltas();
        }
    }

    runtime.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterScenePopulate );

    if ( shouldPreserveRuntimeState )
    {
        RestoreSceneRuntimeResetSnapshot( resetContext, resetSnapshot, suppressExitOnComplete );
    }

    // CLI --time-scale and --fixed-step override anything the scene file sets.
    if ( m_launchOptions.timeScaleOverride > 0.0f )
    {
        SceneState().timeScale = m_launchOptions.timeScaleOverride;
    }
    if ( m_sceneController.UIOverrides().timeScaleOverride > 0.0f )
    {
        SceneState().timeScale = m_sceneController.UIOverrides().timeScaleOverride;
    }
    if ( m_launchOptions.fixedStep )
    {
        SceneState().isFixedStep = true;
    }
    if ( !shouldPreserveRuntimeState )
    {
        m_runtimeSettings.tornadoField = Physics::TornadoFieldConfig();
        m_runtimeSettings.tornadoSystem = Physics::TornadoSystemConfig();
        ApplyTornadoDefaultsForActiveScene( m_runtimeSettings,
                                            m_cWorldEnvironment,
                                            RuntimeActiveCinematicConfig( SceneState(), m_config ) );
        if ( hasSceneTornadoSystem )
        {
            m_runtimeSettings.tornadoSystem = sceneTornadoSystem;
            m_runtimeSettings.tornadoField.enabled = false;
            m_runtimeSettings.tornadoVisual.enabled = true;
        }
    }
    if ( m_launchOptions.hasTornadoOverride )
    {
        if ( m_runtimeSettings.tornadoSystem.enabled || !m_runtimeSettings.tornadoSystem.vortices.empty() )
        {
            m_runtimeSettings.tornadoSystem.enabled = m_launchOptions.tornadoEnabled;
            m_runtimeSettings.tornadoField.enabled = false;
        }
        else
        {
            m_runtimeSettings.tornadoField.enabled = m_launchOptions.tornadoEnabled;
        }
        if ( m_runtimeSettings.tornadoVisual.autoEnableWithTornado )
        {
            m_runtimeSettings.tornadoVisual.enabled = m_launchOptions.tornadoEnabled;
        }
    }
    if ( m_launchOptions.tornadoVectors )
    {
        m_runtimeSettings.tornadoField.visualizeVelocityField = true;
        m_runtimeSettings.tornadoSystem.visualizeVelocityField = true;
    }
    SyncTornadoRuntimeSettingsToPhysics( m_cGameModelCollection, m_runtimeSettings );
    m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
    if ( m_launchOptions.frameCountOverride > 0 )
    {
        SceneState().targetFrameCount = m_launchOptions.frameCountOverride;
        SceneState().isExitOnComplete = true;
    }
    if ( m_launchOptions.uiStress )
    {
        m_diagnosticsRuntime.UIStress().enabled = true;
        m_diagnosticsRuntime.UIStress().randomState = m_launchOptions.uiStressSeed;
        m_diagnosticsRuntime.UIStress().actionsPerFrame = m_launchOptions.uiStressActions;
        m_UI.SetVisible( true, m_timers.simulationTimer.GetTotalTime() );
        m_UI.SetMinimized( false, m_timers.simulationTimer.GetTotalTime() );
    }
    if ( m_launchOptions.graphicsStress )
    {
        // Invariant: scene reloads reset authored scene automation, but a
        // graphics-stress run is operator-owned and must keep running until the
        // launcher or timeout stops the process.
        m_graphicsStress.enabled = true;
        if ( m_graphicsStress.randomState == 0 )
        {
            m_graphicsStress.randomState = m_launchOptions.graphicsStressSeed;
        }
        m_graphicsStress.actionsPerFrame = m_launchOptions.graphicsStressActions;
        m_graphicsStress.sceneIntervalFrames = m_launchOptions.graphicsStressSceneIntervalFrames;
        SceneState().isInteractiveRun = true;
        SceneState().targetFrameCount = 0;
        SceneState().isTestComplete = false;
        SceneState().isExitOnComplete = false;
        m_diagnosticsRuntime.Capture().ResetScreenshot();
        m_diagnosticsRuntime.ClosePerfLog();
        m_diagnosticsRuntime.ResetPerfLogForSceneLoad();
        m_UI.SetVisible( true, m_timers.simulationTimer.GetTotalTime() );
        m_UI.SetMinimized( false, m_timers.simulationTimer.GetTotalTime() );
    }
    if ( m_launchOptions.hasCinematicShadowsOverride )
    {
        RuntimeActiveCinematicConfig( SceneState(), m_config ).shadowsEnabled = m_launchOptions.cinematicShadows;
        SceneState().cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    }
    if ( m_launchOptions.hasPhysicsDebugFlagsOverride )
    {
        m_debug.physicsDebugFlags = m_launchOptions.physicsDebugFlagsOverride;
    }
    if ( m_launchOptions.hasPhysicsDebugTransparentOverride )
    {
        m_debug.isPhysicsDebugTransparent = m_launchOptions.physicsDebugTransparentOverride;
    }
    if ( m_launchOptions.hasPhysicsDebugAlphaOverride )
    {
        m_debug.physicsDebugAlpha = m_launchOptions.physicsDebugAlphaOverride;
    }
    if ( m_launchOptions.hasPhysicsDebugContactLingerOverride )
    {
        m_debug.physicsDebugContactLinger = m_launchOptions.physicsDebugContactLingerOverride;
    }

#ifdef _DEBUG
    Log().WriteEventf(
        "scene_started index=%d load=%d path=\"%s\" renderer=\"%s\" target_frames=%d seed=%u "
        "fixed_step=%d physics=%d text=%d models=%d",
        SceneState().currentSceneIndex,
        SceneState().loadCount,
        scenePath.empty() ? "generated" : scenePath.c_str(),
        m_renderBackendView.renderDiagnostics ? m_renderBackendView.renderDiagnostics->GetRendererName() : "unknown",
        SceneState().targetFrameCount,
        SceneState().rngSeed,
        SceneState().isFixedStep ? 1 : 0,
        SceneState().isScenePhysics ? 1 : 0,
        SceneState().isSceneText ? 1 : 0,
        SceneState().modelCount );
#endif

#ifdef _DEBUG
    BeginPhysicsDiagnosticsRun( scenePath.c_str() );
#endif

    // Runtime swap policy is chosen after config/scene overrides are resolved.
    if ( m_renderBackendView.deviceLifecycle )
    {
        m_renderBackendView.deviceLifecycle->SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
    }

    // Restart timers
    m_timers.frameTimer.StartTimer();
    m_timers.workTimer.StartTimer();
    m_timers.updateTimer.StartTimer();
    m_timers.cameraTimer.StartTimer();
    m_timers.simulationTimer.StartTimer();
    ResetReplayTimelineForActiveScene();

    // Initialize DXR raytracing on first scene load (requires terrain + sphere meshes to exist)
    // Force sphere mesh creation (normally lazy-init on first render)
    SkullbonezCore::Rendering::IRenderRayTracing* rayTracing = m_renderBackendView.rayTracingBackend;
    SkullbonezCore::Rendering::IRenderResourceFactory* renderResources = m_renderBackendView.renderResources;
    SkullbonezCore::Rendering::IRenderCommandContext* renderCommands = m_renderBackendView.renderCommands;
    SkullbonezCore::Rendering::IRenderDiagnostics* renderDiagnostics = m_renderBackendView.renderDiagnostics;
    const bool hasRayTracingReflection =
        renderDiagnostics && renderDiagnostics->GetCapabilities().supportsDxrReflection && rayTracing;
    if ( hasRayTracingReflection && m_renderer.Helper().GetSphereInstMeshHandle() == 0 )
    {
        if ( !renderResources || !renderCommands || !renderDiagnostics )
        {
            throw std::runtime_error(
                "DXR reflection initialization requires render resource, command, and diagnostics facets" );
        }
        const RenderHelperContext helperContext{ *renderResources,
                                                 *renderCommands,
                                                 *renderDiagnostics,
                                                 m_systems.assets,
                                                 m_config,
                                                 m_renderer.Helper() };
        m_renderer.Helper().EnsureSphereMesh( helperContext );
    }
    if ( hasRayTracingReflection && m_systems.terrain && m_systems.terrain->GetMesh() )
    {
        IMesh* terrainMesh = m_systems.terrain->GetMesh();
        uint64_t terrainVBVA = terrainMesh->GetVertexBufferGPUVA();
        int terrainVertCount = terrainMesh->GetVertexCount();
        int terrainStride = terrainMesh->GetStride();

        uint32_t sphereHandle = m_renderer.Helper().GetSphereInstMeshHandle();
        uint64_t sphereVBVA = rayTracing->GetInstancedMeshStaticVBVA( sphereHandle );
        int sphereVertCount = m_renderer.Helper().GetSphereVertexCount();
        int sphereStride = rayTracing->GetInstancedMeshStaticStride( sphereHandle );

        if ( terrainVBVA != 0 && sphereVBVA != 0 )
        {
            rayTracing->InitDXR( terrainVBVA,
                                 terrainVertCount,
                                 terrainStride,
                                 sphereVBVA,
                                 sphereVertCount,
                                 sphereStride,
                                 m_startup.gameModelCapacity );
        }
    }
    runtime.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneActivated );
    m_lastSceneLoadResult = SbResult::Success();
    return m_lastSceneLoadResult;
}


bool Run::SaveCurrentSceneDefaults()
{
    const std::string* scenePath = m_sceneController.CurrentPath();
    if ( !SceneState().isSceneMode || !scenePath || scenePath->empty() )
    {
        return false;
    }
    if ( SceneState().isEditableScene )
    {
        return SaveCurrentEditableSceneSnapshot( *scenePath,
                                                 SceneState(),
                                                 m_cGameModelCollection,
                                                 m_cWorldEnvironment,
                                                 *m_systems.cameras,
                                                 m_debug.isWaterHidden,
                                                 m_debug.isTerrainHidden );
    }

    std::ifstream input( *scenePath );
    if ( !input )
    {
        return false;
    }

    Json root;
    try
    {
        input >> root;
    }
    catch ( const std::exception& )
    {
        return false;
    }

    if ( !root.is_object() )
    {
        return false;
    }

    root["format"] = "skullbonez.scene.json";
    root["version"] = 1;
    Json& simulation = EnsureJsonObject( root, "simulation" );
    Json& playback = EnsureJsonObject( root, "playback" );
    Json& runtime = EnsureJsonObject( root, "runtime" );
    Json& debug = EnsureJsonObject( root, "debug" );
    Json& physicsDebug = EnsureJsonObject( debug, "physics" );
    Json& world = EnsureJsonObject( simulation, "world" );

    simulation["physics"] = SceneState().isScenePhysics;
    simulation["text"] = SceneState().isSceneText;
    simulation["textOnly"] = m_debug.isTextOnly;
    runtime["vsync"] = m_runtimeSettings.isVsyncEnabled;
    runtime["pipelineSync"] = m_runtimeSettings.isPipelineSyncEnabled;
    playback["fixedStep"] = SceneState().isFixedStep;
    if ( SceneState().targetFrameCount > 0 )
    {
        playback["frames"] = SceneState().targetFrameCount;
    }
    else
    {
        playback["frames"] = "unlimited";
    }

    simulation["seed"] = (std::max)( 1u, SceneState().rngSeed );
    simulation["timeScale"] = SceneState().timeScale;
    playback["exitOnComplete"] = SceneState().isExitOnComplete;

    physicsDebug["axes"] = ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_AXES ) != 0;
    physicsDebug["contacts"] = ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_CONTACTS ) != 0;
    physicsDebug["sleep"] = ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_SLEEP ) != 0;
    physicsDebug["pipeline"] = ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_PIPELINE ) != 0;
    physicsDebug["terrainContact"] = ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_TERRAIN_CONTACT ) != 0;
    physicsDebug["transparent"] = m_debug.isPhysicsDebugTransparent;
    physicsDebug["alpha"] = m_debug.physicsDebugAlpha;
    physicsDebug["contactLinger"] = m_debug.physicsDebugContactLinger;

    debug["collisionVisualizer"] = m_debug.isCollisionVisualizer;
    debug["broadphaseOverlay"] = m_debug.isBroadphaseOverlay;
    debug["waterFreeze"] = m_debug.isWaterFreezeDebug;
    debug["waterFlat"] = m_debug.isWaterFlatDebug;
    debug["waterHidden"] = m_debug.isWaterHidden;
    debug["terrainHidden"] = m_debug.isTerrainHidden;
    debug["waterReflection"] = WaterReflectionJsonValue( m_debug.isWaterNoReflect, m_debug.isWaterRTReflect );
    if ( m_camera.trackBallIndex >= 0 && m_camera.trackHeight > 0.0f )
    {
        playback["trackHeight"] = m_camera.trackHeight;
    }
    else
    {
        playback.erase( "trackHeight" );
    }
    if ( m_camera.autoCycleInterval > 0.0f )
    {
        playback["autoCycleInterval"] = m_camera.autoCycleInterval;
    }
    else
    {
        playback.erase( "autoCycleInterval" );
    }
    world["gravity"] = m_cWorldEnvironment.GetGravity();
    world["fluidHeight"] = m_cWorldEnvironment.GetFluidSurfaceHeight();
    world["fluidDensity"] = m_cWorldEnvironment.GetFluidDensity();
    SetTouchedCinematicSceneProperties( root, SceneState().uiCinematicOverrideMask, SceneState().cinematicRender );

    if ( m_sceneController.UIOverrides().modelCountOverride >= 0 )
    {
        simulation["solverBalls"] = m_sceneController.UIOverrides().modelCountOverride;
        simulation.erase( "solverBoxes" );
    }
    else if ( SceneState().solverBallCount > 0 || SceneState().solverBoxCount > 0 ||
              m_sceneController.UIOverrides().solverBallCountOverride >= 0 ||
              m_sceneController.UIOverrides().solverBoxCountOverride >= 0 )
    {
        simulation["solverBalls"] = SceneState().solverBallCount;
        simulation["solverBoxes"] = SceneState().solverBoxCount;
    }

    std::ofstream output( *scenePath, std::ios::trunc );
    if ( !output )
    {
        return false;
    }

    output << root.dump( 2 ) << '\n';
    return output.good();
}
