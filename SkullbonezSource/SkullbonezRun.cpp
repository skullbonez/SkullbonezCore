/*
File: SkullbonezSource/SkullbonezRun.cpp
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  FBO (Framebuffer Object): Engine shorthand for an off-screen render target
  exposed through the renderer abstraction.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Backend-owned render resources must be released while the renderer backend
    is still alive, after a GPU flush, and in the explicit release order below.
  - WorldEnvironment reset can record upload commands; flush after that step
    before later resources are released.

Related:
  - SkullbonezSource/SkullbonezRun.h
  - SkullbonezSource/SkullbonezRunRender.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRunInternal.h"

#include <stdexcept>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;


SkullbonezRun::SkullbonezRun( std::vector<std::string> sceneQueue )
    : m_sceneRuntime( std::move( sceneQueue ) ),
      m_fullscreenQuadPass( *this ),
      m_skyPass( *this ),
      m_sceneTargetPass( *this ),
      m_shadowPass( *this ),
      m_reflectionPass( *this ),
      m_objectPass( *this ),
      m_terrainPass( *this ),
      m_waterPass( *this ),
      m_debugOverlayPass( *this ),
      m_volumetricPass( *this ),
      m_tonemapPass( *this ),
      m_uiTextPass( *this )
{
    RefreshSceneBrowserList();
    m_runtimeSettings.isVsyncEnabled = Cfg().runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = Cfg().runtimeRender.forcePipelineSync;
    m_defaultCinematicRender = Cfg().cinematicRender;
    m_startupGameModelCapacity = ActiveGameModelCapacity();
    m_startupWorkerThreads = Cfg().workerThreads;
}


RunSceneState& SkullbonezRun::SceneState()
{
    return m_sceneRuntime.State();
}


const RunSceneState& SkullbonezRun::SceneState() const
{
    return m_sceneRuntime.State();
}


SkullbonezRun::~SkullbonezRun()
{
#ifdef _DEBUG
    EndPhysicsDiagnosticsRun( "process_end" );
#endif

    RuntimeDiagnostics::ClosePerfLog( m_perfLogState );

    // Hazard: backend resources can still be referenced by queued GPU work.
    // Flush before releasing the runtime's owning pointers so teardown cannot
    // free memory while the device is still reading it.
    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }

    // Lifetime: clean up backend-owned render resources while the current
    // backend is still alive. WorldEnvironment::ResetRenderResources() rebuilds
    // fluid meshes, records GPU upload commands, and leaves the DX12 command
    // list open. Flush immediately after that step so later releases cannot hit
    // "ID3D12Resource deleted before command list close" validation errors.
    ReleaseBackendOwnedRenderResources( "shutdown_release" );

    SkullbonezCore::Assets::BindActiveAssetSystem( nullptr );
}


void SkullbonezRun::ReleaseBackendOwnedRenderResources( const char* phaseName )
{
    enum class BackendResourceStep
    {
        WorldEnvironment,
        HelperResources,
        GameModelResources,
        CollisionVisualizer,
        UIResources,
        RenderPassResources,
        ProfilerQueries,
        TextureCollection,
        CameraCollection,
        SkyBox
    };

    struct BackendResourcePhase
    {
        const char* name;
        BackendResourceStep step;
        bool flushAfter;
    };

    const BackendResourcePhase releaseSteps[] = {
        { "world_environment", BackendResourceStep::WorldEnvironment, true },
        { "helper_resources", BackendResourceStep::HelperResources, false },
        { "game_model_resources", BackendResourceStep::GameModelResources, false },
        { "collision_visualizer", BackendResourceStep::CollisionVisualizer, false },
        { "ui_resources", BackendResourceStep::UIResources, false },
        { "render_pass_resources", BackendResourceStep::RenderPassResources, false },
        { "profiler_queries", BackendResourceStep::ProfilerQueries, false },
        { "texture_collection", BackendResourceStep::TextureCollection, false },
        { "camera_collection", BackendResourceStep::CameraCollection, false },
        { "skybox_singleton", BackendResourceStep::SkyBox, false },
    };

    for ( const BackendResourcePhase& phase : releaseSteps )
    {
        LogRenderResourceLifecycleStep( phaseName, phase.name );
        switch ( phase.step )
        {
        case BackendResourceStep::WorldEnvironment:
            m_cWorldEnvironment.ResetRenderResources();
            break;
        case BackendResourceStep::HelperResources:
            SkullbonezHelper::ResetRenderResources();
            break;
        case BackendResourceStep::GameModelResources:
            m_cGameModelCollection.ResetRenderResources();
            break;
        case BackendResourceStep::CollisionVisualizer:
            m_collisionVisualizer.ResetResources();
            break;
        case BackendResourceStep::UIResources:
            m_UI.ResetResources();
            break;
        case BackendResourceStep::RenderPassResources:
            // Lifetime: release pass-owned GPU resources while the renderer
            // backend is still alive. The order keeps consumers ahead of their
            // producers, so cached handles are invalidated before targets die.
            m_tonemapPass.ReleaseGpuResources();
            m_volumetricPass.ReleaseGpuResources();
            m_sceneTargetPass.ReleaseGpuResources();
            m_shadowPass.ReleaseGpuResources();
            m_reflectionPass.ReleaseGpuResources();
            m_skyPass.ReleaseGpuResources();
            m_fullscreenQuadPass.ReleaseGpuResources();
            m_uiTextPass.ReleaseGpuResources();
            break;
        case BackendResourceStep::ProfilerQueries:
#if defined( SKULLBONEZ_PROFILE_ENABLED )
            Profiler::Instance().InvalidateGpuQueries();
#endif
            break;
        case BackendResourceStep::TextureCollection:
            if ( m_systems.textures )
            {
                m_systems.textures->Destroy();
            }
            break;
        case BackendResourceStep::CameraCollection:
            if ( m_systems.cameras )
            {
                m_systems.cameras->Destroy();
            }
            break;
        case BackendResourceStep::SkyBox:
            if ( m_systems.skyBox )
            {
                m_systems.skyBox->Destroy();
            }
            break;
        }

        if ( phase.flushAfter && IsGfxReady() )
        {
            LogRenderResourceLifecycleStep( phaseName, "flush_after_world_environment" );
            Gfx().FlushGPU();
        }
    }
}


void SkullbonezRun::RegisterBuiltInAssets()
{
    const SkullbonezConfig& cfg = Cfg();
    m_systems.assets.RegisterTextureSourceAsset( "texture.terrain", cfg.terrainTexture.c_str(), TEXTURE_GROUND, true, true, 3 );
    m_systems.assets.RegisterTextureSourceAsset( "texture.sphere", cfg.sphereTexture.c_str(), TEXTURE_BOUNDING_SPHERE, true, true, 3 );
    m_systems.assets.RegisterTextureSourceAsset( "texture.sky.left", cfg.skyLeft.c_str(), TEXTURE_SKY_LEFT, true, true, 3 );
    m_systems.assets.RegisterTextureSourceAsset( "texture.sky.right", cfg.skyRight.c_str(), TEXTURE_SKY_RIGHT, true, true, 3 );
    m_systems.assets.RegisterTextureSourceAsset( "texture.sky.front", cfg.skyFront.c_str(), TEXTURE_SKY_FRONT, true, true, 3 );
    m_systems.assets.RegisterTextureSourceAsset( "texture.sky.back", cfg.skyBack.c_str(), TEXTURE_SKY_BACK, true, true, 3 );
    m_systems.assets.RegisterTextureSourceAsset( "texture.sky.up", cfg.skyUp.c_str(), TEXTURE_SKY_UP, true, true, 3 );
    m_systems.assets.RegisterTextureSourceAsset( "texture.sky.down", cfg.skyDown.c_str(), TEXTURE_SKY_DOWN, true, true, 3 );

    auto contract = []( bool usesTexture, bool usesLighting, bool usesInstancing, bool depthOnly, bool postProcess )
    {
        Assets::ShaderProgramContract result;
        result.usesTexture = usesTexture;
        result.usesLighting = usesLighting;
        result.usesInstancing = usesInstancing;
        result.depthOnly = depthOnly;
        result.postProcess = postProcess;
        return result;
    };

    m_systems.assets.RegisterShaderSourceAsset( "shader.lit_textured", "shaders/lit_textured", Assets::ShaderProgramKind::LitTextured, contract( true, true, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.lit_textured_instanced", "shaders/lit_textured_instanced", Assets::ShaderProgramKind::LitTextured, contract( true, true, true, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.unlit_textured", "shaders/unlit_textured", Assets::ShaderProgramKind::UnlitTextured, contract( true, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.shadow_depth", "shaders/shadow_depth", Assets::ShaderProgramKind::ShadowDepth, contract( false, false, false, true, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.shadow_depth_instanced", "shaders/shadow_depth_instanced", Assets::ShaderProgramKind::ShadowDepth, contract( false, false, true, true, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.post_tonemap", "shaders/post_tonemap", Assets::ShaderProgramKind::PostProcess, contract( true, false, false, false, true ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.post_volumetric_light", "shaders/post_volumetric_light", Assets::ShaderProgramKind::PostProcess, contract( true, false, false, false, true ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.sky_atmosphere", "shaders/sky_atmosphere", Assets::ShaderProgramKind::PostProcess, contract( false, false, false, false, true ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.text", "shaders/text", Assets::ShaderProgramKind::Text, contract( true, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.solid_color", "shaders/solid_color", Assets::ShaderProgramKind::Text, contract( false, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.solid_color_batch", "shaders/solid_color_batch", Assets::ShaderProgramKind::Text, contract( false, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.water_calm", "shaders/water_calm", Assets::ShaderProgramKind::Water, contract( true, true, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.water_ocean", "shaders/water_ocean", Assets::ShaderProgramKind::Water, contract( true, true, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.collision_visualizer", "shaders/collision_visualizer", Assets::ShaderProgramKind::Collision, contract( false, true, true, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.grid_line", "shaders/grid_line", Assets::ShaderProgramKind::DebugLine, contract( false, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.ui_backdrop_blur", "shaders/UIBackdropBlur", Assets::ShaderProgramKind::UI, contract( true, false, false, false, true ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.reflect_rt", "shaders/reflect.rt", Assets::ShaderProgramKind::RayTracing, contract( true, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.generate_mips", "shaders/generate_mips", Assets::ShaderProgramKind::Compute, contract( true, false, false, false, false ) );
}


std::string SkullbonezRun::ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind kind, const char* logicalName, const std::string& relativePath )
{
    const SkullbonezCore::Assets::SourceAssetRecord& record = m_systems.assets.RegisterSourceAsset( kind, logicalName, relativePath.c_str() );
    return record.resolvedPath;
}


TextureCollection& SkullbonezRun::Textures()
{
    if ( !m_systems.textures )
    {
        throw std::runtime_error( "Texture collection is not initialised." );
    }
    return *m_systems.textures;
}


uint32_t SkullbonezRun::TextureHandle( uint32_t textureHash )
{
    return Textures().GetTextureHandle( textureHash );
}


void SkullbonezRun::SelectRenderTexture( uint32_t textureHash )
{
    Textures().SelectTexture( textureHash );
}


int SkullbonezRun::WindowScreenWidth() const
{
    return m_systems.window ? static_cast<int>( m_systems.window->m_sWindowDimensions.x ) : Cfg().window.screenX;
}


int SkullbonezRun::WindowScreenHeight() const
{
    return m_systems.window ? static_cast<int>( m_systems.window->m_sWindowDimensions.y ) : Cfg().window.screenY;
}


void SkullbonezRun::DumpTextureAssets( FILE* out ) const
{
    if ( m_systems.textures )
    {
        m_systems.textures->DumpTextureAssets( out );
    }
}


void SkullbonezRun::LogRenderResourceLifecycleStep( const char* phase, const char* step ) const
{
    const bool gfxReady = IsGfxReady();
    const int backendWidth = gfxReady ? Gfx().GetWidth() : 0;
    const int backendHeight = gfxReady ? Gfx().GetHeight() : 0;
    Log().WriteEventf( "render_resource_lifecycle phase=%s step=%s gfx_ready=%d backend_width=%d backend_height=%d scene_index=%d load=%d",
                       phase ? phase : "unknown",
                       step ? step : "unknown",
                       gfxReady ? 1 : 0,
                       backendWidth,
                       backendHeight,
                       SceneState().currentSceneIndex,
                       SceneState().loadCount );
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


void SkullbonezRun::SetTornadoOverride( bool enabled )
{
    m_cmdHasTornadoOverride = true;
    m_cmdTornadoEnabled = enabled;
    m_runtimeSettings.tornadoField.enabled = enabled;
    SyncTornadoFieldToPhysics();
}


void SkullbonezRun::SetTornadoVectorFieldOverride( bool enabled )
{
    m_cmdTornadoVectors = enabled;
    m_runtimeSettings.tornadoField.visualizeVelocityField = enabled;
    SyncTornadoFieldToPhysics();
}


void SkullbonezRun::SetCinematicRenderingOverride( bool enabled )
{
    m_cmdHasCinematicRenderingOverride = true;
    m_cmdCinematicRendering = enabled;
}


void SkullbonezRun::SetCinematicShadowsOverride( bool enabled )
{
    m_cmdHasCinematicShadowsOverride = true;
    m_cmdCinematicShadows = enabled;
}


void SkullbonezRun::SetDemoHeroStyleOverride()
{
    m_cmdDemoHeroStyle = true;
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
    RuntimeDiagnostics::SetPhysicsRegressionLogOverride( m_perfLogState, path );
}


void SkullbonezRun::SetPhysicsCollisionTimeLogOverride( const char* path )
{
    RuntimeDiagnostics::SetPhysicsCollisionTimeLogOverride( m_perfLogState, path );
}


void SkullbonezRun::SetPhysicsDiagnosticsPath( const char* path, bool fixedStepForcedByDiagnostics )
{
    RuntimeDiagnostics::SetPhysicsDiagnosticsPath( m_physicsDiagnostics, m_cGameModelCollection, path, fixedStepForcedByDiagnostics );
}
#endif


void SkullbonezRun::Initialise()
{
    m_systems.window = SkullbonezWindow::Instance();

    const char* rendererName = Gfx().GetRendererName();
    char titleText[256];
    sprintf_s( titleText, "%s [%s] -- LOADING!!!", TITLE_TEXT, rendererName );
    m_systems.window->SetTitleText( titleText );

    m_systems.textures = TextureCollection::Instance();
    m_systems.textures->BindAssetSystem( &m_systems.assets );
    SkullbonezCore::Assets::BindActiveAssetSystem( &m_systems.assets );
    RegisterBuiltInAssets();

    // Build renderer-owned resources from source asset records.
    RebuildRegisteredRenderResources();

    const std::string terrainRawPath = ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain, "terrain.raw", Cfg().terrainRaw );
    m_systems.terrain = std::make_unique<Terrain>( terrainRawPath.c_str(), 256, 8, 15 );
    m_systems.isFlatSlopeTerrain = false;

    // Init SkyBox (m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax)
    m_systems.skyBox = SkyBox::Instance( -250, 300, -300, 300, -250, 300 );
    m_systems.skyBox->ResetRenderResources();

    {
        const SkullbonezConfig& cfg = Cfg();
        m_cWorldEnvironment = WorldEnvironment( cfg.fluidHeight, cfg.fluidDensity, cfg.gasDensity, cfg.gravity );
        XZBounds tb = m_systems.terrain->GetXZBounds();
        m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );
    }

    // Init font (HDC, font)
    m_uiTextPass.EnsureGpuResources();

    // Init cameras singleton (shared across scenes, Reset() between loads)
    m_systems.cameras = CameraCollection::Instance();

    LoadScene( 0 );
}


void SkullbonezRun::RunSceneLoadOnly()
{
    const int sceneCount = m_sceneRuntime.QueueSize();
    if ( sceneCount <= 0 )
    {
        return;
    }

    printf( "[scene-load-only] Loaded 1/%d: %s\n", sceneCount, m_sceneRuntime.PathAt( 0 ).empty() ? "generated" : m_sceneRuntime.PathAt( 0 ).c_str() );
    for ( int i = 1; i < sceneCount; ++i )
    {
        LoadScene( i );
        printf( "[scene-load-only] Loaded %d/%d: %s\n", i + 1, sceneCount, m_sceneRuntime.PathAt( i ).empty() ? "generated" : m_sceneRuntime.PathAt( i ).c_str() );
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
    FILE* rawFile = nullptr;
    if ( fopen_s( &rawFile, NUDGE_REPRO_SNAPSHOT_PATH, "a" ) != 0 || !rawFile )
    {
        sprintf_s( m_debug.reproSnapshotMessage,
                   sizeof( m_debug.reproSnapshotMessage ),
                   "Failed to write repro snapshot" );
        m_debug.reproSnapshotMessageUntil = m_timers.simulationTimer.GetTimeSinceLastStart() + NUDGE_REPRO_MESSAGE_SECONDS;
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
    fprintf( f, "scene_mode,%d\n", SceneState().isSceneMode ? 1 : 0 );
    fprintf( f, "scene_index,%d\n", SceneState().currentSceneIndex );
    fprintf( f, "scene_load_count,%d\n", SceneState().loadCount );
    fprintf( f, "manual_reset_count,%d\n", SceneState().manualResetCount );
    fprintf( f, "scene_frame,%d\n", SceneState().currentFrame );
    fprintf( f, "target_frame_count,%d\n", SceneState().targetFrameCount );
    fprintf( f, "simulation_seconds,%.6f\n", m_timers.simulationTimer.GetTimeSinceLastStart() );
    fprintf( f, "rng_seed,%u\n", SceneState().rngSeed );
    fprintf( f, "cmd_seed_override,%u\n", m_cmdSeedOverride );
    fprintf( f, "cmd_no_water,%d\n", m_cmdNoWater ? 1 : 0 );
    fprintf( f, "cmd_no_sleep,%d\n", m_cmdNoSleep ? 1 : 0 );
    fprintf( f, "physics_sleep_enabled,%d\n", m_runtimeSettings.isPhysicsSleepEnabled ? 1 : 0 );
    fprintf( f, "fixed_step_effective,%d\n", SceneState().isFixedStep ? 1 : 0 );
    fprintf( f, "cmd_fixed_step_override,%d\n", m_cmdFixedStep ? 1 : 0 );
    fprintf( f, "time_scale,%.6f\n", SceneState().timeScale );
    fprintf( f, "renderer,%s\n", rendererName );
    fprintf( f, "generated_object_override,%s\n", generatedObjectOverride );
    fprintf( f, "model_count,%d\n", m_cGameModelCollection.GetModelCount() );
    fprintf( f, "vsync_enabled,%d\n", m_runtimeSettings.isVsyncEnabled ? 1 : 0 );
    fprintf( f, "pipeline_sync_enabled,%d\n", m_runtimeSettings.isPipelineSyncEnabled ? 1 : 0 );
    if ( SceneState().isSceneMode )
    {
        fprintf( f,
                 "repro_command_hint,Debug\\SKULLBONEZ_CORE.exe --renderer %s --scene \"%s\" --seed %u --time-scale %.6f%s%s%s%s\n",
                 rendererArg,
                 scenePath,
                 SceneState().rngSeed,
                 SceneState().timeScale,
                 SceneState().isFixedStep ? " --fixed-step" : "",
                 m_cmdNoWater ? " --no-water" : "",
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
    fprintf( f, "=== END NUDGE REPRO SNAPSHOT ===\n" );

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
    const char* scenePath = "generated";
    const std::string* currentScenePath = CurrentSceneQueuePath();
    if ( currentScenePath && !currentScenePath->empty() )
    {
        scenePath = currentScenePath->c_str();
    }

    RuntimeDiagnostics::LogSceneFinished( SceneState(), scenePath, IsGfxReady() ? Gfx().GetRendererName() : "unknown", reason );
}


void SkullbonezRun::BeginPhysicsDiagnosticsRun( const char* scenePath )
{
    RuntimeDiagnostics::BeginPhysicsDiagnosticsRun( m_physicsDiagnostics,
                                                    m_cGameModelCollection,
                                                    SceneState(),
                                                    Cfg(),
                                                    scenePath,
                                                    IsGfxReady() ? Gfx().GetRendererName() : "unknown" );
}


void SkullbonezRun::EndPhysicsDiagnosticsRun( const char* status )
{
    RuntimeDiagnostics::EndPhysicsDiagnosticsRun( m_physicsDiagnostics, SceneState(), status );
}
#endif
