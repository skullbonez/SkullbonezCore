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
#include <objbase.h>


// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;


namespace
{

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

// GUI apps have no console by default — attach to the parent terminal so
// fprintf(stderr/stdout) is visible when launched from cmd/PowerShell.
void AttachParentConsole()
{
    if ( AttachConsole( ATTACH_PARENT_PROCESS ) )
    {
        FILE* dummy = nullptr;
        freopen_s( &dummy, "CONOUT$", "w", stdout );
        freopen_s( &dummy, "CONOUT$", "w", stderr );
    }
}

// ---------------------------------------------------------------------------
// --gen-atlas early exit
// Generates the SDF font atlas to a file and exits — no GPU context needed.
// Returns true if the flag was present; outExitCode is 0 on success, 1 on failure.
// ---------------------------------------------------------------------------

bool HandleGenAtlas( const char* cmdLine, int& outExitCode )
{
    if ( !cmdLine || !strstr( cmdLine, "--gen-atlas" ) )
    {
        return false;
    }

    const char* arg = strstr( cmdLine, "--gen-atlas" ) + 11;
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
        outExitCode = 0;
    }
    else
    {
        fprintf( stderr, "[gen-atlas] FAILED.\n" );
        outExitCode = 1;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Command-line parsing
// ---------------------------------------------------------------------------

enum class RendererType
{
    OpenGL,
    DX11,
    DX12
};

struct ParsedArgs
{
    std::vector<std::string> sceneList;
    bool isSuiteOrSceneMode = false;
    RendererType renderer = RendererType::OpenGL;
    bool legacyPhysics = false;
    float switchInterval = -1.0f;
#ifdef _DEBUG
    char physicsLogOverride[256] = {};
#endif
};

// Build the ordered list of scene paths from --suite or --scene.
// Falls back to a single empty string (legacy mode) when neither flag is given.
void ParseSceneArgs( const char* cmdLine, std::vector<std::string>& sceneList, bool& isSuiteOrSceneMode )
{
    if ( cmdLine && cmdLine[0] != '\0' )
    {
        const char* suiteArg = strstr( cmdLine, "--suite" );
        const char* sceneArg = strstr( cmdLine, "--scene" );

        if ( suiteArg )
        {
            suiteArg += 7;
            while ( *suiteArg == ' ' )
            {
                ++suiteArg;
            }

            // Extract just the filename token — support both quoted and unquoted paths.
            const char* suiteStart = suiteArg;
            const char* suiteEnd   = suiteArg;
            if ( *suiteStart == '"' )
            {
                ++suiteStart;
                suiteEnd = suiteStart;
                while ( *suiteEnd != '\0' && *suiteEnd != '"' )
                {
                    ++suiteEnd;
                }
            }
            else
            {
                while ( *suiteEnd != '\0' && *suiteEnd != ' ' && *suiteEnd != '\t' )
                {
                    ++suiteEnd;
                }
            }
            std::string suitePath( suiteStart, suiteEnd );

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
                // Support both quoted ("path with spaces") and unquoted tokens.
                // Quoted paths stop at the closing '"'; unquoted paths stop at whitespace.
                // This handles launchers (CDB, VS debugger) that wrap paths in quotes.
                const char* start = sceneArg;
                const char* end   = sceneArg;
                if ( *start == '"' )
                {
                    ++start;
                    end = start;
                    while ( *end != '\0' && *end != '"' )
                    {
                        ++end;
                    }
                }
                else
                {
                    while ( *end != '\0' && *end != ' ' && *end != '\t' )
                    {
                        ++end;
                    }
                }
                sceneList.push_back( std::string( start, end ) );
                isSuiteOrSceneMode = true;
            }
        }
    }

    if ( sceneList.empty() )
    {
        sceneList.push_back( "" ); // legacy mode — empty string maps to nullptr
    }
}

RendererType ParseRendererArg( const char* cmdLine )
{
    if ( !cmdLine )
    {
        return RendererType::OpenGL;
    }

    const char* rendererArg = strstr( cmdLine, "--renderer" );
    if ( !rendererArg )
    {
        return RendererType::OpenGL;
    }

    rendererArg += 10;
    while ( *rendererArg == ' ' )
    {
        ++rendererArg;
    }

    if ( _strnicmp( rendererArg, "dx12", 4 ) == 0 || _strnicmp( rendererArg, "d3d12", 5 ) == 0 )
    {
        return RendererType::DX12;
    }
    if ( _strnicmp( rendererArg, "dx11", 4 ) == 0 || _strnicmp( rendererArg, "d3d11", 5 ) == 0 )
    {
        return RendererType::DX11;
    }
    return RendererType::OpenGL;
}

// Applies --vsync on|off to the already-loaded Cfg() singleton.
void ApplyVsyncOverride( const char* cmdLine )
{
    if ( !cmdLine )
    {
        return;
    }

    const char* vsyncArg = strstr( cmdLine, "--vsync" );
    if ( !vsyncArg )
    {
        return;
    }

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

float ParseSwitchInterval( const char* cmdLine )
{
    if ( !cmdLine )
    {
        return -1.0f;
    }

    const char* switchArg = strstr( cmdLine, "--switch-interval" );
    if ( !switchArg )
    {
        return -1.0f;
    }

    switchArg += 17;
    while ( *switchArg == ' ' )
    {
        ++switchArg;
    }
    return static_cast<float>( atof( switchArg ) );
}

// Guards --physics-log against use in non-Debug builds.
// Returns false if startup should abort.
bool ValidatePhysicsLog( const char* cmdLine )
{
    if ( !cmdLine || !strstr( cmdLine, "--physics-log" ) )
    {
        return true;
    }

#ifndef _DEBUG
    MessageBoxA( nullptr,
                 "--physics-log is only supported in Debug builds.\n"
                 "Recompile with the Debug configuration to use physics regression logging.",
                 "--physics-log requires Debug build",
                 MB_OK | MB_ICONERROR );
    return false;
#else
    return true;
#endif
}

#ifdef _DEBUG
void ParsePhysicsLogOverride( const char* cmdLine, char ( &outPath )[256] )
{
    outPath[0] = '\0';
    if ( !cmdLine || !strstr( cmdLine, "--physics-log" ) )
    {
        return;
    }

    const char* physLogArg = strstr( cmdLine, "--physics-log" ) + 13;
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
    if ( len >= 256 )
    {
        len = 255;
    }
    memcpy( outPath, physLogArg, len );
    outPath[len] = '\0';
    if ( outPath[0] != '\0' )
    {
        fprintf( stdout, "[physics-log] Output: %s\n", outPath );
    }
}
#endif

// Parses all command-line options into a ParsedArgs struct.
// Also loads engine.cfg and applies any overrides to the global Cfg() singleton.
// Returns false if startup should abort (e.g. --physics-log in Release build).
bool ParseCommandLine( const char* cmdLine, ParsedArgs& out )
{
    ParseSceneArgs( cmdLine, out.sceneList, out.isSuiteOrSceneMode );
    out.renderer = ParseRendererArg( cmdLine );

    Cfg().Load( ( std::string( DATA_ROOT ) + "engine.cfg" ).c_str() );
    ApplyVsyncOverride( cmdLine );

    out.legacyPhysics = ( cmdLine && strstr( cmdLine, "--legacy-physics" ) );
    if ( out.legacyPhysics )
    {
        fprintf( stdout, "[physics] Legacy sphere-only solver enabled.\n" );
    }

    if ( !ValidatePhysicsLog( cmdLine ) )
    {
        return false;
    }

#ifdef _DEBUG
    ParsePhysicsLogOverride( cmdLine, out.physicsLogOverride );
#endif

    out.switchInterval = ParseSwitchInterval( cmdLine );
    return true;
}

// ---------------------------------------------------------------------------
// Render backend
// ---------------------------------------------------------------------------

void InitRenderBackend( RendererType renderer, SkullbonezWindow* window )
{
    if ( renderer == RendererType::OpenGL )
    {
        // Init OpenGL (single context for entire lifetime)
        window->InitialiseOpenGL();
        auto backend = std::make_unique<RenderBackendGL>();
        backend->Init( window->m_sWindow, window->m_sDevice, window->m_sWindowDimensions.x, window->m_sWindowDimensions.y );
        SetGfxBackend( std::move( backend ) );
    }
    else if ( renderer == RendererType::DX12 )
    {
        auto backend = std::make_unique<RenderBackendDX12>();
        backend->Init( window->m_sWindow, window->m_sDevice, window->m_sWindowDimensions.x, window->m_sWindowDimensions.y );
        SetGfxBackend( std::move( backend ) );
    }
    else
    {
        auto backend = std::make_unique<RenderBackendDX11>();
        backend->Init( window->m_sWindow, window->m_sDevice, window->m_sWindowDimensions.x, window->m_sWindowDimensions.y );
        SetGfxBackend( std::move( backend ) );
    }
}

// ---------------------------------------------------------------------------
// Main run
// SkullbonezRun is scoped here so its destructor fires BEFORE the GL context
// is deleted — this ensures any OpenGL cleanup calls inside Run still work.
// ---------------------------------------------------------------------------

void RunApp( SkullbonezWindow* window, ParsedArgs& args )
{
    {
        SkullbonezRun cRun( std::move( args.sceneList ), args.legacyPhysics );
        if ( args.switchInterval > 0.0f )
        {
            cRun.SetRendererSwitchInterval( args.switchInterval );
        }
#ifdef _DEBUG
        if ( args.physicsLogOverride[0] != '\0' )
        {
            cRun.SetPhysicsLogOverride( args.physicsLogOverride );
        }
#endif
        try
        {
            cRun.Initialise();
            cRun.Run();

            if ( !args.isSuiteOrSceneMode )
            {
                window->MsgBox( "Thanks for using the Skullbonez Core!", "Alert!", MB_OK );
            }
        }
        catch ( const std::exception& e )
        {
            fprintf( stderr, "FATAL: %s\n", e.what() );
            window->MsgBox( e.what(), "Alert!", MB_OK );
        }
    } // cRun destroyed here — GL context still alive for proper cleanup
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void CleanupWindow( SkullbonezWindow* window, HINSTANCE hInstance )
{
    DestroyGfxBackend();

    // GL context cleanup — must happen after SkullbonezRun is destroyed.
    // Hot-switching can create an OpenGL context even when startup renderer was DX.
    if ( window->m_sRenderContext )
    {
        wglMakeCurrent( nullptr, nullptr );
        wglDeleteContext( window->m_sRenderContext );
        window->m_sRenderContext = nullptr;
    }

    if ( window->m_sDevice )
    {
        ReleaseDC( window->m_sWindow, window->m_sDevice );
    }

    if ( window->m_fIsFullScreenMode )
    {
        ChangeDisplaySettings( nullptr, 0 ); // Restore desktop mode
        ShowCursor( true );
    }

    UnregisterClass( WINDOW_NAME, hInstance );
    window->Destroy();
}

} // anonymous namespace


// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int WINAPI WinMain( HINSTANCE hInstance,
                    HINSTANCE hPrevInstance,
                    PSTR szCmdLine,
                    int iCmdShow )
{
    // Heap debug code - breaks program at specified allocation
    // _CrtSetBreakAlloc(89);

    // Floating point check routine
    // _controlfp(0, _MCW_EM ^ _EM_INEXACT);

    hPrevInstance;
    iCmdShow;

    // Initialize COM on the main thread (multi-threaded apartment). Required before any
    // WinRT/COM activation occurs — without this, MSCTF.dll throws 0x800401F0 during
    // text/input service initialization triggered by window creation.
    CoInitializeEx( nullptr, COINIT_MULTITHREADED );

    AttachParentConsole();

    int atlasExitCode = 0;
    if ( HandleGenAtlas( szCmdLine, atlasExitCode ) )
    {
        return atlasExitCode;
    }

    ParsedArgs args;
    if ( !ParseCommandLine( szCmdLine, args ) )
    {
        return 1;
    }

    SkullbonezWindow* window = SkullbonezWindow::Instance();
    window->CreateAppWindow( hInstance, Cfg().window.fullscreen );
    window->m_sDevice = GetDC( window->m_sWindow );

    InitRenderBackend( args.renderer, window );
    window->HandleScreenResize();

    RunApp( window, args );

    CleanupWindow( window, hInstance );

    CoUninitialize();

    // Write memory leaks to output window
    // _CrtDumpMemoryLeaks();

    return 0;
}
