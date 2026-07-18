/*
File: SkullbonezSource/Runtime/Editor/LauncherTools.cpp
Purpose:
  Owns launcher-mode raycast, projectile, laser, and repro snapshot behavior.

Summary:
  LauncherTools.cpp owns launcher-mode raycast, projectile, laser, and repro
  snapshot behavior. As an implementation unit, keep edits anchored on local
  owner boundaries and call direction and on the glossary/invariants below.

Glossary:
  Body store: Physics-owned live body records used for pose and velocity
    authority while legacy object-record mirrors are retired.
  Collider store: Physics-owned shape, material, and radius records paired with
    body handles.
  Repro snapshot: Debug-only text dump of the object under the launcher
    crosshair, including enough scene and physics state to recreate the issue.

Invariants:
  - Launcher repro output is a debugging interface; key names and numeric
    precision should stay stable unless every downstream consumer is updated.
  - Target picking is read-only and must not perturb physics, selection, or
    launcher shot history.

Related:
  - SkullbonezSource/Runtime/Editor/LauncherLaser.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../Tools/RuntimeTools.h"
#include "../CameraCollection.h"
#include "../RunDebugState.h"
#include "../RunLaunchOptions.h"
#include "../Scene/SceneGeneratedSetup.h"
#include "../Scene/SceneRuntime.h"
#include "../Scene/SceneController.h"
#include "../../World/WorldEnvironment.h"
#include "../Scene/SceneEntityStore.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../World/TerrainSupportClassifier.h"

#include <cfloat>
#include <memory>
#include <time.h>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Runtime::SceneController;

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
                                                          const ColliderStore& colliderStore,
                                                          int modelIndex )
{
    const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( bodyHandle );
    return colliderStore.RecordForHandle( colliderHandle );
}


const PhysicsBodyRecord* LauncherReproBodyForCollider( const PhysicsBodyStore& bodyStore,
                                                       const ColliderRecord& collider )
{
    return bodyStore.RecordForHandle( collider.body );
}
} // namespace


bool RuntimeTools::PickLauncherReproTarget( SceneController& collection,
                                            SkullbonezCore::Environment::CameraCollection* cameras,
                                            int& outIndex,
                                            float& outRayT,
                                            float& outCrosshairDistance ) const
{
    outIndex = -1;
    outRayT = 0.0f;
    outCrosshairDistance = 0.0f;

    if ( !cameras )
    {
        return false;
    }

    const Vector3& camPos = cameras->GetCameraTranslation();
    Vector3 rayDir = cameras->GetCameraView() - camPos;
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
    const ColliderStore& colliderStore = collection.Scene().Colliders();
    const PhysicsBodyStore& bodyStore = collection.Scene().BodyStore();
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
        float rayT = toModel * rayDir;
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


LauncherReproSnapshotStatus
RuntimeTools::WriteLauncherReproSnapshot( const LauncherReproSnapshotContext& context ) const
{
    int targetIndex = -1;
    float rayT = 0.0f;
    float crosshairDistance = 0.0f;
    if ( !PickLauncherReproTarget( context.collection, context.cameras, targetIndex, rayT, crosshairDistance ) )
    {
        return LauncherReproSnapshotStatus::NoTarget;
    }

    const ColliderStore& colliderStore = context.collection.Scene().Colliders();
    const PhysicsBodyStore& bodyStore = context.collection.Scene().BodyStore();
    const ColliderRecord* collider = LauncherReproColliderForModelIndex( bodyStore, colliderStore, targetIndex );
    if ( !collider )
    {
        return LauncherReproSnapshotStatus::NoTarget;
    }
    const PhysicsBodyRecord* body = LauncherReproBodyForCollider( bodyStore, *collider );
    if ( !body )
    {
        return LauncherReproSnapshotStatus::NoTarget;
    }
    const PhysicsBodyHotState hotState =
        LoadPhysicsBodyHotState( bodyStore.HotFields(), static_cast<std::size_t>( targetIndex ) );

    CreateDirectoryA( "Debug", nullptr );
    FILE* rawFile = nullptr;
    if ( fopen_s( &rawFile, LAUNCHER_REPRO_SNAPSHOT_PATH, "a" ) != 0 || !rawFile )
    {
        return LauncherReproSnapshotStatus::WriteFailed;
    }
    std::unique_ptr<FILE, decltype( &fclose )> file( rawFile, fclose );
    FILE* f = file.get();

    const Vector3& pos = hotState.position;
    const Vector3& vel = hotState.linearVelocity;
    const Vector3& omega = hotState.angularVelocity;
    const Vector3& inertia = body->rotationalInertia;
    const Vector3& invInertia = hotState.inverseRotationalInertia;
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
    Quaternion orientation = hotState.orientation;
    orientation.GetComponents( qx, qy, qz, qw );

    const CollisionShape& shape = collider->shape;
    bool isSphere = std::holds_alternative<BoundingSphere>( shape );
    bool isBox = std::holds_alternative<BoundingBox>( shape );
    const char* shapeName = LauncherReproShapeName( collider->shapeKind );
    float boundingRadius = LauncherReproRadius( *collider );
    float shapeVolume = GetShapeVolume( shape );
    float shapeArea = collider->projectedSurfaceArea;
    float shapeDrag = collider->dragCoefficient;
    float mass = body->mass;
    float restitution = collider->restitution;
    const char* name = context.entities.At( targetIndex ).displayName;
    if ( !name || name[0] == '\0' )
    {
        name = "<unnamed>";
    }

    const char* scenePath = "<generated>";
    if ( context.sceneState.isSceneMode )
    {
        if ( context.currentScenePath )
        {
            scenePath = context.currentScenePath->c_str();
        }
    }

    const char* rendererName =
        context.rendererName && context.rendererName[0] != '\0' ? context.rendererName : "DirectX 12";
    const char* rendererArg = "dx12";
    const char* generatedObjectOverride = "mixed";
    const char* generatedObjectArg = "";
    if ( context.launchOptions.generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
    {
        generatedObjectOverride = "all_balls";
        generatedObjectArg = " --all-balls";
    }
    else if ( context.launchOptions.generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
    {
        generatedObjectOverride = "all_boxes";
        generatedObjectArg = " --all-boxes";
    }
    const Vector3& camPos = context.cameras->GetCameraTranslation();
    const Vector3& camView = context.cameras->GetCameraView();
    const Vector3& camUp = context.cameras->GetCameraUp();

    int sleeping = 0;
    int sleepSupported = 0;
    int sleepInhibited = 0;
    int collisionVisualContact = 0;
    int sleepIslandVisualId = 0;
    const Physics::PhysicsEngine& physics = context.collection.Scene().Physics();
    const auto sleepStates = PhysicsEngine::ReadSleepStates( physics );
    if ( targetIndex < static_cast<int>( sleepStates.size() ) )
    {
        sleeping = sleepStates[targetIndex] ? 1 : 0;
    }
    const auto sleepSupportedStates = PhysicsEngine::ReadSleepSupportedStates( physics );
    if ( targetIndex < static_cast<int>( sleepSupportedStates.size() ) )
    {
        sleepSupported = sleepSupportedStates[targetIndex] ? 1 : 0;
    }
    const auto sleepInhibitedStates = PhysicsEngine::ReadSleepInhibitedStates( physics );
    if ( targetIndex < static_cast<int>( sleepInhibitedStates.size() ) )
    {
        sleepInhibited = sleepInhibitedStates[targetIndex] ? 1 : 0;
    }
    const std::vector<uint8_t>& collisionContacts = PhysicsEngine::ReadCollisionVisualContacts( physics );
    if ( targetIndex < static_cast<int>( collisionContacts.size() ) )
    {
        collisionVisualContact = collisionContacts[targetIndex] ? 1 : 0;
    }
    const auto islandIds = PhysicsEngine::ReadSleepIslandVisualIds( physics );
    if ( targetIndex < static_cast<int>( islandIds.size() ) )
    {
        sleepIslandVisualId = islandIds[targetIndex];
    }

    bool terrainAtCenter = false;
    float terrainHeight = 0.0f;
    Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );
    if ( context.terrain && context.terrain->IsInBounds( pos.x, pos.z ) )
    {
        context.terrain->GetTerrainHeightAndNormalAt( pos.x, pos.z, terrainHeight, terrainNormal );
        terrainAtCenter = true;
    }

    int boxTerrainSupportedVertices = -1;
    float boxMinTerrainGap = 0.0f;
    float boxMaxTerrainGap = 0.0f;
    if ( std::holds_alternative<BoundingBox>( shape ) && context.terrain )
    {
        const BoundingBox& box = std::get<BoundingBox>( shape );
        Quaternion qCopy = hotState.orientation;
        RotationMatrix orientMat = qCopy.GetOrientationMatrix();
        const BoxTerrainVertexSupportProbe supportProbe =
            ProbeBoxTerrainVertices( nullptr, box, pos, orientMat, *context.terrain, context.contactEpsilon, false );

        if ( supportProbe.hasTerrainGaps )
        {
            boxTerrainSupportedVertices = supportProbe.supportedVertices;
            boxMinTerrainGap = supportProbe.minTerrainGap;
            boxMaxTerrainGap = supportProbe.maxTerrainGap;
        }
    }

    time_t now = time( nullptr );
    // Invariant: Snapshot keys are intentionally simple CSV-like rows. Keep
    // names, order, and precision stable so copied repro blocks remain useful
    // across debugging sessions and script revisions.
    fprintf( f, "\n=== LAUNCHER REPRO SNAPSHOT ===\n" );
    fprintf( f, "timestamp_epoch,%lld\n", static_cast<long long>( now ) );
    fprintf( f, "snapshot_file,%s\n", LAUNCHER_REPRO_SNAPSHOT_PATH );
    fprintf( f, "scene,%s\n", scenePath );
    fprintf( f, "scene_mode,%d\n", context.sceneState.isSceneMode ? 1 : 0 );
    fprintf( f, "scene_index,%d\n", context.sceneState.currentSceneIndex );
    fprintf( f, "scene_load_count,%d\n", context.sceneState.loadCount );
    fprintf( f, "manual_reset_count,%d\n", context.sceneState.manualResetCount );
    fprintf( f, "scene_frame,%d\n", context.sceneState.currentFrame );
    fprintf( f, "target_frame_count,%d\n", context.sceneState.targetFrameCount );
    fprintf( f, "simulation_seconds,%.6f\n", context.simulationSeconds );
    fprintf( f, "rng_seed,%u\n", context.sceneState.rngSeed );
    fprintf( f, "cmd_seed_override,%u\n", context.launchOptions.seedOverride );
    fprintf( f, "cmd_no_water,%d\n", context.launchOptions.noWater ? 1 : 0 );
    fprintf( f, "cmd_no_sleep,%d\n", context.launchOptions.noSleep ? 1 : 0 );
    fprintf( f, "physics_sleep_enabled,%d\n", context.physicsSleepEnabled ? 1 : 0 );
    fprintf( f, "fixed_step_effective,%d\n", context.sceneState.isFixedStep ? 1 : 0 );
    fprintf( f, "cmd_fixed_step_override,%d\n", context.launchOptions.fixedStep ? 1 : 0 );
    fprintf( f, "time_scale,%.6f\n", context.sceneState.timeScale );
    fprintf( f, "renderer,%s\n", rendererName );
    fprintf( f, "generated_object_override,%s\n", generatedObjectOverride );
    fprintf( f, "model_count,%d\n", context.collection.Scene().SceneEntityCount() );
    fprintf( f, "vsync_enabled,%d\n", context.vsyncEnabled ? 1 : 0 );
    fprintf( f, "pipeline_sync_enabled,%d\n", context.pipelineSyncEnabled ? 1 : 0 );
    if ( context.sceneState.isSceneMode )
    {
        fprintf( f,
                 "repro_command_hint,Debug\\SKULLBONEZ_CORE.exe --renderer %s --scene \"%s\" --seed %u --time-scale "
                 "%.6f%s%s%s%s\n",
                 rendererArg,
                 scenePath,
                 context.sceneState.rngSeed,
                 context.sceneState.timeScale,
                 context.sceneState.isFixedStep ? " --fixed-step" : "",
                 context.launchOptions.noWater ? " --no-water" : "",
                 context.physicsSleepEnabled ? "" : " --no-sleep",
                 generatedObjectArg );
    }
    else
    {
        fprintf( f,
                 "repro_command_hint,Debug\\SKULLBONEZ_CORE.exe --renderer %s --seed %u --time-scale %.6f%s%s%s%s\n",
                 rendererArg,
                 context.sceneState.rngSeed,
                 context.sceneState.timeScale,
                 context.sceneState.isFixedStep ? " --fixed-step" : "",
                 context.launchOptions.noWater ? " --no-water" : "",
                 context.physicsSleepEnabled ? "" : " --no-sleep",
                 generatedObjectArg );
    }
    fprintf( f, "water_hidden,%d\n", context.debug.isWaterHidden ? 1 : 0 );
    fprintf( f, "terrain_hidden,%d\n", context.debug.isTerrainHidden ? 1 : 0 );
    fprintf( f, "collision_visualizer,%d\n", context.debug.isCollisionVisualizer ? 1 : 0 );
    fprintf( f, "world_gravity,%.6f\n", context.world.GetGravity() );
    fprintf( f, "world_fluid_height,%.6f\n", context.world.GetFluidSurfaceHeight() );
    fprintf( f, "world_fluid_density,%.6f\n", context.world.GetFluidDensity() );
    fprintf( f, "cfg_friction_coeff,%.6f\n", context.frictionCoeff );
    fprintf( f, "cfg_contact_epsilon,%.6f\n", context.contactEpsilon );
    fprintf( f, "camera_eye,%.6f,%.6f,%.6f\n", camPos.x, camPos.y, camPos.z );
    fprintf( f, "camera_view,%.6f,%.6f,%.6f\n", camView.x, camView.y, camView.z );
    fprintf( f, "camera_up,%.6f,%.6f,%.6f\n", camUp.x, camUp.y, camUp.z );
    fprintf( f, "pick_index,%d\n", targetIndex );
    fprintf( f, "pick_name,%s\n", name );
    fprintf( f, "pick_shape,%s\n", shapeName );
    fprintf( f, "pick_ray_t,%.6f\n", rayT );
    fprintf( f, "pick_crosshair_distance,%.6f\n", crosshairDistance );
    fprintf( f, "position,%.6f,%.6f,%.6f\n", pos.x, pos.y, pos.z );
    fprintf( f, "velocity,%.6f,%.6f,%.6f\n", vel.x, vel.y, vel.z );
    fprintf( f, "angular_velocity,%.6f,%.6f,%.6f\n", omega.x, omega.y, omega.z );
    fprintf( f, "speed,%.6f\n", sqrtf( VectorMagSquared( vel ) ) );
    fprintf( f, "omega_mag,%.6f\n", sqrtf( VectorMagSquared( omega ) ) );
    fprintf( f, "orientation_q,%.8f,%.8f,%.8f,%.8f\n", qx, qy, qz, qw );
    fprintf( f, "mass,%.6f\n", mass );
    fprintf( f, "restitution,%.6f\n", restitution );
    fprintf( f, "rotational_inertia,%.6f,%.6f,%.6f\n", inertia.x, inertia.y, inertia.z );
    fprintf( f, "inverse_rotational_inertia,%.6f,%.6f,%.6f\n", invInertia.x, invInertia.y, invInertia.z );
    fprintf( f, "shape_bounding_radius,%.6f\n", boundingRadius );
    fprintf( f, "shape_volume,%.6f\n", shapeVolume );
    fprintf( f, "shape_projected_area,%.6f\n", shapeArea );
    fprintf( f, "shape_drag_coefficient,%.6f\n", shapeDrag );
    if ( isSphere )
    {
        const BoundingSphere& sphere = std::get<BoundingSphere>( shape );
        fprintf( f, "sphere_radius,%.6f\n", sphere.GetRadius() );
    }
    else if ( isBox )
    {
        const BoundingBox& box = std::get<BoundingBox>( shape );
        const Vector3& he = box.GetHalfExtents();
        fprintf( f, "box_half_extents,%.6f,%.6f,%.6f\n", he.x, he.y, he.z );
        fprintf( f, "box_terrain_supported_vertices,%d\n", boxTerrainSupportedVertices );
        fprintf( f, "box_min_terrain_gap,%.6f\n", boxMinTerrainGap );
        fprintf( f, "box_max_terrain_gap,%.6f\n", boxMaxTerrainGap );
    }
    else
    {
        const ConvexHullShape& hull = std::get<ConvexHullShape>( shape );
        fprintf( f, "hull_name,%s\n", hull.GetName() );
        fprintf( f, "hull_vertices,%u\n", static_cast<unsigned>( hull.GetVertexCount() ) );
        fprintf( f, "hull_faces,%u\n", static_cast<unsigned>( hull.GetFaceCount() ) );
        fprintf( f, "hull_edges,%u\n", static_cast<unsigned>( hull.GetEdgeCount() ) );
    }
    fprintf( f, "sleeping,%d\n", sleeping );
    fprintf( f, "sleep_supported_this_frame,%d\n", sleepSupported );
    fprintf( f, "sleep_inhibited_this_frame,%d\n", sleepInhibited );
    fprintf( f, "sleep_island_visual_id,%d\n", sleepIslandVisualId );
    fprintf( f, "collision_visual_contact_this_frame,%d\n", collisionVisualContact );
    fprintf( f, "terrain_at_center,%d\n", terrainAtCenter ? 1 : 0 );
    fprintf( f, "terrain_height_at_center,%.6f\n", terrainHeight );
    fprintf( f, "terrain_normal_at_center,%.6f,%.6f,%.6f\n", terrainNormal.x, terrainNormal.y, terrainNormal.z );
    fprintf( f,
             "scene_object_line_hint,%s %s %.6f %.6f %.6f",
             isSphere ? "ball_state/manual" : ( isBox ? "box/manual" : "convex_hull/manual" ),
             name,
             pos.x,
             pos.y,
             pos.z );
    if ( isSphere )
    {
        const BoundingSphere& sphere = std::get<BoundingSphere>( shape );
        fprintf( f, " radius=%.6f mass=%.6f restitution=%.6f", sphere.GetRadius(), mass, restitution );
    }
    else if ( isBox )
    {
        const BoundingBox& box = std::get<BoundingBox>( shape );
        const Vector3& he = box.GetHalfExtents();
        fprintf( f, " halfExtents=%.6f,%.6f,%.6f mass=%.6f restitution=%.6f", he.x, he.y, he.z, mass, restitution );
    }
    else
    {
        const ConvexHullShape& hull = std::get<ConvexHullShape>( shape );
        fprintf( f,
                 " hull=%s vertices=%u faces=%u edges=%u mass=%.6f restitution=%.6f",
                 hull.GetName(),
                 static_cast<unsigned>( hull.GetVertexCount() ),
                 static_cast<unsigned>( hull.GetFaceCount() ),
                 static_cast<unsigned>( hull.GetEdgeCount() ),
                 mass,
                 restitution );
    }
    fprintf( f, "\n" );
    fprintf( f, "=== END LAUNCHER REPRO SNAPSHOT ===\n" );

    return LauncherReproSnapshotStatus::Wrote;
}


LauncherReproSnapshotStatus
RuntimeTools::WriteLauncherReproSnapshotWithStatusMessage( const LauncherReproSnapshotContext& context,
                                                           RunDebugState& debug ) const
{
    // Why: the debug Enter shortcut should ask the launcher owner for both the
    // cold snapshot artifact and the operator-facing status text, leaving Run to
    // decide only whether the shortcut is currently allowed.
    const LauncherReproSnapshotStatus snapshotStatus = WriteLauncherReproSnapshot( context );
    const char* snapshotMessage = "Failed to write repro snapshot";
    if ( snapshotStatus == LauncherReproSnapshotStatus::Wrote )
    {
        sprintf_s( debug.reproSnapshotMessage,
                   sizeof( debug.reproSnapshotMessage ),
                   "Repro snapshot: %s",
                   LAUNCHER_REPRO_SNAPSHOT_PATH );
    }
    else if ( snapshotStatus == LauncherReproSnapshotStatus::NoTarget )
    {
        snapshotMessage = "No repro target under crosshair";
    }

    if ( snapshotStatus != LauncherReproSnapshotStatus::Wrote )
    {
        sprintf_s( debug.reproSnapshotMessage, sizeof( debug.reproSnapshotMessage ), "%s", snapshotMessage );
    }

    debug.reproSnapshotMessageUntil = context.simulationSeconds + LAUNCHER_REPRO_MESSAGE_SECONDS;
    return snapshotStatus;
}
#endif
