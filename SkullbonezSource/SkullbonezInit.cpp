// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezRun.h"
#include "SkullbonezText.h"
#include "SkullbonezWindow.h"
#include "SkullbonezInput.h"
#include "SkullbonezTimer.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezRenderBackendGL.h"
#include "SkullbonezRenderBackendDX11.h"
#include "SkullbonezRenderBackendDX12.h"
#include <cerrno>
#include <float.h>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <fstream>
#include <vector>
#include <string>
#include <io.h>
#include <objbase.h>

#ifdef _DEBUG
#include <dbghelp.h>
#pragma comment( lib, "dbghelp.lib" )
#endif


// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;


namespace
{
char g_commandLineError[512] = {};

#ifdef _DEBUG
const char* ExceptionCodeName( DWORD code )
{
    switch ( code )
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:
        return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW:
        return "EXCEPTION_STACK_OVERFLOW";
    default:
        return "EXCEPTION_UNKNOWN";
    }
}


void WriteDebugCrashStack( EXCEPTION_POINTERS* exceptionInfo )
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    DWORD symOptions = SymGetOptions();
    symOptions |= SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
    SymSetOptions( symOptions );

    const BOOL symbolsReady = SymInitialize( process, nullptr, TRUE );
    if ( !symbolsReady )
    {
        Log().Writef( SkullbonezLog::EventLogPath(), "    stack_symbols=unavailable error=%lu\n", GetLastError() );
    }

    CONTEXT context = {};
    if ( exceptionInfo && exceptionInfo->ContextRecord )
    {
        context = *exceptionInfo->ContextRecord;
    }
    else
    {
        RtlCaptureContext( &context );
    }

    STACKFRAME64 frame = {};
    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#if defined( _M_X64 )
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
#else
    machineType = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrStack.Mode = AddrModeFlat;
#endif

    Log().Writef( SkullbonezLog::EventLogPath(), "    stack_trace:\n" );
    for ( int frameIndex = 0; frameIndex < 64; ++frameIndex )
    {
        BOOL walked = StackWalk64( machineType,
                                   process,
                                   thread,
                                   &frame,
                                   &context,
                                   nullptr,
                                   SymFunctionTableAccess64,
                                   SymGetModuleBase64,
                                   nullptr );
        if ( !walked || frame.AddrPC.Offset == 0 )
        {
            break;
        }

        const DWORD64 address = frame.AddrPC.Offset;
        char symbolStorage[sizeof( SYMBOL_INFO ) + MAX_SYM_NAME] = {};
        PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>( symbolStorage );
        symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 symbolDisplacement = 0;
        const BOOL hasSymbol = symbolsReady && SymFromAddr( process, address, &symbolDisplacement, symbol );

        IMAGEHLP_LINE64 lineInfo = {};
        lineInfo.SizeOfStruct = sizeof( lineInfo );
        DWORD lineDisplacement = 0;
        const BOOL hasLine = symbolsReady && SymGetLineFromAddr64( process, address, &lineDisplacement, &lineInfo );

        if ( hasSymbol && hasLine )
        {
            Log().Writef( SkullbonezLog::EventLogPath(),
                          "      #%02d 0x%016llX %s+0x%llX (%s:%lu)\n",
                          frameIndex,
                          static_cast<unsigned long long>( address ),
                          symbol->Name,
                          static_cast<unsigned long long>( symbolDisplacement ),
                          lineInfo.FileName,
                          lineInfo.LineNumber );
        }
        else if ( hasSymbol )
        {
            Log().Writef( SkullbonezLog::EventLogPath(),
                          "      #%02d 0x%016llX %s+0x%llX\n",
                          frameIndex,
                          static_cast<unsigned long long>( address ),
                          symbol->Name,
                          static_cast<unsigned long long>( symbolDisplacement ) );
        }
        else
        {
            Log().Writef( SkullbonezLog::EventLogPath(),
                          "      #%02d 0x%016llX <unknown>\n",
                          frameIndex,
                          static_cast<unsigned long long>( address ) );
        }
    }

    if ( symbolsReady )
    {
        SymCleanup( process );
    }
}


LONG WINAPI DebugUnhandledExceptionFilter( EXCEPTION_POINTERS* exceptionInfo )
{
    DWORD exceptionCode = 0;
    void* exceptionAddress = nullptr;
    if ( exceptionInfo && exceptionInfo->ExceptionRecord )
    {
        exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;
        exceptionAddress = exceptionInfo->ExceptionRecord->ExceptionAddress;
    }

    Log().WriteEventf( "crash exception=0x%08lX name=%s address=%p",
                       exceptionCode,
                       ExceptionCodeName( exceptionCode ),
                       exceptionAddress );
    WriteDebugCrashStack( exceptionInfo );
    Log().FlushAll();

    return EXCEPTION_EXECUTE_HANDLER;
}


void InstallDebugCrashLogger()
{
    SetUnhandledExceptionFilter( DebugUnhandledExceptionFilter );
}
#endif

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

struct CommandLineView
{
    std::vector<std::string> tokens;
};

bool IsTokenWhitespace( char c )
{
    return c == ' ' || c == '\t';
}

