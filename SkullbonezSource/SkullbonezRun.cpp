// --- Includes ---
#include "SkullbonezRunInternal.h"

// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;


SkullbonezRun::SkullbonezRun( std::vector<std::string> sceneQueue )
    : m_sceneQueue( std::move( sceneQueue ) )
{
    RefreshSceneBrowserList();
    m_runtimeSettings.isVsyncEnabled = Cfg().runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = Cfg().runtimeRender.forcePipelineSync;
}


SkullbonezRun::~SkullbonezRun()
{
#ifdef _DEBUG
    EndPhysicsDiagnosticsRun( "process_end" );
#endif

    if ( m_perfLogState.perfLogFile )
    {
        fclose( m_perfLogState.perfLogFile );
        m_perfLogState.perfLogFile = nullptr;
    }

    // Flush GPU before destroying resources to avoid use-after-free
    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }

    // Clean up GL resources while context is still alive.
    // WorldEnvironment::ResetGLResources() rebuilds fluid meshes (records GPU upload commands
    // and leaves the DX12 command list open). Flush immediately after so subsequent resource
    // releases don't trigger "ID3D12Resource deleted before command list close" validation
    // errors — resources must not be freed while any open command list could reference them.
    m_cWorldEnvironment.ResetGLResources();
    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }

    SkullbonezHelper::ResetGLResources();
    m_cGameModelCollection.ResetGLResources();
    m_collisionVisualizer.ResetResources();
    m_UI.ResetResources();
    ResetCinematicRenderResources();
    if ( m_systems.reflectionFBO )
    {
        m_systems.reflectionFBO->ResetResources();
    }
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    Profiler::Instance().InvalidateGpuQueries();
#endif
    Text2d::DeleteFont();

    m_systems.textures->Destroy();
    m_systems.cameras->Destroy();
    m_systems.skyBox->Destroy();
}


void SkullbonezRun::SetRendererSwitchInterval( float seconds )
{
    m_debug.rendererSwitchInterval = seconds;
}


void SkullbonezRun::SetTimeScaleOverride( float scale )
{
    m_cmdTimeScaleOverride = scale;
}


void SkullbonezRun::SetFixedStepOverride()
{
    m_cmdFixedStep = true;
}


void SkullbonezRun::SetSeedOverride( unsigned int seed )
{
    m_cmdSeedOverride = seed;
}


void SkullbonezRun::SetNoWaterOverride()
{
    m_cmdNoWater = true;
}


void SkullbonezRun::SetNoSleepOverride()
{
    m_cmdNoSleep = true;
    m_runtimeSettings.isPhysicsSleepEnabled = false;
    m_cGameModelCollection.SetPhysicsSleepEnabled( false );
}


void SkullbonezRun::SetCinematicRenderingOverride( bool enabled )
{
    m_cmdHasCinematicRenderingOverride = true;
    m_cmdCinematicRendering = enabled;
}


void SkullbonezRun::SetInteractiveRunOverride()
{
    m_cmdInteractiveSceneRun = true;
}


void SkullbonezRun::SetFrameCountOverride( int frames )
{
    m_cmdFrameCountOverride = (std::max)( 1, frames );
}


void SkullbonezRun::SetUIStressOverride( unsigned int seed, int actionsPerFrame )
{
    m_cmdUIStress = true;
    m_cmdUIStressSeed = seed > 0 ? seed : 0x7F4A7C15u;
    m_cmdUIStressActions = std::clamp( actionsPerFrame, 1, 32 );
}


void SkullbonezRun::SetInitialOverlayMode( OverlayMode mode )
{
    m_debug.overlayMode = mode;
    if ( mode != OverlayMode::None )
    {
        m_UI.SetVisible( true );
    }
    switch ( mode )
    {
    case OverlayMode::SceneStats:
        m_UI.SetActiveTab( InGameUITab::Scene );
        break;
    case OverlayMode::Keys:
        m_UI.SetActiveTab( InGameUITab::Keys );
        break;
    case OverlayMode::BarsNormalized:
    case OverlayMode::BarsAbsolute:
    case OverlayMode::Timers:
        m_UI.SetActiveTab( InGameUITab::Profiler );
        break;
    default:
        break;
    }
}


void SkullbonezRun::SetTopTextHidden( bool hidden )
{
    m_debug.isTopTextHidden = hidden;
}


void SkullbonezRun::SetBroadphaseVisualizerEnabled( bool enabled )
{
    m_debug.isBroadphaseOverlay = enabled;
}


