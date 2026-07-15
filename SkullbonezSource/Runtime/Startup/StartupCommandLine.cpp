/*
File: StartupCommandLine.cpp
Purpose:
  Tokenizes WinMain input, validates every startup option, and fills ParsedArgs
  plus the startup EngineConfig snapshot.

Summary:
  The parser preserves the historical table order and exact diagnostics. It
  delegates only scene/suite and physics-debug launch policy to the launch
  resolution unit.

Glossary:
  Directive row: Fixed parser-table entry mapping an option spelling to a
    value or flag callback.
  Lane R result: Recoverable startup parse failure reported through the fixed
    command-line error buffer.
  Build-lane option: Debug-only validation flag rejected in unsupported builds.

Invariants:
  - Parser order, output strings, defaults, aliases, and validation precedence
    are frozen compatibility behavior.
  - The fixed error buffer is owned here and read only after ParseCommandLine
    returns false.
  - No startup reference is retained after the synchronous parse.

Related:
  - StartupCommandLine.h
  - StartupLaunchResolution.h
  - Agentic/Reference/runtime-reference.md
*/
#include "StartupCommandLine.h"
#include "StartupLaunchResolution.h"
#include "StartupProbeHarnesses.h"
#include "../../Core/Common.h"
#include "../../Core/PlatformProfiler.h"
#include "../../Core/WorkerPool.h"
#include "../RunLaunchOptions.Renderer.h"
#include "../WindowConstants.h"
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Threading;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;
namespace SkullbonezCore
{
namespace Runtime
{
namespace Startup
{
namespace
{
char g_commandLineError[512] = {};
struct CliValueDirective
{
    const char* name;
    const char* alias;
    bool ( *apply )( const char* value, ParsedArgs& args );
};
struct ConfigCliValueDirective
{
    // Why: startup config options mutate the loaded SkullbonezCore::Core::EngineConfig before any
    // subsystem borrows it, so these handlers must not reopen the global config
    // singleton.
    const char* name;
    const char* alias;
    bool ( *apply )( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config );
};
struct GeneratedObjectOverrideDirective
{
    const char* optionName;
    GeneratedObjectTypeOverride objectType;
    const char* message;
};
} // anonymous namespace
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
namespace
{
bool IsTokenWhitespace( char c )
{
    return c == ' ' || c == '\t';
}
} // anonymous namespace
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
namespace
{
bool IsOptionToken( const std::string& token )
{
    return token.size() >= 2 && token[0] == '-' && token[1] == '-';
}
} // anonymous namespace
bool IsOptionValueMissing( const char* value )
{
    return !value || *value == '\0';
}
namespace
{
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
} // anonymous namespace
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
        if ( OptionTokenMatches( token, optionName ) ||
             OptionTokenHasAssignedValue( token, optionName, assignedValue ) )
        {
            return true;
        }
    }
    return false;
}
namespace
{
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
} // anonymous namespace
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
namespace
{
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
const char* FindValueDirective( const CommandLineView& commandLine, const CliValueDirective& directive )
{
    const char* value = FindOptionValue( commandLine, directive.name );
    if ( value || !directive.alias )
    {
        return value;
    }
    return FindOptionValue( commandLine, directive.alias );
}
const char* FindValueDirective( const CommandLineView& commandLine, const ConfigCliValueDirective& directive )
{
    const char* value = FindOptionValue( commandLine, directive.name );
    if ( value || !directive.alias )
    {
        return value;
    }
    return FindOptionValue( commandLine, directive.alias );
}
template <size_t N>
bool ApplyCliValueDirectives( const CommandLineView& commandLine,
                              ParsedArgs& out,
                              const CliValueDirective ( &directives )[N] )
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
template <size_t N>
bool ApplyConfigCliValueDirectives( const CommandLineView& commandLine,
                                    ParsedArgs& out,
                                    SkullbonezCore::Core::EngineConfig& config,
                                    const ConfigCliValueDirective ( &directives )[N] )
{
    for ( const ConfigCliValueDirective& directive : directives )
    {
        const char* value = FindValueDirective( commandLine, directive );
        if ( value && !directive.apply( value, out, config ) )
        {
            return false;
        }
    }
    return true;
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
bool ParseEnvironmentBool( const char* value, bool& out )
{
    if ( !value || *value == '\0' )
    {
        return false;
    }
    if ( _stricmp( value, "1" ) == 0 || _stricmp( value, "on" ) == 0 || _stricmp( value, "true" ) == 0 ||
         _stricmp( value, "yes" ) == 0 )
    {
        out = true;
        return true;
    }
    if ( _stricmp( value, "0" ) == 0 || _stricmp( value, "off" ) == 0 || _stricmp( value, "false" ) == 0 ||
         _stricmp( value, "no" ) == 0 )
    {
        out = false;
        return true;
    }
    return false;
}
} // anonymous namespace
bool ParseOptionalOnOffValue( const char* value, bool& out )
{
    if ( IsOptionValueMissing( value ) )
    {
        out = true;
        return true;
    }
    return ParseOnOffValue( value, out );
}
namespace
{
bool ParseAllocationGuardModeValue( const char* value, RuntimeAllocation::RuntimeAllocationGuardMode& out )
{
    if ( IsOptionValueMissing( value ) || _stricmp( value, "measure" ) == 0 )
    {
        out = RuntimeAllocation::RuntimeAllocationGuardMode::Measure;
        return true;
    }
    if ( _stricmp( value, "off" ) == 0 || _stricmp( value, "none" ) == 0 )
    {
        out = RuntimeAllocation::RuntimeAllocationGuardMode::Off;
        return true;
    }
    if ( _stricmp( value, "gameplay" ) == 0 || _stricmp( value, "warn" ) == 0 || _stricmp( value, "warnings" ) == 0 )
    {
        out = RuntimeAllocation::RuntimeAllocationGuardMode::Gameplay;
        return true;
    }
    return false;
}
bool ParseRendererArg( const CommandLineView& commandLine )
{
    const char* rendererArg = FindOptionValue( commandLine, "--renderer" );
    if ( !rendererArg )
    {
        return true;
    }
    if ( IsOptionValueMissing( rendererArg ) )
    {
        return FailCommandLineParse( "--renderer expects dx12. GL and DX11 are retired runtime choices." );
    }
    for ( const SkullbonezCore::Runtime::RuntimeRendererOption& renderer :
          SkullbonezCore::Runtime::kRuntimeRendererOptions )
    {
        if ( _stricmp( rendererArg, renderer.name ) == 0 ||
             ( renderer.alias && _stricmp( rendererArg, renderer.alias ) == 0 ) )
        {
            return true;
        }
    }
    return FailCommandLineParse( "--renderer expects dx12. GL and DX11 are retired runtime choices." );
}
bool ApplyVsyncOverride( const CommandLineView& commandLine, SkullbonezCore::Core::EngineConfig& config )
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
    config.runtimeRender.vsyncEnabled = enabled;
    fprintf( stdout, "[vsync] %s via command line.\n", enabled ? "Enabled" : "Disabled" );
    return true;
}
bool ApplyCinematicShadowsOverride( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config )
{
    static_cast<void>( config );
    bool enabled = false;
    if ( !ParseOptionalOnOffValue( value, enabled ) )
    {
        return FailCommandLineParse( "--shadows expects optional on|off." );
    }
    args.hasCinematicShadowsOverride = true;
    args.cinematicShadows = enabled;
    fprintf( stdout, "[shadows] Shadow maps %s via command line.\n", enabled ? "enabled" : "disabled" );
    return true;
}
bool ApplyStartupCliValueDirectives( const CommandLineView& commandLine,
                                     ParsedArgs& out,
                                     SkullbonezCore::Core::EngineConfig& config )
{
    static const ConfigCliValueDirective kValues[] = {
        { "--switch-interval",
          nullptr,
          []( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config ) -> bool
          {
              static_cast<void>( value );
              static_cast<void>( args );
              static_cast<void>( config );
              return FailCommandLineParse( "--switch-interval is retired because DX12 is the only runtime renderer." );
          } },
        { "--time-scale",
          nullptr,
          []( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config ) -> bool
          {
              static_cast<void>( config );
              float timeScale = 0.0f;
              if ( !ParseFloatToken( value, timeScale ) || timeScale <= 0.0f )
              {
                  return FailCommandLineParse( "--time-scale expects a positive float." );
              }
              args.timeScaleOverride = timeScale;
              fprintf( stdout, "[time-scale] Override: %.4f\n", timeScale );
              return true;
          } },
        { "--tornado",
          nullptr,
          []( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config ) -> bool
          {
              static_cast<void>( config );
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--tornado expects optional on|off." );
              }
              args.hasTornadoOverride = true;
              args.tornadoEnabled = enabled;
              fprintf( stdout, "[tornado] Force field %s via command line.\n", enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--tornado-vectors",
          "--tornado-vector-field",
          []( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config ) -> bool
          {
              static_cast<void>( config );
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--tornado-vectors expects optional on|off." );
              }
              args.tornadoVectors = enabled;
              fprintf( stdout,
                       "[tornado] Velocity-field vectors %s via command line.\n",
                       enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--cinematic",
          "--cinematic-rendering",
          []( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config ) -> bool
          {
              static_cast<void>( config );
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
        { "--workers",
          "--worker-threads",
          []( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config ) -> bool
          {
              static_cast<void>( args );
              int workerThreads = 0;
              const int maxWorkerThreads = WorkerPool::MaxThreadCount();
              if ( !ParseIntToken( value, workerThreads ) || workerThreads < -1 || workerThreads > maxWorkerThreads )
              {
                  char message[128] = {};
                  snprintf( message, sizeof( message ), "--workers expects -1, 0, or 1..%d.", maxWorkerThreads );
                  return FailCommandLineParse( message );
              }
              config.runtimeCapacity.workerThreads = workerThreads;
              fprintf( stdout,
                       "[workers] Override: %d (resolved %d, max %d)\n",
                       config.runtimeCapacity.workerThreads,
                       WorkerPool::ResolveThreadCount( config.runtimeCapacity.workerThreads ),
                       maxWorkerThreads );
              return true;
          } },
        { "--model-capacity",
          nullptr,
          []( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config ) -> bool
          {
              static_cast<void>( args );
              int capacity = 0;
              if ( !ParseIntToken( value, capacity ) || capacity < 1 ||
                   capacity > SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS )
              {
                  return FailCommandLineParse( "--model-capacity expects 1..%d.",
                                               SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
              }
              config.runtimeCapacity.gameModelCapacity = capacity;
              fprintf( stdout,
                       "[models] Active model capacity: %d (compiled max %d)\n",
                       config.runtimeCapacity.gameModelCapacity,
                       SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
              return true;
          } },
        { "--physics-parallel",
          "--parallel-physics",
          []( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config ) -> bool
          {
              static_cast<void>( args );
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--physics-parallel expects optional on|off." );
              }
              config.physicsExecution.parallel = enabled;
              config.physicsExecution.parallelApplyForces = enabled;
              config.physicsExecution.parallelTornadoField = enabled;
              config.physicsExecution.parallelNarrowphase = enabled;
              config.physicsExecution.parallelTerrainDetect = enabled;
              config.physicsExecution.parallelIntegrate = enabled;
              fprintf( stdout,
                       "[workers] Physics parallel jobs %s via command line.\n",
                       enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--shadow-parallel-prep",
          "--parallel-shadow-prep",
          []( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config ) -> bool
          {
              static_cast<void>( args );
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--shadow-parallel-prep expects optional on|off." );
              }
              config.runtimeRender.shadowParallelPrep = enabled;
              fprintf( stdout,
                       "[workers] Shadow parallel prep %s via command line.\n",
                       enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--interactive",
          "--hold",
          []( const char* value, ParsedArgs& args, SkullbonezCore::Core::EngineConfig& config ) -> bool
          {
              static_cast<void>( config );
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
    return ApplyConfigCliValueDirectives( commandLine, out, config, kValues );
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
} // anonymous namespace
bool CopyCommandLinePath( const char* value, const char* optionName, char* outPath, size_t outPathSize )
{
    return CopyOptionPath( value, optionName, outPath, outPathSize );
}
bool ParseIntCommandLineToken( const char* value, int& out )
{
    return ParseIntToken( value, out );
}
bool ParseUnsignedCommandLineToken( const char* value, unsigned int& out )
{
    return ParseUnsignedIntToken( value, out );
}
bool ParseAllocationGuardCommandLineToken( const char* value, Allocation::RuntimeAllocationGuardMode& out )
{
    return ParseAllocationGuardModeValue( value, out );
}
bool ParseCommandLine( const CommandLineView& commandLine, SkullbonezCore::Core::EngineConfig& config, ParsedArgs& out )
{
    if ( !ParseSceneArgs( commandLine, out.sceneList, out.isSuiteOrSceneMode ) )
    {
        return false;
    }
    if ( !ParseRendererArg( commandLine ) )
    {
        return false;
    }
    const SkullbonezCore::Core::SbResult configLoad =
        config.Load( ( std::string( DATA_ROOT ) + "engine.cfg" ).c_str() );
    if ( !configLoad.ok )
    {
        return FailCommandLineParse( "%s: %s", configLoad.error.owner, configLoad.error.message );
    }
    if ( !ApplyVsyncOverride( commandLine, config ) )
    {
        return false;
    }
    if ( !ValidateProbeCommandLineOptions( commandLine, out ) )
    {
        return false;
    }
    if ( !ApplyStartupCliValueDirectives( commandLine, out, config ) )
    {
        return false;
    }
    ApplyProbeFlagDirectives( commandLine, out );
    bool envPlatformProfilerMarkers = false;
    char* envPlatformProfilerValue = nullptr;
    size_t envPlatformProfilerValueLen = 0;
    if ( _dupenv_s( &envPlatformProfilerValue, &envPlatformProfilerValueLen, "SKULLBONEZ_PLATFORM_PROFILER_MARKERS" ) ==
             0 &&
         ParseEnvironmentBool( envPlatformProfilerValue, envPlatformProfilerMarkers ) )
    {
        out.platformProfilerMarkers = envPlatformProfilerMarkers;
        out.platformProfilerMarkersExplicit = true;
        if ( envPlatformProfilerMarkers )
        {
            fprintf( stdout,
                     "[platform-profiler] Marker emission requested via SKULLBONEZ_PLATFORM_PROFILER_MARKERS.\n" );
        }
    }
    free( envPlatformProfilerValue );
    bool envPixMarkers = false;
    char* envPixValue = nullptr;
    size_t envPixValueLen = 0;
    if ( _dupenv_s( &envPixValue, &envPixValueLen, "SKULLBONEZ_PIX_MARKERS" ) == 0 &&
         ParseEnvironmentBool( envPixValue, envPixMarkers ) )
    {
        out.platformProfilerMarkers = envPixMarkers;
        out.platformProfilerMarkersExplicit = true;
        if ( envPixMarkers )
        {
            fprintf(
                stdout,
                "[platform-profiler] Marker emission requested via SKULLBONEZ_PIX_MARKERS compatibility alias.\n" );
        }
    }
    free( envPixValue );
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
#ifdef _DEBUG
    if ( out.replayScrubProbe )
    {
        if ( !out.replayRecording )
        {
            return FailCommandLineParse( "--replay-scrub-probe requires replay capture; remove --replay off." );
        }
        if ( !out.physicsDiagnosticsRequested )
        {
            return FailCommandLineParse(
                "--replay-scrub-probe requires --physics-diag so SkullScope can query the result." );
        }
    }
    if ( out.replaySaveProbe && !out.replayRecording )
    {
        return FailCommandLineParse( "--replay-save-probe requires replay capture; remove --replay off." );
    }
#endif
    if ( out.uiStress )
    {
        fprintf( stdout, "[ui-stress] Enabled seed=%u actions=%d.\n", out.uiStressSeed, out.uiStressActions );
    }
    if ( out.graphicsStress )
    {
        fprintf( stdout,
                 "[graphics-stress] Enabled seed=%u actions=%d scene_interval_frames=%d memory_interval_frames=%d.\n",
                 out.graphicsStressSeed,
                 out.graphicsStressActions,
                 out.graphicsStressSceneIntervalFrames,
                 out.graphicsStressMemoryIntervalFrames );
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
        config.Dump( stdout );
    }
    SkullbonezCore::Core::PlatformProfiler::SetEnabled( out.platformProfilerMarkers );
    SkullbonezCore::Core::PlatformProfiler::SetDetailedRangesEnabled( out.platformProfilerMarkers &&
                                                                      out.platformProfilerMarkersExplicit );
    if ( out.platformProfilerMarkers )
    {
        fprintf( stdout,
                 SkullbonezCore::Core::PlatformProfiler::IsAvailable()
                     ? "[platform-profiler] Platform profiler marker emission enabled.\n"
                     : "[platform-profiler] Platform profiler marker emission unavailable in this build; continuing "
                       "with in-engine profiler markers only.\n" );
    }
    return true;
}
} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore
