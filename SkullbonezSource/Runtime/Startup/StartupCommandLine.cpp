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

struct CliFlagDirective
{
    // Table-driven flag parsing keeps aliases beside the canonical spelling.
    // That matters because command-line options are user-facing compatibility
    // surface, not private implementation detail.
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


void ApplyCliFlagDirectives( const CommandLineView& commandLine, ParsedArgs& out )
{
    static const CliFlagDirective kFlags[] = {
        { "--fixed-step",
          nullptr,
          []( ParsedArgs& args ) { args.fixedStep = true; },
          "[fixed-step] Forced via command line." },
        { "--no-water",
          nullptr,
          []( ParsedArgs& args ) { args.noWater = true; },
          "[water] Fluid surface starts below terrain." },
        { "--no-sleep",
          nullptr,
          []( ParsedArgs& args ) { args.noSleep = true; },
          "[physics] Sleep disabled via command line." },
        { "--no-contact-audio",
          "--mute-contact-audio",
          []( ParsedArgs& args ) { args.noContactAudio = true; },
          "[audio] Contact impact audio disabled." },
        { "--contact-audio-smoke",
          "--audio-smoke",
          []( ParsedArgs& args )
          {
              args.contactAudioSmoke = true;
              args.suppressExitDialog = true;
          },
          "[audio] Contact audio standalone smoke requested." },
        { "--scene-load-only",
          "--load-scenes-only",
          []( ParsedArgs& args )
          {
              args.sceneLoadOnly = true;
              args.suppressExitDialog = true;
          },
          "[scene-load-only] Load queued scenes without running frames." },
        { "--demohero",
          "--demo-hero",
          []( ParsedArgs& args )
          {
              args.demoHeroStyle = true;
              args.suppressExitDialog = true;
          },
          "[scene] Generated demo scene will use the low-poly hero rendering mode." },
        { "--profiler",
          "--show-profiler",
          []( ParsedArgs& args ) { args.showProfiler = true; },
          "[overlay] SkullbonezCore::Core::Profiler HUD enabled at startup." },
        { "--platform-profiler-markers",
          "--platform-profiler",
          []( ParsedArgs& args )
          {
              args.platformProfilerMarkers = true;
              args.platformProfilerMarkersExplicit = true;
          },
          "[platform-profiler] Platform profiler marker emission requested." },
        { "--pix-markers",
          "--pix",
          []( ParsedArgs& args )
          {
              args.platformProfilerMarkers = true;
              args.platformProfilerMarkersExplicit = true;
          },
          "[platform-profiler] PIX marker compatibility alias requested." },
        { "--hide-top-text",
          "--no-top-text",
          []( ParsedArgs& args ) { args.hideTopText = true; },
          "[overlay] Top HUD text hidden." },
        { "--automation-hidden-window",
          nullptr,
          []( ParsedArgs& args )
          {
              args.automationWindowHidden = true;
              args.suppressExitDialog = true;
          },
          "[automation] Native window hidden; DX12 rendering and capture remain active." },
        { "--broadphase-visualizer",
          "--broadphase-overlay",
          []( ParsedArgs& args ) { args.showBroadphaseVisualizer = true; },
          "[overlay] Broadphase visualizer enabled at startup." },
        { "--dump-config", nullptr, []( ParsedArgs& args ) { args.dumpConfig = true; }, nullptr },
        { "--dump-assets", nullptr, []( ParsedArgs& args ) { args.dumpAssets = true; }, nullptr },
        { "--replay-scrub-test",
          "--replay_scrub_test",
          []( ParsedArgs& args )
          {
              args.replayScrubProbe = true;
              args.replayScrubProbeNormalized = 0.25f;
              args.replayRecording = true;
              args.replayExplicit = true;
              args.replaySeconds = 1;
              args.fixedStep = true;
              args.suppressExitDialog = true;
          },
          "[replay] Scrub SkullScope probe enabled." },
        { "--replay-restore-test",
          "--replay_restore_test",
          []( ParsedArgs& args )
          {
              args.replayRestoreProbe = true;
              args.replayRestoreProbeNormalized = 0.25f;
              args.replayRecording = true;
              args.replayExplicit = true;
              args.replaySeconds = 1;
              args.fixedStep = true;
              args.suppressExitDialog = true;
          },
          "[replay] Restore hash SkullScope probe enabled." },
        { "--worker-self-test",
          "--workers-self-test",
          []( ParsedArgs& args )
          {
              args.workerSelfTest = true;
              args.suppressExitDialog = true;
          },
          "[workers] Self-test requested." },
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


bool ApplySceneSnapshotOutPath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--scene-snapshot-out expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.sceneSnapshotOutPath ) )
    {
        return FailCommandLineParse( "--scene-snapshot-out path is too long." );
    }

