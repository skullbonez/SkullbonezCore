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
#include "Replay/ReplayExporter.h"
#include "Replay/ReplayV2Artifact.h"
#include "RuntimeFileWriter.h"
#include "../UI/UIInput.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
constexpr uint32_t REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED = 1u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED = 2u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED = 4u;
constexpr uint32_t REPLAY_LAUNCHER_FIRE_PROJECTILE = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_FIXED = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_TERRAIN_ALIGN = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS = 1u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_MODEL_COUNT = 2u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS = 4u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT = 8u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_MASK = 3u << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;
constexpr uint64_t REPLAY_EVENT_FNV_OFFSET = 14695981039346656037ull;
constexpr uint64_t REPLAY_EVENT_FNV_PRIME = 1099511628211ull;

uint32_t ReplayFloatBits( float value )
{
    uint32_t bits = 0;
    static_assert( sizeof( bits ) == sizeof( value ), "Replay float payloads assume 32-bit floats." );
    std::memcpy( &bits, &value, sizeof( bits ) );
    return bits;
}

int32_t ReplayFloatBitsSigned( float value )
{
    const uint32_t bits = ReplayFloatBits( value );
    int32_t signedBits = 0;
    std::memcpy( &signedBits, &bits, sizeof( signedBits ) );
    return signedBits;
}

void HashReplayFloat( uint64_t& hash, float value )
{
    const uint32_t bits = ReplayFloatBits( value );
    for ( int shift = 0; shift < 32; shift += 8 )
    {
        hash ^= static_cast<uint64_t>( ( bits >> shift ) & 0xFFu );
        hash *= REPLAY_EVENT_FNV_PRIME;
    }
}

void HashReplayInt( uint64_t& hash, int32_t value )
{
    const uint32_t bits = static_cast<uint32_t>( value );
    for ( int shift = 0; shift < 32; shift += 8 )
    {
        hash ^= static_cast<uint64_t>( ( bits >> shift ) & 0xFFu );
        hash *= REPLAY_EVENT_FNV_PRIME;
    }
}

void AppendReplayFloatHex( char*& cursor, std::size_t& remaining, float value )
{
    if ( remaining == 0 )
    {
        return;
    }

    const int written = std::snprintf( cursor, remaining, "%08X", ReplayFloatBits( value ) );
    if ( written < 0 )
    {
        cursor[0] = '\0';
        return;
    }
    const std::size_t consumed = (std::min)( static_cast<std::size_t>( written ), remaining > 0 ? remaining - 1 : 0 );
    cursor += consumed;
    remaining -= consumed;
}

void AppendReplayVectorHex( char*& cursor, std::size_t& remaining, const Vector3& value )
{
    AppendReplayFloatHex( cursor, remaining, value.x );
    AppendReplayFloatHex( cursor, remaining, value.y );
    AppendReplayFloatHex( cursor, remaining, value.z );
}


void AppendReplayQuaternionHex( char*& cursor, std::size_t& remaining, const Quaternion& value )
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
    value.GetComponents( x, y, z, w );
    AppendReplayFloatHex( cursor, remaining, x );
    AppendReplayFloatHex( cursor, remaining, y );
    AppendReplayFloatHex( cursor, remaining, z );
    AppendReplayFloatHex( cursor, remaining, w );
}

std::string SolverReplayHashLogPath( const std::string& presentationPath )
{
    if ( presentationPath.empty() )
    {
        return {};
    }

    const std::size_t slash = presentationPath.find_last_of( "/\\" );
    const std::size_t dot = presentationPath.find_last_of( '.' );
    if ( dot != std::string::npos && ( slash == std::string::npos || dot > slash ) )
    {
        return presentationPath.substr( 0, dot ) + ".solver" + presentationPath.substr( dot );
    }
    return presentationPath + ".solver";
}

const ReplayPresentationSample*
LoadedPresentationSampleAtNormalized( const std::vector<ReplayPresentationSample>& samples, float normalized )
{
    if ( samples.empty() )
    {
        return nullptr;
    }

    const float t = std::clamp( normalized, 0.0f, 1.0f );
    const std::size_t maxOffset = samples.size() - 1;
    const std::size_t offset = (std::min)( maxOffset, static_cast<std::size_t>( t * maxOffset + 0.5f ) );
    return &samples[offset];
}
} // namespace


Run::Run( std::vector<std::string> sceneQueue )
    : m_sceneController( std::move( sceneQueue ) ), m_fullscreenQuadPass( *this ), m_skyPass( *this ),
      m_sceneTargetPass( *this ), m_shadowPass( *this ), m_reflectionPass( *this ), m_objectPass( *this ),
      m_terrainPass( *this ), m_waterPass( *this ), m_debugOverlayPass( *this ), m_volumetricPass( *this ),
      m_tonemapPass( *this ), m_uiTextPass( *this )
{
    BindEngineContext();
    RefreshRuntimeViewModel();
    RefreshSceneBrowserList();
    m_runtimeSettings.isVsyncEnabled = Cfg().runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = Cfg().runtimeRender.forcePipelineSync;
    m_defaultCinematicRender = Cfg().cinematicRender;
    m_startupGameModelCapacity = ActiveGameModelCapacity();
    m_startupWorkerThreads = Cfg().workerThreads;
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
                                                 &m_capture,
                                                 &m_diagnostics,
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
}


