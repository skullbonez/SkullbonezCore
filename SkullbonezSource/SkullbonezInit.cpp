// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezRun.h"
#include "SkullbonezText.h"
#include "SkullbonezWindow.h"
#include "SkullbonezTimer.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezRenderBackendGL.h"
#include "SkullbonezRenderBackendDX11.h"
#include "SkullbonezRenderBackendDX12.h"
#include <float.h>
#include <cstring>
#include <vector>
#include <string>
#include <io.h>


// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;


// "Windows Main" - this function is the execution entry point for the application
int WINAPI WinMain( HINSTANCE hInstance,     // Holds info on instance of app
                    HINSTANCE hPrevInstance, // Some useless Win32 junk
                    PSTR szCmdLine,          // Params passed from command line
                    int iCmdShow )           // Window state (maximised, hide etc)
{
    // Heap debug code - breaks program at specified allocation
    // _CrtSetBreakAlloc(89);


    // floating point check routine
    // _controlfp(0, _MCW_EM ^ _EM_INEXACT);

    // Supress benign (level 4) warnings
    hPrevInstance;
    iCmdShow;

    // GUI apps have no console by default — attach to the parent terminal so
    // fprintf(stderr/stdout) is visible when launched from cmd/PowerShell.
    if ( AttachConsole( ATTACH_PARENT_PROCESS ) )
    {
        FILE* dummy = nullptr;
        freopen_s( &dummy, "CONOUT$", "w", stdout );
        freopen_s( &dummy, "CONOUT$", "w", stderr );
    }

    // Handle --gen-atlas before any window/GPU init: generates the SDF font atlas
    // to a file and exits.  No graphics context is needed for this operation.
    // Usage:  SKULLBONEZ_CORE.exe --gen-atlas [optional/output/path.sdf]
    if ( szCmdLine && strstr( szCmdLine, "--gen-atlas" ) )
    {
        const char* arg = strstr( szCmdLine, "--gen-atlas" ) + 11;
        while ( *arg == ' ' )
        {
            ++arg;
        }
        char outPath[MAX_PATH];
        if ( *arg != '\0' && *arg != '-' )
        {
            int len = 0;
            while ( arg[len] != ' ' && arg[len] != '\0' && len < MAX_PATH - 1 )
            {
                outPath[len] = arg[len];
                ++len;
            }
            outPath[len] = '\0';
        }
        else
        {
            strcpy_s( outPath, "SkullbonezData/font_atlas.sdf" );
        }
        fprintf( stdout, "[gen-atlas] Generating SDF font atlas: %s\n", outPath );
        if ( SkullbonezCore::Text::Text2d::GenerateSdfAtlasToFile( "Verdana", outPath ) )
        {
            fprintf( stdout, "[gen-atlas] Done.\n" );
            return 0;
        }
        fprintf( stderr, "[gen-atlas] FAILED.\n" );
        return 1;
    }

    // Build the ordered list of scene paths to run.
    // Each entry is either a .scene path (scene/suite mode) or "" (legacy mode).
    std::vector<std::string> sceneList;
    bool isSuiteOrSceneMode = false;

    if ( szCmdLine && szCmdLine[0] != '\0' )
    {
        const char* suiteArg = strstr( szCmdLine, "--suite" );
        const char* sceneArg = strstr( szCmdLine, "--scene" );

        if ( suiteArg )
        {
            suiteArg += 7;
            while ( *suiteArg == ' ' )
            {
                ++suiteArg;
            }

            // Extract just the filename token (stop before any subsequent flags).
            const char* suiteEnd = suiteArg;
            while ( *suiteEnd != '\0' && *suiteEnd != ' ' && *suiteEnd != '\t' )
            {
                ++suiteEnd;
            }
            std::string suitePath( suiteArg, suiteEnd );

            // Read suite file: one scene path per line, # comments ignored
            FILE* f = nullptr;
            if ( fopen_s( &f, suitePath.c_str(), "r" ) == 0 && f )
            {
                char line[512];
                while ( fgets( line, sizeof( line ), f ) )
                {
                    size_t len = strlen( line );
                    while ( len > 0 && ( line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ' ) )
                    {
                        line[--len] = '\0';
                    }
                    if ( len > 0 && line[0] != '#' )
                    {
                        sceneList.push_back( line );
                    }
                }
                fclose( f );
            }
            isSuiteOrSceneMode = true;
        }
        else if ( sceneArg )
        {
            sceneArg += 7;
            while ( *sceneArg == ' ' )
            {
                ++sceneArg;
            }
            if ( *sceneArg != '\0' )
            {
                // Collect only the first token (stop at whitespace so subsequent
                // flags like --renderer are not accidentally included in the path).
                const char* end = sceneArg;
                while ( *end != '\0' && *end != ' ' && *end != '\t' )
                {
                    ++end;
                }
                sceneList.push_back( std::string( sceneArg, end ) );
                isSuiteOrSceneMode = true;
            }
        }
    }

    if ( sceneList.empty() )
    {
        sceneList.push_back( "" ); // legacy mode — empty string maps to nullptr
    }

    // Parse --renderer arg (default: opengl)
    enum class RendererType
    {
        OpenGL,
        DX11,
        DX12
    };
    RendererType renderer = RendererType::OpenGL;
    if ( szCmdLine )
    {
        const char* rendererArg = strstr( szCmdLine, "--renderer" );
        if ( rendererArg )
        {
            rendererArg += 10;
            while ( *rendererArg == ' ' )
            {
                ++rendererArg;
            }
            if ( _strnicmp( rendererArg, "dx12", 4 ) == 0 || _strnicmp( rendererArg, "d3d12", 5 ) == 0 )
            {
                renderer = RendererType::DX12;
            }
            else if ( _strnicmp( rendererArg, "dx11", 4 ) == 0 || _strnicmp( rendererArg, "d3d11", 5 ) == 0 )
            {
                renderer = RendererType::DX11;
            }
        }
    }

    Cfg().Load( ( std::string( DATA_ROOT ) + "engine.cfg" ).c_str() );

    // Parse --vsync on|off  (overrides the vsync_enabled value from engine.cfg)
    if ( szCmdLine )
    {
        const char* vsyncArg = strstr( szCmdLine, "--vsync" );
        if ( vsyncArg )
        {
            vsyncArg += 7;
            while ( *vsyncArg == ' ' )
            {
                ++vsyncArg;
            }
            if ( _strnicmp( vsyncArg, "off", 3 ) == 0 || _strnicmp( vsyncArg, "0", 1 ) == 0 )
            {
                Cfg().runtimeRender.vsyncEnabled = false;
                fprintf( stdout, "[vsync] Disabled via command line.\n" );
            }
            else if ( _strnicmp( vsyncArg, "on", 2 ) == 0 || _strnicmp( vsyncArg, "1", 1 ) == 0 )
            {
                Cfg().runtimeRender.vsyncEnabled = true;
                fprintf( stdout, "[vsync] Enabled via command line.\n" );
            }
        }
    }

    // Parse --legacy-physics flag (use original sphere-only ad-hoc solver for comparison)
    bool legacyPhysics = false;
    if ( szCmdLine && strstr( szCmdLine, "--legacy-physics" ) )
    {
        legacyPhysics = true;
        fprintf( stdout, "[physics] Legacy sphere-only solver enabled.\n" );
    }

    // Parse --physics-log <path>  (requires Debug build — error and exit in Release/Profile)
    if ( szCmdLine && strstr( szCmdLine, "--physics-log" ) )
    {
#ifndef _DEBUG
        MessageBoxA( nullptr,
                     "--physics-log is only supported in Debug builds.\n"
                     "Recompile with the Debug configuration to use physics regression logging.",
                     "--physics-log requires Debug build",
                     MB_OK | MB_ICONERROR );
        return 1;
#endif
    }

#ifdef _DEBUG
    char physicsLogOverride[256] = {};
    if ( szCmdLine && strstr( szCmdLine, "--physics-log" ) )
    {
        const char* physLogArg = strstr( szCmdLine, "--physics-log" ) + 13;
        while ( *physLogArg == ' ' )
        {
            ++physLogArg;
        }
        const char* physLogEnd = physLogArg;
        while ( *physLogEnd != '\0' && *physLogEnd != ' ' && *physLogEnd != '\t' )
        {
            ++physLogEnd;
        }
        size_t len = static_cast<size_t>( physLogEnd - physLogArg );
        if ( len >= sizeof( physicsLogOverride ) )
        {
            len = sizeof( physicsLogOverride ) - 1;
        }
        memcpy( physicsLogOverride, physLogArg, len );
        physicsLogOverride[len] = '\0';
        if ( physicsLogOverride[0] != '\0' )
        {
            fprintf( stdout, "[physics-log] Output: %s\n", physicsLogOverride );
        }
    }
#endif

    // Parse --switch-interval N (auto-cycle renderers every N seconds, for hot-switch testing)
    float switchInterval = -1.0f;
    if ( szCmdLine )
    {
        const char* switchArg = strstr( szCmdLine, "--switch-interval" );
        if ( switchArg )
        {
            switchArg += 17;
            while ( *switchArg == ' ' )
            {
                ++switchArg;
            }
            switchInterval = (float)atof( switchArg );
        }
    }


    // Create an instance of our window class
    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();

    // Create the application window
    m_cWindow->CreateAppWindow( hInstance, Cfg().window.fullscreen );

    // Get the device context for our window
    m_cWindow->m_sDevice = GetDC( m_cWindow->m_sWindow );

    if ( renderer == RendererType::OpenGL )
    {
        // Init OpenGL (single context for entire lifetime)
        m_cWindow->InitialiseOpenGL();

        auto backend = std::make_unique<RenderBackendGL>();
        backend->Init( m_cWindow->m_sWindow, m_cWindow->m_sDevice, m_cWindow->m_sWindowDimensions.x, m_cWindow->m_sWindowDimensions.y );
        SetGfxBackend( std::move( backend ) );
    }
    else if ( renderer == RendererType::DX12 )
    {
        auto backend = std::make_unique<RenderBackendDX12>();
        backend->Init( m_cWindow->m_sWindow, m_cWindow->m_sDevice, m_cWindow->m_sWindowDimensions.x, m_cWindow->m_sWindowDimensions.y );
        SetGfxBackend( std::move( backend ) );
    }
    else
    {
        auto backend = std::make_unique<RenderBackendDX11>();
        backend->Init( m_cWindow->m_sWindow, m_cWindow->m_sDevice, m_cWindow->m_sWindowDimensions.x, m_cWindow->m_sWindowDimensions.y );
        SetGfxBackend( std::move( backend ) );
    }

    // Now that the backend is ready, set viewport and projection for the active renderer
    m_cWindow->HandleScreenResize();


    {
        // Create the Skullbonez Core instance (scoped so destructor runs
        // BEFORE GL context deletion — ensures GL cleanup calls work)
        SkullbonezRun cRun( std::move( sceneList ), legacyPhysics );
        if ( switchInterval > 0.0f )
        {
            cRun.SetRendererSwitchInterval( switchInterval );
        }
#ifdef _DEBUG
        if ( physicsLogOverride[0] != '\0' )
        {
            cRun.SetPhysicsLogOverride( physicsLogOverride );
        }
#endif

        try
        {
            cRun.Initialise();
            cRun.Run();

            if ( !isSuiteOrSceneMode )
            {
                m_cWindow->MsgBox( "Thanks for using the Skullbonez Core!", "Alert!", MB_OK );
            }
        }
        catch ( const std::exception& e )
        {
            fprintf( stderr, "FATAL: %s\n", e.what() );
            if ( !isSuiteOrSceneMode )
            {
                m_cWindow->MsgBox( e.what(), "Alert!", MB_OK );
            }
        }
        // cRun destroyed here — GL context still alive for proper cleanup
    }

    // Destroy render backend before GL context
    DestroyGfxBackend();

    // Cleanup rendering context AFTER cRun is destroyed.
    // Runtime renderer hot-switching can create an OpenGL context even when startup renderer was DX.
    if ( m_cWindow->m_sRenderContext )
    {
        wglMakeCurrent( nullptr, nullptr );
        wglDeleteContext( m_cWindow->m_sRenderContext );
        m_cWindow->m_sRenderContext = nullptr;
    }

    // Cleanup device context (Free device context associated with our window)
    if ( m_cWindow->m_sDevice )
    {
        ReleaseDC( m_cWindow->m_sWindow,
                   m_cWindow->m_sDevice );
    }

    // Restore desktop settings
    if ( m_cWindow->m_fIsFullScreenMode )
    {
        ChangeDisplaySettings( nullptr, 0 ); // Switch back to desktop mode
        ShowCursor( true );                  // Bring mouse pointer back
    }

    // Free up memory associated with the window class
    UnregisterClass( WINDOW_NAME, hInstance );

    // Delete our window class
    m_cWindow->Destroy();

    // Write memory leaks to output window
    // _CrtDumpMemoryLeaks();

    // Return wParam of our msg struct
    return 0;
}