    strcpy_s( args.sceneSnapshotOutPath, sizeof( args.sceneSnapshotOutPath ), value );
    args.sceneLoadOnly = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[scene-load-only] Snapshot output: %s\n", args.sceneSnapshotOutPath );
    return true;
}


bool ApplyMemoryDumpPath( const char* value, ParsedArgs& args )
{
    if ( !CopyOptionPath( value, "--memory-dump", args.memoryDumpPath, sizeof( args.memoryDumpPath ) ) )
    {
        return false;
    }

    args.suppressExitDialog = true;
    fprintf( stdout, "[memory] Dump output: %s\n", args.memoryDumpPath );
    return true;
}


bool ApplyInteractionScriptPath( const char* value, ParsedArgs& args )
{
    if ( !CopyOptionPath( value,
                          "--interaction-script",
                          args.interactionScriptPath,
                          sizeof( args.interactionScriptPath ) ) )
    {
        return false;
    }

    args.interactiveRun = true;
    args.suppressExitDialog = true;
    args.replayRecording = true;
    fprintf( stdout, "[interaction] Script input: %s\n", args.interactionScriptPath );
    return true;
}


bool ApplyInteractionReportPath( const char* value, ParsedArgs& args )
{
    if ( !CopyOptionPath( value,
                          "--interaction-report",
                          args.interactionReportPath,
                          sizeof( args.interactionReportPath ) ) )
    {
        return false;
    }

    args.suppressExitDialog = true;
    fprintf( stdout, "[interaction] Report output: %s\n", args.interactionReportPath );
    return true;
}


bool ApplyReplayHashLogPath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-hashes expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayHashLogPath ) )
    {
        return FailCommandLineParse( "--replay-hashes path is too long." );
    }

    strcpy_s( args.replayHashLogPath, sizeof( args.replayHashLogPath ), value );
    args.replayRecording = true;
    args.replayExplicit = true;
    fprintf( stdout, "[replay] Hash log: %s\n", args.replayHashLogPath );
    return true;
}


bool ApplyReplaySaveProbePath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-save-probe expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replaySaveProbePath ) )
    {
        return FailCommandLineParse( "--replay-save-probe path is too long." );
    }

    strcpy_s( args.replaySaveProbePath, sizeof( args.replaySaveProbePath ), value );
    args.replaySaveProbe = true;
    args.replayRecording = true;
    args.replayExplicit = true;
    args.replaySeconds = (std::max)( 1, args.replaySeconds );
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Save probe output: %s\n", args.replaySaveProbePath );
    return true;
}


bool ApplyReplayLoadPath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-load expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayLoadPath ) )
    {
        return FailCommandLineParse( "--replay-load path is too long." );
    }

    strcpy_s( args.replayLoadPath, sizeof( args.replayLoadPath ), value );
    args.replayLoad = true;
    args.interactiveRun = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Load artifact: %s\n", args.replayLoadPath );
    return true;
}


bool ApplyReplayLoadProbePath( const char* value, ParsedArgs& args )
{
    if ( !ApplyReplayLoadPath( value, args ) )
    {
        return false;
    }

    args.replayLoadProbe = true;
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Load probe input: %s\n", args.replayLoadPath );
    return true;
}


bool ApplyReplayRestoreFileProbePath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-restore-file-probe expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayRestoreFileProbePath ) )
    {
        return FailCommandLineParse( "--replay-restore-file-probe path is too long." );
    }

    strcpy_s( args.replayRestoreFileProbePath, sizeof( args.replayRestoreFileProbePath ), value );
    args.replayRestoreFileProbe = true;
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Restore file probe input: %s\n", args.replayRestoreFileProbePath );
    return true;
}


bool ApplyReplayRestoreTargetFileProbePath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-restore-target-file-probe expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayRestoreTargetFileProbePath ) )
    {
        return FailCommandLineParse( "--replay-restore-target-file-probe path is too long." );
    }

    strcpy_s( args.replayRestoreTargetFileProbePath, sizeof( args.replayRestoreTargetFileProbePath ), value );
    args.replayRestoreTargetFileProbe = true;
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Restore target probe input: %s\n", args.replayRestoreTargetFileProbePath );
    return true;
}


