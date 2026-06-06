// --- Includes ---
#include "SkullbonezRun.h"
#include "SkullbonezHelper.h"
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezRenderBackendGL.h"
#include "SkullbonezRenderBackendDX11.h"
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezCollisionResponse.h"
#include "SkullbonezImpulseSolver.h"
#include <time.h>
#include <cstring>
#include <cstddef>
#include <psapi.h>
#include <cmath>
#include <dwmapi.h>
#include <fstream>
#pragma comment( lib, "dwmapi.lib" )

// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;

namespace
{
constexpr double PERF_TEST_PASS_SECONDS = 2.0;
constexpr float WATER_HEIGHT_CONTROL_SPEED = 20.0f;
constexpr float NO_WATER_TERRAIN_CLEARANCE = 100.0f;
#ifdef _DEBUG
constexpr const char* NUDGE_REPRO_SNAPSHOT_PATH = "Debug/nudge_repro_snapshots.txt";
constexpr double NUDGE_REPRO_MESSAGE_SECONDS = 3.0;
#endif

#ifdef _DEBUG
std::string JsonEscape( const char* value )
{
    std::string escaped;
    if ( !value )
    {
        return escaped;
    }

    for ( const char* p = value; *p != '\0'; ++p )
    {
        switch ( *p )
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += *p;
            break;
        }
    }
    return escaped;
}
#endif

void DrawUiTestPattern( int screenW, int screenH )
{
    const UiDrawContext draw( screenW, screenH );
    draw.Rect( 0.0f, 0.0f, static_cast<float>( screenW ), static_cast<float>( screenH ), 0.20f, 0.31f, 0.36f, 1.0f );

    constexpr float tile = 88.0f;
    for ( float y = 0.0f; y < static_cast<float>( screenH ); y += tile )
    {
        for ( float x = 0.0f; x < static_cast<float>( screenW ); x += tile )
        {
            const int ix = static_cast<int>( x / tile );
            const int iy = static_cast<int>( y / tile );
            const bool alternate = ( ( ix + iy ) & 1 ) != 0;
            if ( alternate )
            {
                draw.Rect( x, y, tile, tile, 0.10f, 0.78f, 0.96f, 0.96f );
            }
            else
            {
                draw.Rect( x, y, tile, tile, 1.0f, 0.72f, 0.18f, 0.94f );
            }
            draw.Rect( x + 12.0f, y + 12.0f, tile - 24.0f, 5.0f, 0.96f, 0.98f, 1.0f, 0.74f );
            draw.Rect( x + tile - 18.0f, y + 18.0f, 5.0f, tile - 32.0f, 0.12f, 0.20f, 0.24f, 0.54f );
        }
    }

    draw.Rect( 44.0f, 46.0f, 780.0f, 560.0f, 1.0f, 1.0f, 1.0f, 0.18f );
    draw.Rect( 76.0f, 116.0f, 720.0f, 8.0f, 0.98f, 0.12f, 0.46f, 0.82f );
    draw.Rect( 76.0f, 300.0f, 720.0f, 8.0f, 0.30f, 1.0f, 0.56f, 0.78f );
    draw.Rect( 76.0f, 484.0f, 720.0f, 8.0f, 0.38f, 0.54f, 1.0f, 0.82f );
    Text2d::FlushQuads();
}


bool SceneDirectiveMatches( const std::string& line, const char* key )
{
    const size_t keyLen = strlen( key );
    if ( line.compare( 0, keyLen, key ) != 0 )
    {
        return false;
    }
    return line.size() == keyLen || line[keyLen] == ' ' || line[keyLen] == '\t';
}


bool IsSceneBodyDirective( const std::string& line )
{
    return SceneDirectiveMatches( line, "camera" ) ||
           SceneDirectiveMatches( line, "ball" ) ||
           SceneDirectiveMatches( line, "box" ) ||
           SceneDirectiveMatches( line, "floating_box" ) ||
           SceneDirectiveMatches( line, "ball_state" );
}


size_t SceneDefaultInsertIndex( const std::vector<std::string>& lines )
{
    for ( size_t i = 0; i < lines.size(); ++i )
    {
        if ( IsSceneBodyDirective( lines[i] ) )
        {
            return i;
        }
    }
    return lines.size();
}


void SetSceneDirective( std::vector<std::string>& lines, const char* key, const std::string& value, bool includeDirective )
{
    bool replaced = false;
    for ( size_t i = 0; i < lines.size(); )
    {
        if ( SceneDirectiveMatches( lines[i], key ) )
        {
            if ( includeDirective && !replaced )
            {
                lines[i] = value;
                replaced = true;
                ++i;
            }
            else
            {
                lines.erase( lines.begin() + static_cast<std::ptrdiff_t>( i ) );
            }
            continue;
        }
        ++i;
    }

    if ( includeDirective && !replaced )
    {
        lines.insert( lines.begin() + static_cast<std::ptrdiff_t>( SceneDefaultInsertIndex( lines ) ), value );
    }
}


const char* OnOff( bool value )
{
    return value ? "on" : "off";
}


const char* WaterReflectionDirectiveValue( bool noReflect, bool rtReflect )
{
    if ( noReflect )
    {
        return "none";
    }
    return rtReflect ? "dxr" : "fbo";
}


const char* FileNameFromPath( const char* path )
{
    if ( !path )
    {
        return "";
    }

    const char* slash = strrchr( path, '/' );
    const char* backslash = strrchr( path, '\\' );
    const char* separator = slash;
    if ( backslash && ( !separator || backslash > separator ) )
    {
        separator = backslash;
    }
    return separator ? separator + 1 : path;
}
} // namespace


SkullbonezRun::SkullbonezRun( std::vector<std::string> sceneQueue, bool legacyPhysics )
    : m_sceneQueue( std::move( sceneQueue ) )
{
    m_cGameModelCollection.SetLegacyMode( legacyPhysics );
    // Config-driven defaults are resolved at construction; scene-specific defaults remain in-member.
    m_runtimeSettings.isVsyncEnabled = Cfg().runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = Cfg().runtimeRender.forcePipelineSync;
    m_runtimeSettings.defaultRollAlignEnabled = Cfg().rollAlignEnabled;
    m_runtimeSettings.isRollAlignEnabled = m_runtimeSettings.defaultRollAlignEnabled;
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
    m_ui.ResetResources();
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


void SkullbonezRun::SetInitialOverlayMode( OverlayMode mode )
{
    m_debug.overlayMode = mode;
    m_ui.SetVisible( mode != OverlayMode::None );
    switch ( mode )
    {
    case OverlayMode::SceneStats:
        m_ui.SetActiveTab( InGameUiTab::Scene );
        break;
    case OverlayMode::Keys:
        m_ui.SetActiveTab( InGameUiTab::Keys );
        break;
    case OverlayMode::BarsNormalized:
    case OverlayMode::BarsAbsolute:
    case OverlayMode::Timers:
        m_ui.SetActiveTab( InGameUiTab::Profiler );
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
    if ( objectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
    {
        m_cGameModelCollection.SetLegacyMode( false );
    }
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

    // Init font (HDC, font)
    Text2d::BuildFont( "Verdana" );

    // Init cameras singleton (shared across scenes, Reset() between loads)
    m_systems.cameras = CameraCollection::Instance();

    // Load the first scene
    LoadScene( 0 );
}


RuntimeRendererType SkullbonezRun::GetCurrentRendererType() const
{
    const char* rendererName = Gfx().GetRendererName();
    if ( strstr( rendererName, "12" ) )
    {
        return RuntimeRendererType::DX12;
    }
    if ( strstr( rendererName, "11" ) )
    {
        return RuntimeRendererType::DX11;
    }
    return RuntimeRendererType::OpenGL;
}


RuntimeRendererType SkullbonezRun::GetNextRendererType( RuntimeRendererType current ) const
{
    switch ( current )
    {
    case RuntimeRendererType::OpenGL:
        return RuntimeRendererType::DX11;
    case RuntimeRendererType::DX11:
        return RuntimeRendererType::DX12;
    case RuntimeRendererType::DX12:
    default:
        return RuntimeRendererType::OpenGL;
    }
}


void SkullbonezRun::SwitchRenderer( RuntimeRendererType target )
{
    if ( !m_systems.window || !IsGfxReady() )
    {
        return;
    }

    RuntimeRendererType current = GetCurrentRendererType();
    if ( current == target )
    {
        return;
    }

    // --- Phase 1: Tear down the current backend ---
    // All GPU-visible resources must be released while the backend that owns them is still alive.
    // The backend's FlushGPU() ensures all in-flight GPU work completes before resource destruction.
    Gfx().FlushGPU();
    if ( m_systems.reflectionFBO )
    {
        m_systems.reflectionFBO->ResetResources();
        m_systems.reflectionFBO.reset();
    }
    Text2d::DeleteFont();
    m_cGameModelCollection.ResetGLResources();
    SkullbonezHelper::ResetGLResources();
    m_collisionVisualizer.ResetResources();
    m_ui.ResetResources();
    if ( m_systems.textures )
    {
        m_systems.textures->DeleteAllTextures();
    }
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    Profiler::Instance().InvalidateGpuQueries();
#endif

    // Determine if the outgoing backend used a DXGI flip-model swap chain.
    // DXGI swap chains take exclusive ownership of the HWND's DWM composition surface;
    // GDI rendering (used by OpenGL's SwapBuffers) is disabled while DXGI owns the window.
    bool outgoingWasDXGI = ( current == RuntimeRendererType::DX11 || current == RuntimeRendererType::DX12 );

    // Destroy the backend — its destructor calls Shutdown() which releases all API objects
    // including the swap chain.
    DestroyGfxBackend();

    // --- Phase 2: Reset the window surface between DXGI and GDI ownership ---
    // DXGI flip-model swap chains (DXGI_SWAP_EFFECT_FLIP_DISCARD) permanently taint a window's
    // GDI presentation surface. After a DXGI swap chain has been on an HWND, SwapBuffers
    // (used by OpenGL) can no longer present to the screen — it succeeds but DWM continues
    // compositing the stale DXGI surface. No amount of DwmFlush/SetWindowPos/DC re-acquire
    // fixes this; the taint is permanent for the lifetime of that HWND.
    //
    // The only reliable fix is to destroy the HWND and create a fresh one. The new window gets
    // a clean GDI surface that SwapBuffers can present to normally.
    if ( outgoingWasDXGI && target == RuntimeRendererType::OpenGL )
    {
        // Delete the old WGL context — it was bound to the old window's DC.
        if ( m_systems.window->m_sRenderContext )
        {
            wglMakeCurrent( nullptr, nullptr );
            wglDeleteContext( m_systems.window->m_sRenderContext );
            m_systems.window->m_sRenderContext = nullptr;
        }

        // Recreate the window entirely (new HWND, new DC, clean GDI surface)
        m_systems.window->RecreateWindow();
    }
    else if ( outgoingWasDXGI )
    {
        // Switching between DXGI backends (DX11↔DX12): DwmFlush is sufficient since both
        // use DXGI swap chains and don't rely on GDI surfaces.
        DwmFlush();
    }

    // --- Phase 3: Create and initialise the new backend ---
    auto makeBackend = [&]( RuntimeRendererType type ) -> std::unique_ptr<IRenderBackend>
    {
        if ( type == RuntimeRendererType::OpenGL )
        {
            // After Phase 2 recreated the window, we need a fresh WGL context on the new HWND.
            // If we're just cycling GL→GL (shouldn't happen) or DX→DX→GL, the context was
            // already destroyed in Phase 2.
            if ( !m_systems.window->m_sRenderContext )
            {
                m_systems.window->InitialiseOpenGL();
            }
            else
            {
                wglMakeCurrent( m_systems.window->m_sDevice, m_systems.window->m_sRenderContext );
            }
            return std::make_unique<RenderBackendGL>();
        }

        // DX backends: deactivate GL context so DXGI can take exclusive window ownership.
        if ( m_systems.window->m_sRenderContext )
        {
            wglMakeCurrent( nullptr, nullptr );
        }

        if ( type == RuntimeRendererType::DX12 )
        {
            return std::make_unique<RenderBackendDX12>();
        }

        return std::make_unique<RenderBackendDX11>();
    };

    auto initialiseBackend = [&]( RuntimeRendererType type ) -> std::unique_ptr<IRenderBackend>
    {
        std::unique_ptr<IRenderBackend> backend = makeBackend( type );
        if ( !backend->Init( m_systems.window->m_sWindow,
                             m_systems.window->m_sDevice,
                             m_systems.window->m_sWindowDimensions.x,
                             m_systems.window->m_sWindowDimensions.y ) )
        {
            throw std::runtime_error( "Renderer backend initialisation failed." );
        }
        return backend;
    };

    std::string switchFailureReason;
    try
    {
        std::unique_ptr<IRenderBackend> backend = initialiseBackend( target );
        SetGfxBackend( std::move( backend ) );
    }
    catch ( const std::exception& e )
    {
        switchFailureReason = e.what();
        try
        {
            std::unique_ptr<IRenderBackend> rollbackBackend = initialiseBackend( current );
            SetGfxBackend( std::move( rollbackBackend ) );
        }
        catch ( const std::exception& rollbackError )
        {
            char msg[512];
            sprintf_s( msg,
                       sizeof( msg ),
                       "Renderer switch failed (target: %s, rollback: %s).",
                       switchFailureReason.c_str(),
                       rollbackError.what() );
            throw std::runtime_error( msg );
        }
    }

    // --- Phase 4: Rebuild render resources ---
    m_systems.window->HandleScreenResize();
    Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );

    SetInitialOpenGlState();
    if ( m_systems.terrain )
    {
        m_systems.terrain->ResetRenderResources();
    }
    if ( m_systems.skyBox )
    {
        m_systems.skyBox->ResetGLResources();
    }
    m_cWorldEnvironment.ResetGLResources();

    int fboW = Gfx().GetWidth() * 2;
    int fboH = Gfx().GetHeight() * 2;
    m_systems.reflectionFBO = Gfx().CreateFramebuffer( fboW, fboH );

    Text2d::BuildFont( "Verdana" );

    // --- Phase 5: Warm-up frame ---
    // Present one fully-cleared frame to the new backend. This serves two purposes:
    //  1. For DXGI backends (DX11/DX12): the first Present() registers the new swap chain with
    //     DWM and initialises all backbuffers to a known state. Without this, the compositor
    //     may briefly display garbage or stale content from the previous backend.
    //  2. For OpenGL: the first SwapBuffers() claims the GDI pixel buffer from DWM. If DXGI
    //     previously owned the window, this forces DWM to switch back to GDI compositing.
    // FlushGPU after presenting ensures the warm-up frame is fully complete before the main
    // loop begins rendering real content.
    // For GL specifically, we do TWO swap cycles to prime both front and back buffers.
    // The double-buffer swap chain means the first SwapBuffers fills the front buffer while
    // the back buffer may still hold stale DXGI content from the previous backend.
    Gfx().Clear( true, true );
    Gfx().Present();
    if ( target == RuntimeRendererType::OpenGL )
    {
        Gfx().Clear( true, true );
        Gfx().Present();
    }
    Gfx().FlushGPU();

    // --- Phase 6: Update window title ---
    char titleText[256];
    if ( switchFailureReason.empty() )
    {
        sprintf_s( titleText, "%s [%s]", TITLE_TEXT, Gfx().GetRendererName() );
    }
    else
    {
        sprintf_s( titleText, "%s [%s] -- SWITCH FAILED", TITLE_TEXT, Gfx().GetRendererName() );
        fprintf( stderr, "Renderer switch failed, restored previous renderer: %s\n", switchFailureReason.c_str() );
    }
    m_systems.window->SetTitleText( titleText );
}


void SkullbonezRun::SetUpGameModels( int count )
{
    m_scene.modelCount = count;
    m_scene.solverBallCount = 0;
    m_scene.solverBoxCount = 0;

    const SkullbonezConfig& cfg = Cfg();

    auto randFloat = [&]( float base, int range )
    { return base + static_cast<float>( rand() % range ); };
    auto randSigned = [&]( int range ) -> float
    {
        float mag = 1.0f + static_cast<float>( rand() % range );
        return ( rand() % 2 == 0 ) ? mag : -mag;
    };
    auto randSign = []() -> float
    { return ( rand() % 2 == 0 ) ? 1.0f : -1.0f; };

    for ( int x = 0; x < m_scene.modelCount; ++x )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution = cfg.ballRestitutionMin + static_cast<float>( rand() % cfg.ballRestitutionRange ) / 10.0f;
        Vector3 force( randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        bool makeBox = false;
        if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
        {
            makeBox = true;
        }
        else if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
        {
            makeBox = false;
        }
        else
        {
            // ~30% of objects are boxes when using new physics; legacy mode is spheres only
            makeBox = !m_cGameModelCollection.GetLegacyMode() && ( rand() % 10 ) < 3;
        }

        if ( makeBox )
        {
            float halfExtent = ( 1.0f + static_cast<float>( rand() % 3 ) ) * 0.6f;
            float hx = halfExtent * ( 0.7f + static_cast<float>( rand() % 4 ) * 0.2f );
            float hy = halfExtent;
            float hz = halfExtent * ( 0.7f + static_cast<float>( rand() % 4 ) * 0.2f );

            // Box inertia: I = m/3 * (hy² + hz²) etc.
            float hx2 = hx * hx;
            float hy2 = hy * hy;
            float hz2 = hz * hz;
            float m3 = mass / 3.0f;
            Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

            GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), inertia, mass );
            gameModel.SetCoefficientRestitution( restitution );
            gameModel.SetTerrain( m_systems.terrain.get() );
            gameModel.AddBoundingBox( Vector3( hx, hy, hz ) );
            gameModel.SetImpulseForce( force, forcePos );

            m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
        }
        else
        {
            float moment = randFloat( cfg.ballMomentMin, cfg.ballMomentRange );
            float radius = ( 1.0f + static_cast<float>( rand() % cfg.ballRadiusRange ) ) * 0.5f;

            GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), Vector3( moment, moment, moment ), mass );
            gameModel.SetCoefficientRestitution( restitution );
            gameModel.SetTerrain( m_systems.terrain.get() );
            gameModel.AddBoundingSphere( radius );
            gameModel.SetImpulseForce( force, forcePos );

            m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
        }
    }
}


