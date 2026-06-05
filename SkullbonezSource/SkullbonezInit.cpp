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
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
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
char g_commandLineError[512] = {};

bool FailCommandLineParse( const char* fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    vsprintf_s( g_commandLineError, sizeof( g_commandLineError ), fmt, args );
    va_end( args );

    fprintf( stderr, "ERROR: %s\n", g_commandLineError );
    return false;
}

const char* GetCommandLineError()
{
    return g_commandLineError[0] != '\0' ? g_commandLineError : "Command line parsing failed.";
}

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
    float timeScaleOverride = 0.0f; // 0 = not set
    bool fixedStep = false;
    unsigned int seedOverride = 0; // 0 = not set
    bool noWater = false;
    bool showProfiler = false;
    bool hideTopText = false;
    bool showBroadphaseVisualizer = false;
    GeneratedObjectTypeOverride objectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    bool hasPhysicsDebugFlagsOverride = false;
    uint32_t physicsDebugFlagsOverride = PHYSICS_DEBUG_NONE;
    bool hasPhysicsDebugTransparentOverride = false;
    bool physicsDebugTransparentOverride = false;
    bool hasPhysicsDebugAlphaOverride = false;
    float physicsDebugAlphaOverride = 0.28f;
    bool hasPhysicsDebugContactLingerOverride = false;
    float physicsDebugContactLingerOverride = 0.45f;
#ifdef _DEBUG
    char physicsLogOverride[256] = {};
    char physicsDiagnosticsPath[256] = {};
#endif
    bool physicsDiagnosticsRequested = false;
    bool fixedStepForcedByPhysicsDiagnostics = false;
};

bool IsCmdSeparator( char c )
{
    return c == '\0' || c == ' ' || c == '\t';
}

const char* FindOptionValue( const char* cmdLine, const char* optionName )
{
    if ( !cmdLine || !optionName )
    {
        return nullptr;
    }

    const size_t optionLen = strlen( optionName );
    const char* cursor = cmdLine;
    while ( ( cursor = strstr( cursor, optionName ) ) != nullptr )
    {
        const bool startsToken = cursor == cmdLine || cursor[-1] == ' ' || cursor[-1] == '\t';
        const bool endsToken = IsCmdSeparator( cursor[optionLen] );
        if ( startsToken && endsToken )
        {
            cursor += optionLen;
            while ( *cursor == ' ' || *cursor == '\t' )
            {
                ++cursor;
            }
            return cursor;
        }
        cursor += optionLen;
    }
    return nullptr;
}

const char* FindOptionValue( const char* cmdLine, const char* dashedName, const char* underscoredName )
{
    const char* value = FindOptionValue( cmdLine, dashedName );
    return value ? value : FindOptionValue( cmdLine, underscoredName );
}

bool ParseOnOffValue( const char* value, bool& out )
{
    if ( !value )
    {
        return false;
    }
    if ( _strnicmp( value, "on", 2 ) == 0 || _strnicmp( value, "true", 4 ) == 0 || _strnicmp( value, "1", 1 ) == 0 )
    {
        out = true;
        return true;
    }
    if ( _strnicmp( value, "off", 3 ) == 0 || _strnicmp( value, "false", 5 ) == 0 || _strnicmp( value, "0", 1 ) == 0 )
    {
        out = false;
        return true;
    }
    return false;
}

bool ParseOptionalOnOffValue( const char* value, bool& out )
{
    if ( !value || IsCmdSeparator( *value ) || ( value[0] == '-' && value[1] == '-' ) )
    {
        out = true;
        return true;
    }
    return ParseOnOffValue( value, out );
}