bool ApplyReplayRestoreBranchFileProbePath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-restore-branch-file-probe expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayRestoreBranchFileProbePath ) )
    {
        return FailCommandLineParse( "--replay-restore-branch-file-probe path is too long." );
    }

    strcpy_s( args.replayRestoreBranchFileProbePath, sizeof( args.replayRestoreBranchFileProbePath ), value );
    args.replayRestoreBranchFileProbe = true;
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Restore branch probe input: %s\n", args.replayRestoreBranchFileProbePath );
    return true;
}


bool ApplyReplayRestoreFailureFileProbePath( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--replay-restore-failure-file-probe expects a file path." );
    }
    if ( strlen( value ) >= sizeof( args.replayRestoreFailureFileProbePath ) )
    {
        return FailCommandLineParse( "--replay-restore-failure-file-probe path is too long." );
    }

    strcpy_s( args.replayRestoreFailureFileProbePath, sizeof( args.replayRestoreFailureFileProbePath ), value );
    args.replayRestoreFailureFileProbe = true;
    args.fixedStep = true;
    args.suppressExitDialog = true;
    fprintf( stdout, "[replay] Restore failure probe input: %s\n", args.replayRestoreFailureFileProbePath );
    return true;
}


bool ApplyRunCliValueDirectives( const CommandLineView& commandLine, ParsedArgs& out )
{
    static const CliValueDirective kValues[] = {
        { "--seed",
          nullptr,
          []( const char* value, ParsedArgs& args ) -> bool
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
        { "--frames",
          nullptr,
          []( const char* value, ParsedArgs& args ) -> bool
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
        { "--allocation-guard",
          "--allocation_guard",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              RuntimeAllocation::RuntimeAllocationGuardMode mode = RuntimeAllocation::RuntimeAllocationGuardMode::Off;
              if ( !ParseAllocationGuardModeValue( value, mode ) )
              {
                  return FailCommandLineParse( "--allocation-guard expects off|measure|gameplay." );
              }
              args.allocationGuardMode = mode;
              fprintf( stdout,
                       "[allocation-guard] Requested mode: %s\n",
                       RuntimeAllocation::RuntimeAllocationGuardModeName( mode ) );
              return true;
          } },
        { "--live-style-control", "--style-harness", ApplyLiveStyleControlDir },
        { "--live_style_control", "--style_harness", ApplyLiveStyleControlDir },
        { "--scene-snapshot-out", "--scene_snapshot_out", ApplySceneSnapshotOutPath },
        { "--memory-dump", "--memory_dump", ApplyMemoryDumpPath },
        { "--interaction-script", "--interaction_script", ApplyInteractionScriptPath },
        { "--interaction-report", "--interaction_report", ApplyInteractionReportPath },
        { "--replay",
          nullptr,
          []( const char* value, ParsedArgs& args ) -> bool
          {
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--replay expects optional on|off." );
              }
              args.replayRecording = enabled;
              args.replayExplicit = true;
              fprintf( stdout, "[replay] Capture %s via command line.\n", enabled ? "enabled" : "disabled" );
              return true;
          } },
        { "--replay-seconds",
          "--replay_seconds",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              int seconds = 0;
              if ( !ParseIntToken( value, seconds ) || seconds < 1 || seconds > 600 )
              {
                  return FailCommandLineParse( "--replay-seconds expects 1..600." );
              }
              args.replaySeconds = seconds;
              args.replayExplicit = true;
              fprintf( stdout, "[replay] Retention window: %d seconds.\n", args.replaySeconds );
              return true;
          } },
        { "--replay-scrub-probe",
          "--replay_scrub_probe",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              float normalized = 0.0f;
              if ( !ParseFloatToken( value, normalized ) || normalized < 0.0f || normalized >= 0.995f )
              {
                  return FailCommandLineParse(
                      "--replay-scrub-probe expects a normalized position in the range 0..0.995." );
              }
              args.replayScrubProbe = true;
              args.replayScrubProbeNormalized = normalized;
              args.replayRecording = true;
              args.replayExplicit = true;
              args.replaySeconds = (std::max)( 1, args.replaySeconds );
              args.fixedStep = true;
              args.suppressExitDialog = true;
              fprintf( stdout, "[replay] Scrub probe normalized position: %.3f\n", args.replayScrubProbeNormalized );
              return true;
          } },
        { "--replay-restore-probe",
          "--replay_restore_probe",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              float normalized = 0.0f;
              if ( !ParseFloatToken( value, normalized ) || normalized < 0.0f || normalized >= 0.995f )
              {
                  return FailCommandLineParse(
                      "--replay-restore-probe expects a normalized position in the range 0..0.995." );
              }
              args.replayRestoreProbe = true;
              args.replayRestoreProbeNormalized = normalized;
              args.replayRecording = true;
              args.replayExplicit = true;
              args.replaySeconds = (std::max)( 1, args.replaySeconds );
              args.fixedStep = true;
              args.suppressExitDialog = true;
              fprintf( stdout,
                       "[replay] Restore probe normalized position: %.3f\n",
                       args.replayRestoreProbeNormalized );
              return true;
          } },
        // Invariant: each replay workflow has one semantic flag. The underscore
        // spelling remains the command-line parser's universal syntax alias;
        // deleted save-test/play synonyms carried no distinct behavior.
        { "--replay-save-probe", "--replay_save_probe", ApplyReplaySaveProbePath },
        { "--replay-load", "--replay_load", ApplyReplayLoadPath },
        { "--replay-load-probe", "--replay_load_probe", ApplyReplayLoadProbePath },
        { "--replay-restore-file-probe", "--replay_restore_file_probe", ApplyReplayRestoreFileProbePath },
        { "--replay-restore-target-file-probe",
          "--replay_restore_target_file_probe",
          ApplyReplayRestoreTargetFileProbePath },
        { "--replay-restore-branch-file-probe",
          "--replay_restore_branch_file_probe",
          ApplyReplayRestoreBranchFileProbePath },
        { "--replay-restore-failure-file-probe",
          "--replay_restore_failure_file_probe",
          ApplyReplayRestoreFailureFileProbePath },
        { "--replay-hashes", "--replay_hashes", ApplyReplayHashLogPath },
        { "--ui-stress",
          "--ui_stress",
          []( const char* value, ParsedArgs& args ) -> bool
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
        { "--ui-stress-seed",
          "--ui_stress_seed",
          []( const char* value, ParsedArgs& args ) -> bool
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
        { "--ui-stress-actions",
          "--ui_stress_actions",
          []( const char* value, ParsedArgs& args ) -> bool
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
        { "--graphics-stress",
          "--graphics_stress",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              bool enabled = false;
              if ( !ParseOptionalOnOffValue( value, enabled ) )
              {
                  return FailCommandLineParse( "--graphics-stress expects optional on|off." );
              }
              args.graphicsStress = enabled;
              args.interactiveRun = args.interactiveRun || enabled;
              args.suppressExitDialog = args.suppressExitDialog || enabled;
              return true;
          } },
        { "--graphics-stress-seed",
          "--graphics_stress_seed",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              unsigned int seed = 0;
              if ( !ParseUnsignedIntToken( value, seed ) || seed == 0 )
              {
                  return FailCommandLineParse( "--graphics-stress-seed expects a positive 32-bit integer." );
              }
              args.graphicsStress = true;
              args.graphicsStressSeed = seed;
              args.interactiveRun = true;
              args.suppressExitDialog = true;
              return true;
          } },
        { "--graphics-stress-actions",
          "--graphics_stress_actions",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              int actions = 0;
              if ( !ParseIntToken( value, actions ) || actions <= 0 || actions > 64 )
              {
                  return FailCommandLineParse( "--graphics-stress-actions expects 1..64." );
              }
              args.graphicsStress = true;
              args.graphicsStressActions = actions;
              args.interactiveRun = true;
              args.suppressExitDialog = true;
              return true;
          } },
        { "--graphics-stress-scene-interval",
          "--graphics_stress_scene_interval",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              int frames = 0;
              if ( !ParseIntToken( value, frames ) || frames <= 0 || frames > 600 )
              {
                  return FailCommandLineParse( "--graphics-stress-scene-interval expects 1..600 frames." );
              }
              args.graphicsStress = true;
              args.graphicsStressSceneIntervalFrames = frames;
              args.interactiveRun = true;
              args.suppressExitDialog = true;
              return true;
          } },
        { "--graphics-stress-memory-interval",
          "--graphics_stress_memory_interval",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              int frames = 0;
              if ( !ParseIntToken( value, frames ) || frames < 0 || frames > 36000 )
              {
                  return FailCommandLineParse( "--graphics-stress-memory-interval expects 0..36000 frames." );
              }
              args.graphicsStress = true;
              args.graphicsStressMemoryIntervalFrames = frames;
              args.interactiveRun = true;
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


bool ValidatePhysicsRegressionLog( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--physics-regression-log" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--physics-regression-log is only supported in Debug builds. Recompile with the Debug "
                                 "configuration to use physics regression logging." );
#else
    return true;
#endif
}