// Spawns exactly 'balls' sphere objects followed by exactly 'boxes' OBB objects using the
// same random parameter ranges as SetUpGameModels().  Unlike SetUpGameModels() — which uses
// a probabilistic 30% box split — this function gives the caller precise control over the
// object-type mix, which is important for reproducible benchmarks (e.g. "200 balls + 100 boxes").
//
// Spheres are spawned first so the RNG sequence is deterministic given a fixed seed,
// regardless of the balls/boxes ratio.
void SkullbonezRun::SetUpSolverObjects( int balls, int boxes )
{
    balls = (std::max)( 0, balls );
    boxes = (std::max)( 0, boxes );
    const int totalObjects = balls + boxes;
    if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
    {
        balls = totalObjects;
        boxes = 0;
    }
    else if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
    {
        balls = 0;
        boxes = totalObjects;
    }

    m_cGameModelCollection.SetLegacyMode( false );
    m_scene.modelCount = balls + boxes;
    m_scene.solverBallCount = balls;
    m_scene.solverBoxCount = boxes;

    const SkullbonezConfig& cfg = Cfg();

    auto randFloat = [&]( float base, int range )
    { return base + static_cast<float>( rand() % range ); };
    auto randSigned = [&]( int range ) -> float
    {
        float mag = 1.0f + static_cast<float>( rand() % range );
        return ( rand() % 2 == 0 ) ? mag : -mag;
    };
    auto randSign = []() -> float
    { return ( rand() % 2 == 0 ) ? 1.0f : -1.0f; };

    // --- Sphere pass ---
    for ( int i = 0; i < balls; ++i )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution = cfg.ballRestitutionMin + static_cast<float>( rand() % cfg.ballRestitutionRange ) / 10.0f;
        float moment = randFloat( cfg.ballMomentMin, cfg.ballMomentRange );
        float radius = ( 1.0f + static_cast<float>( rand() % cfg.ballRadiusRange ) ) * 0.5f;
        Vector3 force( randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), Vector3( moment, moment, moment ), mass );
        gameModel.SetCoefficientRestitution( restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.AddBoundingSphere( radius );
        gameModel.SetImpulseForce( force, forcePos );
        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    // --- Box pass ---
    // Box inertia tensor (solid cuboid about centre of mass):
    //   Ix = m/12 * (hy² + hz²),  Iy = m/12 * (hx² + hz²),  Iz = m/12 * (hx² + hy²)
    // where hx, hy, hz are the full extents (2 × half-extents).
    // The spawn code uses half-extents internally, so the factor is m/3 (= m/12 * 4).
    for ( int i = 0; i < boxes; ++i )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution = cfg.ballRestitutionMin + static_cast<float>( rand() % cfg.ballRestitutionRange ) / 10.0f;
        Vector3 force( randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        float halfExtent = ( 1.0f + static_cast<float>( rand() % 3 ) ) * 0.6f;
        float hx = halfExtent * ( 0.7f + static_cast<float>( rand() % 4 ) * 0.2f );
        float hy = halfExtent;
        float hz = halfExtent * ( 0.7f + static_cast<float>( rand() % 4 ) * 0.2f );

        float hx2 = hx * hx, hy2 = hy * hy, hz2 = hz * hz;
        float m3 = mass / 3.0f;
        Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

        GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), inertia, mass );
        gameModel.SetCoefficientRestitution( restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.AddBoundingBox( Vector3( hx, hy, hz ) );
        gameModel.SetImpulseForce( force, forcePos );
        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    m_scene.modelCount = balls + boxes;
}


void SkullbonezRun::Run()
{
    MSG msg;

    for ( ;; )
    {
        if ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            if ( msg.message == WM_QUIT )
            {
                break;
            }
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        else
        {
            double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();
            secondsPerFrame = std::clamp( secondsPerFrame, 0.0, 0.05 );

            m_timers.frameTimer.StartTimer();
            PROFILE_FRAME_BEGIN();
            Gfx().ResetFrameDrawCallCount();

            PROFILE_BEGIN( "Frame/Input" );
            TakeInput();
            PROFILE_END( "Frame/Input" );

            TickRendererSwitch( static_cast<float>( secondsPerFrame ) );
            m_cGameModelCollection.BeginCollisionVisualFrame();
            TickPhysics( secondsPerFrame );

            // Update broadphase visualizer state (runs even when overlay is hidden so fades are correct)
            {
                m_broadphaseVisualizer.SetEnabled( m_debug.isBroadphaseOverlay );
                m_broadphaseVisualizer.SetCellSize( m_cGameModelCollection.GetSpatialGrid().GetCellSize() );
                const SpatialGrid& grid = m_cGameModelCollection.GetSpatialGrid();
                SpatialGrid::ActiveCell activeCellBuf[SpatialGrid::MAX_BUCKETS];
                int activeCellCount = grid.GetActiveCellCount();
                grid.GetActiveCells( activeCellBuf, SpatialGrid::MAX_BUCKETS );
                const std::vector<int64_t>& collisionKeys = m_cGameModelCollection.GetCollisionCellKeys();
                m_broadphaseVisualizer.Update( static_cast<float>( secondsPerFrame ), activeCellBuf, activeCellCount, collisionKeys.data(), static_cast<int>( collisionKeys.size() ) );
            }
            m_collisionVisualizer.SetEnabled( m_debug.isCollisionVisualizer || ( m_debug.physicsDebugFlags != PHYSICS_DEBUG_NONE && m_debug.isPhysicsDebugTransparent ) );
            m_collisionVisualizer.Update( static_cast<float>( secondsPerFrame ), m_cGameModelCollection );
            m_physicsDebugVisualizer.SetFlags( m_debug.physicsDebugFlags );
            m_physicsDebugVisualizer.SetContactLingerSeconds( m_debug.physicsDebugContactLinger );
            m_physicsDebugVisualizer.Update( static_cast<float>( secondsPerFrame ), m_cGameModelCollection );
            m_cGameModelCollection.EndCollisionVisualFrame();

            if ( m_runtimeSettings.isPipelineSyncEnabled )
            {
                Gfx().Finish();
            }

            PROFILE_GPU_BEGIN( "Frame/Render" );
            Render();
            PROFILE_GPU_END( "Frame/Render" );

            if ( !m_scene.isSceneMode || m_scene.isSceneText || m_debug.overlayMode != OverlayMode::None || m_ui.IsVisible() )
            {
                PROFILE_GPU_BEGIN( "Frame/UI" );
                DrawWindowText( secondsPerFrame );
                PROFILE_GPU_END( "Frame/UI" );
            }

            if ( TickScreenshots() )
            {
                continue;
            }

            TickAutoCycle();

            PROFILE_BEGIN( "Frame/VsyncWait" );
            Gfx().Present();
            PROFILE_END( "Frame/VsyncWait" );

            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END();

#if defined( SKULLBONEZ_PROFILE_ENABLED )
            {
                using SkullbonezCore::Basics::Profiler;
                static constexpr uint32_t kPhysicsHash = ::HashStr( "Frame/Physics" );
                static constexpr uint32_t kRenderHash = ::HashStr( "Frame/Render" );
                m_timers.physicsTime = Profiler::Instance().LastFrameMsByHash( kPhysicsHash ) * 0.001f;
                m_timers.renderTime = Profiler::Instance().LastFrameMsByHash( kRenderHash ) * 0.001f;
            }
#endif

            TickPerfLog();

            if ( TickSceneAdvance() )
            {
                continue;
            }
        }
    }
}


void SkullbonezRun::TickRendererSwitch( float dt )
{
    if ( m_debug.rendererSwitchInterval <= 0.0f )
    {
        return;
    }
    m_debug.rendererSwitchAccum += dt;
    if ( m_debug.rendererSwitchAccum >= m_debug.rendererSwitchInterval )
    {
        m_debug.rendererSwitchAccum = 0.0f;
        SwitchRenderer( GetNextRendererType( GetCurrentRendererType() ) );
    }
}


void SkullbonezRun::TickPhysics( double secondsPerFrame )
{
    if ( m_scene.isSceneMode && !m_scene.isScenePhysics )
    {
        return;
    }

    if ( m_scene.isFixedStep )
    {
        // Deterministic lock-step: exactly one physics tick per render frame.
        // Ignores wall-clock time entirely — produces identical results every run.
        //
        // time_scale > 1 runs multiple ticks per render frame (integer part) so
        // scenes can simulate faster than real-time while keeping the fixed dt.
        const int ticksThisFrame = (std::max)( 1, static_cast<int>( m_scene.timeScale ) );
        if ( !m_camera.isFlyMode || m_camera.isNudgeMode || Input::IsKeyDown( VK_SPACE ) )
        {
            PROFILE_BEGIN( "Frame/Physics" );
            for ( int tick = 0; tick < ticksThisFrame; ++tick )
            {
                m_cGameModelCollection.RunPhysics( PHYSICS_FIXED_DT );
            }
            PROFILE_END( "Frame/Physics" );
        }
        UpdateLogic( PHYSICS_FIXED_DT );
    }
    else
    {
        float scaledDt = static_cast<float>( secondsPerFrame ) * m_scene.timeScale;

        if ( !m_camera.isFlyMode || m_camera.isNudgeMode || Input::IsKeyDown( VK_SPACE ) )
        {
            PROFILE_BEGIN( "Frame/Physics" );
            if ( m_cGameModelCollection.GetLegacyMode() )
            {
                // Legacy swept physics is frame-rate independent: CollisionDetect* computes the
                // exact time-of-impact and steps to it internally, so external sub-division adds
                // no benefit and was never used before the accumulator was introduced for the solver.
                m_timers.physicsAccumulator = 0.0f;
                m_cGameModelCollection.RunPhysics( scaledDt );
            }
            else
            {
                // Impulse solver uses discrete overlap tests and needs small fixed steps for stability.
                // Only RunPhysics runs in the loop — camera and misc update once per frame below.
                m_timers.physicsAccumulator += scaledDt;

                int steps = 0;
                while ( m_timers.physicsAccumulator >= PHYSICS_FIXED_DT && steps < PHYSICS_MAX_STEPS_PER_FRAME )
                {
                    m_cGameModelCollection.RunPhysics( PHYSICS_FIXED_DT );
                    m_timers.physicsAccumulator -= PHYSICS_FIXED_DT;
                    ++steps;
                }

                // Drain excess accumulator if we hit the step cap (avoids spiral of death)
                if ( steps == PHYSICS_MAX_STEPS_PER_FRAME )
                {
                    m_timers.physicsAccumulator = 0.0f;
                }
            }
            PROFILE_END( "Frame/Physics" );
        }

        // Camera movement, tween, auto-cycle, logs — once per frame with real scaled dt
        UpdateLogic( scaledDt );
    }
}


bool SkullbonezRun::TickScreenshots()
{
    // screenshot_and_exit: on frame 0, save <scenename>.bmp to root then quit
    if ( m_scene.isSceneMode && m_screenshot.isScreenshotAndExit && m_scene.currentFrame == 0 )
    {
        const std::string& scenePath = m_sceneQueue[m_scene.currentSceneIndex];
        char outPath[256];
        const char* base = scenePath.c_str();
        const char* slash = strrchr( base, '/' );
        const char* backslash = strrchr( base, '\\' );
        const char* name = slash ? slash + 1 : ( backslash ? backslash + 1 : base );
        char stem[256];
        strcpy_s( stem, sizeof( stem ), name );
        char* dot = strrchr( stem, '.' );
        if ( dot )
        {
            *dot = '\0';
        }
        sprintf_s( outPath, sizeof( outPath ), "%s.bmp", stem );
        SaveScreenshot( outPath );
        PROFILE_FRAME_END();
        PostQuitMessage( 0 );
        return true;
    }

    // Triggered screenshot: capture when target frame or ms threshold is reached
    if ( m_scene.isSceneMode && m_screenshot.screenshotPath[0] != '\0' && !m_screenshot.isScreenshotSaved )
    {
        bool shouldCapture = false;

        if ( m_screenshot.screenshotFrame > 0 && ( m_scene.currentFrame + 1 ) >= m_screenshot.screenshotFrame )
        {
            shouldCapture = true;
        }
        if ( m_screenshot.screenshotMs > 0 && m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0 >= m_screenshot.screenshotMs )
        {
            shouldCapture = true;
        }

        if ( shouldCapture )
        {
            SaveScreenshot( m_screenshot.screenshotPath );
            m_screenshot.isScreenshotSaved = true;
            PROFILE_FRAME_END();
            if ( !AdvanceScene() )
            {
                PostQuitMessage( 0 );
            }
            return true;
        }
    }

    // Interval capture: save numbered screenshot every N frames
    if ( m_scene.isSceneMode && m_screenshot.screenshotInterval > 0 && m_screenshot.screenshotDir[0] != '\0' )
    {
        if ( ( m_scene.currentFrame + 1 ) % m_screenshot.screenshotInterval == 0 )
        {
            ++m_screenshot.intervalCaptureCount;
            char intervalPath[512];
            sprintf_s( intervalPath, sizeof( intervalPath ), "%s/capture_%04d.bmp", m_screenshot.screenshotDir, m_screenshot.intervalCaptureCount );
            SaveScreenshot( intervalPath );
        }
    }

    return false;
}


void SkullbonezRun::TickAutoCycle()
{
    if ( !m_scene.isSceneMode || m_camera.autoCycleInterval <= 0.0f || m_camera.autoCycleAccum < m_camera.autoCycleInterval )
    {
        return;
    }

    int ballCount = m_cGameModelCollection.GetModelCount();
    char shotPath[256];
    sprintf_s( shotPath, sizeof( shotPath ), "Profile/cardinal_ball%d.bmp", m_camera.autoCycleShotsTaken );
    SaveScreenshot( shotPath );
    fprintf( stdout, "Auto-shot %d: ball index %d -> %s\n", m_camera.autoCycleShotsTaken, m_camera.trackBallIndex, shotPath );
    fflush( stdout );

    ++m_camera.autoCycleShotsTaken;
    m_camera.autoCycleAccum = 0.0f;

    if ( m_camera.autoCycleShotsTaken >= ballCount )
    {
        PostQuitMessage( 0 );
    }
    else
    {
        m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % ballCount;
    }
}


void SkullbonezRun::TickPerfLog()
{
    if ( !m_perfLogState.isPerfTest || !m_perfLogState.perfLogFile )
    {
        return;
    }

#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( !m_perfLogState.perfHeaderWritten )
    {
        Profiler::Instance().WritePerfCSVHeader( m_perfLogState.perfLogFile );
        m_perfLogState.perfHeaderWritten = true;
    }
    Profiler::Instance().WritePerfCSVRow( m_perfLogState.perfLogFile, sPerfPass + 1, m_scene.currentFrame + 1 );
#else
    fprintf( m_perfLogState.perfLogFile, "%d,%d,%.4f,%.4f\n", sPerfPass + 1, m_scene.currentFrame + 1, m_timers.physicsTime * 1000.0f, m_timers.renderTime * 1000.0f );
#endif

    ++m_perfLogState.perfLogWritesSinceFlush;
    if ( m_perfLogState.isPerfLogFlushEnabled ||
         ( m_perfLogState.perfLogFlushInterval > 0 && m_perfLogState.perfLogWritesSinceFlush >= m_perfLogState.perfLogFlushInterval ) )
    {
        fflush( m_perfLogState.perfLogFile );
        m_perfLogState.perfLogWritesSinceFlush = 0;
    }

    if ( ( m_scene.currentFrame + 1 ) % 60 == 0 )
    {
        LogPerfMemory( "periodic" );
    }
}


bool SkullbonezRun::TickSceneAdvance()
{
    ++m_scene.currentFrame;

    // Check if target frame count is reached (skip if screenshot auto-exit is still pending)
    if ( m_scene.isSceneMode && m_scene.targetFrameCount > 0 && !m_screenshot.isScreenshotSaved )
    {
        if ( m_scene.currentFrame >= m_scene.targetFrameCount )
        {
            if ( m_scene.isExitOnComplete )
            {
                if ( !AdvanceScene() )
                {
                    PostQuitMessage( 0 );
                }
                return true;
            }
            else
            {
                m_scene.isTestComplete = true;
            }
        }
    }

    // Legacy (non-scene) mode: restart the scene every 20s to keep running indefinitely
    if ( !m_scene.isSceneMode && !m_camera.isFlyMode && m_timers.simulationTimer.GetTimeSinceLastStart() > 20.0 )
    {
        LoadScene( m_scene.currentSceneIndex );
        m_timers.simulationTimer.StartTimer();
        return true;
    }

    // Perf-log scenes without an explicit frame count still use a timed pass duration.
    if ( m_perfLogState.isPerfTest &&
         m_scene.targetFrameCount <= 0 &&
         m_timers.simulationTimer.GetTimeSinceLastStart() > PERF_TEST_PASS_SECONDS )
    {
        if ( !AdvanceScene() )
        {
            PostQuitMessage( 0 );
        }
        return true;
    }

    return false;
}