CommandLineView TokenizeCommandLine( const char* cmdLine )
{
    CommandLineView result;
    if ( !cmdLine )
    {
        return result;
    }

    const char* cursor = cmdLine;
    while ( *cursor != '\0' )
    {
        while ( IsTokenWhitespace( *cursor ) )
        {
            ++cursor;
        }
        if ( *cursor == '\0' )
        {
            break;
        }

        std::string token;
        bool inQuote = false;
        while ( *cursor != '\0' )
        {
            const char c = *cursor;
            if ( c == '"' )
            {
                inQuote = !inQuote;
                ++cursor;
                continue;
            }
            if ( !inQuote && IsTokenWhitespace( c ) )
            {
                break;
            }
            token.push_back( c );
            ++cursor;
        }

        if ( !token.empty() )
        {
            result.tokens.push_back( token );
        }
    }
    return result;
}

bool IsOptionToken( const std::string& token )
{
    return token.size() >= 2 && token[0] == '-' && token[1] == '-';
}

bool IsOptionValueMissing( const char* value )
{
    return !value || *value == '\0';
}

bool OptionTokenMatches( const std::string& token, const char* optionName )
{
    return optionName && token == optionName;
}

bool OptionTokenHasAssignedValue( const std::string& token, const char* optionName, const char*& outValue )
{
    if ( !optionName )
    {
        return false;
    }

    const size_t optionLen = strlen( optionName );
    if ( token.size() <= optionLen || token.compare( 0, optionLen, optionName ) != 0 || token[optionLen] != '=' )
    {
        return false;
    }

    outValue = token.c_str() + optionLen + 1;
    return true;
}

const char* FindOptionValue( const CommandLineView& commandLine, const char* optionName )
{
    for ( size_t i = 0; i < commandLine.tokens.size(); ++i )
    {
        const std::string& token = commandLine.tokens[i];
        const char* assignedValue = nullptr;
        if ( OptionTokenHasAssignedValue( token, optionName, assignedValue ) )
        {
            return assignedValue;
        }
        if ( OptionTokenMatches( token, optionName ) )
        {
            if ( i + 1 < commandLine.tokens.size() && !IsOptionToken( commandLine.tokens[i + 1] ) )
            {
                return commandLine.tokens[i + 1].c_str();
            }
            return "";
        }
    }
    return nullptr;
}

const char* FindOptionValue( const CommandLineView& commandLine, const char* dashedName, const char* underscoredName )
{
    const char* value = FindOptionValue( commandLine, dashedName );
    return value ? value : FindOptionValue( commandLine, underscoredName );
}

bool HasOption( const CommandLineView& commandLine, const char* optionName )
{
    for ( const std::string& token : commandLine.tokens )
    {
        const char* assignedValue = nullptr;
        if ( OptionTokenMatches( token, optionName ) || OptionTokenHasAssignedValue( token, optionName, assignedValue ) )
        {
            return true;
        }
    }
    return false;
}

char* TrimLineInPlace( char* text )
{
    while ( IsTokenWhitespace( *text ) )
    {
        ++text;
    }

    size_t len = strlen( text );
    while ( len > 0 && ( IsTokenWhitespace( text[len - 1] ) || text[len - 1] == '\r' || text[len - 1] == '\n' ) )
    {
        text[--len] = '\0';
    }
    return text;
}

bool ParseFloatToken( const char* value, float& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const double parsed = strtod( value, &end );
    if ( end == value || *end != '\0' || errno == ERANGE )
    {
        return false;
    }

    out = static_cast<float>( parsed );
    return true;
}

bool ParseIntToken( const char* value, int& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long parsed = strtol( value, &end, 10 );
    if ( end == value || *end != '\0' || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX )
    {
        return false;
    }

    out = static_cast<int>( parsed );
    return true;
}

bool ParseUnsignedIntToken( const char* value, unsigned int& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = strtoul( value, &end, 10 );
    if ( end == value || *end != '\0' || errno == ERANGE || parsed > UINT_MAX )
    {
        return false;
    }

    out = static_cast<unsigned int>( parsed );
    return true;
}

bool CopyOptionPath( const char* value, const char* optionName, char* outPath, size_t outPathSize )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "%s requires an output path.", optionName );
    }
    if ( strlen( value ) >= outPathSize )
    {
        return FailCommandLineParse( "%s path is too long.", optionName );
    }

    strcpy_s( outPath, outPathSize, value );
    return true;
}

// ---------------------------------------------------------------------------
// --gen-atlas early exit
// Generates the SDF font atlas to a file and exits — no GPU context needed.
// Returns true if the flag was present; outExitCode is 0 on success, 1 on failure.
// ---------------------------------------------------------------------------