Run::~Run()
{
#ifdef _DEBUG
    EndPhysicsDiagnosticsRun( "process_end" );
#endif

    m_diagnostics.ClosePerfLog();
    m_replay.FlushHashLog();
    m_solverReplay.FlushHashLog();
    if ( m_replay.IsEnabled() )
    {
        const ReplayRecorderStats replayStats = m_replay.GetStats();
        printf( "[replay] Captured %llu physics samples, retained %llu/%llu, checkpoints %llu/%llu, "
                "latest_hash=0x%016llX\n",
                static_cast<unsigned long long>( replayStats.totalFramesCaptured ),
                static_cast<unsigned long long>( replayStats.sampleCount ),
                static_cast<unsigned long long>( replayStats.sampleCapacity ),
                static_cast<unsigned long long>( replayStats.checkpointCount ),
                static_cast<unsigned long long>( replayStats.checkpointCapacity ),
                static_cast<unsigned long long>( replayStats.latestStateHash ) );
    }
    if ( m_solverReplay.IsEnabled() )
    {
        const ReplayRecorderStats replayStats = m_solverReplay.GetStats();
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
    // backend is still alive. WorldEnvironment::ResetRenderResources() rebuilds
    // fluid meshes, records GPU upload commands, and leaves the DX12 command
    // list open. Flush immediately after that step so later releases cannot hit
    // "ID3D12Resource deleted before command list close" validation errors.
    ReleaseBackendOwnedRenderResources( "shutdown_release" );

    SkullbonezCore::Assets::BindActiveAssetSystem( nullptr );
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
        { "skybox_singleton", BackendResourceStep::SkyBox, false },
        { "launcher_laser", BackendResourceStep::LauncherLaser, false },
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
            RenderHelper::ResetRenderResources();
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
        case BackendResourceStep::LauncherLaser:
            m_launcherLaser.ResetResources();
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
    const EngineConfig& cfg = Cfg();
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


TextureCollection& Run::Textures()
{
    if ( !m_systems.textures )
    {
        throw std::runtime_error( "Texture collection is not initialised." );
    }
    return *m_systems.textures;
}


uint32_t Run::TextureHandle( uint32_t textureHash )
{
    return Textures().GetTextureHandle( textureHash );
}


void Run::SelectRenderTexture( uint32_t textureHash )
{
    Textures().SelectTexture( textureHash );
}


int Run::WindowScreenWidth() const
{
    return m_systems.window ? static_cast<int>( m_systems.window->m_sWindowDimensions.x ) : Cfg().window.screenX;
}


int Run::WindowScreenHeight() const
{
    return m_systems.window ? static_cast<int>( m_systems.window->m_sWindowDimensions.y ) : Cfg().window.screenY;
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
    m_cmdTimeScaleOverride = scale;
}


void Run::SetFixedStepOverride()
{
    m_cmdFixedStep = true;
}


void Run::SetSeedOverride( unsigned int seed )
{
    m_cmdSeedOverride = seed;
}


void Run::SetNoWaterOverride()
{
    m_cmdNoWater = true;
}


void Run::SetNoSleepOverride()
{
    m_cmdNoSleep = true;
    m_runtimeSettings.isPhysicsSleepEnabled = false;
    m_cGameModelCollection.SetPhysicsSleepEnabled( false );
}


void Run::SetTornadoOverride( bool enabled )
{
    m_cmdHasTornadoOverride = true;
    m_cmdTornadoEnabled = enabled;
    m_runtimeSettings.tornadoField.enabled = enabled;
    SyncTornadoFieldToPhysics();
}


void Run::SetTornadoVectorFieldOverride( bool enabled )
{
    m_cmdTornadoVectors = enabled;
    m_runtimeSettings.tornadoField.visualizeVelocityField = enabled;
    SyncTornadoFieldToPhysics();
}


void Run::SetCinematicRenderingOverride( bool enabled )
{
    m_cmdHasCinematicRenderingOverride = true;
    m_cmdCinematicRendering = enabled;
}


void Run::SetCinematicShadowsOverride( bool enabled )
{
    m_cmdHasCinematicShadowsOverride = true;
    m_cmdCinematicShadows = enabled;
}


void Run::SetDemoHeroStyleOverride()
{
    m_cmdDemoHeroStyle = true;
}


void Run::SetInteractiveRunOverride()
{
    m_cmdInteractiveSceneRun = true;
}


void Run::SetFrameCountOverride( int frames )
{
    m_cmdFrameCountOverride = (std::max)( 1, frames );
}


void Run::SetUIStressOverride( unsigned int seed, int actionsPerFrame )
{
    m_cmdUIStress = true;
    m_cmdUIStressSeed = seed > 0 ? seed : 0x7F4A7C15u;
    m_cmdUIStressActions = std::clamp( actionsPerFrame, 1, 32 );
}


void Run::SetReplayRecording( bool enabled, int retentionSeconds, const char* hashLogPath )
{
    ReplayRecorderConfig replayConfig;
    replayConfig.enabled = enabled || ( hashLogPath && hashLogPath[0] != '\0' );
    replayConfig.retentionSeconds = (std::max)( 1, retentionSeconds );
    replayConfig.checkpointIntervalFrames = 30;
    if ( hashLogPath && hashLogPath[0] != '\0' )
    {
        replayConfig.hashLogPath = hashLogPath;
    }

    m_replay.Configure( replayConfig );
    ReplayRecorderConfig solverReplayConfig = replayConfig;
    solverReplayConfig.checkpointIntervalFrames = 60;
    solverReplayConfig.hashLogPath = SolverReplayHashLogPath( replayConfig.hashLogPath );
    m_solverReplay.Configure( solverReplayConfig );
    m_replayEvents.Configure( replayConfig );
    ResetReplayScrubber();
    if ( m_replay.IsEnabled() )
    {
        const ReplayRecorderStats replayStats = m_replay.GetStats();
        const ReplayRecorderStats solverReplayStats = m_solverReplay.GetStats();
        const ReplayEventRecorderStats eventReplayStats = m_replayEvents.GetStats();
        printf( "[replay] Capture enabled: retention_seconds=%d retention_frames=%llu checkpoint_interval_frames=%d "
                "solver_retention_frames=%llu solver_checkpoint_interval_frames=%d event_capacity=%llu%s%s%s%s\n",
                replayConfig.retentionSeconds,
                static_cast<unsigned long long>( replayStats.sampleCapacity ),
                replayConfig.checkpointIntervalFrames,
                static_cast<unsigned long long>( solverReplayStats.sampleCapacity ),
                solverReplayConfig.checkpointIntervalFrames,
                static_cast<unsigned long long>( eventReplayStats.eventCapacity ),
                replayConfig.hashLogPath.empty() ? "" : " hash_log=",
                replayConfig.hashLogPath.empty() ? "" : replayConfig.hashLogPath.c_str(),
                solverReplayConfig.hashLogPath.empty() ? "" : " solver_hash_log=",
                solverReplayConfig.hashLogPath.empty() ? "" : solverReplayConfig.hashLogPath.c_str() );
    }
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

    m_loadedPresentationReplay = RunLoadedReplayPresentationState{};
    m_loadedPresentationReplay.samples.swap( samples );
    m_loadedPresentationReplay.enabled = true;
    m_loadedPresentationReplay.bodyDictionaryCount = result.bodyDictionaryCount;
    m_loadedPresentationReplay.fileBytes = result.fileBytes;
    m_loadedPresentationReplay.firstFrame = result.firstFrame;
    m_loadedPresentationReplay.lastFrame = result.lastFrame;
    strncpy_s( m_loadedPresentationReplay.path, sizeof( m_loadedPresentationReplay.path ), path, _TRUNCATE );

    if ( activateScrubber )
    {
        ArmLoadedReplayPresentationScrubber( 0.25f );
    }

    printf( "[replay] Loaded v2 presentation artifact: path=%s samples=%llu bodies=%llu first_frame=%llu "
            "last_frame=%llu bytes=%llu\n",
            m_loadedPresentationReplay.path,
            static_cast<unsigned long long>( m_loadedPresentationReplay.samples.size() ),
            static_cast<unsigned long long>( m_loadedPresentationReplay.bodyDictionaryCount ),
            static_cast<unsigned long long>( m_loadedPresentationReplay.firstFrame ),
            static_cast<unsigned long long>( m_loadedPresentationReplay.lastFrame ),
            static_cast<unsigned long long>( m_loadedPresentationReplay.fileBytes ) );
    return true;
}


void Run::ResetReplayTimelineForActiveScene( bool preserveBranchMetadata )
{
    if ( !preserveBranchMetadata )
    {
        m_replayBranch = ReplayBranchInfo();
    }
    if ( m_replayScrubber.simulationPaused )
    {
        SetReplaySimulationPaused( false );
    }
    ResetReplayScrubber();
    m_loadedPresentationReplay = RunLoadedReplayPresentationState{};
    ClearReplayPathVisualizer();
    if ( m_replayVelocityEdit.mouseCaptured )
    {
        UI::InputControl::EndMouseCapture();
    }
    m_replayVelocityEdit = RunReplayVelocityEditState{};
    if ( !m_replay.IsEnabled() )
    {
        return;
    }

    const std::string* scenePath = CurrentSceneQueuePath();
    const char* sceneLabel = scenePath && !scenePath->empty() ? scenePath->c_str() : "generated";
    m_replay.ResetTimeline( sceneLabel );
    m_solverReplay.ResetTimeline( sceneLabel );
    m_replayEvents.ResetTimeline( sceneLabel );
    RecordReplayEvent( ReplayEventKind::TimelineStart, 0, 0, 0, 0, 0, 0, 0, sceneLabel );
    RecordReplayGeneratedSceneConfigEvent();
    m_solverReplayMismatchReports = 0;
    m_solverReplayMismatchSuppressed = false;
}

ReplayFrameIndex Run::NextReplayEventFrameIndex() const
{
    const ReplayRecorderStats solverStats = m_solverReplay.GetStats();
    if ( solverStats.enabled )
    {
        return solverStats.nextFrameIndex;
    }

    const ReplayRecorderStats presentationStats = m_replay.GetStats();
    return presentationStats.nextFrameIndex;
}


void Run::RecordReplayEvent( ReplayEventKind kind,
                             ReplayFrameIndex frameIndex,
                             uint32_t flags,
                             int32_t value0,
                             int32_t value1,
                             int32_t value2,
                             int32_t value3,
                             uint64_t data0,
                             const char* text )
{
    if ( !m_replayEvents.IsEnabled() )
    {
        return;
    }

    ReplayEventInput input;
    input.frameIndex = frameIndex;
    input.branch = m_replayBranch;
    input.kind = kind;
    input.flags = flags;
    input.value0 = value0;
    input.value1 = value1;
    input.value2 = value2;
    input.value3 = value3;
    input.data0 = data0;
    input.text = text;
    m_replayEvents.RecordEvent( input );
}


void Run::RecordReplayWorldOverrideEvent( float previousGravity,
                                          float previousFluidHeight,
                                          float previousFluidDensity,
                                          float gravity,
                                          float fluidHeight,
                                          float fluidDensity )
{
    uint32_t flags = 0;
    flags |= previousGravity != gravity ? REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED : 0u;
    flags |= previousFluidHeight != fluidHeight ? REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED : 0u;
    flags |= previousFluidDensity != fluidDensity ? REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED : 0u;
    if ( flags == 0 )
    {
        return;
    }

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    HashReplayFloat( hash, gravity );
    HashReplayFloat( hash, fluidHeight );
    HashReplayFloat( hash, fluidDensity );

    RecordReplayEvent( ReplayEventKind::WorldOverride,
                       NextReplayEventFrameIndex(),
                       flags,
                       ReplayFloatBitsSigned( gravity ),
                       ReplayFloatBitsSigned( fluidHeight ),
                       ReplayFloatBitsSigned( fluidDensity ),
                       0,
                       hash,
                       "world_override" );
}


void Run::RecordReplayLauncherConfigEvent( uint32_t changedFlags )
{
    if ( changedFlags == 0 )
    {
        return;
    }

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    HashReplayFloat( hash, m_rayCastTest.impulseStrength );
    HashReplayFloat( hash, m_rayCastTest.projectileSpeed );

    RecordReplayEvent( ReplayEventKind::LauncherConfig,
                       NextReplayEventFrameIndex(),
                       changedFlags,
                       ReplayFloatBitsSigned( m_rayCastTest.impulseStrength ),
                       ReplayFloatBitsSigned( m_rayCastTest.projectileSpeed ),
                       0,
                       0,
                       hash,
                       "launcher_config" );
}


void Run::RecordReplayLauncherFireEvent( const Vector3& rayOrigin,
                                         const Vector3& rayDirection,
                                         const Vector3& cameraUp )
{
    char payload[96] = {};
    char* cursor = payload;
    std::size_t remaining = sizeof( payload );
    const int prefixWritten = std::snprintf( cursor, remaining, "ray9:" );
    if ( prefixWritten > 0 )
    {
        const std::size_t consumed =
            (std::min)( static_cast<std::size_t>( prefixWritten ), remaining > 0 ? remaining - 1 : 0 );
        cursor += consumed;
        remaining -= consumed;
    }
    AppendReplayVectorHex( cursor, remaining, rayOrigin );
    AppendReplayVectorHex( cursor, remaining, rayDirection );
    AppendReplayVectorHex( cursor, remaining, cameraUp );

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    HashReplayFloat( hash, rayOrigin.x );
    HashReplayFloat( hash, rayOrigin.y );
    HashReplayFloat( hash, rayOrigin.z );
    HashReplayFloat( hash, rayDirection.x );
    HashReplayFloat( hash, rayDirection.y );
    HashReplayFloat( hash, rayDirection.z );
    HashReplayFloat( hash, cameraUp.x );
    HashReplayFloat( hash, cameraUp.y );
    HashReplayFloat( hash, cameraUp.z );

    const bool projectile = m_rayCastTest.fireMode == RunLauncherFireMode::Projectile;
    const uint32_t flags = projectile ? REPLAY_LAUNCHER_FIRE_PROJECTILE : 0u;
    RecordReplayEvent( ReplayEventKind::LauncherFire,
                       NextReplayEventFrameIndex(),
                       flags,
                       projectile ? 1 : 0,
                       ReplayFloatBitsSigned( m_rayCastTest.impulseStrength ),
                       ReplayFloatBitsSigned( m_rayCastTest.projectileSpeed ),
                       m_cGameModelCollection.GetModelCount(),
                       hash,
                       payload );
}


void Run::RecordReplayGeneratedSceneConfigEvent()
{
    if ( SceneState().isSceneMode && SceneState().solverBallCount <= 0 && SceneState().solverBoxCount <= 0 )
    {
        return;
    }

    uint32_t flags = 0;
    flags |= ( SceneState().solverBallCount > 0 || SceneState().solverBoxCount > 0 )
                 ? REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS
                 : 0u;
    flags |= m_UIModelCountOverride >= 0 ? REPLAY_GENERATED_SCENE_UI_MODEL_COUNT : 0u;
    flags |= ( m_UISolverBallCountOverride >= 0 || m_UISolverBoxCountOverride >= 0 )
                 ? REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS
                 : 0u;
    flags |= ( static_cast<uint32_t>( m_generatedObjectTypeOverride ) << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT ) &
             REPLAY_GENERATED_SCENE_OVERRIDE_MASK;

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    HashReplayInt( hash, SceneState().modelCount );
    HashReplayInt( hash, SceneState().solverBallCount );
    HashReplayInt( hash, SceneState().solverBoxCount );
    HashReplayInt( hash, static_cast<int32_t>( SceneState().rngSeed ) );
    HashReplayInt( hash, static_cast<int32_t>( ActiveGameModelCapacity() ) );
    HashReplayInt( hash, static_cast<int32_t>( m_generatedObjectTypeOverride ) );

    RecordReplayEvent( ReplayEventKind::GeneratedSceneConfig,
                       0,
                       flags,
                       SceneState().modelCount,
                       SceneState().solverBallCount,
                       SceneState().solverBoxCount,
                       static_cast<int32_t>( SceneState().rngSeed ),
                       hash,
                       "generated_scene_config" );
}


void Run::RecordReplayEditorPlaceEvent( int objectType,
                                        bool fixedObject,
                                        bool terrainAlign,
                                        int modelCountBefore,
                                        const Vector3& terrainPoint,
                                        const Vector3& placementScale )
{
    char payload[64] = {};
    char* cursor = payload;
    std::size_t remaining = sizeof( payload );
    const int prefixWritten = std::snprintf( cursor, remaining, "place6:" );
    if ( prefixWritten > 0 )
    {
        const std::size_t consumed =
            (std::min)( static_cast<std::size_t>( prefixWritten ), remaining > 0 ? remaining - 1 : 0 );
        cursor += consumed;
        remaining -= consumed;
    }
    AppendReplayVectorHex( cursor, remaining, terrainPoint );
    AppendReplayVectorHex( cursor, remaining, placementScale );

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    HashReplayInt( hash, objectType );
    HashReplayInt( hash, fixedObject ? 1 : 0 );
    HashReplayInt( hash, terrainAlign ? 1 : 0 );
    HashReplayInt( hash, modelCountBefore );
    HashReplayFloat( hash, terrainPoint.x );
    HashReplayFloat( hash, terrainPoint.y );
    HashReplayFloat( hash, terrainPoint.z );
    HashReplayFloat( hash, placementScale.x );
    HashReplayFloat( hash, placementScale.y );
    HashReplayFloat( hash, placementScale.z );

    uint32_t flags = 0;
    flags |= fixedObject ? REPLAY_EDITOR_PLACE_FIXED : 0u;
    flags |= terrainAlign ? REPLAY_EDITOR_PLACE_TERRAIN_ALIGN : 0u;

    RecordReplayEvent( ReplayEventKind::EditorPlace,
                       NextReplayEventFrameIndex(),
                       flags,
                       objectType,
                       fixedObject ? 1 : 0,
                       terrainAlign ? 1 : 0,
                       modelCountBefore,
                       hash,
                       payload );
}


void Run::RecordReplayEditorTransformEvent( int modelIndex, uint32_t changedFlags, const GameModel& model )
{
    changedFlags &= REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE;
    if ( changedFlags == 0 )
    {
        return;
    }

    char payload[80] = {};
    char* cursor = payload;
    std::size_t remaining = sizeof( payload );
    const int prefixWritten = std::snprintf( cursor, remaining, "xform7:" );
    if ( prefixWritten > 0 )
    {
        const std::size_t consumed =
            (std::min)( static_cast<std::size_t>( prefixWritten ), remaining > 0 ? remaining - 1 : 0 );
        cursor += consumed;
        remaining -= consumed;
    }
    AppendReplayVectorHex( cursor, remaining, model.GetPosition() );
    AppendReplayQuaternionHex( cursor, remaining, model.GetOrientation() );

    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;
    model.GetOrientation().GetComponents( qx, qy, qz, qw );

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    HashReplayInt( hash, modelIndex );
    HashReplayInt( hash, static_cast<int32_t>( model.GetReplayBodyId() ) );
    HashReplayInt( hash, m_cGameModelCollection.GetModelCount() );
    HashReplayInt( hash, static_cast<int32_t>( changedFlags ) );
    HashReplayFloat( hash, model.GetPosition().x );
    HashReplayFloat( hash, model.GetPosition().y );
    HashReplayFloat( hash, model.GetPosition().z );
    HashReplayFloat( hash, qx );
    HashReplayFloat( hash, qy );
    HashReplayFloat( hash, qz );
    HashReplayFloat( hash, qw );

    RecordReplayEvent( ReplayEventKind::EditorTransform,
                       NextReplayEventFrameIndex(),
                       changedFlags,
                       modelIndex,
                       static_cast<int32_t>( model.GetReplayBodyId() ),
                       m_cGameModelCollection.GetModelCount(),
                       0,
                       hash,
                       payload );
}


bool Run::HasLoadedReplayPresentation() const
{
    return m_loadedPresentationReplay.enabled && m_loadedPresentationReplay.samples.size() >= 2;
}


const ReplayPresentationSample* Run::LoadedReplayPresentationSampleAtNormalized( float normalized ) const
{
    if ( !HasLoadedReplayPresentation() )
    {
        return nullptr;
    }

    return LoadedPresentationSampleAtNormalized( m_loadedPresentationReplay.samples, normalized );
}


const ReplayPresentationSample* Run::LoadedReplayPresentationLatestSample() const
{
    return HasLoadedReplayPresentation() ? &m_loadedPresentationReplay.samples.back() : nullptr;
}


void Run::ArmLoadedReplayPresentationScrubber( float normalized )
{
    if ( !HasLoadedReplayPresentation() )
    {
        return;
    }

    if ( m_replayScrubber.simulationPaused )
    {
        SetReplaySimulationPaused( false );
    }
    if ( m_replayVelocityEdit.mouseCaptured || m_replayScrubber.mouseCaptured )
    {
        UI::InputControl::EndMouseCapture();
    }

    ClearReplayPathVisualizer();
    m_replayPrediction.enabled = false;
    m_replayPrediction.horizonDragging = false;
    m_replayVelocityEdit = RunReplayVelocityEditState{};
    m_replayScrubber.activeTrack = RunReplayTrack::Presentation;
    ReplayScrubberSetTrackPosition( m_replayScrubber, RunReplayTrack::Presentation, normalized );
    m_replayScrubber.solverPosition = 1.0f;
    m_replayScrubber.dragging = false;
    m_replayScrubber.paused = true;
    m_replayScrubber.mouseCaptured = false;
    m_replayScrubber.visible = true;
    m_replayScrubber.visibleUntil = m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    UpdateReplayInspectionCamera();
}


void Run::ResetReplayScrubber()
{
    if ( m_replayCamera.active && !m_replayScrubber.simulationPaused )
    {
        ExitReplayInspectionCamera();
    }

    const bool leftWasDown = m_replayScrubber.leftWasDown;
    const bool restoreWasDown = m_replayScrubber.restoreWasDown;
    const bool restoreConsumedThisFrame = m_replayScrubber.restoreConsumedThisFrame;
    const bool simulationPaused = m_replayScrubber.simulationPaused;
    const bool pauseRestoreFlyMode = m_replayScrubber.pauseRestoreFlyMode;
    const bool pauseRestoreLauncherMode = m_replayScrubber.pauseRestoreLauncherMode;
    m_replayScrubber = RunReplayScrubberState{};
    m_replayScrubber.leftWasDown = leftWasDown;
    m_replayScrubber.restoreWasDown = restoreWasDown;
    m_replayScrubber.restoreConsumedThisFrame = restoreConsumedThisFrame;
    m_replayScrubber.simulationPaused = simulationPaused;
    m_replayScrubber.pauseRestoreFlyMode = pauseRestoreFlyMode;
    m_replayScrubber.pauseRestoreLauncherMode = pauseRestoreLauncherMode;
}


bool Run::ShouldRenderReplayScrubber() const
{
    if ( m_editor.editorModeEnabled || !m_UI.IsVisible() || !m_UI.IsMinimized() )
    {
        return false;
    }

    const bool loadedPresentation = HasLoadedReplayPresentation();
    const ReplayRecorderStats solverReplayStats = m_solverReplay.GetStats();
    const bool solverReplayAvailable = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2;
    return ( loadedPresentation || solverReplayAvailable ) &&
           ( m_replayScrubber.visible || m_replayScrubber.dragging || m_replayScrubber.paused ||
             m_replayScrubber.simulationPaused );
}


bool Run::IsReplayScrubPaused() const
{
    if ( !m_replayScrubber.paused )
    {
        return false;
    }

    if ( m_replayScrubber.activeTrack == RunReplayTrack::Presentation && HasLoadedReplayPresentation() )
    {
        return LoadedReplayPresentationSampleAtNormalized(
                   ReplayScrubberTrackPosition( m_replayScrubber, RunReplayTrack::Presentation ) ) != nullptr;
    }

    const float position = ReplayScrubberTrackPosition( m_replayScrubber, m_replayScrubber.activeTrack );
    if ( position >= REPLAY_SCRUBBER_LIVE_THRESHOLD )
    {
        return false;
    }

    if ( m_replayScrubber.activeTrack == RunReplayTrack::Presentation )
    {
        return m_replay.IsEnabled() && m_replay.SampleAtNormalized( position ) != nullptr;
    }

    return m_solverReplay.IsEnabled() && m_solverReplay.SampleAtNormalized( position ) != nullptr;
}


const ReplayPresentationSample* Run::CurrentReplayScrubSample() const
{
    if ( m_replayScrubber.activeTrack != RunReplayTrack::Presentation )
    {
        return nullptr;
    }

    if ( HasLoadedReplayPresentation() )
    {
        return m_replayScrubber.paused
                   ? LoadedReplayPresentationSampleAtNormalized(
                         ReplayScrubberTrackPosition( m_replayScrubber, RunReplayTrack::Presentation ) )
                   : nullptr;
    }

    if ( !IsReplayScrubPaused() )
    {
        return nullptr;
    }

    return m_replay.SampleAtNormalized( ReplayScrubberTrackPosition( m_replayScrubber, RunReplayTrack::Presentation ) );
}


const ReplaySolverFrameSample* Run::CurrentReplaySolverScrubSample() const
{
    if ( m_replayScrubber.activeTrack != RunReplayTrack::Solver || !IsReplayScrubPaused() )
    {
        return nullptr;
    }

    return m_solverReplay.SampleAtNormalized( ReplayScrubberTrackPosition( m_replayScrubber, RunReplayTrack::Solver ) );
}

bool Run::SaveReplayBufferFromScrubber( RunReplayTrack track )
{
    static int sReplaySeq = 0;
    static int sSolverReplaySeq = 0;

    char path[256] = {};
    bool saved = false;
    int& sequence = track == RunReplayTrack::Solver ? sSolverReplaySeq : sReplaySeq;
    const char* prefix = track == RunReplayTrack::Solver ? "solver_replay_" : "replay_v2_";
    if ( RuntimeFileWriter::NextNumberedPath( path, sizeof( path ), "replays", prefix, ".skreplay", sequence ) )
    {
        saved =
            track == RunReplayTrack::Solver
                ? ReplayExporter::Save( m_solverReplay, path )
                : ReplayV2Artifact::SavePresentationWithSolverHashes( m_replay, m_solverReplay, m_replayEvents, path );
    }

    const double now = m_timers.simulationTimer.GetTotalTime();
    m_replayScrubber.saveMessageTrack = track;
    if ( saved )
    {
        const char* fileName = strrchr( path, '\\' );
        if ( !fileName )
        {
            fileName = strrchr( path, '/' );
        }
        fileName = fileName ? fileName + 1 : path;
        sprintf_s( m_replayScrubber.saveMessage, sizeof( m_replayScrubber.saveMessage ), "SAVED %s", fileName );
    }
    else
    {
        sprintf_s( m_replayScrubber.saveMessage, sizeof( m_replayScrubber.saveMessage ), "REPLAY SAVE FAILED" );
    }
    m_replayScrubber.saveMessageUntil = now + 2.5;
    m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    m_replayScrubber.visible = true;
    return saved;
}


void Run::RestoreReplayLauncherVisualSample( const ReplayLauncherVisualSample& sample )
{
    m_rayCastTest.lines = {};
    const std::size_t lineCount = (std::min)( sample.rayLines.size(), RunRayCastTestState::MAX_LINES );
    for ( std::size_t i = 0; i < lineCount; ++i )
    {
        RunRayCastTestLine& line = m_rayCastTest.lines[i];
        line.start = sample.rayLines[i].start;
        line.end = sample.rayLines[i].end;
        line.ageSeconds = sample.rayLines[i].ageSeconds;
        line.active = sample.rayLines[i].active;
        line.hit = sample.rayLines[i].hit;
    }
    m_rayCastTest.nextLine = sample.nextRayLine % static_cast<int>( RunRayCastTestState::MAX_LINES );
    if ( m_rayCastTest.nextLine < 0 )
    {
        m_rayCastTest.nextLine += static_cast<int>( RunRayCastTestState::MAX_LINES );
    }
    m_rayCastTest.fireMode = sample.fireMode == ReplayLauncherFireMode::Projectile ? RunLauncherFireMode::Projectile
                                                                                   : RunLauncherFireMode::Laser;
    m_rayCastTest.visualizeRays = sample.visualizeRays;
    m_rayCastTest.impulseStrength = sample.impulseStrength;
    m_rayCastTest.projectileSpeed = sample.projectileSpeed;
    m_launcherLaser.RestoreShots( sample.laserShots, sample.nextLaserShot );
}


bool Run::ApplyReplaySolverSampleState( const ReplaySolverFrameSample& sample, char* outReason, std::size_t reasonSize )
{
    auto writeReason = [outReason, reasonSize]( const char* message )
    {
        if ( outReason && reasonSize > 0 )
        {
            sprintf_s( outReason, reasonSize, "%s", message ? message : "restore failed" );
        }
    };

    if ( sample.worldSnapshot.version != 1 )
    {
        writeReason( "unsupported snapshot version" );
        return false;
    }

    if ( sample.worldSnapshot.modelCount != static_cast<int>( sample.bodies.size() ) )
    {
        writeReason( "snapshot body count mismatch" );
        return false;
    }

    std::vector<GameObjects::GameModel>& models = m_cGameModelCollection.PhysicsModels();
    if ( sample.bodies.size() > models.size() )
    {
        writeReason( "selected frame needs unavailable bodies" );
        return false;
    }

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.modelIndex < 0 || body.modelIndex >= static_cast<int>( models.size() ) )
        {
            writeReason( "selected frame has invalid body index" );
            return false;
        }

        const GameObjects::GameModel& model = models[static_cast<std::size_t>( body.modelIndex )];
        if ( model.GetReplayBodyId() != body.id.value )
        {
            writeReason( "selected frame body ids no longer match" );
            return false;
        }
    }

    RestoreReplayPresentationRenderPose();
    const int restoreModelCount = static_cast<int>( sample.bodies.size() );
    if ( !m_cGameModelCollection.TrimModelsForReplayRestore( restoreModelCount ) )
    {
        writeReason( "failed to trim live model list" );
        return false;
    }

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        GameObjects::GameModel& model = models[static_cast<std::size_t>( body.modelIndex )];
        Math::Orientation::Quaternion orientation( body.orientation[0],
                                                   body.orientation[1],
                                                   body.orientation[2],
                                                   body.orientation[3] );
        orientation.Normalise();
        model.SetFixed( body.fixed );
        model.SetPosition( body.position );
        model.SetOrientation( orientation );
        model.SetLinearVelocity( body.linearVelocity );
        model.SetAngularVelocity( body.angularVelocity );
        model.ClearImpulseForce();
    }

    if ( !m_cGameModelCollection.RestoreReplaySolverWorldSnapshot( sample.worldSnapshot ) )
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

    if ( m_systems.cameras )
    {
        m_systems.cameras->CancelTween();
        m_systems.cameras->SetPrimaryPosition( sample.camera.eye );
        m_systems.cameras->SetViewCoordinates( sample.camera.view );
        m_systems.cameras->SetCamera();
    }

    RestoreReplayLauncherVisualSample( sample.launcherVisual );
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
    BuildReplayLauncherVisualSample( launcherVisual );

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
            sprintf_s( outReason, reasonSize, "%s", message ? message : "restore failed" );
        }
    };

    ReplaySolverFrameSample liveBackup;
    bool hasLiveBackup = false;
    if ( const ReplaySolverFrameSample* latest = m_solverReplay.LatestSample() )
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
    RuntimeDiagnostics::LogReplayRestoreProbe( m_diagnostics.PhysicsDiagnostics(),
                                               SceneState(),
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

    const uint32_t parentBranchId = sample.branch.branchId != 0
                                        ? sample.branch.branchId
                                        : ( m_replayBranch.branchId != 0 ? m_replayBranch.branchId : 1u );
    ReplayBranchInfo restoredBranch;
    restoredBranch.branchId = (std::max)( m_replayBranch.branchId, parentBranchId ) + 1u;
    restoredBranch.parentBranchId = parentBranchId;
    restoredBranch.startFrame = 0;
    restoredBranch.sourceFrame = sample.frameIndex;
    restoredBranch.sourceSolverHash = sample.solverHash;
    m_replayBranch = restoredBranch;
    ResetReplayTimelineForActiveScene( true );
    RecordReplayEvent( ReplayEventKind::BranchRestore,
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
    m_generatedObjectTypeOverride = objectTypeOverride;
}


void Run::SetPhysicsDebugFlagsOverride( uint32_t flags )
{
    m_cmdHasPhysicsDebugFlagsOverride = true;
    m_cmdPhysicsDebugFlagsOverride = flags & PHYSICS_DEBUG_ALL;
}


void Run::SetPhysicsDebugTransparentOverride( bool transparent )
{
    m_cmdHasPhysicsDebugTransparentOverride = true;
    m_cmdPhysicsDebugTransparentOverride = transparent;
}


void Run::SetPhysicsDebugAlphaOverride( float alpha )
{
    m_cmdHasPhysicsDebugAlphaOverride = true;
    m_cmdPhysicsDebugAlphaOverride = (std::max)( 0.05f, (std::min)( alpha, 1.0f ) );
}


void Run::SetPhysicsDebugContactLingerOverride( float seconds )
{
    m_cmdHasPhysicsDebugContactLingerOverride = true;
    m_cmdPhysicsDebugContactLingerOverride = (std::max)( 0.0f, (std::min)( seconds, 5.0f ) );
}


#ifdef _DEBUG
void Run::SetPhysicsRegressionLogOverride( const char* path )
{
    RuntimeDiagnostics::SetPhysicsRegressionLogOverride( m_diagnostics.PerfLog(), path );
}


void Run::SetPhysicsCollisionTimeLogOverride( const char* path )
{
    RuntimeDiagnostics::SetPhysicsCollisionTimeLogOverride( m_diagnostics.PerfLog(), path );
}


void Run::SetPhysicsDiagnosticsPath( const char* path, bool fixedStepForcedByDiagnostics )
{
    RuntimeDiagnostics::SetPhysicsDiagnosticsPath( m_diagnostics.PhysicsDiagnostics(),
                                                   m_cGameModelCollection,
                                                   path,
                                                   fixedStepForcedByDiagnostics );
}
#endif


void Run::Initialise()
{
    m_systems.window = Window::Instance();

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

    const std::string terrainRawPath =
        ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain, "terrain.raw", Cfg().terrainRaw );
    m_systems.terrain = std::make_unique<Terrain>( terrainRawPath.c_str(), 256, 8, 15 );
    m_systems.isFlatSlopeTerrain = false;

    // Init SkyBox (m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax)
    m_systems.skyBox = SkyBox::Instance( -250, 300, -300, 300, -250, 300 );
    m_systems.skyBox->ResetRenderResources();

    {
        const EngineConfig& cfg = Cfg();
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


void Run::RunSceneLoadOnly( const char* snapshotOutPath )
{
    const int sceneCount = m_sceneController.QueueSize();
    if ( sceneCount <= 0 )
    {
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
}


#ifdef _DEBUG
void Run::LogSceneFinished( const char* reason )
{
    const char* scenePath = "generated";
    const std::string* currentScenePath = CurrentSceneQueuePath();
    if ( currentScenePath && !currentScenePath->empty() )
    {
        scenePath = currentScenePath->c_str();
    }

    RuntimeDiagnostics::LogSceneFinished( SceneState(),
                                          scenePath,
                                          IsGfxReady() ? Gfx().GetRendererName() : "unknown",
                                          reason );
}


void Run::BeginPhysicsDiagnosticsRun( const char* scenePath )
{
    RuntimeDiagnostics::BeginPhysicsDiagnosticsRun( m_diagnostics.PhysicsDiagnostics(),
                                                    m_cGameModelCollection,
                                                    SceneState(),
                                                    Cfg(),
                                                    scenePath,
                                                    IsGfxReady() ? Gfx().GetRendererName() : "unknown" );
}


void Run::EndPhysicsDiagnosticsRun( const char* status )
{
    RuntimeDiagnostics::EndPhysicsDiagnosticsRun( m_diagnostics.PhysicsDiagnostics(), SceneState(), status );
}
#endif