void SkullbonezRun::TakeInput()
{
    // Toggle fly mode with F (edge-detected so snapshot-loaded fly mode survives the next frame)
    bool prevFlyMode = m_camera.isFlyMode;
    bool fNow = Input::IsKeyDown( 'F' );
    if ( fNow && !m_camera.input.Get( InputState::FWasDown ) )
    {
        m_camera.isFlyMode = !m_camera.isFlyMode;
        m_camera.isNudgeMode = false; // F-key fly never implies nudge
    }
    m_camera.input.Set( InputState::FWasDown, fNow );

    // N key: toggle nudge mode — free camera with live simulation (edge-detected).
    // Nudge entering also enters fly mode; nudge exiting also exits fly mode.
    {
        bool nNow = Input::IsKeyDown( 'N' );
        if ( nNow && !m_camera.input.Get( InputState::NWasDown ) )
        {
            m_camera.isNudgeMode = !m_camera.isNudgeMode;
            m_camera.isFlyMode = m_camera.isNudgeMode;
        }
        m_camera.input.Set( InputState::NWasDown, nNow );
    }

#ifdef _DEBUG
    {
        bool enterNow = Input::IsKeyDown( VK_RETURN );
        if ( enterNow && !m_camera.input.Get( InputState::EnterWasDown ) && m_camera.isNudgeMode )
        {
            WriteNudgeReproSnapshot();
        }
        m_camera.input.Set( InputState::EnterWasDown, enterNow );
    }
#endif

    if ( m_camera.isFlyMode != prevFlyMode )
    {
        if ( m_camera.isFlyMode )
        {
            // Entering fly mode: in legacy mode snap to free camera; in scene mode stay
            // on the current camera so fly controls work without requiring CAMERA_FREE
            if ( !m_scene.isSceneMode )
            {
                m_systems.cameras->SelectCamera( CAMERA_FREE, false );
            }
            m_camera.cameraTime = 0.0f;
            XZBounds unbounded;
            unbounded.m_xMin = -99999.9f;
            unbounded.m_xMax = 99999.9f;
            unbounded.m_zMin = -99999.9f;
            unbounded.m_zMax = 99999.9f;
            uint32_t activeCam = m_scene.isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
            m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
            SetCursor( nullptr );
            Input::CentreMouseCoordinates();
            m_camera.input.xMove = 0;
            m_camera.input.yMove = 0;
        }
        else
        {
            // Exiting fly mode: restore m_terrain XZ bounds, cursor, camera cycle clock
            uint32_t activeCam = m_scene.isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
            m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
            SetCursor( LoadCursor( nullptr, IDC_ARROW ) );
            m_camera.cameraTime = 0.0f;
            // Exiting fly mode also exits nudge mode
            m_camera.isNudgeMode = false;
        }
    }

    // Water m_shader debug toggles
    bool key1Now = Input::IsKeyDown( '1' );
    if ( key1Now && !m_camera.input.Get( InputState::Key1WasDown ) )
    {
        m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
        if ( m_debug.isWaterFreezeDebug )
        {
            m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
        }
    }
    m_camera.input.Set( InputState::Key1WasDown, key1Now );
    // Key '2' cycles reflection mode: FBO (default) → DXR ray-traced (if supported) → none → FBO
    {
        static bool s_key2WasDown = false;
        bool s_key2Now = ( Input::IsKeyDown( '2' ) != 0 );
        if ( s_key2Now && !s_key2WasDown )
        {
            if ( !m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect )
            {
                if ( Gfx().IsDXRSupported() )
                {
                    m_debug.isWaterRTReflect = true; // FBO → DXR
                }
                else
                {
                    m_debug.isWaterNoReflect = true; // DXR not available, skip to none
                }
            }
            else if ( m_debug.isWaterRTReflect )
            {
                m_debug.isWaterRTReflect = false;
                m_debug.isWaterNoReflect = true; // DXR → none
            }
            else
            {
                m_debug.isWaterNoReflect = false; // none → FBO
            }
        }
        s_key2WasDown = s_key2Now;
    }
    {
        bool key3Now = Input::IsKeyDown( '3' );
        if ( key3Now && !m_camera.input.Get( InputState::Key3WasDown ) )
        {
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
        }
        m_camera.input.Set( InputState::Key3WasDown, key3Now );
    }
    {
        bool key4Now = Input::IsKeyDown( '4' );
        if ( key4Now && !m_camera.input.Get( InputState::Key4WasDown ) )
        {
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
        }
        m_camera.input.Set( InputState::Key4WasDown, key4Now );
    }
    {
        bool key5Now = Input::IsKeyDown( '5' );
        if ( key5Now && !m_camera.input.Get( InputState::Key5WasDown ) )
        {
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
        }
        m_camera.input.Set( InputState::Key5WasDown, key5Now );
    }
    // V key: collision visualizer. Renders balls and boxes as solid debug colours.
    {
        bool vNow = Input::IsKeyDown( 'V' );
        if ( vNow && !m_camera.input.Get( InputState::VWasDown ) )
        {
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
        }
        m_camera.input.Set( InputState::VWasDown, vNow );
    }

    // Debug vectors: mouse UI and keyboard both toggle the same runtime state.
    {
        bool key9Now = Input::IsKeyDown( '9' );
        if ( key9Now && !m_camera.input.Get( InputState::Key9WasDown ) )
        {
            m_debug.isDebugVectors = !m_debug.isDebugVectors;
        }
        m_camera.input.Set( InputState::Key9WasDown, key9Now );
    }

    // C key: cycle physics debug overlay - None -> Axes -> Contacts -> Sleep -> All -> None.
    {
        bool cNow = Input::IsKeyDown( 'C' );
        if ( cNow && !m_camera.input.Get( InputState::CKeyWasDown ) )
        {
            switch ( m_debug.physicsDebugFlags )
            {
            case PHYSICS_DEBUG_NONE:
                m_debug.physicsDebugFlags = PHYSICS_DEBUG_AXES;
                break;
            case PHYSICS_DEBUG_AXES:
                m_debug.physicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
                break;
            case PHYSICS_DEBUG_CONTACTS:
                m_debug.physicsDebugFlags = PHYSICS_DEBUG_SLEEP;
                break;
            case PHYSICS_DEBUG_SLEEP:
                m_debug.physicsDebugFlags = PHYSICS_DEBUG_ALL;
                break;
            default:
                m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
                break;
            }
        }
        m_camera.input.Set( InputState::CKeyWasDown, cNow );
    }

    // 6 key: translucent debug collision volumes for inspecting axes/contact rows inside bodies.
    {
        bool key6Now = Input::IsKeyDown( '6' );
        if ( key6Now && !m_camera.input.Get( InputState::Key6WasDown ) )
        {
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
        }
        m_camera.input.Set( InputState::Key6WasDown, key6Now );
    }

    // Q key: cycle render backend at runtime while preserving current simulation state (GL → DX11 → DX12 → GL).
    {
        bool isQNow = Input::IsKeyDown( 'Q' );
        if ( isQNow && !m_camera.input.Get( InputState::QKeyWasDown ) )
        {
            SwitchRenderer( GetNextRendererType( GetCurrentRendererType() ) );
        }
        m_camera.input.Set( InputState::QKeyWasDown, isQNow );
    }

    // G key: toggle broadphase overlay, or cycle tracked ball if overlay is off.
    bool isGNow = Input::IsKeyDown( 'G' );
    if ( isGNow && !m_camera.input.Get( InputState::GKeyWasDown ) )
    {
        if ( m_scene.isSceneMode && m_camera.trackBallIndex >= 0 && !m_debug.isBroadphaseOverlay )
        {
            int count = m_cGameModelCollection.GetModelCount();
            if ( count > 0 )
            {
                m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % count;
            }
        }
        else
        {
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
        }
    }
    m_camera.input.Set( InputState::GKeyWasDown, isGNow );

    // 0 key: toggle the in-game diagnostics window. Tabs replace the old overlay cycle.
    // Edge-detected in both scene and legacy modes; one toggle per keypress.
    {
        bool key0Now = Input::IsKeyDown( '0' );
        if ( key0Now && !m_camera.input.Get( InputState::Key0WasDown ) )
        {
            m_ui.ToggleVisible( m_timers.simulationTimer.GetTotalTime() );
            m_debug.overlayMode = OverlayMode::None;
        }
        m_camera.input.Set( InputState::Key0WasDown, key0Now );
    }

    if ( m_systems.window )
    {
        InGameUiInputResult uiResult = m_ui.UpdateInput( m_systems.window->m_sWindow,
                                                         static_cast<int>( m_systems.window->m_sWindowDimensions.x ),
                                                         static_cast<int>( m_systems.window->m_sWindowDimensions.y ),
                                                         m_timers.simulationTimer.GetTotalTime() );
        if ( uiResult.toggleVsync )
        {
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
        }
        if ( uiResult.toggleCollisionVisualizer )
        {
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
        }
        if ( uiResult.togglePhysicsDebugFlags != 0 )
        {
            m_debug.physicsDebugFlags ^= ( uiResult.togglePhysicsDebugFlags & PHYSICS_DEBUG_ALL );
        }
        if ( uiResult.togglePhysicsDebugTransparent )
        {
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
        }
        if ( uiResult.toggleDebugVectors )
        {
            m_debug.isDebugVectors = !m_debug.isDebugVectors;
        }
        if ( uiResult.toggleBroadphaseOverlay )
        {
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
        }
        if ( uiResult.toggleScenePhysics )
        {
            m_scene.isScenePhysics = !m_scene.isScenePhysics;
            m_timers.physicsAccumulator = 0.0f;
        }
        if ( uiResult.toggleSceneText )
        {
            m_scene.isSceneText = !m_scene.isSceneText;
        }
        if ( uiResult.toggleTextOnly )
        {
            m_debug.isTextOnly = !m_debug.isTextOnly;
        }
        if ( uiResult.toggleFixedStep )
        {
            m_scene.isFixedStep = !m_scene.isFixedStep;
            m_timers.physicsAccumulator = 0.0f;
        }
        if ( uiResult.toggleExitOnComplete )
        {
            m_scene.isExitOnComplete = !m_scene.isExitOnComplete;
        }
        if ( uiResult.togglePipelineSync )
        {
            m_runtimeSettings.isPipelineSyncEnabled = !m_runtimeSettings.isPipelineSyncEnabled;
        }
        if ( uiResult.toggleRollAlign )
        {
            m_runtimeSettings.isRollAlignEnabled = !m_runtimeSettings.isRollAlignEnabled;
            Cfg().rollAlignEnabled = m_runtimeSettings.isRollAlignEnabled;
        }
        if ( uiResult.toggleTerrainHidden )
        {
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
        }
        if ( uiResult.toggleWaterHidden )
        {
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
        }
        if ( uiResult.toggleWaterFreeze )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        if ( uiResult.toggleWaterFlat )
        {
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
        }
        if ( uiResult.toggleWaterReflection )
        {
            if ( m_debug.isWaterNoReflect )
            {
                m_debug.isWaterNoReflect = false;
            }
            else
            {
                m_debug.isWaterNoReflect = true;
                m_debug.isWaterRTReflect = false;
            }
        }
        if ( uiResult.requestedWaterReflectionMode >= 0 )
        {
            const int mode = std::clamp( uiResult.requestedWaterReflectionMode, 0, 2 );
            m_debug.isWaterRTReflect = mode == 1;
            m_debug.isWaterNoReflect = mode == 2;
        }
        if ( uiResult.requestedPhysicsMode >= 0 )
        {
            const int mode = std::clamp( uiResult.requestedPhysicsMode, 0, 1 );
            m_cGameModelCollection.SetLegacyMode( mode == 0 );
            if ( mode == 0 )
            {
                m_uiSolverBallCountOverride = -1;
                m_uiSolverBoxCountOverride = -1;
            }
            m_timers.physicsAccumulator = 0.0f;
            PROFILE_SCHEDULE_RESET();
        }
        if ( uiResult.requestedTimeScale > 0.0f )
        {
            m_uiTimeScaleOverride = std::clamp( uiResult.requestedTimeScale, 0.10f, 4.00f );
            m_scene.timeScale = m_uiTimeScaleOverride;
            m_timers.physicsAccumulator = 0.0f;
        }
        if ( uiResult.requestedFrameCount >= 0 )
        {
            m_scene.targetFrameCount = uiResult.requestedFrameCount > 0 ? std::clamp( uiResult.requestedFrameCount, 1, 5000 ) : -1;
            m_scene.isTestComplete = false;
        }
        if ( uiResult.requestedSeed > 0 )
        {
            m_scene.rngSeed = static_cast<unsigned int>( std::clamp( uiResult.requestedSeed, 1, 999999 ) );
            srand( m_scene.rngSeed );
        }
        if ( uiResult.requestedPhysicsDebugAlpha >= 0.0f )
        {
            m_debug.physicsDebugAlpha = std::clamp( uiResult.requestedPhysicsDebugAlpha, 0.05f, 1.0f );
        }
        if ( uiResult.requestedPhysicsDebugContactLinger >= 0.0f )
        {
            m_debug.physicsDebugContactLinger = std::clamp( uiResult.requestedPhysicsDebugContactLinger, 0.0f, 5.0f );
        }
        if ( uiResult.requestedModelCount >= 0 )
        {
            ApplyUiModelCountOverride( uiResult.requestedModelCount );
        }
        if ( uiResult.requestedSolverBallCount >= 0 )
        {
            const int boxes = m_uiSolverBoxCountOverride >= 0 ? m_uiSolverBoxCountOverride : m_scene.solverBoxCount;
            ApplyUiSolverObjectCounts( uiResult.requestedSolverBallCount, boxes );
        }
        if ( uiResult.requestedSolverBoxCount >= 0 )
        {
            const int balls = m_uiSolverBallCountOverride >= 0 ? m_uiSolverBallCountOverride : m_scene.solverBallCount;
            ApplyUiSolverObjectCounts( balls, uiResult.requestedSolverBoxCount );
        }
        if ( uiResult.requestedTrackHeight >= 0.0f )
        {
            const float trackHeight = std::clamp( uiResult.requestedTrackHeight, 0.0f, 600.0f );
            if ( trackHeight <= 0.0f || m_scene.modelCount <= 0 )
            {
                m_camera.trackBallIndex = -1;
            }
            else
            {
                m_camera.trackHeight = trackHeight;
                if ( m_camera.trackBallIndex < 0 )
                {
                    m_camera.trackBallIndex = 0;
                }
            }
        }
        if ( uiResult.requestedAutoCycleInterval >= 0.0f )
        {
            const float interval = std::clamp( uiResult.requestedAutoCycleInterval, 0.0f, 10.0f );
            m_camera.autoCycleInterval = interval > 0.0f ? interval : -1.0f;
            m_camera.autoCycleAccum = 0.0f;
            m_camera.autoCycleShotsTaken = 0;
        }
        if ( uiResult.requestWorldGravity || uiResult.requestWorldFluidHeight || uiResult.requestWorldFluidDensity )
        {
            const float gravity = uiResult.requestWorldGravity ? uiResult.requestedWorldGravity : m_cWorldEnvironment.GetGravity();
            const float fluidHeight = uiResult.requestWorldFluidHeight ? uiResult.requestedWorldFluidHeight : m_cWorldEnvironment.GetFluidSurfaceHeight();
            const float fluidDensity = uiResult.requestWorldFluidDensity ? uiResult.requestedWorldFluidDensity : m_cWorldEnvironment.GetFluidDensity();
            ApplyUiWorldOverride( std::clamp( gravity, -100.0f, 0.0f ),
                                  std::clamp( fluidHeight, -100.0f, 200.0f ),
                                  std::clamp( fluidDensity, 0.0f, 5.0f ) );
        }
        if ( uiResult.resetScene )
        {
            ResetCurrentScene();
        }
        if ( uiResult.saveSceneDefaults )
        {
            SaveCurrentSceneDefaults();
        }
        if ( uiResult.requestedRendererIndex >= 0 )
        {
            RuntimeRendererType requestedRenderer = RuntimeRendererType::OpenGL;
            if ( uiResult.requestedRendererIndex == 1 )
            {
                requestedRenderer = RuntimeRendererType::DX11;
            }
            else if ( uiResult.requestedRendererIndex == 2 )
            {
                requestedRenderer = RuntimeRendererType::DX12;
            }
            SwitchRenderer( requestedRenderer );
        }
    }

    // F2: Save scene snapshot to Scenes/
    {
        bool f2Now = Input::IsKeyDown( VK_F2 );
        if ( f2Now && !m_camera.input.Get( InputState::F2WasDown ) )
        {
            CreateDirectoryA( "Scenes", nullptr );
            static int sSnapshotSeq = 0;
            bool saved = false;
            for ( int tries = 0; tries < 100 && !saved; ++tries )
            {
                char path[256];
                sprintf_s( path, sizeof( path ), "Scenes\\snapshot_%04d.scene", sSnapshotSeq++ );
                saved = m_cGameModelCollection.SaveSceneSnapshot(
                    path,
                    m_scene.isScenePhysics,
                    m_scene.isSceneText,
                    m_cWorldEnvironment,
                    m_systems.cameras->GetCameraTranslation(),
                    m_systems.cameras->GetCameraView(),
                    m_systems.cameras->GetCameraUp() );
            }
        }
        m_camera.input.Set( InputState::F2WasDown, f2Now );
    }

    // F3: Save screenshot to Screenshots/
    {
        bool f3Now = Input::IsKeyDown( VK_F3 );
        if ( f3Now && !m_camera.input.Get( InputState::F3WasDown ) )
        {
            CreateDirectoryA( "Screenshots", nullptr );
            static int sScreenshotSeq = 0;
            bool saved = false;
            for ( int tries = 0; tries < 100 && !saved; ++tries )
            {
                char path[256];
                sprintf_s( path, sizeof( path ), "Screenshots\\screenshot_%04d.bmp", sScreenshotSeq++ );
                if ( GetFileAttributesA( path ) == INVALID_FILE_ATTRIBUTES )
                {
                    SaveScreenshot( path );
                    saved = true;
                }
            }
        }
        m_camera.input.Set( InputState::F3WasDown, f3Now );
    }

    // P: toggle between legacy (sphere-only, ad-hoc) and new (sequential impulse) solver at runtime.
    // In legacy mode boxes freeze in place and disappear; they reappear when toggled back.
    {
        bool pNow = Input::IsKeyDown( 'P' );
        if ( pNow && !m_camera.input.Get( InputState::PWasDown ) )
        {
            m_cGameModelCollection.SetLegacyMode( !m_cGameModelCollection.GetLegacyMode() );
            PROFILE_SCHEDULE_RESET();
        }
        m_camera.input.Set( InputState::PWasDown, pNow );
    }

    // Z: fire a ball out of the camera. X: fire a box (solver mode only; ignored in legacy mode).
    // Shift applies the same 3× speed multiplier as walking.
    // Objects are recycled from the model pool — no new allocations.
    {
        bool zNow = Input::IsKeyDown( 'Z' );
        if ( zNow && !m_camera.input.Get( InputState::ZWasDown ) )
        {
            FireProjectile( false );
        }
        m_camera.input.Set( InputState::ZWasDown, zNow );
    }
    {
        bool xNow = Input::IsKeyDown( 'X' );
        if ( xNow && !m_camera.input.Get( InputState::XWasDown ) )
        {
            FireProjectile( true );
        }
        m_camera.input.Set( InputState::XWasDown, xNow );
    }

    // R: reset/reload the current scene from scratch. Backspace remains as a scene-mode alias.
    {
        bool rNow = Input::IsKeyDown( 'R' );
        if ( rNow && !m_camera.input.Get( InputState::RKeyWasDown ) )
        {
            ResetCurrentScene();
        }
        m_camera.input.Set( InputState::RKeyWasDown, rNow );
    }
    if ( m_scene.isSceneMode )
    {
        bool bsNow = Input::IsKeyDown( VK_BACK );
        if ( bsNow && !m_camera.input.Get( InputState::BackspaceWasDown ) )
        {
            ResetCurrentScene();
        }
        m_camera.input.Set( InputState::BackspaceWasDown, bsNow );
    }

    if ( m_camera.isFlyMode )
    {
        // Keep cursor hidden every frame unless the UI owns the mouse.
        SetCursor( m_ui.BlocksCameraMouse() ? LoadCursor( nullptr, IDC_ARROW ) : nullptr );

        // Mouse look: delta from screen centre
        if ( m_ui.BlocksCameraMouse() )
        {
            m_camera.input.xMove = 0;
            m_camera.input.yMove = 0;
        }
        else
        {
            POINT currentCoords = Input::GetMouseCoordinates();
            Input::CentreMouseCoordinates();
            POINT centreCoords = Input::GetMouseCoordinates();
            m_camera.input.xMove = currentCoords.x - centreCoords.x;
            m_camera.input.yMove = currentCoords.y - centreCoords.y;
        }

        // WASD movement
        m_camera.input.Set( InputState::Up, Input::IsKeyDown( 'W' ) );
        m_camera.input.Set( InputState::Left, Input::IsKeyDown( 'A' ) );
        m_camera.input.Set( InputState::Down, Input::IsKeyDown( 'S' ) );
        m_camera.input.Set( InputState::Right, Input::IsKeyDown( 'D' ) );
    }
    else
    {
        m_camera.input.xMove = 0;
        m_camera.input.yMove = 0;
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
    }
}