void SkullbonezRun::SetGeneratedObjectTypeOverride( GeneratedObjectTypeOverride objectTypeOverride )
{
    m_generatedObjectTypeOverride = objectTypeOverride;
}


void SkullbonezRun::SetPhysicsDebugFlagsOverride( uint32_t flags )
{
    m_cmdHasPhysicsDebugFlagsOverride = true;
    m_cmdPhysicsDebugFlagsOverride = flags & PHYSICS_DEBUG_ALL;
}


void SkullbonezRun::SetPhysicsDebugTransparentOverride( bool transparent )
{
    m_cmdHasPhysicsDebugTransparentOverride = true;
    m_cmdPhysicsDebugTransparentOverride = transparent;
}


void SkullbonezRun::SetPhysicsDebugAlphaOverride( float alpha )
{
    m_cmdHasPhysicsDebugAlphaOverride = true;
    m_cmdPhysicsDebugAlphaOverride = (std::max)( 0.05f, (std::min)( alpha, 1.0f ) );
}


void SkullbonezRun::SetPhysicsDebugContactLingerOverride( float seconds )
{
    m_cmdHasPhysicsDebugContactLingerOverride = true;
    m_cmdPhysicsDebugContactLingerOverride = (std::max)( 0.0f, (std::min)( seconds, 5.0f ) );
}


#ifdef _DEBUG
void SkullbonezRun::SetPhysicsRegressionLogOverride( const char* path )
{
    strcpy_s( m_perfLogState.physicsRegressionLogOverride, sizeof( m_perfLogState.physicsRegressionLogOverride ), path );
}


void SkullbonezRun::SetPhysicsCollisionTimeLogOverride( const char* path )
{
    strcpy_s( m_perfLogState.physicsCollisionTimeLogOverride, sizeof( m_perfLogState.physicsCollisionTimeLogOverride ), path );
}


void SkullbonezRun::SetPhysicsDiagnosticsPath( const char* path, bool fixedStepForcedByDiagnostics )
{
    strcpy_s( m_physicsDiagnostics.path, sizeof( m_physicsDiagnostics.path ), path );
    m_physicsDiagnostics.isEnabled = m_physicsDiagnostics.path[0] != '\0';
    m_physicsDiagnostics.fixedStepForcedByDiagnostics = fixedStepForcedByDiagnostics;
    m_cGameModelCollection.SetPhysicsDiagnosticsPath( m_physicsDiagnostics.path );
}
#endif


void SkullbonezRun::Initialise()
{
    // Init window
    m_systems.window = SkullbonezWindow::Instance();

    // Set loading text
    const char* rendererName = Gfx().GetRendererName();
    char titleText[256];
    sprintf_s( titleText, "%s [%s] -- LOADING!!!", TITLE_TEXT, rendererName );
    m_systems.window->SetTitleText( titleText );

    // Init m_textures
    m_systems.textures = TextureCollection::Instance();

    // Init OpenGL
    SetInitialOpenGlState();

    // Init m_terrain
    // path to m_height map | map size pixels | step size | times to wrap texture
    m_systems.terrain = std::make_unique<Terrain>( ( std::string( DATA_ROOT ) + Cfg().terrainRaw ).c_str(), 256, 8, 15 );
    m_systems.isFlatSlopeTerrain = false;

    // Init SkyBox (m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax)
    m_systems.skyBox = SkyBox::Instance( -250, 300, -300, 300, -250, 300 );
    m_systems.skyBox->ResetGLResources();

    // Init world environment
    {
        const SkullbonezConfig& cfg = Cfg();
        m_cWorldEnvironment = WorldEnvironment( cfg.fluidHeight, cfg.fluidDensity, cfg.gasDensity, cfg.gravity );
        XZBounds tb = m_systems.terrain->GetXZBounds();
        m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );
    }

    // Init reflection FBO at the current viewport size
    int fboW = Gfx().GetWidth() * 2;
    int fboH = Gfx().GetHeight() * 2;
    m_systems.reflectionFBO = Gfx().CreateFramebuffer( fboW, fboH );
    EnsureCinematicRenderResources();

    // Init font (HDC, font)
    Text2d::BuildFont( "Verdana" );

    // Init cameras singleton (shared across scenes, Reset() between loads)
    m_systems.cameras = CameraCollection::Instance();

    // Load the first scene
    LoadScene( 0 );
}


