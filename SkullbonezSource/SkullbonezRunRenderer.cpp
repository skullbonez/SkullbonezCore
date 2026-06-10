// --- Includes ---
#include "SkullbonezRunInternal.h"

// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

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

#ifdef _DEBUG
    Log().WriteEventf( "renderer_change_started from=%s to=%s",
                       RuntimeRendererTypeName( current ),
                       RuntimeRendererTypeName( target ) );
#endif

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
    m_UI.ResetResources();
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
#ifdef _DEBUG
            Log().WriteEventf( "renderer_change_failed from=%s to=%s rollback=failed reason=\"%s\" rollback_reason=\"%s\"",
                               RuntimeRendererTypeName( current ),
                               RuntimeRendererTypeName( target ),
                               switchFailureReason.c_str(),
                               rollbackError.what() );
#endif
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

#ifdef _DEBUG
    if ( switchFailureReason.empty() )
    {
        Log().WriteEventf( "renderer_changed from=%s to=%s backend=\"%s\"",
                           RuntimeRendererTypeName( current ),
                           RuntimeRendererTypeName( target ),
                           Gfx().GetRendererName() );
    }
    else
    {
        Log().WriteEventf( "renderer_change_failed from=%s to=%s rollback=ok backend=\"%s\" reason=\"%s\"",
                           RuntimeRendererTypeName( current ),
                           RuntimeRendererTypeName( target ),
                           Gfx().GetRendererName(),
                           switchFailureReason.c_str() );
    }
#endif
}