void SkullbonezRun::UpdateLogic( float fSecondsPerFrame )
{
    // Auto-cycle
    if ( m_scene.isSceneMode && m_camera.autoCycleInterval > 0.0f )
    {
        m_camera.autoCycleAccum += fSecondsPerFrame;
    }

    // move the camera based on input
    // (arguments are calculating time based movement quantities)
    MoveCamera( fSecondsPerFrame * Cfg().keySpeed,
                fSecondsPerFrame * Cfg().mouseSensitivity );

    UpdateWaterHeightControls( fSecondsPerFrame );

    // update camera tweening speed
    m_systems.cameras->SetTweenSpeed( Cfg().cameraTweenRate * fSecondsPerFrame );
}


void SkullbonezRun::UpdateWaterHeightControls( float dt )
{
    const bool downNow = Input::IsKeyDown( VK_NEXT );
    const bool upNow = Input::IsKeyDown( VK_PRIOR );
    if ( downNow == upNow )
    {
        return;
    }

    const float direction = upNow ? 1.0f : -1.0f;
    const float height = m_cWorldEnvironment.GetFluidSurfaceHeight() + direction * WATER_HEIGHT_CONTROL_SPEED * dt;
    m_cWorldEnvironment.SetFluidSurfaceHeight( height );
}


void SkullbonezRun::Render()
{
    // Clear screen pixel and depth into buffers
    Gfx().Clear( true, true );

    // In text_only mode all 3D rendering is skipped ? DrawWindowText handles the display
    if ( m_debug.isTextOnly )
    {
        return;
    }

    // renders camera views etc
    SetViewingOrientation();

    // set the camera into its m_position
    m_systems.cameras->SetCamera();

    // now camera rotation has been done, draw OpenGL primitives
    DrawPrimitives();
}


void SkullbonezRun::DrawPrimitives()
{
    float lightPosition[] = { 200.0f, 400.0f, 1200.0f, 1.0f };

    // Get view and projection matrices from camera/window
    Matrix4 baseView = m_systems.cameras->GetViewMatrix();
    Matrix4 proj = m_systems.window->GetProjectionMatrix();
    Matrix4 reflVP;

    PROFILE_BEGIN( "Frame/Render/PrepareModels" );
    m_cGameModelCollection.PrepareRenderStreams();
    PROFILE_END( "Frame/Render/PrepareModels" );

    // Camera m_position for skybox placement.  During camera transitions the
    // selected camera is already the destination, but SetCamera() renders from
    // the interpolated tween camera.  Reflection math must use the same render
    // camera as baseView; otherwise the mirror pass is generated from the
    // destination camera while the water surface samples it from the in-between
    // camera, which stretches reflected balls during transitions.
    Vector3 eye = m_systems.cameras->GetRenderCameraTranslation();

    // render skybox ------------------------------
    PROFILE_GPU_BEGIN( "Frame/Render/Skybox" );
    Matrix4 skyView = baseView * Matrix4::Translate( eye.x, Cfg().skyboxRenderHeight, eye.z ) * Matrix4::Scale( Cfg().skyboxScale );
    m_systems.skyBox->Render( skyView, proj );
    PROFILE_GPU_END( "Frame/Render/Skybox" );

    // reflection pre-pass: render above-water scene from mirrored camera into FBO (or DXR dispatch)
    PROFILE_GPU_BEGIN( "Frame/Render/Reflection" );
    float waterY = m_cWorldEnvironment.GetFluidSurfaceHeight();
    Vector3 center = m_systems.cameras->GetRenderCameraView();

    // Mirror eye and look-at target about the water plane; flip up vector
    Vector3 reflEye( eye.x, 2.0f * waterY - eye.y, eye.z );
    Vector3 reflCenter( center.x, 2.0f * waterY - center.y, center.z );
    Vector3 up = m_systems.cameras->GetRenderCameraUp();
    Vector3 reflUp( up.x, -up.y, up.z );
    Matrix4 reflView = Matrix4::LookAt( reflEye, reflCenter, reflUp );
    reflVP = proj * reflView;

    if ( Gfx().IsDXRSupported() && m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect && !m_debug.isCollisionVisualizer )
    {
        // DXR path: rebuild TLAS with current ball positions, then dispatch rays
        int ballCount = m_cGameModelCollection.GetModelCount();
        std::vector<float> transforms( (size_t)ballCount * 16 );
        for ( int i = 0; i < ballCount; ++i )
        {
            Matrix4 mdlMat = m_cGameModelCollection.GetModelAtIndex( i ).GetModelMatrix();
            memcpy( transforms.data() + i * 16, mdlMat.Data(), 16 * sizeof( float ) );
        }

        Gfx().BuildTLAS( transforms.data(), ballCount, 0, 0 ); // BLAS VAs retrieved internally

        // Compute inverse VP matrix for ray reconstruction
        Matrix4 vp = proj * baseView;
        Matrix4 invVP = vp.Inverse();
        float cameraPos[3] = { eye.x, eye.y, eye.z };
        float simTime = static_cast<float>( m_timers.simulationTimer.GetTotalTime() );

        uint32_t sphereHandle = m_systems.textures->GetTextureHandle( TEXTURE_BOUNDING_SPHERE );
        uint32_t terrainHandle = m_systems.textures->GetTextureHandle( TEXTURE_GROUND );
        uint32_t skyUpHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_UP );
        uint32_t skyDownHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_DOWN );
        uint32_t skyRightHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_RIGHT );
        uint32_t skyLeftHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_LEFT );
        uint32_t skyFrontHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_FRONT );
        uint32_t skyBackHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_BACK );
        Gfx().DispatchReflectionRays( invVP.Data(), cameraPos, waterY, simTime, lightPosition, m_systems.window->m_sWindowDimensions.x * 2, m_systems.window->m_sWindowDimensions.y * 2, sphereHandle, terrainHandle, skyUpHandle, skyDownHandle, skyRightHandle, skyLeftHandle, skyFrontHandle, skyBackHandle );
    }
    else
    {
        // FBO mirror-camera path (GL, DX11, or DXR fallback)
        m_systems.reflectionFBO->Bind();
        Gfx().SetViewport( 0, 0, m_systems.reflectionFBO->GetWidth(), m_systems.reflectionFBO->GetHeight() );
        Gfx().Clear( true, true );

        // Skybox reflected (XZ follows eye; Y anchored at Cfg().skyboxRenderHeight)
        PROFILE_GPU_BEGIN( "Frame/Render/Reflection/Skybox" );
        Matrix4 skyReflView = reflView * Matrix4::Translate( eye.x, Cfg().skyboxRenderHeight, eye.z ) * Matrix4::Scale( Cfg().skyboxScale );
        m_systems.skyBox->Render( skyReflView, proj );
        PROFILE_GPU_END( "Frame/Render/Reflection/Skybox" );

        // Game models reflected ? clip at water surface (above-water portion only)
        PROFILE_GPU_BEGIN( "Frame/Render/Reflection/Balls" );
        Gfx().SetClipPlane( 0, true );
        SkullbonezHelper::SetClipPlane( 0.0f, 1.0f, 0.0f, -waterY );
        m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, -waterY );
        if ( m_debug.isCollisionVisualizer )
        {
            m_collisionVisualizer.Render( m_cGameModelCollection, reflView, proj, lightPosition );
        }
        else
        {
            m_systems.textures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
            m_cGameModelCollection.RenderModels( reflView, proj, lightPosition );
        }
        Gfx().SetClipPlane( 0, false );
        SkullbonezHelper::SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        PROFILE_GPU_END( "Frame/Render/Reflection/Balls" );

        m_systems.reflectionFBO->Unbind();
        Gfx().SetViewport( 0, 0, m_systems.window->m_sWindowDimensions.x, m_systems.window->m_sWindowDimensions.y );
    }
    PROFILE_GPU_END( "Frame/Render/Reflection" );

    // render game models -----------------------------
    PROFILE_GPU_BEGIN( "Frame/Render/Balls" );
    const bool physicsDebugTransparent = m_debug.physicsDebugFlags != PHYSICS_DEBUG_NONE && m_debug.isPhysicsDebugTransparent;
    if ( m_debug.isCollisionVisualizer || physicsDebugTransparent )
    {
        m_collisionVisualizer.SetAlphaOverride( physicsDebugTransparent && !m_debug.isCollisionVisualizer ? m_debug.physicsDebugAlpha : -1.0f );
        m_collisionVisualizer.Render( m_cGameModelCollection, baseView, proj, lightPosition );
        m_collisionVisualizer.SetAlphaOverride( -1.0f );
    }
    else
    {
        m_systems.textures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
        m_cGameModelCollection.RenderModels( baseView, proj, lightPosition );
    }
    PROFILE_GPU_END( "Frame/Render/Balls" );

    // render m_terrain ------------------------------
    if ( !m_debug.isTerrainHidden )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/Terrain" );
        m_systems.textures->SelectTexture( TEXTURE_GROUND );
        m_systems.terrain->Render( baseView, proj, lightPosition );
        PROFILE_GPU_END( "Frame/Render/Terrain" );
    }

    // render ground shadows on top of m_terrain
    if ( !m_debug.isTerrainHidden )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/Shadows" );
        m_cGameModelCollection.RenderShadows( m_systems.terrain.get(), baseView, proj, waterY );
        PROFILE_GPU_END( "Frame/Render/Shadows" );
    }

    // render the fluid ---------------------------
    if ( !m_debug.isWaterHidden )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/Water" );
        float waterTime = m_debug.isWaterFreezeDebug
                              ? m_debug.frozenWaterTime
                              : static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
        uint32_t reflTex = ( Gfx().IsDXRSupported() && m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect && !m_debug.isCollisionVisualizer )
                               ? Gfx().GetReflectionUAVTexture()
                               : m_systems.reflectionFBO->GetColorTextureHandle();
        // DXR reflection texture is in main-camera screen space, so sample it
        // using the main VP — not the mirror VP used by the FBO path.
        Matrix4 waterSampleVP = ( Gfx().IsDXRSupported() && m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect && !m_debug.isCollisionVisualizer )
                                    ? proj * baseView
                                    : reflVP;
        m_cWorldEnvironment.RenderFluid( baseView, proj, waterSampleVP, waterTime, reflTex, m_debug.isWaterFlatDebug, m_debug.isWaterNoReflect );
        PROFILE_GPU_END( "Frame/Render/Water" );
    }

    // debug vector overlay - toggled with 9 (or debug_vectors in scene)
    //   green  = Travel Vector (velocity, scaled)
    //   red    = Roll Axis Vector (angular velocity, scaled)
    //   white  = Pole Vector outside tolerance
    //   blue   = Pole Vector within tolerance (perpendicular to red within 5?)
    if ( m_debug.isDebugVectors )
    {
        Matrix4 viewProj = proj * baseView;
        std::vector<std::pair<Vector3, Vector3>> velLines;
        std::vector<std::pair<Vector3, Vector3>> omegaLines;
        std::vector<std::pair<Vector3, Vector3>> upAlignedLines;
        std::vector<std::pair<Vector3, Vector3>> upErrorLines;
        const float axisToleranceRad = 5.0f * _PI / 180.0f;
        int modelCount = m_cGameModelCollection.GetModelCount();
        for ( int i = 0; i < modelCount; ++i )
        {
            GameModel& mdl = m_cGameModelCollection.GetModelAtIndex( i );
            Vector3 pos = mdl.GetPosition();
            Vector3 vel = mdl.GetVelocity();
            Vector3 omega = mdl.GetAngularVelocity();
            const float velScale = 0.5f;
            const float omegaScale = 2.0f;
            velLines.push_back( { pos, pos + vel * velScale } );
            omegaLines.push_back( { pos, pos + omega * omegaScale } );

            // White spike: starts at sphere centre, points along the visual "up" axis.
            // Length = 2.5? radius so it clearly protrudes above the ball surface.
            Vector3 orientUp = mdl.GetOrientationUp();
            float radius = mdl.GetBoundingRadius();
            bool isWithinPlaneTolerance = false;
            float omegaMag = Vector::VectorMag( omega );
            float orientUpMag = Vector::VectorMag( orientUp );
            if ( omegaMag > TOLERANCE && orientUpMag > TOLERANCE )
            {
                float dotRed = ( orientUp * omega ) / ( orientUpMag * omegaMag );
                if ( dotRed > 1.0f )
                {
                    dotRed = 1.0f;
                }
                else if ( dotRed < -1.0f )
                {
                    dotRed = -1.0f;
                }
                float redPerpDeviationRad = asinf( fabsf( dotRed ) );
                isWithinPlaneTolerance = ( redPerpDeviationRad <= axisToleranceRad );
            }

            Vector3 upLineEnd = pos + orientUp * ( radius * 2.5f );
            if ( isWithinPlaneTolerance )
            {
                upAlignedLines.push_back( { pos, upLineEnd } );
            }
            else
            {
                upErrorLines.push_back( { pos, upLineEnd } );
            }
        }
        SkullbonezHelper::DrawDebugVectors( viewProj, velLines, 0.0f, 1.0f, 0.0f );
        SkullbonezHelper::DrawDebugVectors( viewProj, omegaLines, 1.0f, 0.0f, 0.0f );
        SkullbonezHelper::DrawDebugVectors( viewProj, upAlignedLines, 0.0f, 0.0f, 1.0f );
        SkullbonezHelper::DrawDebugVectors( viewProj, upErrorLines, 1.0f, 1.0f, 1.0f );
    }

    // Broadphase spatial grid overlay (G key toggle)
    if ( m_debug.isBroadphaseOverlay )
    {
        Matrix4 viewProj = proj * baseView;
        m_broadphaseVisualizer.Render( viewProj );
    }

    if ( m_debug.physicsDebugFlags != PHYSICS_DEBUG_NONE )
    {
        Matrix4 viewProj = proj * baseView;
        m_physicsDebugVisualizer.SetFlags( m_debug.physicsDebugFlags );
        m_physicsDebugVisualizer.Render( m_cGameModelCollection, viewProj );
    }
}