bool HandleGenAtlas( const CommandLineView& commandLine, int& outExitCode )
{
    if ( !HasOption( commandLine, "--gen-atlas" ) )
    {
        return false;
    }

    char outPath[MAX_PATH];
    const char* atlasArg = FindOptionValue( commandLine, "--gen-atlas" );
    if ( atlasArg && *atlasArg != '\0' )
    {
        if ( strlen( atlasArg ) >= MAX_PATH )
        {
            fprintf( stderr, "[gen-atlas] Output path is too long.\n" );
            outExitCode = 1;
            return true;
        }
        strcpy_s( outPath, atlasArg );
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

struct RendererOption
{
    const char* name;
    const char* alias;
    RendererType type;
};

struct ParsedArgs
{
    std::vector<std::string> sceneList;
    bool isSuiteOrSceneMode = false;
    RendererType renderer = RendererType::OpenGL;
    float switchInterval = -1.0f;
    float timeScaleOverride = 0.0f; // 0 = not set
    bool fixedStep = false;
    unsigned int seedOverride = 0; // 0 = not set
    bool noWater = false;
    bool noSleep = false;
    bool hasCinematicRenderingOverride = false;
    bool cinematicRendering = false;
    bool hasCinematicShadowsOverride = false;
    bool cinematicShadows = false;
    bool interactiveRun = false;
    int frameCountOverride = -1;
    bool sceneLoadOnly = false;
    bool demoHeroStyle = false;
    bool uiStress = false;
    unsigned int uiStressSeed = 0x7F4A7C15u;
    int uiStressActions = 5;
    char liveStyleControlDir[260] = {};
    bool suppressExitDialog = false;
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
    char physicsRegressionLogOverride[256] = {};
    char physicsCollisionTimeLogOverride[256] = {};
    char physicsDiagnosticsPath[256] = {};
#endif
    bool physicsDiagnosticsRequested = false;
    bool fixedStepForcedByPhysicsDiagnostics = false;
    bool dumpConfig = false;
};

struct CliFlagDirective
{
    const char* name;
    const char* alias;
    void ( *apply )( ParsedArgs& args );
    const char* message;
};

struct CliValueDirective
{
    const char* name;
    const char* alias;
    bool ( *apply )( const char* value, ParsedArgs& args );
};

struct PhysicsDebugComponentDirective
{
    const char* dashedName;
    const char* underscoredName;
    uint32_t flag;
};

struct PhysicsDebugFloatDirective
{
    const char* dashedName;
    const char* underscoredName;
    bool ParsedArgs::* hasOverride;
    float ParsedArgs::* value;
    float minValue;
    float maxValue;
    const char* errorMessage;
    bool enableTransparentBodies;
};

struct GeneratedObjectOverrideDirective
{
    const char* optionName;
    GeneratedObjectTypeOverride objectType;
    const char* message;
};

bool HasFlagDirective( const CommandLineView& commandLine, const CliFlagDirective& directive )
{
    return HasOption( commandLine, directive.name ) || ( directive.alias && HasOption( commandLine, directive.alias ) );
}

const char* FindValueDirective( const CommandLineView& commandLine, const CliValueDirective& directive )
{
    const char* value = FindOptionValue( commandLine, directive.name );
    if ( value || !directive.alias )
    {
        return value;
    }
    return FindOptionValue( commandLine, directive.alias );
}

template <size_t N>
bool ApplyCliValueDirectives( const CommandLineView& commandLine, ParsedArgs& out, const CliValueDirective ( &directives )[N] )
{
    for ( const CliValueDirective& directive : directives )
    {
        const char* value = FindValueDirective( commandLine, directive );
        if ( value && !directive.apply( value, out ) )
        {
            return false;
        }
    }
    return true;
}

void ApplyCliFlagDirectives( const CommandLineView& commandLine, ParsedArgs& out )
{
    static const CliFlagDirective kFlags[] = {
        { "--fixed-step", nullptr, []( ParsedArgs& args )
          { args.fixedStep = true; },
          "[fixed-step] Forced via command line." },
        { "--no-water", nullptr, []( ParsedArgs& args )
          { args.noWater = true; },
          "[water] Fluid surface starts below terrain." },
        { "--no-sleep", nullptr, []( ParsedArgs& args )
          { args.noSleep = true; },
          "[physics] Sleep disabled via command line." },
        { "--scene-load-only", "--load-scenes-only", []( ParsedArgs& args )
          {
              args.sceneLoadOnly = true;
              args.suppressExitDialog = true;
          },
          "[scene-load-only] Load queued scenes without running frames." },
        { "--demohero", "--demo-hero", []( ParsedArgs& args )
          {
              args.demoHeroStyle = true;
              args.suppressExitDialog = true;
          },
          "[scene] Generated demo scene will use the low-poly hero rendering mode." },
        { "--profiler", "--show-profiler", []( ParsedArgs& args )
          { args.showProfiler = true; },
          "[overlay] Profiler HUD enabled at startup." },
        { "--hide-top-text", "--no-top-text", []( ParsedArgs& args )
          { args.hideTopText = true; },
          "[overlay] Top HUD text hidden." },
        { "--broadphase-visualizer", "--broadphase-overlay", []( ParsedArgs& args )
          { args.showBroadphaseVisualizer = true; },
          "[overlay] Broadphase visualizer enabled at startup." },
        { "--dump-config", nullptr, []( ParsedArgs& args )
          { args.dumpConfig = true; },
          nullptr },
    };

    for ( const CliFlagDirective& flag : kFlags )
    {
        if ( HasFlagDirective( commandLine, flag ) )
        {
            flag.apply( out );
            if ( flag.message )
            {
                fprintf( stdout, "%s\n", flag.message );
            }
        }
    }
}

bool ParseOnOffValue( const char* value, bool& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }
    if ( _stricmp( value, "on" ) == 0 || _stricmp( value, "true" ) == 0 || _stricmp( value, "yes" ) == 0 )
    {
        out = true;
        return true;
    }
    if ( _stricmp( value, "off" ) == 0 || _stricmp( value, "false" ) == 0 || _stricmp( value, "no" ) == 0 )
    {
        out = false;
        return true;
    }

    int numeric = 0;
    if ( ParseIntToken( value, numeric ) )
    {
        out = numeric != 0;
        return true;
    }
    return false;
}

bool ParseOptionalOnOffValue( const char* value, bool& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        out = true;
        return true;
    }
    return ParseOnOffValue( value, out );
}


