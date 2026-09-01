/*
File: SkullbonezSource/Runtime/Tools/LauncherTools.cpp
Purpose:
  Owns launcher-mode raycast, projectile, laser, and repro snapshot behavior.

Summary:
  Launcher tools translate launcher-mode intent into read-only picks,
  projectile or impulse requests, laser presentation, and stable debug repro
  snapshots without retaining scene or physics owners.

Glossary:
  Repro snapshot: Debug-only text dump of the object under the launcher
    crosshair, including enough scene and physics state to recreate the issue.

Invariants:
  - Launcher repro output is a debugging interface; key names and numeric
    precision should stay stable unless every downstream consumer is updated.
  - Target picking is read-only and must not perturb physics, selection, or
    launcher shot history.

Related:
  - SkullbonezSource/Runtime/Tools/LauncherLaser.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "RuntimeTools.h"
#include "../Camera/CameraCollection.h"
#include "../Startup/RunLaunchOptions.h"
#include "../Scene/SceneGeneratedSetup.h"
#include "../Scene/SceneSessionState.h"
#include "../Scene/SceneWorld.h"
#include "../../World/WorldEnvironment.h"
#include "../Scene/SceneEntityStore.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/TerrainSupportClassifier.h"

#include <cfloat>
#include <memory>
#include <time.h>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Math::Vector::Dot;
using SkullbonezCore::Math::Vector::Vector3;

#ifdef _DEBUG
namespace
{
constexpr const char* LAUNCHER_REPRO_SNAPSHOT_PATH = "Debug/launcher_repro_snapshots.txt";
constexpr double LAUNCHER_REPRO_MESSAGE_SECONDS = 3.0;

const char* LauncherReproShapeName( ColliderShapeKind kind )
{
    switch ( kind )
    {
    case ColliderShapeKind::Box:
        return "box";
    case ColliderShapeKind::ConvexHull:
        return "convex_hull";
    case ColliderShapeKind::Sphere:
    default:
        return "sphere";
    }
}


float LauncherReproRadius( const ColliderRecord& collider )
{
    return collider.boundingRadius > 0.0f ? collider.boundingRadius : GetShapeBoundingRadius( collider.shape );
}


const ColliderRecord* LauncherReproColliderForModelIndex( const PhysicsBodyStore& bodyStore,
                                                          const ColliderStore& colliderStore, int modelIndex )
{
    const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( bodyHandle );
    return colliderStore.RecordForHandle( colliderHandle );
}


const PhysicsBodyRecord* LauncherReproBodyForCollider( const PhysicsBodyStore& bodyStore, const ColliderRecord& collider )
{
    return bodyStore.RecordForHandle( collider.body );
}

struct LauncherReproSessionSnapshot
{
    std::string scenePath;
    int currentSceneIndex = -1;
    int loadCount = 0;
    int manualResetCount = 0;
    int currentFrame = 0;
    int targetFrameCount = -1;
    unsigned int rngSeed = 0;
    float timeScale = 1.0f;
    bool sceneMode = false;
    bool fixedStep = false;
};

struct LauncherReproLaunchSnapshot
{
    const char* generatedObjectOverride = "mixed";
    const char* generatedObjectArg = "";
    unsigned int seedOverride = 0;
    bool noWater = false;
    bool noSleep = false;
    bool fixedStep = false;
    bool physicsSleepEnabled = false;
};

struct LauncherReproRuntimeSnapshot
{
    std::string rendererName;
    Vector3 cameraPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 cameraView = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 cameraUp = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    double simulationSeconds = 0.0;
    float gravity = 0.0f;
    float fluidHeight = 0.0f;
    float fluidDensity = 0.0f;
    float frictionCoeff = 0.0f;
    float contactEpsilon = 0.0f;
    int modelCount = 0;
    bool vsyncEnabled = false;
    bool pipelineSyncEnabled = false;
    bool waterHidden = false;
    bool terrainHidden = false;
    bool collisionVisualizer = false;
};

struct LauncherReproTargetSnapshot
{
    std::string name;
    std::string hullName;
    ColliderShapeKind shapeKind = ColliderShapeKind::Sphere;
    Vector3 position = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 velocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 angularVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 rotationalInertia = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 inverseRotationalInertia = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 boxHalfExtents = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 terrainNormal = Vector3( 0.0f, 1.0f, 0.0f );
    float orientationX = 0.0f;
    float orientationY = 0.0f;
    float orientationZ = 0.0f;
    float orientationW = 1.0f;
    float rayT = 0.0f;
    float crosshairDistance = 0.0f;
    float mass = 0.0f;
    float restitution = 0.0f;
    float boundingRadius = 0.0f;
    float shapeVolume = 0.0f;
    float shapeArea = 0.0f;
    float shapeDrag = 0.0f;
    float sphereRadius = 0.0f;
    float boxMinTerrainGap = 0.0f;
    float boxMaxTerrainGap = 0.0f;
    float terrainHeight = 0.0f;
    unsigned int hullVertexCount = 0;
    unsigned int hullFaceCount = 0;
    unsigned int hullEdgeCount = 0;
    int targetIndex = -1;
    int boxTerrainSupportedVertices = -1;
    int sleeping = 0;
    int sleepSupported = 0;
    int sleepInhibited = 0;
    int sleepIslandVisualId = 0;
    int collisionVisualContact = 0;
    bool terrainAtCenter = false;
};

struct LauncherReproSnapshot
{
    LauncherReproSessionSnapshot session;
    LauncherReproLaunchSnapshot launch;
    LauncherReproRuntimeSnapshot runtime;
    LauncherReproTargetSnapshot target;
};

void WriteLauncherReproCommandHint( FILE* file, const LauncherReproSessionSnapshot& session,
                                    const LauncherReproLaunchSnapshot& launch )
{
    if ( session.sceneMode )
    {
        fprintf( file,
                 "repro_command_hint,Debug\\SKULLBONEZ_CORE.exe --renderer dx12 --scene \"%s\" --seed %u --time-scale "
                 "%.6f%s%s%s%s\n",
                 session.scenePath.c_str(), session.rngSeed, session.timeScale, launch.fixedStep ? " --fixed-step" : "",
                 launch.noWater ? " --no-water" : "", launch.physicsSleepEnabled ? "" : " --no-sleep",
                 launch.generatedObjectArg );
        return;
    }

    fprintf( file, "repro_command_hint,Debug\\SKULLBONEZ_CORE.exe --renderer dx12 --seed %u --time-scale %.6f%s%s%s%s\n",
             session.rngSeed, session.timeScale, launch.fixedStep ? " --fixed-step" : "",
             launch.noWater ? " --no-water" : "", launch.physicsSleepEnabled ? "" : " --no-sleep",
             launch.generatedObjectArg );
}

void WriteLauncherReproShape( FILE* file, const LauncherReproTargetSnapshot& target )
{
    if ( target.shapeKind == ColliderShapeKind::Sphere )
    {
        fprintf( file, "sphere_radius,%.6f\n", target.sphereRadius );
        return;
    }

    if ( target.shapeKind == ColliderShapeKind::Box )
    {
        fprintf( file, "box_half_extents,%.6f,%.6f,%.6f\n", target.boxHalfExtents.x, target.boxHalfExtents.y,
                 target.boxHalfExtents.z );
        fprintf( file, "box_terrain_supported_vertices,%d\n", target.boxTerrainSupportedVertices );
        fprintf( file, "box_min_terrain_gap,%.6f\n", target.boxMinTerrainGap );
        fprintf( file, "box_max_terrain_gap,%.6f\n", target.boxMaxTerrainGap );
        return;
    }

    fprintf( file, "hull_name,%s\n", target.hullName.c_str() );
    fprintf( file, "hull_vertices,%u\n", target.hullVertexCount );
    fprintf( file, "hull_faces,%u\n", target.hullFaceCount );
    fprintf( file, "hull_edges,%u\n", target.hullEdgeCount );
}

void WriteLauncherReproObjectHint( FILE* file, const LauncherReproTargetSnapshot& target )
{
    const char* objectType = target.shapeKind == ColliderShapeKind::Sphere
                                 ? "ball_state/manual"
                                 : ( target.shapeKind == ColliderShapeKind::Box ? "box/manual" : "convex_hull/manual" );
    fprintf( file, "scene_object_line_hint,%s %s %.6f %.6f %.6f", objectType, target.name.c_str(), target.position.x,
             target.position.y, target.position.z );

    if ( target.shapeKind == ColliderShapeKind::Sphere )
    {
        fprintf( file, " radius=%.6f mass=%.6f restitution=%.6f", target.sphereRadius, target.mass, target.restitution );
    }
    else if ( target.shapeKind == ColliderShapeKind::Box )
    {
        fprintf( file, " halfExtents=%.6f,%.6f,%.6f mass=%.6f restitution=%.6f", target.boxHalfExtents.x,
                 target.boxHalfExtents.y, target.boxHalfExtents.z, target.mass, target.restitution );
    }
    else
    {
        fprintf( file, " hull=%s vertices=%u faces=%u edges=%u mass=%.6f restitution=%.6f", target.hullName.c_str(),
                 target.hullVertexCount, target.hullFaceCount, target.hullEdgeCount, target.mass, target.restitution );
    }
    fprintf( file, "\n" );
}

void WriteLauncherReproTargetRows( FILE* file, const LauncherReproTargetSnapshot& target )
{
    fprintf( file, "pick_index,%d\n", target.targetIndex );
    fprintf( file, "pick_name,%s\n", target.name.c_str() );
    fprintf( file, "pick_shape,%s\n", LauncherReproShapeName( target.shapeKind ) );
    fprintf( file, "pick_ray_t,%.6f\n", target.rayT );
    fprintf( file, "pick_crosshair_distance,%.6f\n", target.crosshairDistance );
    fprintf( file, "position,%.6f,%.6f,%.6f\n", target.position.x, target.position.y, target.position.z );
    fprintf( file, "velocity,%.6f,%.6f,%.6f\n", target.velocity.x, target.velocity.y, target.velocity.z );
    fprintf( file, "angular_velocity,%.6f,%.6f,%.6f\n", target.angularVelocity.x, target.angularVelocity.y,
             target.angularVelocity.z );
    fprintf( file, "speed,%.6f\n", sqrtf( VectorMagSquared( target.velocity ) ) );
    fprintf( file, "omega_mag,%.6f\n", sqrtf( VectorMagSquared( target.angularVelocity ) ) );
    fprintf( file, "orientation_q,%.8f,%.8f,%.8f,%.8f\n", target.orientationX, target.orientationY, target.orientationZ,
             target.orientationW );
    fprintf( file, "mass,%.6f\n", target.mass );
    fprintf( file, "restitution,%.6f\n", target.restitution );
    fprintf( file, "rotational_inertia,%.6f,%.6f,%.6f\n", target.rotationalInertia.x, target.rotationalInertia.y,
             target.rotationalInertia.z );
    fprintf( file, "inverse_rotational_inertia,%.6f,%.6f,%.6f\n", target.inverseRotationalInertia.x,
             target.inverseRotationalInertia.y, target.inverseRotationalInertia.z );
    fprintf( file, "shape_bounding_radius,%.6f\n", target.boundingRadius );
    fprintf( file, "shape_volume,%.6f\n", target.shapeVolume );
    fprintf( file, "shape_projected_area,%.6f\n", target.shapeArea );
    fprintf( file, "shape_drag_coefficient,%.6f\n", target.shapeDrag );
    WriteLauncherReproShape( file, target );
    fprintf( file, "sleeping,%d\n", target.sleeping );
    fprintf( file, "sleep_supported_this_frame,%d\n", target.sleepSupported );
    fprintf( file, "sleep_inhibited_this_frame,%d\n", target.sleepInhibited );
    fprintf( file, "sleep_island_visual_id,%d\n", target.sleepIslandVisualId );
    fprintf( file, "collision_visual_contact_this_frame,%d\n", target.collisionVisualContact );
    fprintf( file, "terrain_at_center,%d\n", target.terrainAtCenter ? 1 : 0 );
    fprintf( file, "terrain_height_at_center,%.6f\n", target.terrainHeight );
    fprintf( file, "terrain_normal_at_center,%.6f,%.6f,%.6f\n", target.terrainNormal.x, target.terrainNormal.y,
             target.terrainNormal.z );
    WriteLauncherReproObjectHint( file, target );
}

LauncherReproSnapshotStatus WriteLauncherReproSnapshotFile( const LauncherReproSnapshot& snapshot )
{
    // Invariant: this function and its section writers receive detached values;
    // no SceneWorld, Physics, Terrain, camera, entity, or renderer owner is reachable.
    CreateDirectoryA( "Debug", nullptr );
    FILE* rawFile = nullptr;
    if ( fopen_s( &rawFile, LAUNCHER_REPRO_SNAPSHOT_PATH, "a" ) != 0 || !rawFile )
    {
        return LauncherReproSnapshotStatus::WriteFailed;
    }

    std::unique_ptr<FILE, decltype( &fclose )> fileOwner( rawFile, fclose );
    FILE* file = fileOwner.get();
    fprintf( file, "\n=== LAUNCHER REPRO SNAPSHOT ===\n" );
    fprintf( file, "timestamp_epoch,%lld\n", static_cast<long long>( time( nullptr ) ) );
    fprintf( file, "snapshot_file,%s\n", LAUNCHER_REPRO_SNAPSHOT_PATH );
    fprintf( file, "scene,%s\n", snapshot.session.scenePath.c_str() );
    fprintf( file, "scene_mode,%d\n", snapshot.session.sceneMode ? 1 : 0 );
    fprintf( file, "scene_index,%d\n", snapshot.session.currentSceneIndex );
    fprintf( file, "scene_load_count,%d\n", snapshot.session.loadCount );
    fprintf( file, "manual_reset_count,%d\n", snapshot.session.manualResetCount );
    fprintf( file, "scene_frame,%d\n", snapshot.session.currentFrame );
    fprintf( file, "target_frame_count,%d\n", snapshot.session.targetFrameCount );
    fprintf( file, "simulation_seconds,%.6f\n", snapshot.runtime.simulationSeconds );
    fprintf( file, "rng_seed,%u\n", snapshot.session.rngSeed );
    fprintf( file, "cmd_seed_override,%u\n", snapshot.launch.seedOverride );
    fprintf( file, "cmd_no_water,%d\n", snapshot.launch.noWater ? 1 : 0 );
    fprintf( file, "cmd_no_sleep,%d\n", snapshot.launch.noSleep ? 1 : 0 );
    fprintf( file, "physics_sleep_enabled,%d\n", snapshot.launch.physicsSleepEnabled ? 1 : 0 );
    fprintf( file, "fixed_step_effective,%d\n", snapshot.session.fixedStep ? 1 : 0 );
    fprintf( file, "cmd_fixed_step_override,%d\n", snapshot.launch.fixedStep ? 1 : 0 );
    fprintf( file, "scene_session_render_frame_lockstep_requested,%d\n", snapshot.session.fixedStep ? 1 : 0 );
    fprintf( file, "explicit_render_frame_lockstep,%d\n", snapshot.launch.fixedStep ? 1 : 0 );
    fprintf( file, "time_scale,%.6f\n", snapshot.session.timeScale );
    fprintf( file, "renderer,%s\n", snapshot.runtime.rendererName.c_str() );
    fprintf( file, "generated_object_override,%s\n", snapshot.launch.generatedObjectOverride );
    fprintf( file, "model_count,%d\n", snapshot.runtime.modelCount );
    fprintf( file, "vsync_enabled,%d\n", snapshot.runtime.vsyncEnabled ? 1 : 0 );
    fprintf( file, "pipeline_sync_enabled,%d\n", snapshot.runtime.pipelineSyncEnabled ? 1 : 0 );
    WriteLauncherReproCommandHint( file, snapshot.session, snapshot.launch );
    fprintf( file, "water_hidden,%d\n", snapshot.runtime.waterHidden ? 1 : 0 );
    fprintf( file, "terrain_hidden,%d\n", snapshot.runtime.terrainHidden ? 1 : 0 );
    fprintf( file, "collision_visualizer,%d\n", snapshot.runtime.collisionVisualizer ? 1 : 0 );
    fprintf( file, "world_gravity,%.6f\n", snapshot.runtime.gravity );
    fprintf( file, "world_fluid_height,%.6f\n", snapshot.runtime.fluidHeight );
    fprintf( file, "world_fluid_density,%.6f\n", snapshot.runtime.fluidDensity );
    fprintf( file, "cfg_friction_coeff,%.6f\n", snapshot.runtime.frictionCoeff );
    fprintf( file, "cfg_contact_epsilon,%.6f\n", snapshot.runtime.contactEpsilon );
    fprintf( file, "camera_eye,%.6f,%.6f,%.6f\n", snapshot.runtime.cameraPosition.x, snapshot.runtime.cameraPosition.y,
             snapshot.runtime.cameraPosition.z );
    fprintf( file, "camera_view,%.6f,%.6f,%.6f\n", snapshot.runtime.cameraView.x, snapshot.runtime.cameraView.y,
             snapshot.runtime.cameraView.z );
    fprintf( file, "camera_up,%.6f,%.6f,%.6f\n", snapshot.runtime.cameraUp.x, snapshot.runtime.cameraUp.y,
             snapshot.runtime.cameraUp.z );
    WriteLauncherReproTargetRows( file, snapshot.target );
    fprintf( file, "=== END LAUNCHER REPRO SNAPSHOT ===\n" );
    return LauncherReproSnapshotStatus::Wrote;
}

LauncherReproSessionSnapshot CaptureLauncherReproSession( const LauncherReproSceneCaptureView& scene )
{
    LauncherReproSessionSnapshot snapshot;
    snapshot.scenePath = "<generated>";

    if ( scene.sceneState.isSceneMode && scene.currentScenePath )
    {
        snapshot.scenePath = *scene.currentScenePath;
    }

    snapshot.currentSceneIndex = scene.sceneState.currentSceneIndex;
    snapshot.loadCount = scene.sceneState.loadCount;
    snapshot.manualResetCount = scene.sceneState.manualResetCount;
    snapshot.currentFrame = scene.sceneState.currentFrame;
    snapshot.targetFrameCount = scene.sceneState.targetFrameCount;
    snapshot.rngSeed = scene.sceneState.rngSeed;
    snapshot.timeScale = scene.sceneState.timeScale;
    snapshot.sceneMode = scene.sceneState.isSceneMode;
    snapshot.fixedStep = scene.sceneState.isFixedStep;
    return snapshot;
}

LauncherReproLaunchSnapshot CaptureLauncherReproLaunch( const LauncherReproLaunchView& launch )
{
    LauncherReproLaunchSnapshot snapshot;

    if ( launch.launchOptions.generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
    {
        snapshot.generatedObjectOverride = "all_balls";
        snapshot.generatedObjectArg = " --all-balls";
    }
    else if ( launch.launchOptions.generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
    {
        snapshot.generatedObjectOverride = "all_boxes";
        snapshot.generatedObjectArg = " --all-boxes";
    }

    snapshot.seedOverride = launch.launchOptions.seedOverride;
    snapshot.noWater = launch.launchOptions.noWater;
    snapshot.noSleep = launch.launchOptions.noSleep;
    snapshot.fixedStep = launch.launchOptions.fixedStep;
    snapshot.physicsSleepEnabled = launch.physicsSleepEnabled;
    return snapshot;
}

LauncherReproRuntimeSnapshot CaptureLauncherReproRuntime( const LauncherReproSceneCaptureView& scene,
                                                          const LauncherReproLaunchView& launch,
                                                          const LauncherReproPresentationView& presentation )
{
    LauncherReproRuntimeSnapshot snapshot;
    snapshot.rendererName = presentation.rendererName && presentation.rendererName[0] != '\0' ? presentation.rendererName
                                                                                              : "DirectX 12";
    snapshot.cameraPosition = scene.world.Cameras().GetCameraTranslation();
    snapshot.cameraView = scene.world.Cameras().GetCameraView();
    snapshot.cameraUp = scene.world.Cameras().GetCameraUp();
    snapshot.simulationSeconds = presentation.simulationSeconds;
    snapshot.gravity = scene.world.Environment().GetGravity();
    snapshot.fluidHeight = scene.world.Environment().GetFluidSurfaceHeight();
    snapshot.fluidDensity = scene.world.Environment().GetFluidDensity();
    snapshot.frictionCoeff = launch.frictionCoeff;
    snapshot.contactEpsilon = launch.contactEpsilon;
    snapshot.modelCount = scene.world.SceneEntityCount();
    snapshot.vsyncEnabled = presentation.vsyncEnabled;
    snapshot.pipelineSyncEnabled = presentation.pipelineSyncEnabled;
    snapshot.waterHidden = presentation.waterHidden;
    snapshot.terrainHidden = presentation.terrainHidden;
    snapshot.collisionVisualizer = presentation.collisionVisualizer;
    return snapshot;
}

void CaptureLauncherReproShape( const ColliderRecord& collider, LauncherReproTargetSnapshot& snapshot )
{
    snapshot.shapeKind = collider.shapeKind;
    snapshot.boundingRadius = LauncherReproRadius( collider );
    snapshot.shapeVolume = GetShapeVolume( collider.shape );
    snapshot.shapeArea = collider.projectedSurfaceArea;
    snapshot.shapeDrag = collider.dragCoefficient;

    if ( const BoundingSphere* sphere = GetShapeIf<BoundingSphere>( &collider.shape ) )
    {
        snapshot.sphereRadius = sphere->GetRadius();
    }
    else if ( const BoundingBox* box = GetShapeIf<BoundingBox>( &collider.shape ) )
    {
        snapshot.boxHalfExtents = box->GetHalfExtents();
    }
    else if ( const ConvexHullShape* hull = GetShapeIf<ConvexHullShape>( &collider.shape ) )
    {
        snapshot.hullName = hull->GetName();
        snapshot.hullVertexCount = static_cast<unsigned int>( hull->GetVertexCount() );
        snapshot.hullFaceCount = static_cast<unsigned int>( hull->GetFaceCount() );
        snapshot.hullEdgeCount = static_cast<unsigned int>( hull->GetEdgeCount() );
    }
}

void CaptureLauncherReproDiagnostics( const SceneWorld& world, int targetIndex, LauncherReproTargetSnapshot& snapshot )
{
    const PhysicsEngine& physics = world.Physics();
    const auto sleepStates = PhysicsEngine::ReadSleepStates( physics );
    const auto supportedStates = PhysicsEngine::ReadSleepSupportedStates( physics );
    const auto inhibitedStates = PhysicsEngine::ReadSleepInhibitedStates( physics );
    const auto collisionContacts = PhysicsEngine::ReadCollisionVisualContacts( physics );
    const auto islandIds = PhysicsEngine::ReadSleepIslandVisualIds( physics );

    if ( targetIndex < static_cast<int>( sleepStates.size() ) )
    {
        snapshot.sleeping = sleepStates[targetIndex] ? 1 : 0;
    }
    if ( targetIndex < static_cast<int>( supportedStates.size() ) )
    {
        snapshot.sleepSupported = supportedStates[targetIndex] ? 1 : 0;
    }
    if ( targetIndex < static_cast<int>( inhibitedStates.size() ) )
    {
        snapshot.sleepInhibited = inhibitedStates[targetIndex] ? 1 : 0;
    }
    if ( targetIndex < static_cast<int>( collisionContacts.size() ) )
    {
        snapshot.collisionVisualContact = collisionContacts[targetIndex] ? 1 : 0;
    }
    if ( targetIndex < static_cast<int>( islandIds.size() ) )
    {
        snapshot.sleepIslandVisualId = islandIds[targetIndex];
    }
}

void CaptureLauncherReproTerrain( SceneWorld& world, const ColliderRecord& collider, const PhysicsBodyHotState& hotState,
                                  float contactEpsilon, LauncherReproTargetSnapshot& snapshot )
{
    SkullbonezCore::Geometry::Terrain* terrain = world.Terrain().Get();
    if ( !terrain )
    {
        return;
    }

    if ( terrain->IsInBounds( hotState.position.x, hotState.position.z ) )
    {
        terrain->GetTerrainHeightAndNormalAt( hotState.position.x, hotState.position.z, snapshot.terrainHeight,
                                              snapshot.terrainNormal );
        snapshot.terrainAtCenter = true;
    }

    const BoundingBox* box = GetShapeIf<BoundingBox>( &collider.shape );
    if ( !box )
    {
        return;
    }

    Quaternion orientation = hotState.orientation;
    const RotationMatrix orientationMatrix = orientation.GetOrientationMatrix();
    const BoxTerrainVertexSupportProbe support = ProbeBoxTerrainVertices( nullptr, *box, hotState.position,
                                                                          orientationMatrix, terrain->PhysicsView(),
                                                                          contactEpsilon, false );
    if ( support.hasTerrainGaps )
    {
        snapshot.boxTerrainSupportedVertices = support.supportedVertices;
        snapshot.boxMinTerrainGap = support.minTerrainGap;
        snapshot.boxMaxTerrainGap = support.maxTerrainGap;
    }
}

bool CaptureLauncherReproTarget( SceneWorld& world, int targetIndex, float rayT, float crosshairDistance,
                                 float contactEpsilon, LauncherReproTargetSnapshot& snapshot )
{
    const ColliderStore& colliderStore = world.Colliders();
    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const ColliderRecord* collider = LauncherReproColliderForModelIndex( bodyStore, colliderStore, targetIndex );
    if ( !collider )
    {
        return false;
    }

    const PhysicsBodyRecord* body = LauncherReproBodyForCollider( bodyStore, *collider );
    if ( !body )
    {
        return false;
    }

    const PhysicsBodyHotState hotState = LoadPhysicsBodyHotState( bodyStore.HotFields(),
                                                                  static_cast<std::size_t>( targetIndex ) );
    const char* name = world.Entities().At( targetIndex ).displayName;
    snapshot.name = name && name[0] != '\0' ? name : "<unnamed>";
    snapshot.targetIndex = targetIndex;
    snapshot.rayT = rayT;
    snapshot.crosshairDistance = crosshairDistance;
    snapshot.position = hotState.position;
    snapshot.velocity = hotState.linearVelocity;
    snapshot.angularVelocity = hotState.angularVelocity;
    snapshot.rotationalInertia = body->rotationalInertia;
    snapshot.inverseRotationalInertia = hotState.inverseRotationalInertia;
    snapshot.mass = body->mass;
    snapshot.restitution = collider->restitution;
    Quaternion orientation = hotState.orientation;
    orientation.GetComponents( snapshot.orientationX, snapshot.orientationY, snapshot.orientationZ, snapshot.orientationW );
    CaptureLauncherReproShape( *collider, snapshot );
    CaptureLauncherReproDiagnostics( world, targetIndex, snapshot );
    CaptureLauncherReproTerrain( world, *collider, hotState, contactEpsilon, snapshot );
    return true;
}
} // namespace


bool RuntimeTools::PickLauncherReproTarget( const SceneWorld& world, int& outIndex, float& outRayT,
                                            float& outCrosshairDistance ) const
{
    outIndex = -1;
    outRayT = 0.0f;
    outCrosshairDistance = 0.0f;

    const CameraCollection& cameras = world.Cameras();
    const Vector3& camPos = cameras.GetCameraTranslation();
    Vector3 rayDir = cameras.GetCameraView() - camPos;
    float rayMagSq = VectorMagSquared( rayDir );

    if ( rayMagSq < TOLERANCE )
    {
        return false;
    }

    rayDir = rayDir * ( 1.0f / sqrtf( rayMagSq ) );

    float bestT = FLT_MAX;
    float bestCrosshairDist = 0.0f;
    int bestIndex = -1;

    // Concept: Repro target picking approximates each model as a bounding
    // sphere around its current physics body position, then chooses the nearest
    // sphere pierced by the camera ray. The body record remains only the cold
    // identity table for the eventual snapshot row.
    const ColliderStore& colliderStore = world.Colliders();
    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const auto colliders = colliderStore.Records();
    const auto hotFields = bodyStore.HotFields();

    for ( const ColliderRecord& collider : colliders )
    {
        const PhysicsBodyRecord* body = LauncherReproBodyForCollider( bodyStore, collider );

        if ( !body )
        {
            continue;
        }

        const int modelIndex = colliderStore.ModelIndexForHandle( collider.handle );

        if ( modelIndex < 0 )
        {
            continue;
        }

        Vector3 toModel = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( modelIndex ) ) - camPos;
        float rayT = Dot( toModel, rayDir );

        if ( rayT <= 0.0f )
        {
            continue;
        }

        float distSq = VectorMagSquared( toModel );
        float crosshairDistSq = distSq - rayT * rayT;

        if ( crosshairDistSq < 0.0f )
        {
            crosshairDistSq = 0.0f;
        }

        float radius = LauncherReproRadius( collider );

        if ( crosshairDistSq > radius * radius )
        {
            continue;
        }

        float hitOffset = sqrtf( radius * radius - crosshairDistSq );
        float hitT = rayT - hitOffset;

        if ( hitT < 0.0f )
        {
            hitT = rayT;
        }

        if ( hitT < bestT )
        {
            bestT = hitT;
            bestCrosshairDist = sqrtf( crosshairDistSq );
            bestIndex = modelIndex;
        }
    }

    if ( bestIndex < 0 )
    {
        return false;
    }

    outIndex = bestIndex;
    outRayT = bestT;
    outCrosshairDistance = bestCrosshairDist;
    return true;
}


LauncherReproSnapshotStatus RuntimeTools::WriteLauncherReproSnapshot( const LauncherReproSnapshotRequest& request ) const
{
    int targetIndex = -1;
    float rayT = 0.0f;
    float crosshairDistance = 0.0f;

    if ( !PickLauncherReproTarget( request.scene.world, targetIndex, rayT, crosshairDistance ) )
    {
        return LauncherReproSnapshotStatus::NoTarget;
    }

    LauncherReproSnapshot snapshot;
    snapshot.session = CaptureLauncherReproSession( request.scene );
    snapshot.launch = CaptureLauncherReproLaunch( request.launch );
    snapshot.runtime = CaptureLauncherReproRuntime( request.scene, request.launch, request.presentation );
    if ( !CaptureLauncherReproTarget( request.scene.world, targetIndex, rayT, crosshairDistance,
                                      request.launch.contactEpsilon, snapshot.target ) )
    {
        return LauncherReproSnapshotStatus::NoTarget;
    }

    return WriteLauncherReproSnapshotFile( snapshot );
}


LauncherReproSnapshotResult
RuntimeTools::WriteLauncherReproSnapshotWithStatusMessage( const LauncherReproSnapshotRequest& request ) const
{
    LauncherReproSnapshotResult result;
    result.status = WriteLauncherReproSnapshot( request );
    const char* snapshotMessage = "Failed to write repro snapshot";

    if ( result.status == LauncherReproSnapshotStatus::Wrote )
    {
        sprintf_s( result.message.data(), result.message.size(), "Repro snapshot: %s", LAUNCHER_REPRO_SNAPSHOT_PATH );
    }
    else if ( result.status == LauncherReproSnapshotStatus::NoTarget )
    {
        snapshotMessage = "No repro target under crosshair";
    }

    if ( result.status != LauncherReproSnapshotStatus::Wrote )
    {
        sprintf_s( result.message.data(), result.message.size(), "%s", snapshotMessage );
    }

    result.messageUntil = request.presentation.simulationSeconds + LAUNCHER_REPRO_MESSAGE_SECONDS;
    return result;
}
#endif