void SkullbonezRun::SetUpCameras()
{
    m_systems.cameras = CameraCollection::Instance();

    m_systems.cameras->AddCamera( Vector3( 321.0f, 110.0f, 557.0f ), // Position
                                  Vector3( 581.0f, 40.0f, 633.0f ),  // View
                                  Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                  CAMERA_GAME_MODEL_1 );

    m_systems.cameras->AddCamera( Vector3( 730.0f, 100.0f, 380.0f ), // Position
                                  Vector3( 709.0f, 92.0f, 482.0f ),  // View
                                  Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                  CAMERA_GAME_MODEL_2 );

    m_systems.cameras->AddCamera( Vector3( 900.0f, 110.0f, 900.0f ), // Position
                                  Vector3( 313.0f, 31.0f, 282.0f ),  // View
                                  Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                  CAMERA_FREE );

    // set the camera m_boundaries
    m_systems.cameras->SetCameraXZBounds( m_systems.terrain->GetXZBounds() );

    // set the m_terrain
    m_systems.cameras->SetTerrain( m_systems.terrain.get() );

    // lock the m_cameras
    m_systems.cameras->SetLockedMode( true );
}


void SkullbonezRun::SetInitialOpenGlState()
{
    SkullbonezHelper::ResetGLResources();

    // load m_textures
    const SkullbonezConfig& cfg = Cfg();
    m_systems.textures->CreateJpegTexture( ( std::string( DATA_ROOT ) + cfg.terrainTexture ).c_str(), TEXTURE_GROUND );
    m_systems.textures->CreateJpegTexture( ( std::string( DATA_ROOT ) + cfg.sphereTexture ).c_str(), TEXTURE_BOUNDING_SPHERE );
}


void SkullbonezRun::DrawWindowText( const double dSecondsPerFrame )
{
    // Update rolling timers — runs every frame regardless of overlay state
    m_timers.updateTimer.StopTimer();
    m_timers.timeSinceLastRender += static_cast<float>( m_timers.updateTimer.GetElapsedTime() );
    m_timers.updateTimer.StartTimer();

    const double currentSceneEnergy = m_cGameModelCollection.GetSceneKineticEnergy();
    m_timers.sceneEnergyAccumulator += currentSceneEnergy;
    ++m_timers.sceneEnergySampleCount;

    if ( m_timers.timeSinceLastRender > 0.5f )
    {
        if ( dSecondsPerFrame )
        {
            m_timers.rollingFpsTime = 1.0f / static_cast<float>( dSecondsPerFrame );
            m_timers.rollingPhysicsTime = m_timers.physicsTime;
            m_timers.rollingRenderTime = m_timers.renderTime;
        }
        if ( m_timers.sceneEnergySampleCount > 0 )
        {
            m_timers.rollingSceneEnergy = static_cast<float>( m_timers.sceneEnergyAccumulator / static_cast<double>( m_timers.sceneEnergySampleCount ) );
            m_timers.sceneEnergyAccumulator = 0.0;
            m_timers.sceneEnergySampleCount = 0;
        }
        m_timers.timeSinceLastRender = 0.0f;
    }

    float sceneEnergyForDisplay = m_timers.rollingSceneEnergy;
    if ( m_timers.sceneEnergySampleCount > 0 && sceneEnergyForDisplay == 0.0f )
    {
        sceneEnergyForDisplay = static_cast<float>( m_timers.sceneEnergyAccumulator / static_cast<double>( m_timers.sceneEnergySampleCount ) );
    }

    const char* rendererName = Gfx().GetRendererName();

    // text_only mode: solid background + full-screen pangram, no HUD/profiler
    if ( m_debug.isTextOnly )
    {
        // Dark background covering the full viewport
        Text2d::Render2dQuad( -0.55f, -0.45f, 0.55f, 0.45f, 0.08f, 0.08f, 0.12f, 1.0f );

        // Three rows of the pangram — each line uses a slightly different colour
        // so hue/brightness fringing artefacts are visible on all channel combinations
        const float sz = 0.09f;
        Text2d::Render2dTextColor( -0.46f, 0.22f, sz, 1.00f, 1.00f, 1.00f, "The quick brown fox" );
        Text2d::Render2dTextColor( -0.46f, 0.07f, sz, 1.00f, 0.90f, 0.20f, "jumps over the" );
        Text2d::Render2dTextColor( -0.46f, -0.08f, sz, 0.40f, 0.90f, 1.00f, "lazy dog" );

        // Renderer name in small text at bottom so we know which backend we're looking at
        Text2d::Render2dTextColor( -0.46f, -0.38f, 0.015f, 0.60f, 0.60f, 0.60f, "renderer: %s", rendererName );

        Text2d::FlushText();
        return;
    }

    const float hw = Text2d::HalfW();
    const float hh = Text2d::HalfH();
    const float mX = 0.022f; // horizontal inset from left/right edge
    const float mY = 0.015f; // vertical inset from top/bottom edge

    // Crosshair — always visible when nudge mode is active, regardless of overlay state.
    // Drawn as two thin quads forming a + shape centred on screen.
    if ( m_camera.isNudgeMode )
    {
        const float cArm = 0.025f;                                                   // half-length of each crosshair arm
        const float cHalf = 0.001f;                                                  // half-thickness of each arm
        Text2d::Render2dQuad( -cArm, -cHalf, cArm, cHalf, 1.0f, 1.0f, 1.0f, 0.85f ); // horizontal
        Text2d::Render2dQuad( -cHalf, -cArm, cHalf, cArm, 1.0f, 1.0f, 1.0f, 0.85f ); // vertical
#ifdef _DEBUG
        if ( m_debug.reproSnapshotMessage[0] != '\0' &&
             m_timers.simulationTimer.GetTimeSinceLastStart() <= m_debug.reproSnapshotMessageUntil )
        {
            const float msgSz = 0.014f;
            float msgW = Text2d::MeasureText( msgSz, m_debug.reproSnapshotMessage );
            Text2d::Render2dTextColor( -msgW * 0.5f,
                                       -0.065f,
                                       msgSz,
                                       0.65f,
                                       0.92f,
                                       1.0f,
                                       "%s",
                                       m_debug.reproSnapshotMessage );
        }
#endif
    }

    const char* sceneName = "";
    if ( m_scene.isSceneMode && m_scene.currentSceneIndex >= 0 && m_scene.currentSceneIndex < static_cast<int>( m_sceneQueue.size() ) )
    {
        sceneName = FileNameFromPath( m_sceneQueue[m_scene.currentSceneIndex].c_str() );
    }

    if ( m_ui.IsVisible() )
    {
        InGameUiFrameData uiData;
        uiData.screenW = m_systems.window ? static_cast<int>( m_systems.window->m_sWindowDimensions.x ) : Cfg().window.screenX;
        uiData.screenH = m_systems.window ? static_cast<int>( m_systems.window->m_sWindowDimensions.y ) : Cfg().window.screenY;
        if ( m_debug.isUiTestPattern )
        {
            DrawUiTestPattern( uiData.screenW, uiData.screenH );
        }
        uiData.rendererName = rendererName;
        uiData.sceneName = sceneName;
        uiData.uiDrawCalls = m_timers.lastUiDrawCalls;
        uiData.fps = m_timers.rollingFpsTime > 0.0f ? m_timers.rollingFpsTime : ( dSecondsPerFrame > 0.0 ? 1.0f / static_cast<float>( dSecondsPerFrame ) : 0.0f );
        uiData.renderMs = ( m_timers.rollingRenderTime > 0.0f ? m_timers.rollingRenderTime : m_timers.renderTime ) * 1000.0f;
        uiData.physicsMs = ( m_timers.rollingPhysicsTime > 0.0f ? m_timers.rollingPhysicsTime : m_timers.physicsTime ) * 1000.0f;
        uiData.modelCount = m_scene.modelCount;
        uiData.currentFrame = m_scene.currentFrame;
        uiData.targetFrameCount = m_scene.targetFrameCount;
        uiData.rngSeed = m_scene.rngSeed;
        uiData.solverBallCount = m_scene.solverBallCount;
        uiData.solverBoxCount = m_scene.solverBoxCount;
        uiData.currentSceneIndex = m_scene.currentSceneIndex;
        uiData.sceneCount = static_cast<int>( m_sceneQueue.size() );
        uiData.now = m_timers.simulationTimer.GetTotalTime();
        uiData.sceneMode = m_scene.isSceneMode;
        uiData.scenePhysicsEnabled = m_scene.isScenePhysics;
        uiData.sceneTextEnabled = m_scene.isSceneText;
        uiData.textOnly = m_debug.isTextOnly;
        uiData.legacyPhysics = m_cGameModelCollection.GetLegacyMode();
        uiData.fixedStep = m_scene.isFixedStep;
        uiData.exitOnComplete = m_scene.isExitOnComplete;
        uiData.testComplete = m_scene.isTestComplete;
        uiData.vsyncEnabled = m_runtimeSettings.isVsyncEnabled;
        uiData.pipelineSyncEnabled = m_runtimeSettings.isPipelineSyncEnabled;
        uiData.rollAlignEnabled = m_runtimeSettings.isRollAlignEnabled;
        uiData.sceneEnergy = sceneEnergyForDisplay;
        uiData.timeScale = m_scene.timeScale;
        uiData.trackHeight = m_camera.trackBallIndex >= 0 ? m_camera.trackHeight : 0.0f;
        uiData.autoCycleInterval = m_camera.autoCycleInterval > 0.0f ? m_camera.autoCycleInterval : 0.0f;
        uiData.worldGravity = m_cWorldEnvironment.GetGravity();
        uiData.worldFluidHeight = m_cWorldEnvironment.GetFluidSurfaceHeight();
        uiData.worldFluidDensity = m_cWorldEnvironment.GetFluidDensity();
        uiData.physicsDebugFlags = m_debug.physicsDebugFlags;
        uiData.physicsDebugAlpha = m_debug.physicsDebugAlpha;
        uiData.physicsDebugContactLinger = m_debug.physicsDebugContactLinger;
        uiData.collisionVisualizer = m_debug.isCollisionVisualizer;
        uiData.physicsDebugTransparent = m_debug.isPhysicsDebugTransparent;
        uiData.debugVectors = m_debug.isDebugVectors;
        uiData.broadphaseOverlay = m_debug.isBroadphaseOverlay;
        uiData.waterFreezeDebug = m_debug.isWaterFreezeDebug;
        uiData.waterFlatDebug = m_debug.isWaterFlatDebug;
        uiData.terrainHidden = m_debug.isTerrainHidden;
        uiData.waterHidden = m_debug.isWaterHidden;
        uiData.waterNoReflect = m_debug.isWaterNoReflect;
        uiData.waterRTReflect = m_debug.isWaterRTReflect;
        uiData.canSaveSceneDefaults = m_scene.isSceneMode &&
                                      m_scene.currentSceneIndex >= 0 &&
                                      m_scene.currentSceneIndex < static_cast<int>( m_sceneQueue.size() ) &&
                                      !m_sceneQueue[m_scene.currentSceneIndex].empty();

        Text2d::FlushText();
        uiData.drawCallsBeforeUi = Gfx().GetFrameDrawCallCount();
        const int uiDrawCallStart = uiData.drawCallsBeforeUi;
        PROFILE_GPU_BEGIN( "Frame/UI/Quads" );
        m_ui.Draw( uiData );
        PROFILE_GPU_END( "Frame/UI/Quads" );
        PROFILE_GPU_BEGIN( "Frame/UI/Text" );
        Text2d::FlushText();
        PROFILE_GPU_END( "Frame/UI/Text" );
        const int uiDrawCallEnd = Gfx().GetFrameDrawCallCount();
        m_timers.lastUiDrawCalls = (std::max)( 0, uiDrawCallEnd - uiDrawCallStart );
        return;
    }

    // --- Overlay: None ---
    if ( m_debug.overlayMode == OverlayMode::None )
    {
        Text2d::FlushText();
        return;
    }

    // --- Overlay: Scene telemetry ---
    if ( m_debug.overlayMode == OverlayMode::SceneStats )
    {
        const float titleSz = 0.013f;
        const float entrySz = 0.012f;
        const float lineH = 0.025f;
        const float panPad = 0.014f;
        const float panW = 0.36f;
        const float panH = panPad * 2.0f + titleSz + lineH * 2.0f;
        const float panX0 = -( hw - mX );
        const float panY0 = -( hh - mY );
        const float panX1 = panX0 + panW;
        const float panY1 = panY0 + panH;

        Text2d::Render2dQuad( panX0, panY0, panX1, panY1, 0.04f, 0.04f, 0.07f, 0.93f );
        Text2d::Render2dTextColor( panX0 + panPad, panY1 - panPad - titleSz, titleSz, 1.0f, 0.85f, 0.35f, "SCENE TELEMETRY" );
        Text2d::Render2dTextColor( panX0 + panPad, panY1 - panPad - titleSz - lineH, entrySz, 0.85f, 0.85f, 0.85f, "Model Count: %d", m_scene.modelCount );
        Text2d::Render2dTextColor( panX0 + panPad,
                                   panY1 - panPad - titleSz - lineH * 2.0f,
                                   entrySz,
                                   0.85f,
                                   0.85f,
                                   0.85f,
                                   "Scene Energy: %.6f",
                                   sceneEnergyForDisplay );
        Text2d::FlushText();
        return;
    }

    // --- Overlay: Visual profiler bars (normalized or absolute) ---
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( m_debug.overlayMode == OverlayMode::BarsNormalized || m_debug.overlayMode == OverlayMode::BarsAbsolute )
    {
        // Panel anchored bottom-left, filling most of the width. Height kept modest — leave vertical
        // space above for future multi-core stacked rows.
        const float panW = ( hw - mX ) * 2.0f * 0.85f; // 85% of screen width
        const float panH = ( hh - mY ) * 2.0f * 0.22f; // 22% of screen height
        const float panX = -( hw - mX ) + mX * 0.5f;   // slight left margin
        const float panY = -( hh - mY ) + mY * 0.5f;   // slight bottom margin
        const bool absolute = ( m_debug.overlayMode == OverlayMode::BarsAbsolute );
        Profiler::Instance().RenderBarOverlay( panX, panY, panW, panH, absolute );
        Text2d::FlushText();
        return;
    }
#endif

    // --- Overlay: Keys reference screen (compact, bottom-left) ---
    if ( m_debug.overlayMode == OverlayMode::Keys )
    {
        const float titleSz = 0.013f;
        const float entrySz = 0.011f;
        const float lineH = 0.020f;
        const int nRows = 13;
        const float panPad = 0.012f;
        const float titleGap = 0.016f; // space between title baseline and first entry
        const float keyW = 0.058f;     // key-name column width
        const float descW = 0.120f;    // description column width
        const float colGap = 0.012f;   // gap between the two content columns

        // Panel dimensions — anchored to bottom-left corner
        const float panH = panPad + titleSz + titleGap + static_cast<float>( nRows ) * lineH + panPad;
        const float panW = panPad + keyW + descW + colGap + keyW + descW + panPad;
        const float panX0 = -( hw - mX );
        const float panY0 = -( hh - mY );
        const float panX1 = panX0 + panW;
        const float panY1 = panY0 + panH;

        Text2d::Render2dQuad( panX0, panY0, panX1, panY1, 0.04f, 0.04f, 0.07f, 0.93f );

        // Title left-aligned inside panel
        const float titleY = panY1 - panPad - titleSz;
        Text2d::Render2dTextColor( panX0 + panPad, titleY, titleSz, 1.0f, 0.85f, 0.35f, "KEYBOARD REFERENCE" );

        // Column X positions
        const float col1Key = panX0 + panPad;
        const float col1Desc = col1Key + keyW;
        const float col2Key = col1Desc + descW + colGap;
        const float col2Desc = col2Key + keyW;
        const float firstY = titleY - titleGap;

        struct KeyEntry
        {
            const char* key;
            const char* desc;
        };
        static const KeyEntry kLeft[nRows] = {
            { "N", "Nudge mode" },
            { "Enter", "Dump repro" },
            { "F", "Fly mode" },
            { "WASD", "Move camera" },
            { "Mouse", "Look" },
            { "Shift", "Sprint (3x speed)" },
            { "Z", "Fire ball" },
            { "X", "Fire box" },
            { "P", "Physics solver" },
            { "Q", "Cycle renderer" },
            { "V", "Collision visual" },
            { "Space", "Step physics" },
            { "R/Bksp", "Reset scene" },
        };
        static const KeyEntry kRight[nRows] = {
            { "Esc", "Quit" },
            { "0", "Cycle overlay" },
            { "1", "Freeze water" },
            { "2", "Reflection mode" },
            { "3", "Toggle water flat" },
            { "4", "Toggle terrain" },
            { "5", "Toggle water" },
            { "6", "Debug body alpha" },
            { "9", "Debug vectors" },
            { "G", "Broadphase overlay" },
            { "C", "Physics debug" },
            { "PgUp/Dn", "Water height" },
            { "F3", "Screenshot" },
        };

        for ( int i = 0; i < nRows; ++i )
        {
            float y = firstY - static_cast<float>( i ) * lineH;
            Text2d::Render2dTextColor( col1Key, y, entrySz, 0.70f, 0.88f, 1.0f, "%s", kLeft[i].key );
            Text2d::Render2dTextColor( col1Desc, y, entrySz, 0.85f, 0.85f, 0.85f, "%s", kLeft[i].desc );
            Text2d::Render2dTextColor( col2Key, y, entrySz, 0.70f, 0.88f, 1.0f, "%s", kRight[i].key );
            Text2d::Render2dTextColor( col2Desc, y, entrySz, 0.85f, 0.85f, 0.85f, "%s", kRight[i].desc );
        }

        Text2d::FlushText();
        return;
    }

    // --- Overlay: Timers / HUD (OverlayMode::Timers) ---

    // Profiler overlay — bottom-left anchored.
    // Compiled out in Release; always shown when overlay is Timers in Debug/Profile.
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    {
        const float lineH = 0.018f;
        const float profFSz = 0.012f;
        const float padY = lineH * 1.2f;
        Profiler::Instance().RenderOverlay( -( hw - mX ), -( hh - mY ) - padY, lineH, profFSz, m_timers.rollingFpsTime );
    }
#endif

    Text2d::FlushText();
}