bool SceneArgHasPathSyntax( const std::string& sceneArg )
{
    return sceneArg.find( '/' ) != std::string::npos ||
           sceneArg.find( '\\' ) != std::string::npos ||
           sceneArg.find( ':' ) != std::string::npos;
}


bool SceneArgHasExtension( const std::string& sceneArg )
{
    const size_t slash = sceneArg.find_last_of( "/\\" );
    const size_t dot = sceneArg.find_last_of( '.' );
    return dot != std::string::npos && ( slash == std::string::npos || dot > slash );
}


bool FileExistsForLaunch( const std::string& path )
{
    return _access( path.c_str(), 0 ) == 0;
}


std::string HeroSceneLaunchPath()
{
    return std::string( DATA_ROOT ) + "scenes/concept_12_low_poly_art_style.scene";
}


std::string ResolveSceneLaunchPath( const char* rawSceneArg )
{
    std::string sceneArg( rawSceneArg );
    if ( sceneArg.empty() || SceneArgHasPathSyntax( sceneArg ) )
    {
        return sceneArg;
    }

    if ( _stricmp( sceneArg.c_str(), "hero" ) == 0 ||
         _stricmp( sceneArg.c_str(), "low_poly_hero" ) == 0 ||
         _stricmp( sceneArg.c_str(), "low-poly-hero" ) == 0 )
    {
        return HeroSceneLaunchPath();
    }

    const std::string sceneDir = std::string( DATA_ROOT ) + "scenes/";
    if ( !SceneArgHasExtension( sceneArg ) )
    {
        const std::string sceneCandidate = sceneDir + sceneArg + ".scene";
        if ( FileExistsForLaunch( sceneCandidate ) )
        {
            return sceneCandidate;
        }
    }

    const std::string directCandidate = sceneDir + sceneArg;
    if ( FileExistsForLaunch( directCandidate ) )
    {
        return directCandidate;
    }

    return sceneArg;
}