bool ValidatePhysicsCollisionTimeLog( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--physics-collision-time-log" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--physics-collision-time-log is only supported in Debug builds. Recompile with the "
                                 "Debug configuration to use collision-time logging." );
#else
    return true;
#endif
}


bool ValidatePhysicsDiagnostics( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--physics-diag" ) && !HasOption( commandLine, "--physics-diagnostics" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--physics-diag is only supported in Debug builds. Recompile with the Debug "
                                 "configuration to use queryable physics diagnostics." );
#else
    return true;
#endif
}


bool ValidateReplayScrubProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-scrub-test" ) && !HasOption( commandLine, "--replay_scrub_test" ) &&
         !HasOption( commandLine, "--replay-scrub-probe" ) && !HasOption( commandLine, "--replay_scrub_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse(
        "--replay-scrub-probe is only supported in Debug builds with SkullScope diagnostics." );
#else
    return true;
#endif
}


bool ValidateReplayRestoreProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-restore-test" ) && !HasOption( commandLine, "--replay_restore_test" ) &&
         !HasOption( commandLine, "--replay-restore-probe" ) && !HasOption( commandLine, "--replay_restore_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse(
        "--replay-restore-probe is only supported in Debug builds with SkullScope diagnostics." );
#else
    return true;
#endif
}