void SkullbonezRun::SetViewingOrientation()
{
    // In scene mode, use the first camera without cycling.
    // If ball-tracking is active, keep the camera locked onto the selected ball.
    if ( m_scene.isSceneMode )
    {
        if ( m_camera.trackBallIndex >= 0 && m_camera.trackBallIndex < m_cGameModelCollection.GetModelCount() )
        {
            Vector3 ballPos = m_cGameModelCollection.GetModelPosition( m_camera.trackBallIndex );
            m_systems.cameras->SetPrimaryPosition( Vector3( ballPos.x, ballPos.y + m_camera.trackHeight, ballPos.z ) );
            m_systems.cameras->SetViewCoordinates( ballPos );
        }
        return;
    }

    // In fly mode, freeze the cycle clock and keep the free camera
    if ( m_camera.isFlyMode )
    {
        m_camera.cameraTime = 0.0f;
        m_timers.cameraTimer.StopTimer();
        m_timers.cameraTimer.StartTimer();
        return;
    }

    // set viewing m_orientation
    /*
        if(Input::IsKeyDown('1')) m_camera.selectedCamera = 0;
        if(Input::IsKeyDown('2')) m_camera.selectedCamera = 1;
        if(Input::IsKeyDown('3')) m_camera.selectedCamera = 2;
    */

    // maintain the camera timer
    m_timers.cameraTimer.StopTimer();
    m_camera.cameraTime += static_cast<float>( m_timers.cameraTimer.GetElapsedTime() );
    m_timers.cameraTimer.StartTimer();

    // change the viewing camera automatically
    if ( m_camera.cameraTime > 5.0f )
    {
        ++m_camera.selectedCamera;
        if ( m_camera.selectedCamera == 3 )
        {
            m_camera.selectedCamera = 0;
        }
        m_camera.cameraTime = 0.0f;
    }

    // select camera based on input
    switch ( m_camera.selectedCamera )
    {
    case 0:
        m_systems.cameras->SelectCamera( CAMERA_GAME_MODEL_1, true );
        break;
    case 1:
        m_systems.cameras->SelectCamera( CAMERA_GAME_MODEL_2, true );
        break;
    case 2:
        m_systems.cameras->SelectCamera( CAMERA_FREE, true );
        break;
    }

    // set the view m_position of the selected camera based on the game model m_position
    if ( m_systems.cameras->IsCameraSelected( CAMERA_GAME_MODEL_1 ) )
    {
        m_systems.cameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 0 ) );
    }
    if ( m_systems.cameras->IsCameraSelected( CAMERA_GAME_MODEL_2 ) )
    {
        m_systems.cameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 1 ) );
    }

    /*
        // reset relativity when a new request for synchronisation comes in
        if(m_camera.input.Get( InputState::Aux1 )) m_systems.cameras->ResetRelativity();

        // sync m_cameras if in sync mode
        if(m_camera.input.Get( InputState::Aux2 ))
        {
            // perform the relative update
            RelativeUpdateCamera(CAMERA_GAME_MODEL_1);
            RelativeUpdateCamera(CAMERA_GAME_MODEL_2);
            RelativeUpdateCamera(CAMERA_FREE);

            // reset the relative variable as we have already performed the action on desired m_cameras
            m_systems.cameras->ResetRelativity();
        }
    */
}


void SkullbonezRun::RelativeUpdateCamera( uint32_t hash )
{
    if ( !m_systems.cameras->IsCameraSelected( hash ) )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation( hash );
        float minY = m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) + Cfg().minCameraHeight;
        m_systems.cameras->RelativeUpdate( hash, minY, Cfg().maxCameraHeight );
    }
}


void SkullbonezRun::MoveCamera( float keyMovementQty, float mouseMovementQty )
{
    if ( m_camera.isFlyMode )
    {
        // Shift held = 3x speed
        float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;

        // Mouse look
        if ( m_camera.input.xMove != 0 || m_camera.input.yMove != 0 )
        {
            m_systems.cameras->RotatePrimary( m_camera.input.xMove * mouseMovementQty,
                                              m_camera.input.yMove * mouseMovementQty );
        }

        // WASD movement
        if ( m_camera.input.Get( InputState::Up ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Forward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Left ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Left, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Down ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Backward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Right ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Right, keyMovementQty * speedMult );
        }

        // Capture movement buffer before Apply zeroes it — used for model nudge below
        const Vector3 moveVec = m_systems.cameras->GetPrimaryMovementBuffer();
        m_systems.cameras->ApplyPrimaryMovementBuffer();

        NudgeModelsWithCamera( moveVec );
    }

    // Clamp camera Y between m_terrain surface and Cfg().maxCameraHeight (not in fly mode, not in scene mode)
    if ( !m_camera.isFlyMode && !m_scene.isSceneMode )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation();
        float minY = m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) + Cfg().minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_systems.cameras->AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > Cfg().maxCameraHeight )
        {
            m_systems.cameras->AmmendPrimaryY( Cfg().maxCameraHeight );
        }
    }
}