bool ParsePhysicsDebugMode( const char* value, uint32_t& outFlags )
{
    if ( IsOptionValueMissing( value ) )
    {
        return false;
    }
    if ( _stricmp( value, "none" ) == 0 || _stricmp( value, "off" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_NONE;
        return true;
    }
    if ( _stricmp( value, "axes" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_AXES;
        return true;
    }
    if ( _stricmp( value, "contacts" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_CONTACTS;
        return true;
    }
    if ( _stricmp( value, "sleep" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_SLEEP;
        return true;
    }
    if ( _stricmp( value, "pipeline" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_PIPELINE;
        return true;
    }
    if ( _stricmp( value, "all" ) == 0 || _stricmp( value, "on" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_ALL;
        return true;
    }
    return false;
}

bool ApplyPhysicsDebugComponentOverride( const CommandLineView& commandLine, const char* dashedName, const char* underscoredName, uint32_t flag, ParsedArgs& out )
{
    const char* value = FindOptionValue( commandLine, dashedName, underscoredName );
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

bool ApplyPhysicsDebugFloatOverride( const CommandLineView& commandLine, const PhysicsDebugFloatDirective& directive, ParsedArgs& out )
{
    const char* value = FindOptionValue( commandLine, directive.dashedName, directive.underscoredName );
    if ( !value )
    {
        return true;
    }

    float parsed = 0.0f;
    if ( !ParseFloatToken( value, parsed ) || parsed < directive.minValue || parsed > directive.maxValue )
    {
        return FailCommandLineParse( directive.errorMessage );
    }

    out.*( directive.hasOverride ) = true;
    out.*( directive.value ) = parsed;
    if ( directive.enableTransparentBodies && !out.hasPhysicsDebugTransparentOverride )
    {
        out.hasPhysicsDebugTransparentOverride = true;
        out.physicsDebugTransparentOverride = true;
    }
    return true;
}

bool ParsePhysicsDebugOverrides( const CommandLineView& commandLine, ParsedArgs& out )
{
    const char* modeValue = FindOptionValue( commandLine, "--physics-debug", "--physics_debug" );
    if ( modeValue )
    {
        if ( !ParsePhysicsDebugMode( modeValue, out.physicsDebugFlagsOverride ) )
        {
            return FailCommandLineParse( "--physics-debug expects none|axes|contacts|sleep|pipeline|all|on|off." );
        }
        out.hasPhysicsDebugFlagsOverride = true;
    }

    static const PhysicsDebugComponentDirective kComponentOverrides[] = {
        { "--physics-debug-axes", "--physics_debug_axes", PHYSICS_DEBUG_AXES },
        { "--physics-debug-contacts", "--physics_debug_contacts", PHYSICS_DEBUG_CONTACTS },
        { "--physics-debug-sleep", "--physics_debug_sleep", PHYSICS_DEBUG_SLEEP },
        { "--physics-debug-pipeline", "--physics_debug_pipeline", PHYSICS_DEBUG_PIPELINE },
    };
    for ( const PhysicsDebugComponentDirective& component : kComponentOverrides )
    {
        if ( !ApplyPhysicsDebugComponentOverride( commandLine, component.dashedName, component.underscoredName, component.flag, out ) )
        {
            return false;
        }
    }

    const char* transparentValue = FindOptionValue( commandLine, "--physics-debug-transparent", "--physics_debug_transparent" );
    if ( transparentValue )
    {
        if ( !ParseOptionalOnOffValue( transparentValue, out.physicsDebugTransparentOverride ) )
        {
            return FailCommandLineParse( "--physics-debug-transparent expects optional on|off." );
        }
        out.hasPhysicsDebugTransparentOverride = true;
    }

    static const PhysicsDebugFloatDirective kFloatOverrides[] = {
        { "--physics-debug-alpha", "--physics_debug_alpha", &ParsedArgs::hasPhysicsDebugAlphaOverride, &ParsedArgs::physicsDebugAlphaOverride, 0.05f, 1.0f, "--physics-debug-alpha expects 0.05..1.0.", true },
        { "--physics-debug-contact-linger", "--physics_debug_contact_linger", &ParsedArgs::hasPhysicsDebugContactLingerOverride, &ParsedArgs::physicsDebugContactLingerOverride, 0.0f, 5.0f, "--physics-debug-contact-linger expects 0.0..5.0 seconds.", false },
    };
    for ( const PhysicsDebugFloatDirective& directive : kFloatOverrides )
    {
        if ( !ApplyPhysicsDebugFloatOverride( commandLine, directive, out ) )
        {
            return false;
        }
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
// Falls back to a single empty string (generated demo mode) when neither flag is given.
bool ParseSceneArgs( const CommandLineView& commandLine, std::vector<std::string>& sceneList, bool& isSuiteOrSceneMode )
{
    const char* suiteArg = FindOptionValue( commandLine, "--suite" );
    const char* sceneArg = FindOptionValue( commandLine, "--scene" );
    const bool heroArg = HasOption( commandLine, "--hero" );
    const bool demoHeroArg = HasOption( commandLine, "--demohero" ) || HasOption( commandLine, "--demo-hero" );

    if ( ( suiteArg && sceneArg ) ||
         ( heroArg && ( suiteArg || sceneArg ) ) ||
         ( demoHeroArg && ( suiteArg || sceneArg || heroArg ) ) )
    {
        return FailCommandLineParse( "--demohero, --hero, --suite, and --scene are mutually exclusive." );
    }

    if ( heroArg )
    {
        sceneList.push_back( HeroSceneLaunchPath() );
        isSuiteOrSceneMode = true;
        fprintf( stdout, "[scene] Hero scene selected.\n" );
    }
    else if ( suiteArg )
    {
        if ( IsOptionValueMissing( suiteArg ) )
        {
            return FailCommandLineParse( "--suite requires a path." );
        }

        // Extract just the filename token — support both quoted and unquoted paths.
        std::string suitePath( suiteArg );

        // Read suite file: one scene path per line, # comments ignored
        std::ifstream suiteFile( suitePath );
        if ( suiteFile )
        {
            std::string line;
            while ( std::getline( suiteFile, line ) )
            {
                while ( !line.empty() && ( line.back() == '\r' || line.back() == ' ' ) )
                {
                    line.pop_back();
                }
                if ( !line.empty() && line[0] != '#' )
                {
                    sceneList.push_back( line );
                }
            }
        }
        else
        {
            return FailCommandLineParse( "--suite could not open '%s'.", suitePath.c_str() );
        }
        isSuiteOrSceneMode = true;
    }
    else if ( sceneArg )
    {
        if ( IsOptionValueMissing( sceneArg ) )
        {
            return FailCommandLineParse( "--scene requires a path." );
        }

        if ( *sceneArg != '\0' )
        {
            // Support both quoted ("path with spaces") and unquoted tokens.
            // Quoted paths stop at the closing '"'; unquoted paths stop at whitespace.
            // This handles launchers (CDB, VS debugger) that wrap paths in quotes.
            sceneList.push_back( ResolveSceneLaunchPath( sceneArg ) );
            isSuiteOrSceneMode = true;
        }
    }

    if ( sceneList.empty() )
    {
        sceneList.push_back( "" ); // generated demo mode
    }
    return true;
}

bool ParseRendererArg( const CommandLineView& commandLine, RendererType& out )
{
    static const RendererOption kRenderers[] = {
        { "gl", "opengl", RendererType::OpenGL },
        { "dx11", "d3d11", RendererType::DX11 },
        { "dx12", "d3d12", RendererType::DX12 },
    };

    const char* rendererArg = FindOptionValue( commandLine, "--renderer" );
    if ( !rendererArg )
    {
        out = RendererType::OpenGL;
        return true;
    }

    if ( IsOptionValueMissing( rendererArg ) )
    {
        return FailCommandLineParse( "--renderer expects gl|dx11|dx12." );
    }

    for ( const RendererOption& renderer : kRenderers )
    {
        if ( _stricmp( rendererArg, renderer.name ) == 0 ||
             ( renderer.alias && _stricmp( rendererArg, renderer.alias ) == 0 ) )
        {
            out = renderer.type;
            return true;
        }
    }

    return FailCommandLineParse( "--renderer expects gl|dx11|dx12." );
}

// Applies --vsync on|off to the already-loaded Cfg() singleton.
bool ApplyVsyncOverride( const CommandLineView& commandLine )
{
    const char* vsyncArg = FindOptionValue( commandLine, "--vsync" );
    if ( !vsyncArg )
    {
        return true;
    }

    bool enabled = false;
    if ( !ParseOnOffValue( vsyncArg, enabled ) )
    {
        return FailCommandLineParse( "--vsync expects on|off." );
    }

    Cfg().runtimeRender.vsyncEnabled = enabled;
    fprintf( stdout, "[vsync] %s via command line.\n", enabled ? "Enabled" : "Disabled" );
    return true;
}


bool ApplyCinematicShadowsOverride( const char* value, ParsedArgs& args )
{
    bool enabled = false;
    if ( !ParseOptionalOnOffValue( value, enabled ) )
    {
        return FailCommandLineParse( "--shadows expects optional on|off." );
    }

    args.hasCinematicShadowsOverride = true;
    args.cinematicShadows = enabled;
    fprintf( stdout,
             "[shadows] Shadow maps %s via command line.\n",
             enabled ? "enabled" : "disabled" );
    return true;
}


bool ApplyStartupCliValueDirectives( const CommandLineView& commandLine, ParsedArgs& out )
{
    static const CliValueDirective kValues[] = {
        { "--switch-interval", nullptr, []( const char* value, ParsedArgs& args ) -> bool
          {
              float interval = 0.0f;
              if ( !ParseFloatToken( value, interval ) || interval <= 0.0f )
              {
                  return FailCommandLineParse( "--switch-interval expects a positive float." );
              }
              args.switchInterval = interval;
              return true;
          } },
        { "--time-scale", nullptr, []( const char* value, ParsedArgs& args ) -> bool
          {
              float timeScale = 0.0f;
              if ( !ParseFloatToken( value, timeScale ) || timeScale <= 0.0f )
              {
                  return FailCommandLineParse( "--time-scale expects a positive float." );
              }
              args.timeScaleOverride = timeScale;
              fprintf( stdout, "[time-scale] Override: %.4f\n", timeScale );
              return true;
          } },
        { "--cinematic", "--cinematic-rendering", []( const char* value, ParsedArgs& args ) -> bool
          {
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--cinematic expects optional on|off." );
              }
              args.hasCinematicRenderingOverride = true;
              args.cinematicRendering = enabled;
              fprintf( stdout, "[cinematic] Rendering %s via command line.\n", enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--shadows", "--shadow-maps", ApplyCinematicShadowsOverride },
        { "--cinematic-shadows", "--cinematic_shadows", ApplyCinematicShadowsOverride },
        { "--interactive", "--hold", []( const char* value, ParsedArgs& args ) -> bool
          {
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--interactive expects optional on|off." );
              }
              args.interactiveRun = enabled;
              args.suppressExitDialog = args.suppressExitDialog || enabled;
              if ( enabled )
              {
                  fprintf( stdout, "[scene] Interactive hold enabled; scene automation will not quit the app.\n" );
              }
              return true;
          } },
    };

    out.switchInterval = -1.0f;
    return ApplyCliValueDirectives( commandLine, out, kValues );
}


bool ApplyLiveStyleControlDir( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--live-style-control expects a directory path." );
    }
    if ( strlen( value ) >= sizeof( args.liveStyleControlDir ) )
    {
        return FailCommandLineParse( "--live-style-control path is too long." );
    }

    strcpy_s( args.liveStyleControlDir, sizeof( args.liveStyleControlDir ), value );
    args.interactiveRun = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[style-harness] Live style control directory: %s\n", args.liveStyleControlDir );
    return true;
}


bool ApplyRunCliValueDirectives( const CommandLineView& commandLine, ParsedArgs& out )
{
    static const CliValueDirective kValues[] = {
        { "--seed", nullptr, []( const char* value, ParsedArgs& args ) -> bool
          {
              unsigned int seed = 0;
              if ( !ParseUnsignedIntToken( value, seed ) || seed == 0 )
              {
                  return FailCommandLineParse( "--seed expects a positive 32-bit integer." );
              }
              args.seedOverride = seed;
              fprintf( stdout, "[seed] Override: %u\n", args.seedOverride );
              return true;
          } },
        { "--frames", nullptr, []( const char* value, ParsedArgs& args ) -> bool
          {
              int frames = 0;
              if ( !ParseIntToken( value, frames ) || frames <= 0 )
              {
                  return FailCommandLineParse( "--frames expects a positive integer." );
              }
              args.frameCountOverride = frames;
              args.suppressExitDialog = true;
              fprintf( stdout, "[frames] Exit after %d frames.\n", args.frameCountOverride );
              return true;
          } },
        { "--live-style-control", "--style-harness", ApplyLiveStyleControlDir },
        { "--live_style_control", "--style_harness", ApplyLiveStyleControlDir },
        { "--ui-stress", "--ui_stress", []( const char* value, ParsedArgs& args ) -> bool
          {
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--ui-stress expects optional on|off." );
              }
              args.uiStress = enabled;
              args.suppressExitDialog = args.suppressExitDialog || enabled;
              return true;
          } },
        { "--ui-stress-seed", "--ui_stress_seed", []( const char* value, ParsedArgs& args ) -> bool
          {
              unsigned int seed = 0;
              if ( !ParseUnsignedIntToken( value, seed ) || seed == 0 )
              {
                  return FailCommandLineParse( "--ui-stress-seed expects a positive 32-bit integer." );
              }
              args.uiStress = true;
              args.uiStressSeed = seed;
              args.suppressExitDialog = true;
              return true;
          } },
        { "--ui-stress-actions", "--ui_stress_actions", []( const char* value, ParsedArgs& args ) -> bool
          {
              int actions = 0;
              if ( !ParseIntToken( value, actions ) || actions <= 0 || actions > 32 )
              {
                  return FailCommandLineParse( "--ui-stress-actions expects 1..32." );
              }
              args.uiStress = true;
              args.uiStressActions = actions;
              args.suppressExitDialog = true;
              return true;
          } },
    };

    return ApplyCliValueDirectives( commandLine, out, kValues );
}

bool ApplyGeneratedObjectOverride( const CommandLineView& commandLine, ParsedArgs& out )
{
    static const GeneratedObjectOverrideDirective kOverrides[] = {
        { "--all-balls", GeneratedObjectTypeOverride::AllBalls, "[objects] Generated objects forced to balls." },
        { "--all-boxes", GeneratedObjectTypeOverride::AllBoxes, "[objects] Generated objects forced to boxes." },
    };

    const GeneratedObjectOverrideDirective* selected = nullptr;
    for ( const GeneratedObjectOverrideDirective& directive : kOverrides )
    {
        if ( !HasOption( commandLine, directive.optionName ) )
        {
            continue;
        }
        if ( selected )
        {
            return FailCommandLineParse( "--all-balls and --all-boxes are mutually exclusive." );
        }
        selected = &directive;
    }

    if ( selected )
    {
        out.objectTypeOverride = selected->objectType;
        fprintf( stdout, "%s\n", selected->message );
    }
    return true;
}

// Guards --physics-regression-log against use in non-Debug builds.
// Returns false if startup should abort.
bool ValidatePhysicsRegressionLog( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--physics-regression-log" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--physics-regression-log is only supported in Debug builds. Recompile with the Debug configuration to use physics regression logging." );
#else
    return true;
#endif
}


// Guards --physics-collision-time-log against use in non-Debug builds.
// Returns false if startup should abort.
bool ValidatePhysicsCollisionTimeLog( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--physics-collision-time-log" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--physics-collision-time-log is only supported in Debug builds. Recompile with the Debug configuration to use collision-time logging." );
#else
    return true;
#endif
}


// Guards --physics-diag / --physics-diagnostics against use in non-Debug builds.
// Diagnostics traces are model-facing debug artifacts and are not a Profile/Release dependency.
bool ValidatePhysicsDiagnostics( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--physics-diag" ) &&
         !HasOption( commandLine, "--physics-diagnostics" ) )
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
bool ParsePhysicsRegressionLogOverride( const CommandLineView& commandLine, char ( &outPath )[256] )
{
    outPath[0] = '\0';
    const char* physLogArg = FindOptionValue( commandLine, "--physics-regression-log" );
    if ( !physLogArg )
    {
        return true;
    }

    if ( !CopyOptionPath( physLogArg, "--physics-regression-log", outPath, sizeof( outPath ) ) )
    {
        return false;
    }

    fprintf( stdout, "[physics-regression-log] Output: %s\n", outPath );
    return true;
}


bool ParsePhysicsCollisionTimeLogOverride( const CommandLineView& commandLine, char ( &outPath )[256] )
{
    outPath[0] = '\0';
    const char* collisionLogArg = FindOptionValue( commandLine, "--physics-collision-time-log" );
    if ( !collisionLogArg )
    {
        return true;
    }

    if ( !CopyOptionPath( collisionLogArg, "--physics-collision-time-log", outPath, sizeof( outPath ) ) )
    {
        return false;
    }

    fprintf( stdout, "[physics-collision-time-log] Output: %s\n", outPath );
    return true;
}


bool ParsePhysicsDiagnosticsPath( const CommandLineView& commandLine, char ( &outPath )[256] )
{
    outPath[0] = '\0';
    const char* diagArg = FindOptionValue( commandLine, "--physics-diag" );
    if ( !diagArg )
    {
        diagArg = FindOptionValue( commandLine, "--physics-diagnostics" );
    }
    if ( !diagArg )
    {
        return true;
    }

    return CopyOptionPath( diagArg, "--physics-diag", outPath, sizeof( outPath ) );
}
#endif

// Parses all command-line options into a ParsedArgs struct.
// Also loads engine.cfg and applies any overrides to the global Cfg() singleton.
// Returns false if startup should abort (e.g. --physics-regression-log in Release build).
bool ParseCommandLine( const CommandLineView& commandLine, ParsedArgs& out )
{
    if ( !ParseSceneArgs( commandLine, out.sceneList, out.isSuiteOrSceneMode ) )
    {
        return false;
    }
    if ( !ParseRendererArg( commandLine, out.renderer ) )
    {
        return false;
    }

    Cfg().Load( ( std::string( DATA_ROOT ) + "engine.cfg" ).c_str() );
    if ( !ApplyVsyncOverride( commandLine ) )
    {
        return false;
    }

    if ( !ValidatePhysicsRegressionLog( commandLine ) )
    {
        return false;
    }
    if ( !ValidatePhysicsCollisionTimeLog( commandLine ) )
    {
        return false;
    }
    if ( !ValidatePhysicsDiagnostics( commandLine ) )
    {
        return false;
    }

#ifdef _DEBUG
    if ( !ParsePhysicsRegressionLogOverride( commandLine, out.physicsRegressionLogOverride ) )
    {
        return false;
    }
    if ( !ParsePhysicsCollisionTimeLogOverride( commandLine, out.physicsCollisionTimeLogOverride ) )
    {
        return false;
    }
    if ( !ParsePhysicsDiagnosticsPath( commandLine, out.physicsDiagnosticsPath ) )
    {
        return false;
    }
    out.physicsDiagnosticsRequested = out.physicsDiagnosticsPath[0] != '\0';
#endif

    if ( !ApplyStartupCliValueDirectives( commandLine, out ) )
    {
        return false;
    }

    ApplyCliFlagDirectives( commandLine, out );

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

    if ( !ApplyRunCliValueDirectives( commandLine, out ) )
    {
        return false;
    }

    if ( out.uiStress )
    {
        fprintf( stdout, "[ui-stress] Enabled seed=%u actions=%d.\n", out.uiStressSeed, out.uiStressActions );
    }

    if ( !ApplyGeneratedObjectOverride( commandLine, out ) )
    {
        return false;
    }

    if ( !ParsePhysicsDebugOverrides( commandLine, out ) )
    {
        return false;
    }

    if ( out.dumpConfig )
    {
        Cfg().Dump( stdout );
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

int RunApp( SkullbonezWindow* window, ParsedArgs& args )
{
    {
        SkullbonezRun cRun( std::move( args.sceneList ) );
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
        if ( args.noSleep )
        {
            cRun.SetNoSleepOverride();
        }
        if ( args.hasCinematicRenderingOverride )
        {
            cRun.SetCinematicRenderingOverride( args.cinematicRendering );
        }
        if ( args.hasCinematicShadowsOverride )
        {
            cRun.SetCinematicShadowsOverride( args.cinematicShadows );
        }
        if ( args.demoHeroStyle )
        {
            cRun.SetDemoHeroStyleOverride();
        }
        if ( args.interactiveRun )
        {
            cRun.SetInteractiveRunOverride();
        }
        if ( args.liveStyleControlDir[0] != '\0' )
        {
            cRun.SetLiveStyleControlDirectory( args.liveStyleControlDir );
        }
        if ( args.frameCountOverride > 0 )
        {
            cRun.SetFrameCountOverride( args.frameCountOverride );
        }
        if ( args.uiStress )
        {
            cRun.SetUIStressOverride( args.uiStressSeed, args.uiStressActions );
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
        if ( args.physicsRegressionLogOverride[0] != '\0' )
        {
            cRun.SetPhysicsRegressionLogOverride( args.physicsRegressionLogOverride );
        }
        if ( args.physicsCollisionTimeLogOverride[0] != '\0' )
        {
            cRun.SetPhysicsCollisionTimeLogOverride( args.physicsCollisionTimeLogOverride );
        }
        if ( args.physicsDiagnosticsPath[0] != '\0' )
        {
            cRun.SetPhysicsDiagnosticsPath( args.physicsDiagnosticsPath, args.fixedStepForcedByPhysicsDiagnostics );
        }
#endif
        try
        {
            cRun.Initialise();
            if ( args.sceneLoadOnly )
            {
                cRun.RunSceneLoadOnly();
            }
            else
            {
                cRun.Run();
            }

            if ( !args.isSuiteOrSceneMode && !args.suppressExitDialog )
            {
                window->MsgBox( "Thanks for using the Skullbonez Core!", "Alert!", MB_OK );
            }
        }
        catch ( const std::exception& e )
        {
            Log().WriteEventf( "fatal_exception message=\"%s\"", e.what() );
            fprintf( stderr, "FATAL: %s\n", e.what() );
            fflush( stderr );
            Log().FlushAll();
            if ( !args.isSuiteOrSceneMode && !args.suppressExitDialog )
            {
                window->MsgBox( e.what(), "Alert!", MB_OK );
            }
            return 1;
        }
    } // cRun destroyed here — GL context still alive for proper cleanup
    return 0;
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
        Input::SetSystemCursorVisible( true );
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

    const CommandLineView commandLine = TokenizeCommandLine( szCmdLine );

#ifdef _DEBUG
    InstallDebugCrashLogger();
    Log().WriteEventf( "process_started command_line=\"%s\"", szCmdLine ? szCmdLine : "" );
    if ( HasOption( commandLine, "--debug-crash-test" ) )
    {
        Log().WriteEventf( "debug_crash_test_requested" );
        volatile int* crashAddress = nullptr;
        *crashAddress = 1;
    }
#endif

    // Initialize COM on the main thread (multi-threaded apartment). Required before any
    // WinRT/COM activation occurs — without this, MSCTF.dll throws 0x800401F0 during
    // text/input service initialization triggered by window creation.
    CoInitializeEx( nullptr, COINIT_MULTITHREADED );

    AttachParentConsole();

    int atlasExitCode = 0;
    if ( HandleGenAtlas( commandLine, atlasExitCode ) )
    {
        return atlasExitCode;
    }

    ParsedArgs args;
    if ( !ParseCommandLine( commandLine, args ) )
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

    const int runExitCode = RunApp( window, args );

    CleanupWindow( window, hInstance );

    CoUninitialize();

    // Write memory leaks to output window
    // _CrtDumpMemoryLeaks();

    return runExitCode;
}