bool ValidateReplaySaveProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-save-probe" ) && !HasOption( commandLine, "--replay_save_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-save-probe is only supported in Debug builds." );
#else
    return true;
#endif
}


bool ValidateReplayLoadProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-load-probe" ) && !HasOption( commandLine, "--replay_load_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-load-probe is only supported in Debug builds." );
#else
    return true;
#endif
}


bool ValidateReplayRestoreFileProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-restore-file-probe" ) &&
         !HasOption( commandLine, "--replay_restore_file_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-restore-file-probe is only supported in Debug builds." );
#else
    return true;
#endif
}


bool ValidateReplayRestoreTargetFileProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-restore-target-file-probe" ) &&
         !HasOption( commandLine, "--replay_restore_target_file_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-restore-target-file-probe is only supported in Debug builds." );
#else
    return true;
#endif
}


bool ValidateReplayRestoreBranchFileProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-restore-branch-file-probe" ) &&
         !HasOption( commandLine, "--replay_restore_branch_file_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-restore-branch-file-probe is only supported in Debug builds." );
#else
    return true;
#endif
}


bool ValidateReplayRestoreFailureFileProbe( const CommandLineView& commandLine )
{
    if ( !HasOption( commandLine, "--replay-restore-failure-file-probe" ) &&
         !HasOption( commandLine, "--replay_restore_failure_file_probe" ) )
    {
        return true;
    }

#ifndef _DEBUG
    return FailCommandLineParse( "--replay-restore-failure-file-probe is only supported in Debug builds." );
#else
    if ( !HasOption( commandLine, "--physics-diag" ) && !HasOption( commandLine, "--physics-diagnostics" ) )
    {
        return FailCommandLineParse(
            "--replay-restore-failure-file-probe requires --physics-diag so SkullScope can query the failure row." );
    }
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
#endif


#ifdef _DEBUG
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
#endif


#ifdef _DEBUG
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


} // anonymous namespace

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
    if ( !ValidateReplayScrubProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayRestoreProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplaySaveProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayLoadProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayRestoreFileProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayRestoreTargetFileProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayRestoreBranchFileProbe( commandLine ) )
    {
        return false;
    }
    if ( !ValidateReplayRestoreFailureFileProbe( commandLine ) )
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

    if ( !ApplyStartupCliValueDirectives( commandLine, out, config ) )
    {
        return false;
    }

    ApplyCliFlagDirectives( commandLine, out );

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
