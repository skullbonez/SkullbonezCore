/*
File: SkullbonezSource/Runtime/Run.cpp
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
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/RunRender.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "Editor/EditorOverlayTools.h"
#include "Replay/ReplayOverlayLayout.h"
#include "Replay/ReplayV2Artifact.h"
#include "RuntimeFileWriter.h"
#include "Scene/SceneRuntimeLoad.h"
#include "../UI/UIInput.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;
using namespace SkullbonezCore::Basics::ReplayOverlay;

namespace
{
constexpr uint32_t REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS = 1u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_MODEL_COUNT = 2u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS = 4u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT = 8u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_MASK = 3u << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;
constexpr uint64_t REPLAY_EVENT_FNV_OFFSET = 14695981039346656037ull;
constexpr uint64_t REPLAY_EVENT_FNV_PRIME = 1099511628211ull;

void HashReplayInt( uint64_t& hash, int32_t value )
{
    const uint32_t bits = static_cast<uint32_t>( value );
    for ( int shift = 0; shift < 32; shift += 8 )
    {
        hash ^= static_cast<uint64_t>( ( bits >> shift ) & 0xFFu );
        hash *= REPLAY_EVENT_FNV_PRIME;
    }
}

SkullbonezCore::Environment::CameraMovementSettings BuildCameraMovementSettings( const EngineConfig& cfg )
{
    SkullbonezCore::Environment::CameraMovementSettings settings;
    settings.minViewMag = cfg.minViewMag;
    settings.maxViewMag = cfg.maxViewMag;
    settings.minCameraHeight = cfg.minCameraHeight;
    settings.cameraCollisionThreshold = cfg.cameraCollisionThreshold;
    return settings;
}

} // namespace


RuntimeRenderHostBindings Run::BuildRuntimeRenderHostBindings()
{
    RuntimeRenderHostBindings bindings;
    bindings.backend.active = &m_renderBackendView;
    bindings.runtime.systems = &m_systems;
    bindings.runtime.config = &m_config;
    bindings.runtime.launchOptions = &m_launchOptions;
    bindings.runtime.runtimeSettings = &m_runtimeSettings;
    bindings.world.gameModelCollection = &m_cGameModelCollection;
    bindings.world.worldEnvironment = &m_cWorldEnvironment;
    bindings.world.collisionVisualizer = &m_collisionVisualizer;
    bindings.world.broadphaseVisualizer = &m_broadphaseVisualizer;
    bindings.world.physicsDebugVisualizer = &m_physicsDebugVisualizer;
    bindings.scene.sceneController = &m_sceneController;
    bindings.scene.sceneBrowser = &m_sceneController.Browser();
    bindings.replayOverlay.replayRuntime = &m_replayRuntime;
    bindings.toolOverlay.tools = &m_runtimeTools;
    bindings.ui.ui = &m_UI;
    bindings.ui.runtimeInput = &m_runtimeInput;
    bindings.ui.camera = &m_camera;
    bindings.ui.runtimeViewModel = &m_runtimeViewModel;
    bindings.diagnostics.diagnosticsRuntime = &m_diagnosticsRuntime;
    bindings.diagnostics.debug = &m_debug;
    bindings.diagnostics.timers = &m_timers;
    return bindings;
}


RuntimeRenderHostCallbacks Run::BuildRuntimeRenderHostCallbacks()
{
    RuntimeRenderHostCallbacks callbacks;
    callbacks.user = this;
    callbacks.logRenderResourceLifecycleStep = []( void* user, const char* phase, const char* step )
    { static_cast<Run*>( user )->LogRenderResourceLifecycleStep( phase, step ); };
    callbacks.renderEditorOverlay = []( void* user,
                                        SkullbonezCore::Rendering::IRenderResourceFactory& renderResources,
                                        const Math::Transformation::Matrix4& viewProjection,
                                        const Math::Vector::Vector3& cameraEye,
                                        const Math::Vector::Vector3& cameraUp )
    {
        Run* run = static_cast<Run*>( user );
        RunEditorTracer& tracer = run->m_runtimeTools.EditorTracer();
        tracer.Clear();

        int attachedTargetIndex = -1;
        if ( run->IsAttachedCameraMode() )
        {
            int targetIndex = -1;
            if ( run->TryResolveAttachedCameraTarget( targetIndex ) )
            {
                attachedTargetIndex = targetIndex;
            }
        }

        BuildEditorToolOverlayTrace( { run->m_runtimeTools.Editor(),
                                       run->m_runtimeTools.RayCastTest(),
                                       run->m_runtimeTools.MousePickup(),
                                       run->m_cGameModelCollection,
                                       run->m_systems.assets,
                                       tracer },
                                     { run->m_debug.physicsDebugContactLinger,
                                       run->InspectGizmoInteractionActive(),
                                       Input::IsKeyDown( VK_CONTROL ),
                                       attachedTargetIndex,
                                       run->m_attachedCamera.activeFollow } );
        run->RenderReplayPathVisualizer( tracer );
        run->RenderReplayCauseFocusOverlay( tracer );
        run->RenderReplayVelocityEditOverlay( tracer );
        tracer.Render( viewProjection );
        run->m_runtimeTools.Laser().Render( viewProjection,
                                            cameraEye,
                                            cameraUp,
                                            run->m_systems.assets,
                                            renderResources );
    };
    callbacks.refreshRuntimeViewModel = []( void* user ) { static_cast<Run*>( user )->RefreshRuntimeViewModel(); };
    callbacks.cameraModeEnabledMask = []( void* user ) -> uint32_t
    { return static_cast<Run*>( user )->CameraModeEnabledMask(); };
    callbacks.cameraModeLabel = []( void* user, RunCameraMode mode ) -> const char*
    { return static_cast<Run*>( user )->CameraModeLabel( mode ); };
    return callbacks;
}


Run::Run( Window& window,
          std::vector<std::string> sceneQueue,
          EngineConfig& config,
          Threading::WorkerPool& workerPool,
          RuntimeRenderBackendView renderBackendView )
    : m_config( config ), m_sceneController( std::move( sceneQueue ) ), m_sceneCoordinator( m_sceneController ),
      m_renderBackendView( renderBackendView ),
      m_renderHost( BuildRuntimeRenderHostBindings(), BuildRuntimeRenderHostCallbacks() ), m_renderer( m_renderHost )
{
    m_systems.window = &window;
    m_systems.workerPool = &workerPool;
    BindEngineContext();
    RefreshRuntimeViewModel();
    RefreshSceneBrowserList( m_sceneController.Browser() );
    const EngineConfig& cfg = m_config;
    m_systems.config = &cfg;
    m_systems.cameraCollection.ApplyMovementSettings( BuildCameraMovementSettings( cfg ) );
    m_cGameModelCollection.BindWorkerPool( workerPool );
    m_cGameModelCollection.ApplyRuntimeConfig( cfg );
    m_runtimeSettings.isVsyncEnabled = cfg.runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = cfg.runtimeRender.forcePipelineSync;
    m_runtimeSettings.contactAudioDebugCounters = cfg.contactAudio.debugCounters;
    m_defaultCinematicRender = cfg.cinematicRender;
    m_startup.gameModelCapacity = std::clamp( cfg.gameModelCapacity, 1, MAX_GAME_MODELS );
    m_startup.workerThreads = cfg.workerThreads;
}


RunSceneState& Run::SceneState()
{
    return m_sceneController.State();
}


const RunSceneState& Run::SceneState() const
{
    return m_sceneController.State();
}


void Run::BindEngineContext()
{
    m_engineContext.Bind( EngineContextBindings{ &m_sceneController,
                                                 &m_simulation,
                                                 &m_diagnosticsRuntime.Capture(),
                                                 &m_diagnosticsRuntime.Diagnostics(),
                                                 &m_runtimeCommands,
                                                 &m_systems,
                                                 &m_runtimeSettings,
                                                 &m_runtimeInput,
                                                 &m_camera,
                                                 &m_debug,
                                                 &m_cWorldEnvironment,
                                                 &m_cGameModelCollection } );
}


void Run::RefreshRuntimeViewModel()
{
    m_runtimeViewModel = RuntimeViewModelBuilder::Build( m_engineContext );
    RuntimeContactAudioSnapshot& audio = m_runtimeViewModel.contactAudio;
    audio.enabled = m_contactAudio.IsEnabled();
    audio.available = m_contactAudio.IsAvailable();
    audio.debugCounters = m_runtimeSettings.contactAudioDebugCounters;
    audio.flashOnSubmit = m_runtimeSettings.contactAudioFlashOnSubmit;
    audio.masterGain = m_contactAudio.MasterGain();
    audio.maxDistanceScale = m_contactAudio.MaxDistanceScale();
    audio.minClosingSpeed = m_contactAudio.MinClosingSpeed();
    audio.minImpactScore = m_contactAudio.MinImpactScore();
    audio.impactScoreRangeSeconds = m_contactAudio.ImpactScoreRangeSeconds();
    audio.stats = m_contactAudio.Stats();
    audio.soundSetCount = (std::min)( m_contactAudio.SoundSetCount(), RUNTIME_CONTACT_AUDIO_SET_MAX );
    audio.soundSampleCount = (std::min)( m_contactAudio.SoundSampleCount(), RUNTIME_CONTACT_AUDIO_SAMPLE_MAX );
    for ( int setIndex = 0; setIndex < audio.soundSetCount; ++setIndex )
    {
        m_contactAudio.GetSoundSetTuning( setIndex, audio.soundSets[setIndex] );
    }
    for ( int sampleIndex = 0; sampleIndex < audio.soundSampleCount; ++sampleIndex )
    {
        audio.soundSamplePaths[sampleIndex] = m_contactAudio.SoundSamplePath( sampleIndex );
    }
}


Run::~Run()
{
#ifdef _DEBUG
    EndPhysicsDiagnosticsRun( "process_end" );
#endif

    if ( m_diagnosticsRuntime.MainMemoryDumpRequested() )
    {
        m_diagnosticsRuntime.WriteMainMemoryDump( m_replayRuntime,
                                                  m_cGameModelCollection,
                                                  SceneState(),
                                                  "shutdown",
                                                  m_timers.simulationTimer.GetTotalTime() );
    }
    m_diagnosticsRuntime.ClosePerfLog();
    m_replayRuntime.FlushHashLogs();
    if ( m_replayRuntime.IsPresentationEnabled() )
    {
        const ReplayRecorderStats replayStats = m_replayRuntime.PresentationStats();
        printf( "[replay] Captured %llu physics samples, retained %llu/%llu, checkpoints %llu/%llu, "
                "latest_hash=0x%016llX\n",
                static_cast<unsigned long long>( replayStats.totalFramesCaptured ),
                static_cast<unsigned long long>( replayStats.sampleCount ),
                static_cast<unsigned long long>( replayStats.sampleCapacity ),
                static_cast<unsigned long long>( replayStats.checkpointCount ),
                static_cast<unsigned long long>( replayStats.checkpointCapacity ),
                static_cast<unsigned long long>( replayStats.latestStateHash ) );
    }
    if ( m_replayRuntime.SolverStats().enabled )
    {
        const ReplayRecorderStats replayStats = m_replayRuntime.SolverStats();
        printf( "[replay] Solver track captured %llu physics samples, retained %llu/%llu, checkpoints %llu/%llu, "
                "latest_solver_hash=0x%016llX\n",
                static_cast<unsigned long long>( replayStats.totalFramesCaptured ),
                static_cast<unsigned long long>( replayStats.sampleCount ),
                static_cast<unsigned long long>( replayStats.sampleCapacity ),
                static_cast<unsigned long long>( replayStats.checkpointCount ),
                static_cast<unsigned long long>( replayStats.checkpointCapacity ),
                static_cast<unsigned long long>( replayStats.latestStateHash ) );
    }

    // Hazard: backend resources can still be referenced by queued GPU work.
    // Flush before releasing the runtime's owning pointers so teardown cannot
    // free memory while the device is still reading it.
    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }

    // Lifetime: clean up backend-owned render resources while the current
    // backend is still alive. The world step now releases water GPU resources
    // without rebuilding; the flush keeps any already-submitted GPU work out of
    // teardown.
    ReleaseBackendOwnedRenderResources( "shutdown_release" );
}


void Run::ReleaseBackendOwnedRenderResources( const char* phaseName )
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
        SkyBox,
        LauncherLaser
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
        { "skybox", BackendResourceStep::SkyBox, false },
        { "launcher_laser", BackendResourceStep::LauncherLaser, false },
    };

    SkullbonezCore::Rendering::IRenderResourceFactory* releaseRenderResources = nullptr;
    if ( IsGfxReady() )
    {
        releaseRenderResources = &static_cast<SkullbonezCore::Rendering::IRenderResourceFactory&>( Gfx() );
    }

    for ( const BackendResourcePhase& phase : releaseSteps )
    {
        LogRenderResourceLifecycleStep( phaseName, phase.name );
        switch ( phase.step )
        {
        case BackendResourceStep::WorldEnvironment:
            m_cWorldEnvironment.ReleaseRenderResources();
            break;
        case BackendResourceStep::HelperResources:
            RenderHelper::ResetRenderResources( releaseRenderResources );
            break;
        case BackendResourceStep::GameModelResources:
            m_cGameModelCollection.ResetRenderResources();
            break;
        case BackendResourceStep::CollisionVisualizer:
            m_collisionVisualizer.ResetResources();
            break;
        case BackendResourceStep::UIResources:
            m_UI.ResetResources( releaseRenderResources );
            break;
        case BackendResourceStep::RenderPassResources:
            // Lifetime: shutdown can run after a failed backend init. Pass
            // resources still need their CPU-side handles reset, but dynamic
            // buffer destruction can only call into a live backend.
            m_renderer.ReleaseBackendOwnedResources( releaseRenderResources );
            break;
        case BackendResourceStep::ProfilerQueries:
#if defined( SKULLBONEZ_PROFILE_ENABLED )
            Profiler::Instance().InvalidateGpuQueries();
#endif
            break;
        case BackendResourceStep::TextureCollection:
            if ( m_systems.textures )
            {
                m_systems.textures->DeleteAllTextures();
                m_systems.textures->BindAssetSystem( nullptr );
                m_systems.textures->BindRenderContexts( nullptr, nullptr );
            }
            break;
        case BackendResourceStep::CameraCollection:
            if ( m_systems.cameras )
            {
                m_systems.cameras->Reset();
                m_systems.cameras->SetTerrain( nullptr );
            }
            break;
        case BackendResourceStep::SkyBox:
            if ( m_systems.skyBox )
            {
                m_systems.skyBox->ReleaseRenderResources();
                m_systems.skyBoxOwner.reset();
                m_systems.skyBox = nullptr;
            }
            break;
        case BackendResourceStep::LauncherLaser:
            m_runtimeTools.Laser().ResetResources();
            break;
        }

        if ( phase.flushAfter && IsGfxReady() )
        {
            LogRenderResourceLifecycleStep( phaseName, "flush_after_world_environment" );
            Gfx().FlushGPU();
        }
    }
}


void Run::RegisterBuiltInAssets()
{
    const EngineConfig& cfg = m_config;
    m_systems.assets
        .RegisterTextureSourceAsset( "texture.terrain", cfg.terrainTexture.c_str(), TEXTURE_GROUND, true, true, 3 );
    m_systems.assets.RegisterTextureSourceAsset( "texture.sphere",
                                                 cfg.sphereTexture.c_str(),
                                                 TEXTURE_BOUNDING_SPHERE,
                                                 true,
                                                 true,
                                                 3 );
    m_systems.assets
        .RegisterTextureSourceAsset( "texture.sky.left", cfg.skyLeft.c_str(), TEXTURE_SKY_LEFT, true, true, 3 );
    m_systems.assets
        .RegisterTextureSourceAsset( "texture.sky.right", cfg.skyRight.c_str(), TEXTURE_SKY_RIGHT, true, true, 3 );
    m_systems.assets
        .RegisterTextureSourceAsset( "texture.sky.front", cfg.skyFront.c_str(), TEXTURE_SKY_FRONT, true, true, 3 );
    m_systems.assets
        .RegisterTextureSourceAsset( "texture.sky.back", cfg.skyBack.c_str(), TEXTURE_SKY_BACK, true, true, 3 );
    m_systems.assets.RegisterTextureSourceAsset( "texture.sky.up", cfg.skyUp.c_str(), TEXTURE_SKY_UP, true, true, 3 );
    m_systems.assets
        .RegisterTextureSourceAsset( "texture.sky.down", cfg.skyDown.c_str(), TEXTURE_SKY_DOWN, true, true, 3 );

    m_systems.assets.RegisterAssetLibrarySourceAsset( "assetlib.low_poly_nature",
                                                      "assets/low_poly_nature.assets.json" );
    m_systems.assets.RegisterAssetLibrarySourceAsset( "assetlib.buildings", "assets/buildings.assets.json" );
    m_systems.assets.RegisterAssetLibrarySourceAsset( "assetlib.physics_props", "assets/physics_props.assets.json" );

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

    m_systems.assets.RegisterShaderSourceAsset( "shader.lit_textured",
                                                "shaders/lit_textured",
                                                Assets::ShaderProgramKind::LitTextured,
                                                contract( true, true, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.lit_textured_instanced",
                                                "shaders/lit_textured_instanced",
                                                Assets::ShaderProgramKind::LitTextured,
                                                contract( true, true, true, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.unlit_textured",
                                                "shaders/unlit_textured",
                                                Assets::ShaderProgramKind::UnlitTextured,
                                                contract( true, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.shadow_depth",
                                                "shaders/shadow_depth",
                                                Assets::ShaderProgramKind::ShadowDepth,
                                                contract( false, false, false, true, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.shadow_depth_instanced",
                                                "shaders/shadow_depth_instanced",
                                                Assets::ShaderProgramKind::ShadowDepth,
                                                contract( false, false, true, true, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.post_tonemap",
                                                "shaders/post_tonemap",
                                                Assets::ShaderProgramKind::PostProcess,
                                                contract( true, false, false, false, true ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.post_volumetric_light",
                                                "shaders/post_volumetric_light",
                                                Assets::ShaderProgramKind::PostProcess,
                                                contract( true, false, false, false, true ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.sky_atmosphere",
                                                "shaders/sky_atmosphere",
                                                Assets::ShaderProgramKind::PostProcess,
                                                contract( false, false, false, false, true ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.text",
                                                "shaders/text",
                                                Assets::ShaderProgramKind::Text,
                                                contract( true, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.solid_color",
                                                "shaders/solid_color",
                                                Assets::ShaderProgramKind::Text,
                                                contract( false, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.solid_color_batch",
                                                "shaders/solid_color_batch",
                                                Assets::ShaderProgramKind::Text,
                                                contract( false, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.water_calm",
                                                "shaders/water_calm",
                                                Assets::ShaderProgramKind::Water,
                                                contract( true, true, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.water_ocean",
                                                "shaders/water_ocean",
                                                Assets::ShaderProgramKind::Water,
                                                contract( true, true, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.collision_visualizer",
                                                "shaders/collision_visualizer",
                                                Assets::ShaderProgramKind::Collision,
                                                contract( false, true, true, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.grid_line",
                                                "shaders/grid_line",
                                                Assets::ShaderProgramKind::DebugLine,
                                                contract( false, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.launcher_laser",
                                                "shaders/launcher_laser",
                                                Assets::ShaderProgramKind::DebugLine,
                                                contract( false, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.tornado_fx",
                                                "shaders/tornado_fx",
                                                Assets::ShaderProgramKind::DebugLine,
                                                contract( false, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.ui_backdrop_blur",
                                                "shaders/UIBackdropBlur",
                                                Assets::ShaderProgramKind::UI,
                                                contract( true, false, false, false, true ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.reflect_rt",
                                                "shaders/reflect.rt",
                                                Assets::ShaderProgramKind::RayTracing,
                                                contract( true, false, false, false, false ) );
    m_systems.assets.RegisterShaderSourceAsset( "shader.generate_mips",
                                                "shaders/generate_mips",
                                                Assets::ShaderProgramKind::Compute,
                                                contract( true, false, false, false, false ) );
}


std::string Run::ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind kind,
                                         const char* logicalName,
                                         const std::string& relativePath )
{
    const SkullbonezCore::Assets::SourceAssetRecord& record =
        m_systems.assets.RegisterSourceAsset( kind, logicalName, relativePath.c_str() );
    return record.resolvedPath;
}


void Run::DumpTextureAssets( FILE* out ) const
{
    if ( m_systems.textures )
    {
        m_systems.textures->DumpTextureAssets( out );
    }
}


void Run::LogRenderResourceLifecycleStep( const char* phase, const char* step ) const
{
    const bool gfxReady = IsGfxReady();
    const int backendWidth = gfxReady ? Gfx().GetWidth() : 0;
    const int backendHeight = gfxReady ? Gfx().GetHeight() : 0;
    Log().WriteEventf( "render_resource_lifecycle phase=%s step=%s gfx_ready=%d backend_width=%d backend_height=%d "
                       "scene_index=%d load=%d",
                       phase ? phase : "unknown",
                       step ? step : "unknown",
                       gfxReady ? 1 : 0,
                       backendWidth,
                       backendHeight,
                       SceneState().currentSceneIndex,
                       SceneState().loadCount );
}


void Run::SetTimeScaleOverride( float scale )
{
    m_launchOptions.timeScaleOverride = scale;
}


void Run::SetFixedStepOverride()
{
    m_launchOptions.fixedStep = true;
}


void Run::SetSeedOverride( unsigned int seed )
{
    m_launchOptions.seedOverride = seed;
}


void Run::SetNoWaterOverride()
{
    m_launchOptions.noWater = true;
}


void Run::SetNoSleepOverride()
{
    m_launchOptions.noSleep = true;
    m_runtimeSettings.isPhysicsSleepEnabled = false;
    m_cGameModelCollection.SetPhysicsSleepEnabled( false );
}


void Run::SetNoContactAudioOverride()
{
    m_launchOptions.noContactAudio = true;
    m_contactAudio.SetEnabled( false );
}


void Run::SetTornadoOverride( bool enabled )
{
    m_launchOptions.hasTornadoOverride = true;
    m_launchOptions.tornadoEnabled = enabled;
    m_runtimeSettings.tornadoField.enabled = enabled;
    if ( m_runtimeSettings.tornadoVisual.autoEnableWithTornado )
    {
        m_runtimeSettings.tornadoVisual.enabled = enabled;
    }
    SyncTornadoRuntimeSettingsToPhysics( m_cGameModelCollection, m_runtimeSettings );
}


void Run::SetTornadoVectorFieldOverride( bool enabled )
{
    m_launchOptions.tornadoVectors = enabled;
    m_runtimeSettings.tornadoField.visualizeVelocityField = enabled;
    SyncTornadoRuntimeSettingsToPhysics( m_cGameModelCollection, m_runtimeSettings );
}


void Run::SetCinematicRenderingOverride( bool enabled )
{
    m_launchOptions.hasCinematicRenderingOverride = true;
    m_launchOptions.cinematicRendering = enabled;
}


void Run::SetCinematicShadowsOverride( bool enabled )
{
    m_launchOptions.hasCinematicShadowsOverride = true;
    m_launchOptions.cinematicShadows = enabled;
}


void Run::SetDemoHeroStyleOverride()
{
    m_launchOptions.demoHeroStyle = true;
}


void Run::SetInteractiveRunOverride()
{
    m_launchOptions.interactiveSceneRun = true;
}


void Run::SetFrameCountOverride( int frames )
{
    m_launchOptions.frameCountOverride = (std::max)( 1, frames );
}


void Run::SetUIStressOverride( unsigned int seed, int actionsPerFrame )
{
    m_launchOptions.uiStress = true;
    m_launchOptions.uiStressSeed = seed > 0 ? seed : 0x7F4A7C15u;
    m_launchOptions.uiStressActions = std::clamp( actionsPerFrame, 1, 32 );
}


void Run::SetGraphicsStressOverride( unsigned int seed,
                                     int actionsPerFrame,
                                     int sceneIntervalFrames,
                                     int memoryLogIntervalFrames )
{
    const unsigned int resolvedSeed = seed > 0 ? seed : 0xC11E2026u;
    m_launchOptions.graphicsStress = true;
    m_launchOptions.graphicsStressSeed = resolvedSeed;
    m_launchOptions.graphicsStressActions = std::clamp( actionsPerFrame, 1, 64 );
    m_launchOptions.graphicsStressSceneIntervalFrames = std::clamp( sceneIntervalFrames, 1, 600 );
    m_launchOptions.graphicsStressMemoryIntervalFrames = std::clamp( memoryLogIntervalFrames, 0, 36000 );
    m_launchOptions.interactiveSceneRun = true;

    m_graphicsStress.enabled = true;
    m_graphicsStress.randomState = resolvedSeed;
    m_graphicsStress.actionsPerFrame = m_launchOptions.graphicsStressActions;
    m_graphicsStress.sceneIntervalFrames = m_launchOptions.graphicsStressSceneIntervalFrames;
    m_graphicsStress.memoryLogIntervalFrames = m_launchOptions.graphicsStressMemoryIntervalFrames;
}


void Run::SetReplayRecording( bool enabled, int retentionSeconds, const char* hashLogPath )
{
    const ReplayRuntime::RecordingConfigResult replayConfig =
        m_replayRuntime.ConfigureRecording( enabled, retentionSeconds, hashLogPath );
    if ( m_replayRuntime.ResetScrubberState() )
    {
        ExitReplayInspectionCamera();
    }
    if ( replayConfig.presentationStats.enabled )
    {
        printf( "[replay] Capture enabled: retention_seconds=%d retention_frames=%llu checkpoint_interval_frames=%d "
                "solver_retention_frames=%llu solver_checkpoint_interval_frames=%d event_capacity=%llu%s%s%s%s\n",
                replayConfig.presentationConfig.retentionSeconds,
                static_cast<unsigned long long>( replayConfig.presentationStats.sampleCapacity ),
                replayConfig.presentationConfig.checkpointIntervalFrames,
                static_cast<unsigned long long>( replayConfig.solverStats.sampleCapacity ),
                replayConfig.solverConfig.checkpointIntervalFrames,
                static_cast<unsigned long long>( replayConfig.eventStats.eventCapacity ),
                replayConfig.presentationConfig.hashLogPath.empty() ? "" : " hash_log=",
                replayConfig.presentationConfig.hashLogPath.empty()
                    ? ""
                    : replayConfig.presentationConfig.hashLogPath.c_str(),
                replayConfig.solverConfig.hashLogPath.empty() ? "" : " solver_hash_log=",
                replayConfig.solverConfig.hashLogPath.empty() ? "" : replayConfig.solverConfig.hashLogPath.c_str() );
    }
}


void Run::SetMainMemoryDumpPath( const char* path )
{
    m_diagnosticsRuntime.SetMainMemoryDumpPath( path );
}

void Run::SetInteractionAutomation( const char* scriptPath, const char* reportPath )
{
    if ( !scriptPath || scriptPath[0] == '\0' )
    {
        throw std::runtime_error( "interaction automation requires a script path" );
    }

    m_interactionAutomation = RunInteractionAutomationState{};
    strcpy_s( m_interactionAutomation.scriptPath, sizeof( m_interactionAutomation.scriptPath ), scriptPath );
    if ( reportPath && reportPath[0] != '\0' )
    {
        strcpy_s( m_interactionAutomation.reportPath, sizeof( m_interactionAutomation.reportPath ), reportPath );
    }
    else
    {
        strcpy_s( m_interactionAutomation.reportPath,
                  sizeof( m_interactionAutomation.reportPath ),
                  "TestOutput\\interaction\\interaction_report.json" );
    }
    m_interactionAutomation.enabled = true;
    printf( "[interaction] Script: %s\n", m_interactionAutomation.scriptPath );
    printf( "[interaction] Report: %s\n", m_interactionAutomation.reportPath );
}


#ifdef _DEBUG
void Run::SetReplayScrubProbe( float normalized )
{
    m_replayScrubProbe.enabled = true;
    m_replayScrubProbe.completed = false;
    m_replayScrubProbe.normalized = std::clamp( normalized, 0.0f, 0.99f );
    printf( "[replay] Scrub probe enabled: normalized=%.3f\n", m_replayScrubProbe.normalized );
}

void Run::SetReplayRestoreProbe( float normalized )
{
    m_replayRestoreProbe.enabled = true;
    m_replayRestoreProbe.completed = false;
    m_replayRestoreProbe.normalized = std::clamp( normalized, 0.0f, 0.99f );
    printf( "[replay] Restore probe enabled: normalized=%.3f\n", m_replayRestoreProbe.normalized );
}

void Run::SetReplaySaveProbe( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        throw std::runtime_error( "replay save probe requires an output path" );
    }

    m_replaySaveProbe.enabled = true;
    m_replaySaveProbe.completed = false;
    strcpy_s( m_replaySaveProbe.path, sizeof( m_replaySaveProbe.path ), path );
    printf( "[replay] Save probe enabled: path=%s\n", m_replaySaveProbe.path );
}
#endif

bool Run::LoadReplayPresentationArtifact( const char* path, bool activateScrubber )
{
    if ( !path || path[0] == '\0' )
    {
        return false;
    }

    std::vector<ReplayPresentationSample> samples;
    ReplayV2LoadResult result;
    if ( !ReplayV2Artifact::LoadPresentation( path, samples, &result ) || samples.size() < 2 )
    {
        return false;
    }

    m_replayRuntime.LoadedPresentation() = RunLoadedReplayPresentationState{};
    m_replayRuntime.LoadedPresentation().samples.swap( samples );
    m_replayRuntime.LoadedPresentation().enabled = true;
    m_replayRuntime.LoadedPresentation().bodyDictionaryCount = result.bodyDictionaryCount;
    m_replayRuntime.LoadedPresentation().fileBytes = result.fileBytes;
    m_replayRuntime.LoadedPresentation().firstFrame = result.firstFrame;
    m_replayRuntime.LoadedPresentation().lastFrame = result.lastFrame;
    strncpy_s( m_replayRuntime.LoadedPresentation().path,
               sizeof( m_replayRuntime.LoadedPresentation().path ),
               path,
               _TRUNCATE );

    if ( activateScrubber )
    {
        if ( m_replayRuntime.Scrubber().liveAdvanceHeld )
        {
            m_replayRuntime.SetLiveAdvanceHeld( false );
        }
        CancelReplayToolDragState();

        m_replayRuntime.ClearCameraFocusForRestore();
        ExitReplayInspectionCamera();
        m_replayRuntime.ArmLoadedPresentationScrubber( 0.25f, m_timers.simulationTimer.GetTotalTime() );
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                            InteractionExitReason::EnterReplay );
        if ( m_replayRuntime.ShouldUseInspectionCamera() )
        {
            EnterReplayInspectionCamera();
        }
        else
        {
            ExitReplayInspectionCamera();
        }
    }

    printf( "[replay] Loaded v2 presentation artifact: path=%s samples=%llu bodies=%llu first_frame=%llu "
            "last_frame=%llu bytes=%llu\n",
            m_replayRuntime.LoadedPresentation().path,
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().samples.size() ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().bodyDictionaryCount ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().firstFrame ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().lastFrame ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().fileBytes ) );
    return true;
}


void Run::ResetReplayTimelineForActiveScene( bool preserveBranchMetadata )
{
    if ( !preserveBranchMetadata )
    {
        m_replayRuntime.ResetBranch();
    }
    CancelReplayToolDragState();
    if ( m_replayRuntime.Scrubber().liveAdvanceHeld )
    {
        m_replayRuntime.SetLiveAdvanceHeld( false );
    }
    if ( m_replayRuntime.ResetScrubberState() )
    {
        ExitReplayInspectionCamera();
    }
    m_replayRuntime.LoadedPresentation() = RunLoadedReplayPresentationState{};
    m_replayRuntime.ClearCameraFocusForRestore();
    ExitReplayInspectionCamera();
    m_replayRuntime.ClearPathVisualizerState();
    m_replayRuntime.VelocityEdit() = RunReplayVelocityEditState{};
    if ( !m_replayRuntime.IsPresentationEnabled() )
    {
        return;
    }

    const std::string* scenePath = m_sceneController.CurrentPath();
    const char* sceneLabel = scenePath && !scenePath->empty() ? scenePath->c_str() : "generated";
    m_replayRuntime.ResetTimeline( sceneLabel );
    m_replayRuntime.RecordEvent( ReplayEventKind::TimelineStart, 0, 0, 0, 0, 0, 0, 0, sceneLabel );
    if ( !( SceneState().isSceneMode && SceneState().solverBallCount <= 0 && SceneState().solverBoxCount <= 0 ) )
    {
        uint32_t flags = 0;
        flags |= ( SceneState().solverBallCount > 0 || SceneState().solverBoxCount > 0 )
                     ? REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS
                     : 0u;
        flags |= m_sceneController.UIOverrides().modelCountOverride >= 0 ? REPLAY_GENERATED_SCENE_UI_MODEL_COUNT : 0u;
        flags |= ( m_sceneController.UIOverrides().solverBallCountOverride >= 0 ||
                   m_sceneController.UIOverrides().solverBoxCountOverride >= 0 )
                     ? REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS
                     : 0u;
        flags |= ( static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride )
                   << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT ) &
                 REPLAY_GENERATED_SCENE_OVERRIDE_MASK;

        uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
        HashReplayInt( hash, SceneState().modelCount );
        HashReplayInt( hash, SceneState().solverBallCount );
        HashReplayInt( hash, SceneState().solverBoxCount );
        HashReplayInt( hash, static_cast<int32_t>( SceneState().rngSeed ) );
        HashReplayInt( hash, static_cast<int32_t>( ActiveGameModelCapacity() ) );
        HashReplayInt( hash, static_cast<int32_t>( m_launchOptions.generatedObjectTypeOverride ) );

        m_replayRuntime.RecordEvent( ReplayEventKind::GeneratedSceneConfig,
                                     0,
                                     flags,
                                     SceneState().modelCount,
                                     SceneState().solverBallCount,
                                     SceneState().solverBoxCount,
                                     static_cast<int32_t>( SceneState().rngSeed ),
                                     hash,
                                     "generated_scene_config" );
    }
    m_solverReplayMismatch.reports = 0;
    m_solverReplayMismatch.suppressed = false;
}


bool Run::ApplyReplaySolverSampleState( const ReplaySolverFrameSample& sample, char* outReason, std::size_t reasonSize )
{
    auto writeReason = [outReason, reasonSize]( const char* message )
    {
        if ( outReason && reasonSize > 0 )
        {
            strncpy_s( outReason, reasonSize, message ? message : "restore failed", _TRUNCATE );
        }
    };

    if ( sample.worldSnapshot.version < 1 || sample.worldSnapshot.version > 2 )
    {
        writeReason( "unsupported snapshot version" );
        return false;
    }

    if ( sample.worldSnapshot.modelCount != static_cast<int>( sample.bodies.size() ) )
    {
        writeReason( "snapshot body count mismatch" );
        return false;
    }

    const int liveModelCount = m_cGameModelCollection.GetModelCount();
    if ( sample.bodies.size() > static_cast<std::size_t>( liveModelCount ) )
    {
        writeReason( "selected frame needs unavailable bodies" );
        return false;
    }

    const int restoreModelCount = static_cast<int>( sample.bodies.size() );
    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.modelIndex < 0 || body.modelIndex >= liveModelCount || body.modelIndex >= restoreModelCount )
        {
            writeReason( "selected frame has invalid body index" );
            return false;
        }

        const GameObjects::GameModel* model = m_cGameModelCollection.TryGetModel( body.modelIndex );
        if ( !model || model->GetReplayBodyId() != body.id.value )
        {
            writeReason( "selected frame body ids no longer match" );
            return false;
        }
    }

    m_replayRuntime.RestoreRenderPose( m_cGameModelCollection );
    if ( !m_cGameModelCollection.TrimModelsForReplayRestore( restoreModelCount ) )
    {
        writeReason( "failed to trim live model list" );
        return false;
    }

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        Math::Orientation::Quaternion orientation( body.orientation[0],
                                                   body.orientation[1],
                                                   body.orientation[2],
                                                   body.orientation[3] );
        if ( !m_cGameModelCollection.TryRestoreReplayBodyState( body.modelIndex,
                                                                body.id.value,
                                                                body.fixed,
                                                                body.position,
                                                                orientation,
                                                                body.linearVelocity,
                                                                body.angularVelocity ) )
        {
            writeReason( "failed to restore replay body state" );
            return false;
        }
    }
    (void)m_cGameModelCollection.GetPhysicsBodyStore();
    m_cGameModelCollection.GetPhysicsEngine().ClearPendingBodyImpulses();

    if ( !m_cGameModelCollection.GetPhysicsEngine().RestoreReplaySolverSnapshot(
             sample.worldSnapshot,
             m_cGameModelCollection.GetModelCount() ) )
    {
        writeReason( "failed to restore solver world snapshot" );
        return false;
    }

    m_cWorldEnvironment.SetGravity( sample.world.gravity );
    m_cWorldEnvironment.SetFluidSurfaceHeight( sample.world.fluidHeight );
    m_cWorldEnvironment.SetFluidDensity( sample.world.fluidDensity );
    m_debug.isWaterHidden = sample.world.waterHidden;
    m_debug.isTerrainHidden = sample.world.terrainHidden;
    SceneState().isFixedStep = sample.world.fixedStep;
    SceneState().isScenePhysics = sample.world.scenePhysicsEnabled;
    SceneState().isSceneText = sample.world.sceneTextEnabled;
    SceneState().modelCount = m_cGameModelCollection.GetModelCount();
    m_runtimeSettings.isPhysicsSleepEnabled = sample.worldSnapshot.sleepEnabled;
    m_runtimeSettings.tornadoField = sample.worldSnapshot.tornadoConfig;
    m_runtimeSettings.tornadoSystem = sample.worldSnapshot.tornadoSystemConfig;
    if ( m_runtimeSettings.tornadoVisual.autoEnableWithTornado )
    {
        m_runtimeSettings.tornadoVisual.enabled =
            m_runtimeSettings.tornadoField.enabled || m_runtimeSettings.tornadoSystem.enabled;
    }

    if ( m_systems.cameras )
    {
        m_systems.cameras->CancelTween();
        m_systems.cameras->SetPrimaryPosition( sample.camera.eye );
        m_systems.cameras->SetViewCoordinates( sample.camera.view );
        m_systems.cameras->SetCamera();
    }

    m_runtimeTools.RestoreReplayLauncherVisualSample( sample.launcherVisual );
    writeReason( "applied" );
    return true;
}

bool Run::CaptureCurrentReplaySolverHash( const ReplaySolverFrameSample& reference,
                                          uint64_t& outSolverHash,
                                          uint64_t& outPresentationHash,
                                          std::size_t& outBodyCount )
{
    ReplayRecorderConfig config;
    config.enabled = true;
    config.retentionSeconds = 1;
    config.checkpointIntervalFrames = 1;

    ReplaySolverRecorder verifier;
    if ( !verifier.Configure( config ) )
    {
        return false;
    }

    ReplayLauncherVisualSample launcherVisual;
    m_runtimeTools.BuildReplayLauncherVisualSample( launcherVisual );

    ReplayCaptureInput input;
    input.branch = reference.branch;
    input.eventCursor = reference.eventCursor;
    input.sceneFrame = reference.sceneFrame;
    input.simulationSeconds = reference.simulationSeconds;
    input.physicsDt = reference.physicsDt > 0.0f ? reference.physicsDt : PHYSICS_FIXED_DT;
    input.fixedStep = SceneState().isFixedStep;
    input.scenePhysicsEnabled = SceneState().isScenePhysics;
    input.sceneTextEnabled = SceneState().isSceneText;
    input.waterHidden = m_debug.isWaterHidden;
    input.terrainHidden = m_debug.isTerrainHidden;
    input.cameras = m_systems.cameras;
    input.world = &m_cWorldEnvironment;
    input.models = &m_cGameModelCollection;
    input.launcherVisual = &launcherVisual;
    verifier.CaptureFrame( input );

    const ReplaySolverFrameSample* verified = verifier.LatestSample();
    if ( !verified )
    {
        return false;
    }

    outSolverHash = verified->solverHash;
    outPresentationHash = verified->presentationHash;
    outBodyCount = verified->bodies.size();
    return true;
}

bool Run::RestoreReplaySolverSampleAsLive( const ReplaySolverFrameSample& sample,
                                           char* outReason,
                                           std::size_t reasonSize )
{
    auto writeReason = [outReason, reasonSize]( const char* message )
    {
        if ( outReason && reasonSize > 0 )
        {
            strncpy_s( outReason, reasonSize, message ? message : "restore failed", _TRUNCATE );
        }
    };

    ReplaySolverFrameSample liveBackup;
    bool hasLiveBackup = false;
    if ( const ReplaySolverFrameSample* latest = m_replayRuntime.Solver().LatestSample() )
    {
        if ( latest->frameIndex != sample.frameIndex || latest->solverHash != sample.solverHash )
        {
            liveBackup = *latest;
            hasLiveBackup = true;
        }
    }

    char applyReason[128] = {};
    if ( !ApplyReplaySolverSampleState( sample, applyReason, sizeof( applyReason ) ) )
    {
        writeReason( applyReason[0] != '\0' ? applyReason : "restore apply failed" );
        return false;
    }

    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    const bool hashCaptured =
        CaptureCurrentReplaySolverHash( sample, restoredSolverHash, restoredPresentationHash, restoredBodyCount );
    const bool hashMatched = hashCaptured && restoredSolverHash == sample.solverHash;
    bool fallbackRestored = false;

    if ( !hashMatched && hasLiveBackup )
    {
        char fallbackReason[128] = {};
        fallbackRestored = ApplyReplaySolverSampleState( liveBackup, fallbackReason, sizeof( fallbackReason ) );
    }

#ifdef _DEBUG
    m_diagnosticsRuntime.LogReplayRestoreProbe( SceneState(),
                                                sample,
                                                restoredSolverHash,
                                                restoredPresentationHash,
                                                restoredBodyCount,
                                                hashCaptured,
                                                hashMatched,
                                                !hashMatched && hasLiveBackup,
                                                fallbackRestored );
#endif

    if ( !hashCaptured )
    {
        writeReason( "restore hash capture failed" );
        return false;
    }
    if ( !hashMatched )
    {
        writeReason( fallbackRestored ? "restore hash mismatch; live state restored"
                                      : "restore hash mismatch; fallback unavailable" );
        return false;
    }

    const uint32_t parentBranchId =
        sample.branch.branchId != 0
            ? sample.branch.branchId
            : ( m_replayRuntime.Branch().branchId != 0 ? m_replayRuntime.Branch().branchId : 1u );
    ReplayBranchInfo restoredBranch;
    restoredBranch.branchId = (std::max)( m_replayRuntime.Branch().branchId, parentBranchId ) + 1u;
    restoredBranch.parentBranchId = parentBranchId;
    restoredBranch.startFrame = 0;
    restoredBranch.sourceFrame = sample.frameIndex;
    restoredBranch.sourceSolverHash = sample.solverHash;
    m_replayRuntime.Branch() = restoredBranch;
    ResetReplayTimelineForActiveScene( true );
    m_replayRuntime.RecordEvent( ReplayEventKind::BranchRestore,
                                 0,
                                 0,
                                 static_cast<int32_t>( parentBranchId ),
                                 sample.sceneFrame,
                                 0,
                                 0,
                                 sample.solverHash,
                                 "hash-verified solver restore" );
    writeReason( "restored hash match" );
    return true;
}


void Run::SetInitialOverlayMode( OverlayMode mode )
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


void Run::SetTopTextHidden( bool hidden )
{
    m_debug.isTopTextHidden = hidden;
}


void Run::SetBroadphaseVisualizerEnabled( bool enabled )
{
    m_debug.isBroadphaseOverlay = enabled;
}


void Run::SetGeneratedObjectTypeOverride( GeneratedObjectTypeOverride objectTypeOverride )
{
    m_launchOptions.generatedObjectTypeOverride = objectTypeOverride;
}


void Run::SetPhysicsDebugFlagsOverride( uint32_t flags )
{
    m_launchOptions.hasPhysicsDebugFlagsOverride = true;
    m_launchOptions.physicsDebugFlagsOverride = flags & PHYSICS_DEBUG_ALL;
}


void Run::SetPhysicsDebugTransparentOverride( bool transparent )
{
    m_launchOptions.hasPhysicsDebugTransparentOverride = true;
    m_launchOptions.physicsDebugTransparentOverride = transparent;
}


void Run::SetPhysicsDebugAlphaOverride( float alpha )
{
    m_launchOptions.hasPhysicsDebugAlphaOverride = true;
    m_launchOptions.physicsDebugAlphaOverride = (std::max)( 0.05f, (std::min)( alpha, 1.0f ) );
}


void Run::SetPhysicsDebugContactLingerOverride( float seconds )
{
    m_launchOptions.hasPhysicsDebugContactLingerOverride = true;
    m_launchOptions.physicsDebugContactLingerOverride = (std::max)( 0.0f, (std::min)( seconds, 5.0f ) );
}


#ifdef _DEBUG
void Run::SetPhysicsRegressionLogOverride( const char* path )
{
    m_diagnosticsRuntime.SetPhysicsRegressionLogOverride( path );
}


void Run::SetPhysicsCollisionTimeLogOverride( const char* path )
{
    m_diagnosticsRuntime.SetPhysicsCollisionTimeLogOverride( path );
}


void Run::SetPhysicsDiagnosticsPath( const char* path, bool fixedStepForcedByDiagnostics )
{
    m_diagnosticsRuntime.SetPhysicsDiagnosticsPath( m_cGameModelCollection, path, fixedStepForcedByDiagnostics );
}
#endif


void Run::Initialise()
{
    assert( m_systems.window );

    IRenderBackend& renderBackend = Gfx();
    auto& renderResources = static_cast<SkullbonezCore::Rendering::IRenderResourceFactory&>( renderBackend );
    auto& renderCommands = static_cast<SkullbonezCore::Rendering::IRenderCommandContext&>( renderBackend );

    const char* rendererName = renderBackend.GetRendererName();
    char titleText[256];
    sprintf_s( titleText, "%s [%s] -- LOADING!!!", TITLE_TEXT, rendererName );
    m_systems.window->SetTitleText( titleText );
    const EngineConfig& cfg = m_config;

    m_systems.textures = &m_systems.textureCollection;
    m_systems.textures->BindAssetSystem( &m_systems.assets );
    m_systems.textures->BindRenderContexts( &renderResources, &renderCommands );
    RegisterBuiltInAssets();

    // Build renderer-owned resources from source asset records.
    RebuildRegisteredRenderResources();

    const std::string terrainRawPath =
        ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain, "terrain.raw", cfg.terrainRaw );
    m_systems.terrain =
        std::make_unique<Terrain>( terrainRawPath.c_str(), 256, 8, 15, m_config, m_systems.assets, renderResources );
    m_systems.isFlatSlopeTerrain = false;

    // Init SkyBox (m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax)
    m_systems.skyBoxOwner = std::make_unique<SkyBox>( -250, 300, -300, 300, -250, 300 );
    m_systems.skyBox = m_systems.skyBoxOwner.get();
    m_systems.skyBox->BindTextures( *m_systems.textures );
    m_systems.skyBox->BindRenderContexts( m_config, m_systems.assets, renderResources );
    m_systems.skyBox->ResetRenderResources();

    m_cWorldEnvironment = WorldEnvironment( cfg.fluidHeight, cfg.fluidDensity, cfg.gasDensity, cfg.gravity );
    m_cWorldEnvironment.BindRenderContexts( m_config, m_systems.assets, renderResources );
    XZBounds tb = m_systems.terrain->GetXZBounds();
    m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );

    // Init font (HDC, font)
    m_renderer.EnsureUiTextResources( renderResources, m_systems.assets, cfg.window.screenX, cfg.window.screenY );

    // Init cameras (shared across scenes, Reset() between loads)
    m_systems.cameras = &m_systems.cameraCollection;

    m_contactAudio.SetMasterGain( cfg.contactAudio.masterGain );
    m_contactAudio.SetMaxDistanceScale( cfg.contactAudio.maxDistanceScale );
    if ( !m_launchOptions.noContactAudio && cfg.contactAudio.enabled )
    {
        const bool audioReady =
            m_contactAudio.Initialize() &&
            m_contactAudio.LoadContactAudioMap( "SkullbonezData/audio/contact_audio.materials.json" );
        m_contactAudio.SetEnabled( audioReady );
    }
    else
    {
        m_contactAudio.SetEnabled( false );
    }

    LoadScene( 0 );
}


void Run::RunSceneLoadOnly( const char* snapshotOutPath )
{
    const int sceneCount = m_sceneController.QueueSize();
    if ( sceneCount <= 0 )
    {
        printf( "[scene-load-only] Exiting because --scene-load-only was requested, but no scenes were queued.\n" );
        fflush( stdout );
        return;
    }

    const bool writeSnapshot = snapshotOutPath && snapshotOutPath[0] != '\0';
    if ( writeSnapshot && sceneCount != 1 )
    {
        throw std::runtime_error( "--scene-snapshot-out requires exactly one loaded scene." );
    }

    printf( "[scene-load-only] Loaded 1/%d: %s\n",
            sceneCount,
            m_sceneController.PathAt( 0 ).empty() ? "generated" : m_sceneController.PathAt( 0 ).c_str() );
    if ( writeSnapshot )
    {
        const bool saved = m_cGameModelCollection.SaveSceneSnapshot( snapshotOutPath,
                                                                     SceneState().isScenePhysics,
                                                                     SceneState().isSceneText,
                                                                     m_cWorldEnvironment,
                                                                     m_systems.cameras->GetCameraTranslation(),
                                                                     m_systems.cameras->GetCameraView(),
                                                                     m_systems.cameras->GetCameraUp(),
                                                                     SceneState().isEditableScene,
                                                                     SceneState().isFixedStep,
                                                                     m_debug.isWaterHidden,
                                                                     m_debug.isTerrainHidden,
                                                                     SceneState().hasFlatSlope,
                                                                     SceneState().flatBaseY,
                                                                     SceneState().flatSlopeX,
                                                                     SceneState().flatSlopeZ );
        if ( !saved )
        {
            throw std::runtime_error( "Failed to write scene snapshot." );
        }
        printf( "[scene-load-only] Snapshot written: %s\n", snapshotOutPath );
    }
    for ( int i = 1; i < sceneCount; ++i )
    {
        LoadScene( i );
        printf( "[scene-load-only] Loaded %d/%d: %s\n",
                i + 1,
                sceneCount,
                m_sceneController.PathAt( i ).empty() ? "generated" : m_sceneController.PathAt( i ).c_str() );
    }
    // Why: --scene-load-only is expected to close immediately after loading;
    // make that intentional automation end visible in Profile and Debug logs.
    printf( "[scene-load-only] Exiting because --scene-load-only finished loading %d queued scene(s) without running "
            "frames.\n",
            sceneCount );
    fflush( stdout );
}


#ifdef _DEBUG
void Run::LogSceneFinished( const char* reason )
{
    const char* scenePath = "generated";
    const std::string* currentScenePath = m_sceneController.CurrentPath();
    if ( currentScenePath && !currentScenePath->empty() )
    {
        scenePath = currentScenePath->c_str();
    }

    m_diagnosticsRuntime.LogSceneFinished( SceneState(),
                                           scenePath,
                                           IsGfxReady() ? Gfx().GetRendererName() : "unknown",
                                           reason );
}


void Run::BeginPhysicsDiagnosticsRun( const char* scenePath )
{
    m_diagnosticsRuntime.BeginPhysicsDiagnosticsRun( m_cGameModelCollection,
                                                     SceneState(),
                                                     m_config,
                                                     scenePath,
                                                     IsGfxReady() ? Gfx().GetRendererName() : "unknown" );
}


void Run::EndPhysicsDiagnosticsRun( const char* status )
{
    m_diagnosticsRuntime.EndPhysicsDiagnosticsRun( SceneState(), status );
}
#endif