void SkullbonezRun::RunSceneLoadOnly()
{
    const int sceneCount = static_cast<int>( m_sceneQueue.size() );
    if ( sceneCount <= 0 )
    {
        return;
    }

    printf( "[scene-load-only] Loaded 1/%d: %s\n", sceneCount, m_sceneQueue[0].empty() ? "generated" : m_sceneQueue[0].c_str() );
    for ( int i = 1; i < sceneCount; ++i )
    {
        LoadScene( i );
        printf( "[scene-load-only] Loaded %d/%d: %s\n", i + 1, sceneCount, m_sceneQueue[i].empty() ? "generated" : m_sceneQueue[i].c_str() );
    }
}


#ifdef _DEBUG
bool SkullbonezRun::PickNudgeReproTarget( int& outIndex, float& outRayT, float& outCrosshairDistance )
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


void SkullbonezRun::WriteNudgeReproSnapshot()
{
    int targetIndex = -1;
    float rayT = 0.0f;
    float crosshairDistance = 0.0f;
    if ( !PickNudgeReproTarget( targetIndex, rayT, crosshairDistance ) )
    {
        sprintf_s( m_debug.reproSnapshotMessage,
                   sizeof( m_debug.reproSnapshotMessage ),
                   "No repro target under crosshair" );
        m_debug.reproSnapshotMessageUntil = m_timers.simulationTimer.GetTimeSinceLastStart() + NUDGE_REPRO_MESSAGE_SECONDS;
        return;
    }

    CreateDirectoryA( "Debug", nullptr );
    FILE* f = nullptr;
    if ( fopen_s( &f, NUDGE_REPRO_SNAPSHOT_PATH, "a" ) != 0 || !f )
    {
        sprintf_s( m_debug.reproSnapshotMessage,
                   sizeof( m_debug.reproSnapshotMessage ),
                   "Failed to write repro snapshot" );
        m_debug.reproSnapshotMessageUntil = m_timers.simulationTimer.GetTimeSinceLastStart() + NUDGE_REPRO_MESSAGE_SECONDS;
        return;
    }

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
    if ( m_scene.isSceneMode && m_scene.currentSceneIndex >= 0 &&
         m_scene.currentSceneIndex < static_cast<int>( m_sceneQueue.size() ) )
    {
        scenePath = m_sceneQueue[m_scene.currentSceneIndex].c_str();
    }

    const char* rendererName = IsGfxReady() ? Gfx().GetRendererName() : "<uninitialised>";
    const RuntimeRendererType rendererType = IsGfxReady() ? GetCurrentRendererType() : RuntimeRendererType::OpenGL;
    const char* rendererArg = "gl";
    if ( rendererType == RuntimeRendererType::DX11 )
    {
        rendererArg = "dx11";
    }
    else if ( rendererType == RuntimeRendererType::DX12 )
    {
        rendererArg = "dx12";
    }
    const char* generatedObjectOverride = "mixed";
    const char* generatedObjectArg = "";
    if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
    {
        generatedObjectOverride = "all_balls";
        generatedObjectArg = " --all-balls";
    }
    else if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
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
    fprintf( f, "\n=== NUDGE REPRO SNAPSHOT ===\n" );
    fprintf( f, "timestamp_epoch,%lld\n", static_cast<long long>( now ) );
    fprintf( f, "snapshot_file,%s\n", NUDGE_REPRO_SNAPSHOT_PATH );
    fprintf( f, "scene,%s\n", scenePath );
    fprintf( f, "scene_mode,%d\n", m_scene.isSceneMode ? 1 : 0 );
    fprintf( f, "scene_index,%d\n", m_scene.currentSceneIndex );
    fprintf( f, "scene_load_count,%d\n", m_scene.loadCount );
    fprintf( f, "manual_reset_count,%d\n", m_scene.manualResetCount );
    fprintf( f, "scene_frame,%d\n", m_scene.currentFrame );
    fprintf( f, "target_frame_count,%d\n", m_scene.targetFrameCount );
    fprintf( f, "simulation_seconds,%.6f\n", m_timers.simulationTimer.GetTimeSinceLastStart() );
    fprintf( f, "rng_seed,%u\n", m_scene.rngSeed );
    fprintf( f, "cmd_seed_override,%u\n", m_cmdSeedOverride );
    fprintf( f, "cmd_no_water,%d\n", m_cmdNoWater ? 1 : 0 );
    fprintf( f, "cmd_no_sleep,%d\n", m_cmdNoSleep ? 1 : 0 );
    fprintf( f, "physics_sleep_enabled,%d\n", m_runtimeSettings.isPhysicsSleepEnabled ? 1 : 0 );
    fprintf( f, "fixed_step_effective,%d\n", m_scene.isFixedStep ? 1 : 0 );
    fprintf( f, "cmd_fixed_step_override,%d\n", m_cmdFixedStep ? 1 : 0 );
    fprintf( f, "time_scale,%.6f\n", m_scene.timeScale );
    fprintf( f, "renderer,%s\n", rendererName );
    fprintf( f, "generated_object_override,%s\n", generatedObjectOverride );
    fprintf( f, "model_count,%d\n", m_cGameModelCollection.GetModelCount() );
    fprintf( f, "vsync_enabled,%d\n", m_runtimeSettings.isVsyncEnabled ? 1 : 0 );
    fprintf( f, "pipeline_sync_enabled,%d\n", m_runtimeSettings.isPipelineSyncEnabled ? 1 : 0 );
    if ( m_scene.isSceneMode )
    {
        fprintf( f,
                 "repro_command_hint,Debug\\SKULLBONEZ_CORE.exe --renderer %s --scene \"%s\" --seed %u --time-scale %.6f%s%s%s%s\n",
                 rendererArg,
                 scenePath,
                 m_scene.rngSeed,
                 m_scene.timeScale,
                 m_scene.isFixedStep ? " --fixed-step" : "",
                 m_cmdNoWater ? " --no-water" : "",
                 m_runtimeSettings.isPhysicsSleepEnabled ? "" : " --no-sleep",
                 generatedObjectArg );
    }
    else
    {
        fprintf( f,
                 "repro_command_hint,Debug\\SKULLBONEZ_CORE.exe --renderer %s --seed %u --time-scale %.6f%s%s%s%s\n",
                 rendererArg,
                 m_scene.rngSeed,
                 m_scene.timeScale,
                 m_scene.isFixedStep ? " --fixed-step" : "",
                 m_cmdNoWater ? " --no-water" : "",
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
    fprintf( f, "pick_shape,%s\n", isSphere ? "sphere" : "box" );
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
    else
    {
        const BoundingBox& box = std::get<BoundingBox>( shape );
        const Vector3& he = box.GetHalfExtents();
        fprintf( f, "box_half_extents,%.6f,%.6f,%.6f\n", he.x, he.y, he.z );
        fprintf( f, "box_terrain_supported_vertices,%d\n", boxTerrainSupportedVertices );
        fprintf( f, "box_min_terrain_gap,%.6f\n", boxMinTerrainGap );
        fprintf( f, "box_max_terrain_gap,%.6f\n", boxMaxTerrainGap );
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
             isSphere ? "ball_state/manual" : "box/manual",
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
    else
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
    fprintf( f, "\n" );
    fprintf( f, "=== END NUDGE REPRO SNAPSHOT ===\n" );
    fclose( f );

    sprintf_s( m_debug.reproSnapshotMessage,
               sizeof( m_debug.reproSnapshotMessage ),
               "Repro snapshot: %s",
               NUDGE_REPRO_SNAPSHOT_PATH );
    m_debug.reproSnapshotMessageUntil = m_timers.simulationTimer.GetTimeSinceLastStart() + NUDGE_REPRO_MESSAGE_SECONDS;
}
#endif


#ifdef _DEBUG
void SkullbonezRun::LogSceneFinished( const char* reason )
{
    if ( m_scene.isFinishLogged )
    {
        return;
    }

    const char* scenePath = "generated";
    if ( m_scene.currentSceneIndex >= 0 &&
         m_scene.currentSceneIndex < static_cast<int>( m_sceneQueue.size() ) &&
         !m_sceneQueue[m_scene.currentSceneIndex].empty() )
    {
        scenePath = m_sceneQueue[m_scene.currentSceneIndex].c_str();
    }

    Log().WriteEventf( "scene_finished index=%d load=%d path=\"%s\" reason=%s frame=%d target_frames=%d renderer=\"%s\" models=%d test_complete=%d",
                       m_scene.currentSceneIndex,
                       m_scene.loadCount,
                       scenePath,
                       reason && reason[0] != '\0' ? reason : "unknown",
                       m_scene.currentFrame,
                       m_scene.targetFrameCount,
                       IsGfxReady() ? Gfx().GetRendererName() : "unknown",
                       m_scene.modelCount,
                       m_scene.isTestComplete ? 1 : 0 );

    m_scene.isFinishLogged = true;
}


void SkullbonezRun::BeginPhysicsDiagnosticsRun( const char* scenePath )
{
    if ( !m_physicsDiagnostics.isEnabled )
    {
        return;
    }

    ++m_physicsDiagnostics.runSequence;
    sprintf_s( m_physicsDiagnostics.currentRunId,
               sizeof( m_physicsDiagnostics.currentRunId ),
               "run_%04d",
               m_physicsDiagnostics.runSequence );
    m_physicsDiagnostics.isRunActive = true;
    m_cGameModelCollection.SetPhysicsDiagnosticsRunId( m_physicsDiagnostics.currentRunId );

    const char* rendererName = IsGfxReady() ? Gfx().GetRendererName() : "unknown";
    const char* solverName = "solver";
    std::string escapedScene = JsonEscape( scenePath && scenePath[0] != '\0' ? scenePath : "generated" );
    std::string escapedRenderer = JsonEscape( rendererName );
    std::string escapedSolver = JsonEscape( solverName );

    Log().Writef( m_physicsDiagnostics.path,
                  "{\"kind\":\"run\",\"run\":\"%s\",\"scene\":\"%s\",\"scene_index\":%d,\"load_count\":%d,\"manual_reset_count\":%d,\"renderer\":\"%s\",\"solver\":\"%s\",\"seed\":%u,\"fixed_step\":%d,\"fixed_step_forced_by_diag\":%d,\"target_frames\":%d,\"model_count\":%d,\"config\":{\"gravity\":%.6f,\"contact_epsilon\":%.6f,\"contact_restitution_threshold\":%.6f,\"friction_coeff\":%.6f,\"rolling_friction_coeff\":%.6f,\"spin_friction_coeff\":%.6f,\"broadphase_cell\":%.6f,\"persistent_contact_slop\":%.6f,\"persistent_contact_baumgarte_beta\":%.6f,\"persistent_contact_position_correction_percent\":%.6f,\"persistent_contact_solver_iterations\":%d,\"terrain_contact_threshold\":%.6f,\"terrain_contact_slop\":%.6f,\"terrain_contact_baumgarte_beta\":%.6f,\"terrain_max_baumgarte_bias\":%.6f,\"physics_sleep_linear_speed\":%.6f,\"physics_sleep_angular_speed\":%.6f,\"physics_sleep_frames\":%d}}\n",
                  m_physicsDiagnostics.currentRunId,
                  escapedScene.c_str(),
                  m_scene.currentSceneIndex,
                  m_scene.loadCount,
                  m_scene.manualResetCount,
                  escapedRenderer.c_str(),
                  escapedSolver.c_str(),
                  m_scene.rngSeed,
                  m_scene.isFixedStep ? 1 : 0,
                  m_physicsDiagnostics.fixedStepForcedByDiagnostics ? 1 : 0,
                  m_scene.targetFrameCount,
                  m_scene.modelCount,
                  Cfg().gravity,
                  Cfg().contactEpsilon,
                  Cfg().contactRestitutionThreshold,
                  Cfg().frictionCoeff,
                  Cfg().rollingFrictionCoeff,
                  Cfg().spinFrictionCoeff,
                  Cfg().broadphaseCell,
                  Cfg().persistentContactSlop,
                  Cfg().persistentContactBaumgarteBeta,
                  Cfg().persistentContactPositionCorrectionPercent,
                  Cfg().persistentContactSolverIterations,
                  Cfg().terrainContactThreshold,
                  Cfg().terrainContactSlop,
                  Cfg().terrainContactBaumgarteBeta,
                  Cfg().terrainMaxBaumgarteBias,
                  Cfg().physicsSleepLinearSpeed,
                  Cfg().physicsSleepAngularSpeed,
                  Cfg().physicsSleepFrames );
}


void SkullbonezRun::EndPhysicsDiagnosticsRun( const char* status )
{
    if ( !m_physicsDiagnostics.isEnabled || !m_physicsDiagnostics.isRunActive )
    {
        return;
    }

    std::string escapedStatus = JsonEscape( status && status[0] != '\0' ? status : "ended" );
    Log().Writef( m_physicsDiagnostics.path,
                  "{\"kind\":\"end\",\"run\":\"%s\",\"frame\":%d,\"status\":\"%s\"}\n",
                  m_physicsDiagnostics.currentRunId,
                  m_scene.currentFrame,
                  escapedStatus.c_str() );
    Log().FlushAll();

    m_physicsDiagnostics.isRunActive = false;
}
#endif