bool ParsePhysicsDebugMode( const char* value, uint32_t& outFlags )
{
    if ( !value )
    {
        return false;
    }
    if ( _strnicmp( value, "none", 4 ) == 0 || _strnicmp( value, "off", 3 ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_NONE;
        return true;
    }
    if ( _strnicmp( value, "axes", 4 ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_AXES;
        return true;
    }
    if ( _strnicmp( value, "contacts", 8 ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_CONTACTS;
        return true;
    }
    if ( _strnicmp( value, "sleep", 5 ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_SLEEP;
        return true;
    }
    if ( _strnicmp( value, "all", 3 ) == 0 || _strnicmp( value, "on", 2 ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_ALL;
        return true;
    }
    return false;
}

bool ApplyPhysicsDebugComponentOverride( const char* cmdLine, const char* dashedName, const char* underscoredName, uint32_t flag, ParsedArgs& out )
{
    const char* value = FindOptionValue( cmdLine, dashedName, underscoredName );
    if ( !value )
    {
        return true;
    }

    bool enabled = false;
    if ( !ParseOptionalOnOffValue( value, enabled ) )
    {
        return FailCommandLineParse( "%s expects optional on|off.", dashedName );
    }

    if ( !out.hasPhysicsDebugFlagsOverride )
    {
        out.physicsDebugFlagsOverride = PHYSICS_DEBUG_NONE;
    }
    out.hasPhysicsDebugFlagsOverride = true;
    if ( enabled )
    {
        out.physicsDebugFlagsOverride |= flag;
    }
    else
    {
        out.physicsDebugFlagsOverride &= ~flag;
    }
    return true;
}

bool ParsePhysicsDebugOverrides( const char* cmdLine, ParsedArgs& out )
{
    const char* modeValue = FindOptionValue( cmdLine, "--physics-debug", "--physics_debug" );
    if ( modeValue )
    {
        if ( !ParsePhysicsDebugMode( modeValue, out.physicsDebugFlagsOverride ) )
        {
            return FailCommandLineParse( "--physics-debug expects none|axes|contacts|sleep|all|on|off." );
        }
        out.hasPhysicsDebugFlagsOverride = true;
    }

    if ( !ApplyPhysicsDebugComponentOverride( cmdLine, "--physics-debug-axes", "--physics_debug_axes", PHYSICS_DEBUG_AXES, out ) ||
         !ApplyPhysicsDebugComponentOverride( cmdLine, "--physics-debug-contacts", "--physics_debug_contacts", PHYSICS_DEBUG_CONTACTS, out ) ||
         !ApplyPhysicsDebugComponentOverride( cmdLine, "--physics-debug-sleep", "--physics_debug_sleep", PHYSICS_DEBUG_SLEEP, out ) )
    {
        return false;
    }

    const char* transparentValue = FindOptionValue( cmdLine, "--physics-debug-transparent", "--physics_debug_transparent" );
    if ( transparentValue )
    {
        if ( !ParseOptionalOnOffValue( transparentValue, out.physicsDebugTransparentOverride ) )
        {
            return FailCommandLineParse( "--physics-debug-transparent expects optional on|off." );
        }
        out.hasPhysicsDebugTransparentOverride = true;
    }

    const char* alphaValue = FindOptionValue( cmdLine, "--physics-debug-alpha", "--physics_debug_alpha" );
    if ( alphaValue )
    {
        const float alpha = static_cast<float>( atof( alphaValue ) );
        if ( alpha < 0.05f || alpha > 1.0f )
        {
            return FailCommandLineParse( "--physics-debug-alpha expects 0.05..1.0." );
        }
        out.hasPhysicsDebugAlphaOverride = true;
        out.physicsDebugAlphaOverride = alpha;
        if ( !out.hasPhysicsDebugTransparentOverride )
        {
            out.hasPhysicsDebugTransparentOverride = true;
            out.physicsDebugTransparentOverride = true;
        }
    }

    const char* lingerValue = FindOptionValue( cmdLine, "--physics-debug-contact-linger", "--physics_debug_contact_linger" );
    if ( lingerValue )
    {
        const float linger = static_cast<float>( atof( lingerValue ) );
        if ( linger < 0.0f || linger > 5.0f )
        {
            return FailCommandLineParse( "--physics-debug-contact-linger expects 0.0..5.0 seconds." );
        }
        out.hasPhysicsDebugContactLingerOverride = true;
        out.physicsDebugContactLingerOverride = linger;
    }

    if ( out.hasPhysicsDebugFlagsOverride )
    {
        fprintf( stdout, "[physics-debug] Flags override: 0x%02x\n", out.physicsDebugFlagsOverride );
    }
    if ( out.hasPhysicsDebugTransparentOverride )
    {
        fprintf( stdout, "[physics-debug] Transparent bodies: %s\n", out.physicsDebugTransparentOverride ? "on" : "off" );
    }
    if ( out.hasPhysicsDebugAlphaOverride )
    {
        fprintf( stdout, "[physics-debug] Body alpha: %.3f\n", out.physicsDebugAlphaOverride );
    }
    if ( out.hasPhysicsDebugContactLingerOverride )
    {
        fprintf( stdout, "[physics-debug] Contact linger: %.3fs\n", out.physicsDebugContactLingerOverride );
    }

    return true;
}

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
            const char* suiteEnd = suiteArg;
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
                const char* end = sceneArg;
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
    return FailCommandLineParse( "--physics-log is only supported in Debug builds. Recompile with the Debug configuration to use physics regression logging." );
#else
    return true;
#endif
}

// Guards --physics-diag / --physics-diagnostics against use in non-Debug builds.
// Diagnostics traces are model-facing debug artifacts and are not a Profile/Release dependency.
bool ValidatePhysicsDiagnostics( const char* cmdLine )
{
    if ( !FindOptionValue( cmdLine, "--physics-diag" ) &&
         !FindOptionValue( cmdLine, "--physics-diagnostics" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--physics-diag is only supported in Debug builds. Recompile with the Debug configuration to use queryable physics diagnostics." );
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

bool ParsePhysicsDiagnosticsPath( const char* cmdLine, char ( &outPath )[256] )
{
    outPath[0] = '\0';
    const char* diagArg = FindOptionValue( cmdLine, "--physics-diag" );
    if ( !diagArg )
    {
        diagArg = FindOptionValue( cmdLine, "--physics-diagnostics" );
    }
    if ( !diagArg )
    {
        return true;
    }
    if ( *diagArg == '\0' || *diagArg == '-' )
    {
        return FailCommandLineParse( "--physics-diag requires an output path." );
    }

    const char* diagEnd = diagArg;
    while ( *diagEnd != '\0' && *diagEnd != ' ' && *diagEnd != '\t' )
    {
        ++diagEnd;
    }
    size_t len = static_cast<size_t>( diagEnd - diagArg );
    if ( len >= 256 )
    {
        len = 255;
    }
    memcpy( outPath, diagArg, len );
    outPath[len] = '\0';
    return true;
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
    if ( !ValidatePhysicsDiagnostics( cmdLine ) )
    {
        return false;
    }

#ifdef _DEBUG
    ParsePhysicsLogOverride( cmdLine, out.physicsLogOverride );
    if ( !ParsePhysicsDiagnosticsPath( cmdLine, out.physicsDiagnosticsPath ) )
    {
        return false;
    }
    out.physicsDiagnosticsRequested = out.physicsDiagnosticsPath[0] != '\0';
#endif

    out.switchInterval = ParseSwitchInterval( cmdLine );

    // --time-scale <F>: positive float, 0 = not set
    const char* tsArg = strstr( cmdLine, "--time-scale" );
    if ( tsArg )
    {
        tsArg += 12;
        while ( *tsArg == ' ' )
        {
            ++tsArg;
        }
        const float ts = static_cast<float>( atof( tsArg ) );
        if ( ts > 0.0f )
        {
            out.timeScaleOverride = ts;
            fprintf( stdout, "[time-scale] Override: %.4f\n", ts );
        }
    }

    // --fixed-step: flag only, no value
    out.fixedStep = ( cmdLine && strstr( cmdLine, "--fixed-step" ) != nullptr );
    if ( out.fixedStep )
    {
        fprintf( stdout, "[fixed-step] Forced via command line.\n" );
    }
#ifdef _DEBUG
    if ( out.physicsDiagnosticsRequested )
    {
        if ( !out.fixedStep )
        {
            out.fixedStep = true;
            out.fixedStepForcedByPhysicsDiagnostics = true;
            fprintf( stdout, "[physics-diag] Enabled: forcing fixed_step for deterministic queryable trace.\n" );
        }
        else
        {
            fprintf( stdout, "[physics-diag] Enabled: fixed_step already active.\n" );
        }
        fprintf( stdout, "[physics-diag] Output: %s\n", out.physicsDiagnosticsPath );
    }
#endif

    // --seed <N>: positive unsigned integer, 0 = not set.
    const char* seedArg = cmdLine ? strstr( cmdLine, "--seed" ) : nullptr;
    if ( seedArg )
    {
        seedArg += 6;
        while ( *seedArg == ' ' )
        {
            ++seedArg;
        }
        const unsigned long seed = strtoul( seedArg, nullptr, 10 );
        if ( seed > 0 && seed <= UINT_MAX )
        {
            out.seedOverride = static_cast<unsigned int>( seed );
            fprintf( stdout, "[seed] Override: %u\n", out.seedOverride );
        }
    }

    out.noWater = cmdLine && strstr( cmdLine, "--no-water" ) != nullptr;
    if ( out.noWater )
    {
        fprintf( stdout, "[water] Fluid surface starts below terrain.\n" );
    }

    out.showProfiler = cmdLine && ( strstr( cmdLine, "--profiler" ) != nullptr || strstr( cmdLine, "--show-profiler" ) != nullptr );
    if ( out.showProfiler )
    {
        fprintf( stdout, "[overlay] Profiler HUD enabled at startup.\n" );
    }

    out.hideTopText = cmdLine && ( strstr( cmdLine, "--hide-top-text" ) != nullptr || strstr( cmdLine, "--no-top-text" ) != nullptr );
    if ( out.hideTopText )
    {
        fprintf( stdout, "[overlay] Top HUD text hidden.\n" );
    }

    out.showBroadphaseVisualizer = cmdLine && ( strstr( cmdLine, "--broadphase-visualizer" ) != nullptr || strstr( cmdLine, "--broadphase-overlay" ) != nullptr );
    if ( out.showBroadphaseVisualizer )
    {
        fprintf( stdout, "[overlay] Broadphase visualizer enabled at startup.\n" );
    }

    const bool allBalls = cmdLine && strstr( cmdLine, "--all-balls" ) != nullptr;
    const bool allBoxes = cmdLine && strstr( cmdLine, "--all-boxes" ) != nullptr;
    if ( allBalls && allBoxes )
    {
        return FailCommandLineParse( "--all-balls and --all-boxes are mutually exclusive." );
    }
    if ( allBalls )
    {
        out.objectTypeOverride = GeneratedObjectTypeOverride::AllBalls;
        fprintf( stdout, "[objects] Generated objects forced to balls.\n" );
    }
    else if ( allBoxes )
    {
        out.objectTypeOverride = GeneratedObjectTypeOverride::AllBoxes;
        fprintf( stdout, "[objects] Generated objects forced to boxes.\n" );
    }

    if ( !ParsePhysicsDebugOverrides( cmdLine, out ) )
    {
        return false;
    }

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
        if ( args.timeScaleOverride > 0.0f )
        {
            cRun.SetTimeScaleOverride( args.timeScaleOverride );
        }
        if ( args.fixedStep )
        {
            cRun.SetFixedStepOverride();
        }
        if ( args.seedOverride > 0 )
        {
            cRun.SetSeedOverride( args.seedOverride );
        }
        if ( args.noWater )
        {
            cRun.SetNoWaterOverride();
        }
        if ( args.showProfiler )
        {
            cRun.SetInitialOverlayMode( OverlayMode::Timers );
        }
        if ( args.hideTopText )
        {
            cRun.SetTopTextHidden( true );
        }
        if ( args.showBroadphaseVisualizer )
        {
            cRun.SetBroadphaseVisualizerEnabled( true );
        }
        if ( args.objectTypeOverride != GeneratedObjectTypeOverride::Mixed )
        {
            cRun.SetGeneratedObjectTypeOverride( args.objectTypeOverride );
        }
        if ( args.hasPhysicsDebugFlagsOverride )
        {
            cRun.SetPhysicsDebugFlagsOverride( args.physicsDebugFlagsOverride );
        }
        if ( args.hasPhysicsDebugTransparentOverride )
        {
            cRun.SetPhysicsDebugTransparentOverride( args.physicsDebugTransparentOverride );
        }
        if ( args.hasPhysicsDebugAlphaOverride )
        {
            cRun.SetPhysicsDebugAlphaOverride( args.physicsDebugAlphaOverride );
        }
        if ( args.hasPhysicsDebugContactLingerOverride )
        {
            cRun.SetPhysicsDebugContactLingerOverride( args.physicsDebugContactLingerOverride );
        }
#ifdef _DEBUG
        if ( args.physicsLogOverride[0] != '\0' )
        {
            cRun.SetPhysicsLogOverride( args.physicsLogOverride );
        }
        if ( args.physicsDiagnosticsPath[0] != '\0' )
        {
            cRun.SetPhysicsDiagnosticsPath( args.physicsDiagnosticsPath, args.fixedStepForcedByPhysicsDiagnostics );
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
        const char* error = GetCommandLineError();
        fprintf( stderr, "FATAL: %s\n", error );
        MessageBoxA( nullptr, error, "Command line parse failed", MB_OK | MB_ICONERROR );
        CoUninitialize();
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
