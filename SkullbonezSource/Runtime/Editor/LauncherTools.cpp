/*
File: SkullbonezSource/Runtime/Editor/LauncherTools.cpp
Purpose:
  Owns launcher-mode raycast, projectile, laser, and repro snapshot behavior.

Mental model:
  Input decides when launcher actions fire. This file turns those actions into
  world queries, transient visuals, physics impulses, and debug repro output.

Glossary:
  Repro snapshot: Debug-only text dump of the object under the launcher
  crosshair, including enough scene and physics state to recreate the issue.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/Runtime/Editor/LauncherLaser.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../RunInternal.h"

#include <cfloat>
#include <memory>
#include <time.h>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

#ifdef _DEBUG
bool Run::PickLauncherReproTarget( int& outIndex, float& outRayT, float& outCrosshairDistance )
{
    outIndex = -1;
    outRayT = 0.0f;
    outCrosshairDistance = 0.0f;

    const Vector3& camPos = m_systems.cameras->GetCameraTranslation();
    Vector3 rayDir = m_systems.cameras->GetCameraView() - camPos;
    float rayMagSq = VectorMagSquared( rayDir );
    if ( rayMagSq < TOLERANCE )
    {
        return false;
    }
    rayDir = rayDir * ( 1.0f / sqrtf( rayMagSq ) );

    float bestT = FLT_MAX;
    float bestCrosshairDist = 0.0f;
    int bestIndex = -1;

    int count = m_cGameModelCollection.GetModelCount();
    for ( int i = 0; i < count; ++i )
    {
        GameModel& model = m_cGameModelCollection.GetModelAtIndex( i );
        Vector3 toModel = model.GetPosition() - camPos;
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

        float radius = GetShapeBoundingRadius( model.GetCollisionShape() );
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
            bestIndex = i;
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


void Run::WriteLauncherReproSnapshot()
{
    int targetIndex = -1;
    float rayT = 0.0f;
    float crosshairDistance = 0.0f;
    if ( !PickLauncherReproTarget( targetIndex, rayT, crosshairDistance ) )
    {
        sprintf_s( m_debug.reproSnapshotMessage,
                   sizeof( m_debug.reproSnapshotMessage ),
                   "No repro target under crosshair" );
        m_debug.reproSnapshotMessageUntil =
            m_timers.simulationTimer.GetTimeSinceLastStart() + LAUNCHER_REPRO_MESSAGE_SECONDS;
        return;
    }

    CreateDirectoryA( "Debug", nullptr );
    FILE* rawFile = nullptr;
    if ( fopen_s( &rawFile, LAUNCHER_REPRO_SNAPSHOT_PATH, "a" ) != 0 || !rawFile )
    {
        sprintf_s( m_debug.reproSnapshotMessage,
                   sizeof( m_debug.reproSnapshotMessage ),
                   "Failed to write repro snapshot" );
        m_debug.reproSnapshotMessageUntil =
            m_timers.simulationTimer.GetTimeSinceLastStart() + LAUNCHER_REPRO_MESSAGE_SECONDS;
        return;
    }
    std::unique_ptr<FILE, decltype( &fclose )> file( rawFile, fclose );
    FILE* f = file.get();

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( targetIndex );
    const Vector3& pos = model.GetPosition();
    const Vector3& vel = model.GetVelocity();
    const Vector3& omega = model.GetAngularVelocity();
    const Vector3& inertia = model.GetRotationalInertia();
    const Vector3& invInertia = model.GetInvertedRotationalInertia();
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
    model.GetOrientation().GetComponents( qx, qy, qz, qw );

    const CollisionShape& shape = model.GetCollisionShape();
    bool isSphere = std::holds_alternative<BoundingSphere>( shape );
    bool isBox = std::holds_alternative<BoundingBox>( shape );
    const char* shapeName = model.GetShapeName();
    float boundingRadius = GetShapeBoundingRadius( shape );
    float shapeVolume = GetShapeVolume( shape );
    float shapeArea = GetShapeProjectedSurfaceArea( shape );
    float shapeDrag = GetShapeDragCoefficient( shape );
    const char* name = model.GetName();
    if ( !name || name[0] == '\0' )
    {
        name = "<unnamed>";
    }

    const char* scenePath = "<generated>";
    if ( SceneState().isSceneMode )
    {
        const std::string* currentScenePath = CurrentSceneQueuePath();
        if ( currentScenePath )
        {
            scenePath = currentScenePath->c_str();
        }
    }

    const char* rendererName = IsGfxReady() ? Gfx().GetRendererName() : "DirectX 12";
    const char* rendererArg = "dx12";
    const char* generatedObjectOverride = "mixed";
    const char* generatedObjectArg = "";
    if ( m_launchOptions.generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
    {
        generatedObjectOverride = "all_balls";
        generatedObjectArg = " --all-balls";
    }
    else if ( m_launchOptions.generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
    {
        generatedObjectOverride = "all_boxes";
        generatedObjectArg = " --all-boxes";
    }
    const Vector3& camPos = m_systems.cameras->GetCameraTranslation();
    const Vector3& camView = m_systems.cameras->GetCameraView();
    const Vector3& camUp = m_systems.cameras->GetCameraUp();

    int sleeping = 0;
    int sleepSupported = 0;
    int sleepInhibited = 0;
    int collisionVisualContact = 0;
    int sleepIslandVisualId = 0;
    const std::vector<uint8_t>& sleepStates = m_cGameModelCollection.GetSleepStates();
    if ( targetIndex < static_cast<int>( sleepStates.size() ) )
    {
        sleeping = sleepStates[targetIndex] ? 1 : 0;
    }
    const std::vector<uint8_t>& sleepSupportedStates = m_cGameModelCollection.GetSleepSupportedStates();
    if ( targetIndex < static_cast<int>( sleepSupportedStates.size() ) )
    {
        sleepSupported = sleepSupportedStates[targetIndex] ? 1 : 0;
    }
    const std::vector<uint8_t>& sleepInhibitedStates = m_cGameModelCollection.GetSleepInhibitedStates();
    if ( targetIndex < static_cast<int>( sleepInhibitedStates.size() ) )
    {
        sleepInhibited = sleepInhibitedStates[targetIndex] ? 1 : 0;
    }
    const std::vector<uint8_t>& collisionContacts = m_cGameModelCollection.GetCollisionVisualContacts();
    if ( targetIndex < static_cast<int>( collisionContacts.size() ) )
    {
        collisionVisualContact = collisionContacts[targetIndex] ? 1 : 0;
    }
    const std::vector<int>& islandIds = m_cGameModelCollection.GetSleepIslandVisualIds();
    if ( targetIndex < static_cast<int>( islandIds.size() ) )
    {
        sleepIslandVisualId = islandIds[targetIndex];
    }

    bool terrainAtCenter = false;
    float terrainHeight = 0.0f;
    Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );
    if ( m_systems.terrain && m_systems.terrain->IsInBounds( pos.x, pos.z ) )
    {
        m_systems.terrain->GetTerrainHeightAndNormalAt( pos.x, pos.z, terrainHeight, terrainNormal );
        terrainAtCenter = true;
    }

    int boxTerrainSupportedVertices = -1;
    float boxMinTerrainGap = 0.0f;
    float boxMaxTerrainGap = 0.0f;
    if ( std::holds_alternative<BoundingBox>( shape ) && m_systems.terrain )
    {
        const BoundingBox& box = std::get<BoundingBox>( shape );
        Quaternion qCopy = model.GetOrientation();
        RotationMatrix orientMat = qCopy.GetOrientationMatrix();
        const BoxTerrainVertexSupportProbe supportProbe =
            ProbeBoxTerrainVertices( box, pos, orientMat, *m_systems.terrain, Cfg().contactEpsilon, false );

        if ( supportProbe.hasTerrainGaps )
        {
            boxTerrainSupportedVertices = supportProbe.supportedVertices;
            boxMinTerrainGap = supportProbe.minTerrainGap;
            boxMaxTerrainGap = supportProbe.maxTerrainGap;
        }
    }

    time_t now = time( nullptr );
    fprintf( f, "\n=== LAUNCHER REPRO SNAPSHOT ===\n" );
    fprintf( f, "timestamp_epoch,%lld\n", static_cast<long long>( now ) );
    fprintf( f, "snapshot_file,%s\n", LAUNCHER_REPRO_SNAPSHOT_PATH );
    fprintf( f, "scene,%s\n", scenePath );
    fprintf( f, "scene_mode,%d\n", SceneState().isSceneMode ? 1 : 0 );
    fprintf( f, "scene_index,%d\n", SceneState().currentSceneIndex );
    fprintf( f, "scene_load_count,%d\n", SceneState().loadCount );
    fprintf( f, "manual_reset_count,%d\n", SceneState().manualResetCount );
    fprintf( f, "scene_frame,%d\n", SceneState().currentFrame );
    fprintf( f, "target_frame_count,%d\n", SceneState().targetFrameCount );
    fprintf( f, "simulation_seconds,%.6f\n", m_timers.simulationTimer.GetTimeSinceLastStart() );
    fprintf( f, "rng_seed,%u\n", SceneState().rngSeed );
    fprintf( f, "cmd_seed_override,%u\n", m_launchOptions.seedOverride );
    fprintf( f, "cmd_no_water,%d\n", m_launchOptions.noWater ? 1 : 0 );
    fprintf( f, "cmd_no_sleep,%d\n", m_launchOptions.noSleep ? 1 : 0 );
    fprintf( f, "physics_sleep_enabled,%d\n", m_runtimeSettings.isPhysicsSleepEnabled ? 1 : 0 );
    fprintf( f, "fixed_step_effective,%d\n", SceneState().isFixedStep ? 1 : 0 );
    fprintf( f, "cmd_fixed_step_override,%d\n", m_launchOptions.fixedStep ? 1 : 0 );
    fprintf( f, "time_scale,%.6f\n", SceneState().timeScale );
    fprintf( f, "renderer,%s\n", rendererName );
    fprintf( f, "generated_object_override,%s\n", generatedObjectOverride );
    fprintf( f, "model_count,%d\n", m_cGameModelCollection.GetModelCount() );
    fprintf( f, "vsync_enabled,%d\n", m_runtimeSettings.isVsyncEnabled ? 1 : 0 );
    fprintf( f, "pipeline_sync_enabled,%d\n", m_runtimeSettings.isPipelineSyncEnabled ? 1 : 0 );
    if ( SceneState().isSceneMode )
    {
        fprintf( f,
                 "repro_command_hint,Debug\\SKULLBONEZ_CORE.exe --renderer %s --scene \"%s\" --seed %u --time-scale "
                 "%.6f%s%s%s%s\n",
                 rendererArg,
                 scenePath,
                 SceneState().rngSeed,
                 SceneState().timeScale,
                 SceneState().isFixedStep ? " --fixed-step" : "",
                 m_launchOptions.noWater ? " --no-water" : "",
                 m_runtimeSettings.isPhysicsSleepEnabled ? "" : " --no-sleep",
                 generatedObjectArg );
    }
    else
    {
        fprintf( f,
                 "repro_command_hint,Debug\\SKULLBONEZ_CORE.exe --renderer %s --seed %u --time-scale %.6f%s%s%s%s\n",
                 rendererArg,
                 SceneState().rngSeed,
                 SceneState().timeScale,
                 SceneState().isFixedStep ? " --fixed-step" : "",
                 m_launchOptions.noWater ? " --no-water" : "",
                 m_runtimeSettings.isPhysicsSleepEnabled ? "" : " --no-sleep",
                 generatedObjectArg );
    }
    fprintf( f, "water_hidden,%d\n", m_debug.isWaterHidden ? 1 : 0 );
    fprintf( f, "terrain_hidden,%d\n", m_debug.isTerrainHidden ? 1 : 0 );
    fprintf( f, "collision_visualizer,%d\n", m_debug.isCollisionVisualizer ? 1 : 0 );
    fprintf( f, "world_gravity,%.6f\n", m_cWorldEnvironment.GetGravity() );
    fprintf( f, "world_fluid_height,%.6f\n", m_cWorldEnvironment.GetFluidSurfaceHeight() );
    fprintf( f, "world_fluid_density,%.6f\n", m_cWorldEnvironment.GetFluidDensity() );
    fprintf( f, "cfg_friction_coeff,%.6f\n", Cfg().frictionCoeff );
    fprintf( f, "cfg_contact_epsilon,%.6f\n", Cfg().contactEpsilon );
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
    fprintf( f, "mass,%.6f\n", model.GetMass() );
    fprintf( f, "restitution,%.6f\n", model.GetCoefficientRestitution() );
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
        fprintf( f,
                 " radius=%.6f mass=%.6f restitution=%.6f",
                 sphere.GetRadius(),
                 model.GetMass(),
                 model.GetCoefficientRestitution() );
    }
    else if ( isBox )
    {
        const BoundingBox& box = std::get<BoundingBox>( shape );
        const Vector3& he = box.GetHalfExtents();
        fprintf( f,
                 " halfExtents=%.6f,%.6f,%.6f mass=%.6f restitution=%.6f",
                 he.x,
                 he.y,
                 he.z,
                 model.GetMass(),
                 model.GetCoefficientRestitution() );
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
                 model.GetMass(),
                 model.GetCoefficientRestitution() );
    }
    fprintf( f, "\n" );
    fprintf( f, "=== END LAUNCHER REPRO SNAPSHOT ===\n" );

    sprintf_s( m_debug.reproSnapshotMessage,
               sizeof( m_debug.reproSnapshotMessage ),
               "Repro snapshot: %s",
               LAUNCHER_REPRO_SNAPSHOT_PATH );
    m_debug.reproSnapshotMessageUntil =
        m_timers.simulationTimer.GetTimeSinceLastStart() + LAUNCHER_REPRO_MESSAGE_SECONDS;
}
#endif