void SkullbonezRun::NudgeModelsWithCamera( const Vector3& moveVec )
{
    float magSq = VectorMagSquared( moveVec );
    if ( magSq < 1e-8f )
    {
        return;
    }

    // Nudge reach: camera "body" radius added to the model's bounding radius for the overlap test.
    static constexpr float CAMERA_NUDGE_RADIUS = 15.0f;

    // Camera speed this frame (same formula as MoveCamera: keySpeed * speedMult)
    float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;
    float nudgeSpeed = Cfg().keySpeed * speedMult;

    // Direction the camera is moving
    Vector3 nudgeDir = moveVec * ( 1.0f / sqrtf( magSq ) );

    const Vector3& camPos = m_systems.cameras->GetCameraTranslation();

    int count = m_cGameModelCollection.GetModelCount();
    for ( int i = 0; i < count; ++i )
    {
        GameModel& model = m_cGameModelCollection.GetModelAtIndex( i );
        if ( model.IsFixed() )
        {
            continue;
        }
        Vector3 toModel = model.GetPosition() - camPos;

        // Only push models that are in the direction we're moving — no pulling things behind us
        if ( ( toModel * nudgeDir ) <= 0.0f )
        {
            continue;
        }

        float distSq = VectorMagSquared( toModel );
        float reach = model.GetBoundingRadius() + CAMERA_NUDGE_RADIUS;
        if ( distSq > reach * reach )
        {
            continue;
        }

        // Bring the ball's velocity component along the nudge direction up to camera speed.
        // Guard prevents the formula from becoming subtractive and braking the ball.
        const Vector3& vel = model.GetVelocity();
        float currentComponent = vel * nudgeDir;
        if ( currentComponent < nudgeSpeed )
        {
            model.SetLinearVelocity( vel + nudgeDir * ( nudgeSpeed - currentComponent ) );
        }
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

    const char* scenePath = "<legacy/random>";
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
    const char* physicsMode = m_cGameModelCollection.GetLegacyMode() ? "legacy" : "solver";
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
        const Vector3& he = box.GetHalfExtents();
        Quaternion qCopy = model.GetOrientation();
        RotationMatrix orientMat = qCopy.GetOrientationMatrix();
        constexpr float vertexSupportSlack = 0.15f;
        float supportGap = Cfg().contactEpsilon + vertexSupportSlack;
        bool foundVertex = false;
        float minGap = FLT_MAX;
        float maxGap = -FLT_MAX;
        int supported = 0;

        for ( int v = 0; v < 8; ++v )
        {
            Vector3 local(
                ( v & 1 ) ? he.x : -he.x,
                ( v & 2 ) ? he.y : -he.y,
                ( v & 4 ) ? he.z : -he.z );
            Vector3 worldVertex = pos + ( orientMat * local );
            if ( !m_systems.terrain->IsInBounds( worldVertex.x, worldVertex.z ) )
            {
                continue;
            }

            float vertexTerrainHeight = 0.0f;
            Plane vertexPlane;
            m_systems.terrain->GetTerrainHeightAndPlaneAt( worldVertex.x,
                                                           worldVertex.z,
                                                           vertexTerrainHeight,
                                                           vertexPlane );
            float gap = worldVertex.y - vertexTerrainHeight;
            if ( gap <= supportGap )
            {
                ++supported;
            }
            if ( gap < minGap )
            {
                minGap = gap;
            }
            if ( gap > maxGap )
            {
                maxGap = gap;
            }
            foundVertex = true;
        }

        if ( foundVertex )
        {
            boxTerrainSupportedVertices = supported;
            boxMinTerrainGap = minGap;
            boxMaxTerrainGap = maxGap;
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
    fprintf( f, "fixed_step_effective,%d\n", m_scene.isFixedStep ? 1 : 0 );
    fprintf( f, "cmd_fixed_step_override,%d\n", m_cmdFixedStep ? 1 : 0 );
    fprintf( f, "time_scale,%.6f\n", m_scene.timeScale );
    fprintf( f, "renderer,%s\n", rendererName );
    fprintf( f, "physics_mode,%s\n", physicsMode );
    fprintf( f, "generated_object_override,%s\n", generatedObjectOverride );
    fprintf( f, "model_count,%d\n", m_cGameModelCollection.GetModelCount() );
    fprintf( f, "roll_align_enabled,%d\n", m_runtimeSettings.isRollAlignEnabled ? 1 : 0 );
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
                 m_cGameModelCollection.GetLegacyMode() ? " --legacy-physics" : "",
                 m_cmdNoWater ? " --no-water" : "",
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
                 m_cGameModelCollection.GetLegacyMode() ? " --legacy-physics" : "",
                 m_cmdNoWater ? " --no-water" : "",
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


void SkullbonezRun::FireProjectile( bool isBox )
{
    // Legacy mode has no boxes — ALT fires nothing.
    if ( isBox && m_cGameModelCollection.GetLegacyMode() )
    {
        return;
    }

    int count = m_cGameModelCollection.GetModelCount();
    if ( count == 0 )
    {
        return;
    }

    // Pick the next model of the right type by stepping backwards through the array.
    // m_fire.ballNext / m_fire.boxNext remembers where we left off so rapid-fire
    // successive shots use different objects instead of always grabbing the same one.
    int& cycleIdx = isBox ? m_fire.boxNext : m_fire.ballNext;
    if ( cycleIdx < 0 || cycleIdx >= count )
    {
        cycleIdx = count - 1;
    }

    int found = -1;
    for ( int i = 0; i < count; ++i )
    {
        int idx = ( ( cycleIdx - i ) % count + count ) % count;
        GameModel& candidate = m_cGameModelCollection.GetModelAtIndex( idx );
        if ( !candidate.IsFixed() && candidate.IsBox() == isBox )
        {
            found = idx;
            cycleIdx = ( ( idx - 1 ) % count + count ) % count;
            break;
        }
    }

    if ( found < 0 )
    {
        return; // No models of the requested type in the scene
    }

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( found );

    // Camera forward direction (normalised look vector)
    const Vector3& camPos = m_systems.cameras->GetCameraTranslation();
    const Vector3& camView = m_systems.cameras->GetCameraView();
    Vector3 forward = camView - camPos;
    float lenSq = forward * forward;
    if ( lenSq < 1e-8f )
    {
        return;
    }
    forward = forward * ( 1.0f / sqrtf( lenSq ) );

    // Spawn just ahead of the camera, clear of the model's bounding radius
    float clearance = model.GetBoundingRadius() * 2.0f + 2.0f;
    Vector3 spawnPos = camPos + forward * clearance;

    // Speed matches camera walk speed; Shift gives the 3× sprint multiplier
    float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;
    float fireSpeed = Cfg().keySpeed * speedMult;

    // Wake the recycled model before repositioning it — a settled model's sleep state
    // must be cleared or RunSolverPhysics will skip gravity/integration and it hangs in air.
    m_cGameModelCollection.WakeModel( found );

    model.SetPosition( spawnPos );
    model.SetLinearVelocity( forward * fireSpeed );
    model.SetAngularVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
}


void SkullbonezRun::SetUpCamerasFromScene( const TestScene& scene )
{
    m_systems.cameras = CameraCollection::Instance();

    for ( int i = 0; i < scene.GetCameraCount(); ++i )
    {
        const SceneCamera& cam = scene.GetCamera( i );
        uint32_t hash = HashStr( cam.name );
        m_systems.cameras->AddCamera( cam.m_position, cam.view, cam.up, hash );
    }

    // set the camera m_boundaries
    m_systems.cameras->SetCameraXZBounds( m_systems.terrain->GetXZBounds() );

    // set the m_terrain
    m_systems.cameras->SetTerrain( m_systems.terrain.get() );

    // lock the m_cameras
    m_systems.cameras->SetLockedMode( false );
}


void SkullbonezRun::SetUpGameModelsFromScene( const TestScene& scene )
{
    m_scene.modelCount = scene.GetBallCount() + scene.GetBallStateCount() + scene.GetBoxCount();

    for ( int i = 0; i < scene.GetBallCount(); ++i )
    {
        const SceneBall& ball = scene.GetBall( i );

        GameModel gameModel( &m_cWorldEnvironment,
                             Vector3( ball.posX, ball.posY, ball.posZ ),
                             Vector3( ball.moment, ball.moment, ball.moment ),
                             ball.m_mass );

        gameModel.SetCoefficientRestitution( ball.restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.SetName( ball.name );
        gameModel.AddBoundingSphere( ball.m_radius );

        // apply initial orientation if specified (euler angles in degrees, XYZ order)
        if ( ball.hasInitOrient )
        {
            gameModel.SetInitialOrientation( ball.eulerX, ball.eulerY, ball.eulerZ );
        }

        // apply force if any is specified
        if ( ball.forceX != 0.0f || ball.forceY != 0.0f || ball.forceZ != 0.0f )
        {
            gameModel.SetImpulseForce(
                Vector3( ball.forceX, ball.forceY, ball.forceZ ),
                Vector3( ball.forcePosX, ball.forcePosY, ball.forcePosZ ) );
        }

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    // ball_state entries: full dynamic state from a snapshot
    for ( int i = 0; i < scene.GetBallStateCount(); ++i )
    {
        const SceneBallState& bs = scene.GetBallState( i );

        GameModel gameModel( &m_cWorldEnvironment,
                             Vector3( bs.posX, bs.posY, bs.posZ ),
                             Vector3( bs.inertiaX, bs.inertiaY, bs.inertiaZ ),
                             bs.mass );

        gameModel.SetCoefficientRestitution( bs.restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.SetName( bs.name );
        gameModel.AddBoundingSphere( bs.radius );
        gameModel.SetLinearVelocity( Vector3( bs.velX, bs.velY, bs.velZ ) );
        gameModel.SetAngularVelocity( Vector3( bs.angVelX, bs.angVelY, bs.angVelZ ) );
        gameModel.SetOrientation( Quaternion( bs.orientX, bs.orientY, bs.orientZ, bs.orientW ) );

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    // box entries: rigid box entities
    for ( int i = 0; i < scene.GetBoxCount(); ++i )
    {
        const SceneBox& box = scene.GetBox( i );

        // Box inertia: I = m/3 * (hy² + hz²) etc. for half-extents
        float hx2 = box.halfX * box.halfX;
        float hy2 = box.halfY * box.halfY;
        float hz2 = box.halfZ * box.halfZ;
        float m3 = box.mass / 3.0f;
        Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

        GameModel gameModel( &m_cWorldEnvironment,
                             Vector3( box.posX, box.posY, box.posZ ),
                             inertia,
                             box.mass );

        gameModel.SetCoefficientRestitution( box.restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.SetName( box.name );
        gameModel.AddBoundingBox( Vector3( box.halfX, box.halfY, box.halfZ ) );

        if ( box.hasInitOrient )
        {
            gameModel.SetInitialOrientation( box.eulerX, box.eulerY, box.eulerZ );
        }

        if ( box.hasInitVelocity )
        {
            gameModel.SetLinearVelocity( Vector3( box.velX, box.velY, box.velZ ) );
        }

        gameModel.SetFixed( box.isFixed );

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }
}


void SkullbonezRun::LoadScene( int index )
{
#ifdef _DEBUG
    EndPhysicsDiagnosticsRun( "scene_reload" );
#endif

    // Flush GPU before destroying scene resources to avoid use-after-free
    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }

    m_scene.currentSceneIndex = index;
    ++m_scene.loadCount;
    const std::string& scenePath = m_sceneQueue[index];

    // Close previous perf log if open
    if ( m_perfLogState.perfLogFile )
    {
        LogPerfMemory( "end" );
        if ( m_perfLogState.perfLogWritesSinceFlush > 0 )
        {
            fflush( m_perfLogState.perfLogFile );
            m_perfLogState.perfLogWritesSinceFlush = 0;
        }
        fclose( m_perfLogState.perfLogFile );
        m_perfLogState.perfLogFile = nullptr;
    }

    // Reset scene config to defaults
    m_scene.isScenePhysics = true;
    m_scene.isSceneText = true;
    m_perfLogState.isPerfTest = false;
    m_perfLogState.perfHeaderWritten = false;
    m_screenshot.isScreenshotSaved = false;
    m_screenshot.isScreenshotAndExit = false;
    m_scene.targetFrameCount = -1;
    m_scene.currentFrame = 0;
    m_scene.solverBallCount = 0;
    m_scene.solverBoxCount = 0;
    m_scene.isTestComplete = false;
    m_timers.physicsAccumulator = 0.0f;
    m_screenshot.screenshotFrame = -1;
    m_screenshot.screenshotMs = -1;
    m_screenshot.screenshotPath[0] = '\0';
    m_screenshot.screenshotInterval = -1;
    m_screenshot.intervalCaptureCount = 0;
    m_screenshot.screenshotDir[0] = '\0';
    m_perfLogState.perfLogPath[0] = '\0';
    m_perfLogState.isPerfLogFlushEnabled = false;
    m_perfLogState.perfLogFlushInterval = 0;
    m_perfLogState.perfLogWritesSinceFlush = 0;
    m_runtimeSettings.isVsyncEnabled = Cfg().runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = Cfg().runtimeRender.forcePipelineSync;
    m_runtimeSettings.isRollAlignEnabled = m_runtimeSettings.defaultRollAlignEnabled;
    Cfg().rollAlignEnabled = m_runtimeSettings.isRollAlignEnabled;

    // Reset cameras and game models
    m_systems.cameras->Reset();
    m_cGameModelCollection.Clear();

    // Reset input and debug state
    m_camera.isFlyMode = false;
    m_camera.isNudgeMode = false;
    m_fire.ballNext = -1;
    m_fire.boxNext = -1;
    m_debug.isWaterFreezeDebug = false;
    m_debug.isWaterNoReflect = false;
    m_debug.isWaterRTReflect = false;
    m_debug.isWaterFlatDebug = false;
    m_debug.isTerrainHidden = false;
    m_debug.isWaterHidden = false;
    m_debug.isDebugVectors = false;
    m_debug.isTextOnly = false;
    m_debug.isUiTestPattern = false;
    m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
    m_debug.isPhysicsDebugTransparent = false;
    m_debug.physicsDebugAlpha = 0.28f;
    m_debug.physicsDebugContactLinger = 0.45f;
    m_physicsDebugVisualizer.SetFlags( PHYSICS_DEBUG_NONE );
#ifdef _DEBUG
    m_debug.reproSnapshotMessage[0] = '\0';
    m_debug.reproSnapshotMessageUntil = 0.0;
#endif
    m_scene.timeScale = 1.0f;
    m_scene.isFixedStep = false;
    m_scene.isExitOnComplete = false;
    m_debug.frozenWaterTime = 0.0f;
    m_camera.trackBallIndex = -1;
    m_camera.trackHeight = 300.0f;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
    m_camera.autoCycleShotsTaken = 0;
    m_camera.input = {};
    // overlayMode intentionally preserved — the user's HUD state persists across scene reloads.
    m_camera.selectedCamera = 0;

    // Reset timing
    m_timers.timeSinceLastRender = 0.0f;
    m_timers.renderTime = 0.0f;
    m_camera.cameraTime = 0.0f;
    m_timers.rollingRenderTime = 0.0f;
    m_timers.physicsTime = 0.0f;
    m_timers.rollingPhysicsTime = 0.0f;
    m_timers.rollingFpsTime = 0.0f;
    m_timers.rollingSceneEnergy = 0.0f;
    m_timers.sceneEnergyAccumulator = 0.0;
    m_timers.sceneEnergySampleCount = 0;
    m_timers.lastUiDrawCalls = 0;

    // Reseed RNG. Unseeded reruns mix in the load/reset counters so quick repeated
    // Q resets do not collapse to the same time(nullptr) seed. Scene files and CLI
    // overrides can still pin this exactly for repro.
    unsigned int rngSeed = static_cast<unsigned int>( time( nullptr ) );
    rngSeed ^= static_cast<unsigned int>( m_scene.loadCount ) * 2654435761u;
    rngSeed ^= static_cast<unsigned int>( m_scene.manualResetCount ) * 2246822519u;
    if ( rngSeed == 0 )
    {
        rngSeed = 1;
    }

    // Branch on scene mode vs legacy mode
    if ( scenePath.empty() )
    {
        if ( m_cmdSeedOverride > 0 )
        {
            rngSeed = m_cmdSeedOverride;
        }
        m_scene.rngSeed = rngSeed;
        srand( rngSeed );
        ApplyNoWaterOverride();

        m_scene.isSceneMode = false;
        SetUpCameras();
        if ( m_uiSolverBallCountOverride >= 0 || m_uiSolverBoxCountOverride >= 0 )
        {
            SetUpSolverObjects( (std::max)( 0, m_uiSolverBallCountOverride ), (std::max)( 0, m_uiSolverBoxCountOverride ) );
        }
        else
        {
            SetUpGameModels( m_uiModelCountOverride >= 0 ? m_uiModelCountOverride : DEFAULT_GAME_MODELS );
        }
        const char* rendererName = Gfx().GetRendererName();
        char titleText[256];
        sprintf_s( titleText, "%s [%s]", TITLE_TEXT, rendererName );
        m_systems.window->SetTitleText( titleText );
    }
    else
    {
        m_scene.isSceneMode = true;
        TestScene scene = TestScene::LoadFromFile( scenePath.c_str() );
        m_scene.isScenePhysics = scene.IsPhysicsEnabled();
        m_scene.isSceneText = scene.IsTextEnabled();
        m_debug.isDebugVectors = scene.IsDebugVectors();
        m_perfLogState.isPerfLogFlushEnabled = scene.IsPerfLogFlushEnabled();
        m_perfLogState.perfLogFlushInterval = scene.GetPerfLogFlushInterval();
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
        if ( scene.HasRollAlignOverride() )
        {
            m_runtimeSettings.isRollAlignEnabled = scene.IsRollAlignEnabled();
        }
        Cfg().rollAlignEnabled = m_runtimeSettings.isRollAlignEnabled;
        m_debug.isTextOnly = scene.IsTextOnly();
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
        m_scene.timeScale = scene.GetTimeScale();
        m_scene.isFixedStep = scene.IsFixedStep();

        const SceneUiOptions& uiOptions = scene.GetUiOptions();
        const double uiNow = m_timers.simulationTimer.GetTotalTime();
        if ( !uiOptions.hasVisible && m_debug.overlayMode == OverlayMode::None )
        {
            m_ui.SetVisible( false, uiNow );
        }
        if ( uiOptions.hasWindowRect )
        {
            m_ui.SetWindowBounds( uiOptions.windowX, uiOptions.windowY, uiOptions.windowW, uiOptions.windowH );
        }
        if ( uiOptions.hasActiveTab )
        {
            m_ui.SetActiveTab( static_cast<InGameUiTab>( uiOptions.activeTab ) );
        }
        if ( uiOptions.hasBlur )
        {
            m_ui.SetBlurEnabled( uiOptions.blurEnabled );
        }
        if ( uiOptions.hasProfilerExpandAll )
        {
            m_ui.SetProfilerExpandAll( uiOptions.profilerExpandAll );
        }
        if ( uiOptions.hasProfilerTimeline )
        {
            m_ui.SetProfilerTimelineEnabled( uiOptions.profilerTimeline );
        }
        if ( uiOptions.hasRendererComboOpen )
        {
            m_ui.SetRendererComboOpen( uiOptions.rendererComboOpen );
        }
        m_ui.SetMouseOverride( uiOptions.hasMouseOverride, uiOptions.mouseX, uiOptions.mouseY );
        if ( uiOptions.hasVisible )
        {
            m_ui.SetVisible( uiOptions.isVisible, uiNow );
        }
        if ( uiOptions.hasMinimized )
        {
            m_ui.SetMinimized( uiOptions.isMinimized, uiNow );
        }
        if ( uiOptions.hasTestPattern )
        {
            m_debug.isUiTestPattern = uiOptions.testPatternEnabled;
        }

        // Apply per-scene physics mode override — supersedes the --legacy CLI flag for this scene.
        // physicsMode 0 = inherit (no change), 1 = legacy, 2 = impulse solver.
        if ( scene.GetPhysicsMode() != 0 )
        {
            m_cGameModelCollection.SetLegacyMode( scene.GetPhysicsMode() == 1 );
        }
        if ( m_generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
        {
            m_cGameModelCollection.SetLegacyMode( false );
        }

        m_scene.targetFrameCount = scene.GetFrameCount();
        m_scene.isExitOnComplete = scene.IsExitOnComplete();
        m_screenshot.screenshotFrame = scene.GetScreenshotFrame();
        m_screenshot.screenshotMs = scene.GetScreenshotMs();
        m_screenshot.isScreenshotAndExit = scene.IsScreenshotAndExit();

        if ( scene.GetScreenshotPath()[0] != '\0' )
        {
            strcpy_s( m_screenshot.screenshotPath, sizeof( m_screenshot.screenshotPath ), scene.GetScreenshotPath() );
        }
        // Interval capture: create output directory
        m_screenshot.screenshotInterval = scene.GetScreenshotInterval();
        if ( scene.GetScreenshotDir()[0] != '\0' )
        {
            strcpy_s( m_screenshot.screenshotDir, sizeof( m_screenshot.screenshotDir ), scene.GetScreenshotDir() );
            CreateDirectoryA( m_screenshot.screenshotDir, nullptr );
        }

        // Perf test: open CSV log file
        const char* pPerfPath = scene.GetPerfLogPath();
        if ( pPerfPath[0] != '\0' )
        {
            m_perfLogState.isPerfTest = true;
            strcpy_s( m_perfLogState.perfLogPath, sizeof( m_perfLogState.perfLogPath ), pPerfPath );
            const char* mode = ( sPerfPass == 0 ) ? "w" : "a";
            fopen_s( &m_perfLogState.perfLogFile, m_perfLogState.perfLogPath, mode );
            if ( m_perfLogState.perfLogFile )
            {
                m_perfLogState.perfLogWritesSinceFlush = 0;
                LogPerfMemory( "start" );
            }
        }

        // Physics regression log: legacy per-frame CSV enabled only by command line.
#ifdef _DEBUG
        m_cGameModelCollection.SetPhysicsRegressionLogPath( m_perfLogState.physicsRegressionLogOverride );
#endif

        // Override RNG seed for deterministic scenes. CLI --seed wins so a nudge snapshot can
        // replay an unseeded/random scene or deliberately override a scene file seed.
        if ( scene.GetSeed() > 0 )
        {
            rngSeed = scene.GetSeed();
        }
        if ( m_cmdSeedOverride > 0 )
        {
            rngSeed = m_cmdSeedOverride;
        }
        m_scene.rngSeed = rngSeed;
        srand( rngSeed );

        // Replace terrain with analytic flat slope when the scene requests it
        if ( scene.HasFlatSlope() )
        {
            Gfx().FlushGPU();
            m_systems.terrain = std::make_unique<Terrain>( scene.GetFlatBaseY(), scene.GetFlatSlopeX(), scene.GetFlatSlopeZ() );
        }

        // Override world environment if scene specifies world values
        if ( scene.HasWorldOverride() )
        {
            m_cWorldEnvironment = WorldEnvironment( scene.GetWorldFluidHeight(), scene.GetWorldFluidDensity(), Cfg().gasDensity, scene.GetWorldGravity() );
            XZBounds tb = m_systems.terrain->GetXZBounds();
            m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );
        }
        ApplyNoWaterOverride();

        SetUpCamerasFromScene( scene );

        if ( m_uiSolverBallCountOverride >= 0 || m_uiSolverBoxCountOverride >= 0 )
        {
            SetUpSolverObjects( (std::max)( 0, m_uiSolverBallCountOverride ), (std::max)( 0, m_uiSolverBoxCountOverride ) );
        }
        else if ( m_uiModelCountOverride >= 0 )
        {
            SetUpGameModels( m_uiModelCountOverride );
        }
        else if ( scene.GetSolverBallCount() > 0 || scene.GetSolverBoxCount() > 0 )
        {
            // Exact-count solver spawn — explicit ball/box split for benchmarks.
            SetUpSolverObjects( scene.GetSolverBallCount(), scene.GetSolverBoxCount() );
        }
        else if ( scene.HasLegacyBallCount() )
        {
            SetUpGameModels( scene.GetLegacyBallCount() );
        }
        else
        {
            SetUpGameModelsFromScene( scene );
        }

        // Ball-tracking camera: enabled when scene specifies a positive track_height
        if ( scene.GetTrackHeight() > 0.0f )
        {
            m_camera.trackHeight = scene.GetTrackHeight();
            m_camera.trackBallIndex = 0;
            m_camera.autoCycleInterval = scene.GetAutoCycleInterval(); // -1 if not specified = disabled
        }

        const char* rendererName = Gfx().GetRendererName();
        char titleText[256];
        sprintf_s( titleText, "%s [SCENE MODE] [%s]", TITLE_TEXT, rendererName );
        m_systems.window->SetTitleText( titleText );

        // Snapshot scenes (ball_state) start paused in free camera mode ?
        // user presses F to resume simulation and attach to scene camera
        if ( scene.GetBallStateCount() > 0 )
        {
            m_camera.isFlyMode = true;
            m_camera.cameraTime = 0.0f;
            XZBounds unbounded;
            unbounded.m_xMin = -99999.9f;
            unbounded.m_xMax = 99999.9f;
            unbounded.m_zMin = -99999.9f;
            unbounded.m_zMax = 99999.9f;
            uint32_t activeCam = m_systems.cameras->GetSelectedCameraName();
            m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
            SetCursor( nullptr );
            Input::CentreMouseCoordinates();
            m_camera.input.xMove = 0;
            m_camera.input.yMove = 0;
        }
    }

    // CLI --time-scale and --fixed-step override anything the scene file (or legacy defaults) set.
    if ( m_cmdTimeScaleOverride > 0.0f )
    {
        m_scene.timeScale = m_cmdTimeScaleOverride;
    }
    if ( m_uiTimeScaleOverride > 0.0f )
    {
        m_scene.timeScale = m_uiTimeScaleOverride;
    }
    if ( m_cmdFixedStep )
    {
        m_scene.isFixedStep = true;
    }
    if ( m_cmdHasPhysicsDebugFlagsOverride )
    {
        m_debug.physicsDebugFlags = m_cmdPhysicsDebugFlagsOverride;
    }
    if ( m_cmdHasPhysicsDebugTransparentOverride )
    {
        m_debug.isPhysicsDebugTransparent = m_cmdPhysicsDebugTransparentOverride;
    }
    if ( m_cmdHasPhysicsDebugAlphaOverride )
    {
        m_debug.physicsDebugAlpha = m_cmdPhysicsDebugAlphaOverride;
    }
    if ( m_cmdHasPhysicsDebugContactLingerOverride )
    {
        m_debug.physicsDebugContactLinger = m_cmdPhysicsDebugContactLingerOverride;
    }

#ifdef _DEBUG
    BeginPhysicsDiagnosticsRun( scenePath.c_str() );
#endif

    // Apply runtime swap policy after config/scene overrides are resolved.
    Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );

    // Restart timers
    m_timers.frameTimer.StartTimer();
    m_timers.workTimer.StartTimer();
    m_timers.updateTimer.StartTimer();
    m_timers.cameraTimer.StartTimer();
    m_timers.simulationTimer.StartTimer();

    // Initialize DXR raytracing on first scene load (requires terrain + sphere meshes to exist)
    // Force sphere mesh creation (normally lazy-init on first render)
    if ( Gfx().IsDXRSupported() && SkullbonezHelper::GetSphereInstMeshHandle() == 0 )
    {
        SkullbonezHelper::EnsureSphereMesh();
    }
    {
    }
    if ( Gfx().IsDXRSupported() && m_systems.terrain && m_systems.terrain->GetMesh() )
    {
        IMesh* terrainMesh = m_systems.terrain->GetMesh();
        uint64_t terrainVBVA = terrainMesh->GetVertexBufferGPUVA();
        int terrainVertCount = terrainMesh->GetVertexCount();
        int terrainStride = terrainMesh->GetStride();

        uint32_t sphereHandle = SkullbonezHelper::GetSphereInstMeshHandle();
        uint64_t sphereVBVA = Gfx().GetInstancedMeshStaticVBVA( sphereHandle );
        int sphereVertCount = SkullbonezHelper::GetSphereVertexCount();
        int sphereStride = Gfx().GetInstancedMeshStaticStride( sphereHandle );

        {
        }

        if ( terrainVBVA != 0 && sphereVBVA != 0 )
        {
            Gfx().InitDXR( terrainVBVA, terrainVertCount, terrainStride, sphereVBVA, sphereVertCount, sphereStride, MAX_GAME_MODELS );
        }
    }
}


#ifdef _DEBUG
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
    const char* solverName = m_cGameModelCollection.GetLegacyMode() ? "legacy" : "solver";
    std::string escapedScene = JsonEscape( scenePath && scenePath[0] != '\0' ? scenePath : "legacy" );
    std::string escapedRenderer = JsonEscape( rendererName );
    std::string escapedSolver = JsonEscape( solverName );

    Log().Writef( m_physicsDiagnostics.path,
                  "{\"kind\":\"run\",\"run\":\"%s\",\"scene\":\"%s\",\"scene_index\":%d,\"load_count\":%d,\"manual_reset_count\":%d,\"renderer\":\"%s\",\"solver\":\"%s\",\"seed\":%u,\"fixed_step\":%d,\"fixed_step_forced_by_diag\":%d,\"target_frames\":%d,\"model_count\":%d,\"config\":{\"gravity\":%.6f,\"contact_epsilon\":%.6f,\"contact_restitution_threshold\":%.6f,\"friction_coeff\":%.6f,\"rolling_friction_coeff\":%.6f,\"spin_friction_coeff\":%.6f,\"broadphase_cell\":%.6f}}\n",
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
                  Cfg().broadphaseCell );
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

    m_physicsDiagnostics.isRunActive = false;
}
#endif


bool SkullbonezRun::SaveCurrentSceneDefaults()
{
    if ( !m_scene.isSceneMode ||
         m_scene.currentSceneIndex < 0 ||
         m_scene.currentSceneIndex >= static_cast<int>( m_sceneQueue.size() ) ||
         m_sceneQueue[m_scene.currentSceneIndex].empty() )
    {
        return false;
    }

    const std::string& scenePath = m_sceneQueue[m_scene.currentSceneIndex];
    std::ifstream input( scenePath );
    if ( !input )
    {
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    while ( std::getline( input, line ) )
    {
        if ( !line.empty() && line.back() == '\r' )
        {
            line.pop_back();
        }
        lines.push_back( line );
    }

    char buf[128] = {};
    SetSceneDirective( lines, "physics", std::string( "physics " ) + OnOff( m_scene.isScenePhysics ), true );
    SetSceneDirective( lines, "text", std::string( "text " ) + OnOff( m_scene.isSceneText ), true );
    SetSceneDirective( lines, "text_only", std::string( "text_only " ) + OnOff( m_debug.isTextOnly ), true );
    SetSceneDirective( lines, "vsync", std::string( "vsync " ) + OnOff( m_runtimeSettings.isVsyncEnabled ), true );
    SetSceneDirective( lines, "pipeline_sync", std::string( "pipeline_sync " ) + OnOff( m_runtimeSettings.isPipelineSyncEnabled ), true );
    SetSceneDirective( lines, "roll_align", std::string( "roll_align " ) + OnOff( m_runtimeSettings.isRollAlignEnabled ), true );
    SetSceneDirective( lines, "fixed_step", "fixed_step", m_scene.isFixedStep );
    if ( m_scene.targetFrameCount > 0 )
    {
        snprintf( buf, sizeof( buf ), "frames %d", m_scene.targetFrameCount );
    }
    else
    {
        strcpy_s( buf, sizeof( buf ), "frames unlimited" );
    }
    SetSceneDirective( lines, "frames", buf, true );
    snprintf( buf, sizeof( buf ), "seed %u", (std::max)( 1u, m_scene.rngSeed ) );
    SetSceneDirective( lines, "seed", buf, true );
    SetSceneDirective( lines, "exit_on_complete", "exit_on_complete", m_scene.isExitOnComplete );
    SetSceneDirective( lines, "debug_vectors", std::string( "debug_vectors " ) + OnOff( m_debug.isDebugVectors ), true );
    SetSceneDirective( lines, "physics_debug_axes", std::string( "physics_debug_axes " ) + OnOff( ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_AXES ) != 0 ), true );
    SetSceneDirective( lines, "physics_debug_contacts", std::string( "physics_debug_contacts " ) + OnOff( ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_CONTACTS ) != 0 ), true );
    SetSceneDirective( lines, "physics_debug_sleep", std::string( "physics_debug_sleep " ) + OnOff( ( m_debug.physicsDebugFlags & PHYSICS_DEBUG_SLEEP ) != 0 ), true );
    SetSceneDirective( lines, "physics_debug_transparent", std::string( "physics_debug_transparent " ) + OnOff( m_debug.isPhysicsDebugTransparent ), true );
    snprintf( buf, sizeof( buf ), "physics_debug_alpha %.2f", m_debug.physicsDebugAlpha );
    SetSceneDirective( lines, "physics_debug_alpha", buf, true );
    snprintf( buf, sizeof( buf ), "physics_debug_contact_linger %.2f", m_debug.physicsDebugContactLinger );
    SetSceneDirective( lines, "physics_debug_contact_linger", buf, true );
    snprintf( buf, sizeof( buf ), "time_scale %.2f", m_scene.timeScale );
    SetSceneDirective( lines, "time_scale", buf, true );
    SetSceneDirective( lines, "collision_visualizer", std::string( "collision_visualizer " ) + OnOff( m_debug.isCollisionVisualizer ), true );
    SetSceneDirective( lines, "broadphase_overlay", std::string( "broadphase_overlay " ) + OnOff( m_debug.isBroadphaseOverlay ), true );
    SetSceneDirective( lines, "water_freeze", std::string( "water_freeze " ) + OnOff( m_debug.isWaterFreezeDebug ), true );
    SetSceneDirective( lines, "water_flat", std::string( "water_flat " ) + OnOff( m_debug.isWaterFlatDebug ), true );
    SetSceneDirective( lines, "water_hidden", std::string( "water_hidden " ) + OnOff( m_debug.isWaterHidden ), true );
    SetSceneDirective( lines, "terrain_hidden", std::string( "terrain_hidden " ) + OnOff( m_debug.isTerrainHidden ), true );
    SetSceneDirective( lines, "water_reflection", std::string( "water_reflection " ) + WaterReflectionDirectiveValue( m_debug.isWaterNoReflect, m_debug.isWaterRTReflect ), true );
    SetSceneDirective( lines, "physics_mode", std::string( "physics_mode " ) + ( m_cGameModelCollection.GetLegacyMode() ? "legacy" : "solver" ), true );
    if ( m_camera.trackBallIndex >= 0 && m_camera.trackHeight > 0.0f )
    {
        snprintf( buf, sizeof( buf ), "track_height %.2f", m_camera.trackHeight );
        SetSceneDirective( lines, "track_height", buf, true );
    }
    else
    {
        SetSceneDirective( lines, "track_height", "", false );
    }
    if ( m_camera.autoCycleInterval > 0.0f )
    {
        snprintf( buf, sizeof( buf ), "auto_cycle_interval %.2f", m_camera.autoCycleInterval );
        SetSceneDirective( lines, "auto_cycle_interval", buf, true );
    }
    else
    {
        SetSceneDirective( lines, "auto_cycle_interval", "", false );
    }
    snprintf( buf, sizeof( buf ), "world %.2f %.2f %.2f", m_cWorldEnvironment.GetGravity(), m_cWorldEnvironment.GetFluidSurfaceHeight(), m_cWorldEnvironment.GetFluidDensity() );
    SetSceneDirective( lines, "world", buf, true );

    if ( m_uiModelCountOverride >= 0 )
    {
        snprintf( buf, sizeof( buf ), "legacy_balls %d", m_uiModelCountOverride );
        SetSceneDirective( lines, "legacy_balls", buf, true );
        SetSceneDirective( lines, "solver_balls", "", false );
        SetSceneDirective( lines, "solver_boxes", "", false );
    }
    else if ( m_scene.solverBallCount > 0 || m_scene.solverBoxCount > 0 || m_uiSolverBallCountOverride >= 0 || m_uiSolverBoxCountOverride >= 0 )
    {
        snprintf( buf, sizeof( buf ), "solver_balls %d", m_scene.solverBallCount );
        SetSceneDirective( lines, "solver_balls", buf, true );
        snprintf( buf, sizeof( buf ), "solver_boxes %d", m_scene.solverBoxCount );
        SetSceneDirective( lines, "solver_boxes", buf, true );
        SetSceneDirective( lines, "legacy_balls", "", false );
    }

    std::ofstream output( scenePath, std::ios::trunc );
    if ( !output )
    {
        return false;
    }

    for ( const std::string& outLine : lines )
    {
        output << outLine << '\n';
    }
    return output.good();
}


void SkullbonezRun::ResetCurrentScene()
{
    if ( m_scene.currentSceneIndex < 0 ||
         m_scene.currentSceneIndex >= static_cast<int>( m_sceneQueue.size() ) )
    {
        return;
    }

    ++m_scene.manualResetCount;
    LoadScene( m_scene.currentSceneIndex );
}


void SkullbonezRun::ApplyUiModelCountOverride( int count )
{
    m_uiModelCountOverride = std::clamp( count, 0, 1000 );
    m_uiSolverBallCountOverride = -1;
    m_uiSolverBoxCountOverride = -1;
    if ( m_scene.currentSceneIndex < 0 ||
         m_scene.currentSceneIndex >= static_cast<int>( m_sceneQueue.size() ) )
    {
        return;
    }

    m_cGameModelCollection.Clear();
    m_fire.ballNext = -1;
    m_fire.boxNext = -1;
    m_timers.physicsAccumulator = 0.0f;
    m_scene.currentFrame = 0;
    m_scene.isTestComplete = false;
    if ( m_uiModelCountOverride <= 0 )
    {
        m_scene.modelCount = 0;
        m_camera.trackBallIndex = -1;
        PROFILE_SCHEDULE_RESET();
        return;
    }

    const unsigned int seed = m_scene.rngSeed > 0 ? m_scene.rngSeed : 1u;
    srand( seed );
    SetUpGameModels( m_uiModelCountOverride );
    if ( m_camera.trackBallIndex >= m_uiModelCountOverride )
    {
        m_camera.trackBallIndex = m_uiModelCountOverride - 1;
    }
    PROFILE_SCHEDULE_RESET();
}


void SkullbonezRun::ApplyUiSolverObjectCounts( int balls, int boxes )
{
    m_uiSolverBallCountOverride = std::clamp( balls, 0, 1000 );
    m_uiSolverBoxCountOverride = std::clamp( boxes, 0, 1000 );
    m_uiModelCountOverride = -1;
    if ( m_scene.currentSceneIndex < 0 ||
         m_scene.currentSceneIndex >= static_cast<int>( m_sceneQueue.size() ) )
    {
        return;
    }

    m_cGameModelCollection.Clear();
    m_fire.ballNext = -1;
    m_fire.boxNext = -1;
    m_timers.physicsAccumulator = 0.0f;
    m_scene.currentFrame = 0;
    m_scene.isTestComplete = false;

    const unsigned int seed = m_scene.rngSeed > 0 ? m_scene.rngSeed : 1u;
    srand( seed );
    SetUpSolverObjects( m_uiSolverBallCountOverride, m_uiSolverBoxCountOverride );
    if ( m_scene.modelCount <= 0 )
    {
        m_camera.trackBallIndex = -1;
    }
    else if ( m_camera.trackBallIndex >= m_scene.modelCount )
    {
        m_camera.trackBallIndex = m_scene.modelCount - 1;
    }
    PROFILE_SCHEDULE_RESET();
}


void SkullbonezRun::ApplyUiWorldOverride( float gravity, float fluidHeight, float fluidDensity )
{
    m_cWorldEnvironment.SetGravity( gravity );
    m_cWorldEnvironment.SetFluidSurfaceHeight( fluidHeight );
    m_cWorldEnvironment.SetFluidDensity( fluidDensity );
}


void SkullbonezRun::ApplyNoWaterOverride()
{
    if ( !m_cmdNoWater || !m_systems.terrain )
    {
        return;
    }

    m_cWorldEnvironment.SetFluidSurfaceHeight( m_systems.terrain->GetMinHeight() - NO_WATER_TERRAIN_CLEARANCE );
}


bool SkullbonezRun::AdvanceScene()
{
    // For perf tests with 2 passes, the second pass re-runs the same scene
    if ( m_perfLogState.isPerfTest && sPerfPass == 0 )
    {
        sPerfPass = 1;
        LoadScene( m_scene.currentSceneIndex );
        return true;
    }

    // Reset perf pass counter for next scene
    sPerfPass = 0;

    int nextIndex = m_scene.currentSceneIndex + 1;
    if ( nextIndex >= static_cast<int>( m_sceneQueue.size() ) )
    {
        return false;
    }

    LoadScene( nextIndex );
    return true;
}


void SkullbonezRun::SaveScreenshot( const char* path )
{
    // Capture backbuffer via render backend (returns BGR, bottom-up, 4-byte aligned rows)
    int m_width = 0;
    int m_height = 0;
    std::vector<uint8_t> pixels = Gfx().CaptureBackbuffer( m_width, m_height );

    // Row stride padded to 4-byte boundary (BMP requirement)
    int rowStride = ( m_width * 3 + 3 ) & ~3;
    int imageSize = rowStride * m_height;

    // BMP file header (14 bytes)
    unsigned char fileHeader[14] = {};
    int fileSize = 14 + 40 + imageSize;
    fileHeader[0] = 'B';
    fileHeader[1] = 'M';
    fileHeader[2] = (unsigned char)( fileSize );
    fileHeader[3] = (unsigned char)( fileSize >> 8 );
    fileHeader[4] = (unsigned char)( fileSize >> 16 );
    fileHeader[5] = (unsigned char)( fileSize >> 24 );
    fileHeader[10] = 54; // pixel data offset

    // BMP info header (40 bytes)
    unsigned char infoHeader[40] = {};
    infoHeader[0] = 40; // header size
    infoHeader[4] = (unsigned char)( m_width );
    infoHeader[5] = (unsigned char)( m_width >> 8 );
    infoHeader[6] = (unsigned char)( m_width >> 16 );
    infoHeader[7] = (unsigned char)( m_width >> 24 );
    infoHeader[8] = (unsigned char)( m_height );
    infoHeader[9] = (unsigned char)( m_height >> 8 );
    infoHeader[10] = (unsigned char)( m_height >> 16 );
    infoHeader[11] = (unsigned char)( m_height >> 24 );
    infoHeader[12] = 1;  // color planes
    infoHeader[14] = 24; // bits per pixel
    infoHeader[20] = (unsigned char)( imageSize );
    infoHeader[21] = (unsigned char)( imageSize >> 8 );
    infoHeader[22] = (unsigned char)( imageSize >> 16 );
    infoHeader[23] = (unsigned char)( imageSize >> 24 );

    // Write to file
    FILE* file = nullptr;
    errno_t err = fopen_s( &file, path, "wb" );
    if ( err != 0 || !file )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to open screenshot file: %s  (SkullbonezRun::SaveScreenshot)", path );
        throw std::runtime_error( msg );
    }

    fwrite( fileHeader, 1, 14, file );
    fwrite( infoHeader, 1, 40, file );
    fwrite( pixels.data(), 1, static_cast<size_t>( imageSize ), file );
    fclose( file );
}


void SkullbonezRun::LogPerfMemory( const char* checkpoint )
{
    if ( !m_perfLogState.perfLogFile )
    {
        return;
    }

    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof( pmc );
    if ( GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
    {
        double mb = static_cast<double>( pmc.WorkingSetSize ) / ( 1024.0 * 1024.0 );
        fprintf( m_perfLogState.perfLogFile, "# MEM %s pass=%d working_set_mb=%.2f\n", checkpoint, sPerfPass + 1, mb );
        ++m_perfLogState.perfLogWritesSinceFlush;
        if ( m_perfLogState.isPerfLogFlushEnabled ||
             ( m_perfLogState.perfLogFlushInterval > 0 && m_perfLogState.perfLogWritesSinceFlush >= m_perfLogState.perfLogFlushInterval ) )
        {
            fflush( m_perfLogState.perfLogFile );
            m_perfLogState.perfLogWritesSinceFlush = 0;
        }
    }
}
