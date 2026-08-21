/*
File: StartupLaunchResolution.cpp
Purpose:
  Owns scene/suite path resolution and the launch-policy packet passed to Run.

Summary:
  This unit resolves authored launch tokens, visualization-only physics-debug
  overrides, suite JSON, run/replay/stress value directives, and the final
  RunStartupOverrides value without retaining command-line storage.

Glossary:
  Launch token: CLI value naming a scene, suite, built-in hero, or generated
    demo mode.
  Launch policy: Caller-owned values that configure Run before its first frame.

Invariants:
  - Scene paths, suite order, diagnostics, defaults, and failure strings remain
    byte-identical to the pre-split Init.cpp.
  - ParsedArgs is borrowed synchronously; returned Run values own or borrow only
    the same storage lifetime as before extraction.
  - Resolution performs no window, renderer, worker, or Run construction.

Related:
  - StartupLaunchResolution.h
  - StartupCommandLine.h
  - Agentic/Reference/engine-glossary.md
*/
#include "StartupLaunchResolution.h"
#include "StartupCommandLine.h"
#include "../../Core/Common.h"
#include "../App/RunLaunchOptions.h"
#include "../Replay/ReplayOverlaySurface.h"
#include "../../Core/WindowConstants.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <io.h>
#include <string>
#include <vector>
using namespace SkullbonezCore::Physics;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;
#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )
namespace SkullbonezCore
{
namespace Runtime
{
namespace Startup
{
namespace
{
using Json = nlohmann::ordered_json;
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
bool SceneArgHasPathSyntax( const std::string& sceneArg )
{
    return sceneArg.find( '/' ) != std::string::npos || sceneArg.find( '\\' ) != std::string::npos ||
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
    return std::string( DATA_ROOT ) + "scenes/concept_12_low_poly_art_style.scene.json";
}
std::string ResolveSceneLaunchPath( const char* rawSceneArg )
{
    std::string sceneArg( rawSceneArg );

    if ( sceneArg.empty() || SceneArgHasPathSyntax( sceneArg ) )
    {
        return sceneArg;
    }

    if ( _stricmp( sceneArg.c_str(), "hero" ) == 0 || _stricmp( sceneArg.c_str(), "low_poly_hero" ) == 0 ||
         _stricmp( sceneArg.c_str(), "low-poly-hero" ) == 0 )
    {
        return HeroSceneLaunchPath();
    }

    const std::string sceneDir = std::string( DATA_ROOT ) + "scenes/";

    if ( !SceneArgHasExtension( sceneArg ) )
    {
        const std::string sceneCandidate = sceneDir + sceneArg + ".scene.json";

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
std::string ResolveSuiteLaunchPath( const char* rawSuiteArg )
{
    std::string suiteArg( rawSuiteArg );

    if ( suiteArg.empty() || SceneArgHasPathSyntax( suiteArg ) )
    {
        return suiteArg;
    }

    const std::string sceneDir = std::string( DATA_ROOT ) + "scenes/";

    if ( !SceneArgHasExtension( suiteArg ) )
    {
        const std::string suiteCandidate = sceneDir + suiteArg + ".suite.json";

        if ( FileExistsForLaunch( suiteCandidate ) )
        {
            return suiteCandidate;
        }
    }

    const std::string directCandidate = sceneDir + suiteArg;

    if ( FileExistsForLaunch( directCandidate ) )
    {
        return directCandidate;
    }

    return suiteArg;
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

    if ( _stricmp( value, "terrain" ) == 0 || _stricmp( value, "terrain_contact" ) == 0 ||
         _stricmp( value, "terrain-contact" ) == 0 || _stricmp( value, "terrain_probe" ) == 0 ||
         _stricmp( value, "terrain-probe" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_TERRAIN_CONTACT;
        return true;
    }

    if ( _stricmp( value, "all" ) == 0 || _stricmp( value, "on" ) == 0 )
    {
        outFlags = PHYSICS_DEBUG_ALL;
        return true;
    }

    return false;
}
bool ApplyPhysicsDebugComponentOverride( const CommandLineView& commandLine, const char* dashedName,
                                         const char* underscoredName, uint32_t flag, ParsedArgs& out )
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
bool ApplyPhysicsDebugFloatOverride( const CommandLineView& commandLine, const PhysicsDebugFloatDirective& directive,
                                     ParsedArgs& out )
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
struct RunCliValueDirective
{
    const char* name;
    const char* alias;
    bool ( *apply )( const char* value, ParsedArgs& args );
};

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
bool ApplyPredictTargetName( const char* value, ParsedArgs& args )
{
    if ( IsOptionValueMissing( value ) )
    {
        return FailCommandLineParse( "--predict expects a scene object display name." );
    }

    if ( strlen( value ) >= sizeof( args.predictTargetName ) )
    {
        return FailCommandLineParse( "--predict body name is too long." );
    }

    strcpy_s( args.predictTargetName, sizeof( args.predictTargetName ), value );

    // Why: scene mode leaves replay capture off by default. Prediction seeds
    // from live physics either way, but with no solver track the source
    // frame/hash never changes, so a running scene would build one horizon and
    // then hold it. Capture is what makes the armed target keep re-predicting.
    args.replayRecording = true;
    args.replayExplicit = true;
    fprintf( stdout, "[predict] Startup prediction target: %s\n", args.predictTargetName );
    return true;
}
bool ApplyPredictHorizonSeconds( const char* value, ParsedArgs& args )
{
    float seconds = 0.0f;

    if ( IsOptionValueMissing( value ) || !ParseFloatToken( value, seconds ) ||
         seconds < ReplayOverlay::REPLAY_PREDICTION_MIN_SECONDS || seconds > ReplayOverlay::REPLAY_PREDICTION_MAX_SECONDS )
    {
        return FailCommandLineParse( "--predict-seconds expects 1.0..120.0 seconds." );
    }

    args.predictHorizonSeconds = seconds;
    fprintf( stdout, "[predict] Startup prediction horizon: %.2f s\n", static_cast<double>( seconds ) );
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
    if ( !CopyCommandLinePath( value, "--memory-dump", args.memoryDumpPath, sizeof( args.memoryDumpPath ) ) )
    {
        return false;
    }

    args.suppressExitDialog = true;
    fprintf( stdout, "[memory] Dump output: %s\n", args.memoryDumpPath );
    return true;
}
bool ApplyInteractionScriptPath( const char* value, ParsedArgs& args )
{
    if ( !CopyCommandLinePath( value, "--interaction-script", args.interactionScriptPath,
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
    if ( !CopyCommandLinePath( value, "--interaction-report", args.interactionReportPath,
                               sizeof( args.interactionReportPath ) ) )
    {
        return false;
    }

    args.suppressExitDialog = true;
    fprintf( stdout, "[interaction] Report output: %s\n", args.interactionReportPath );
    return true;
}
bool ApplyInteractionRecordPath( const char* value, ParsedArgs& args )
{
    if ( !CopyCommandLinePath( value, "--record-automation", args.interactionRecordPath,
                               sizeof( args.interactionRecordPath ) ) )
    {
        return false;
    }

    fprintf( stdout, "[recorder] Automation recording target: %s\n", args.interactionRecordPath );
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
} // anonymous namespace
bool ParsePhysicsDebugOverrides( const CommandLineView& commandLine, ParsedArgs& out )
{
    const char* modeValue = FindOptionValue( commandLine, "--physics-debug", "--physics_debug" );

    if ( modeValue )
    {
        if ( !ParsePhysicsDebugMode( modeValue, out.physicsDebugFlagsOverride ) )
        {
            return FailCommandLineParse( "--physics-debug expects none|axes|contacts|sleep|pipeline|terrain|all|on|off." );
        }

        out.hasPhysicsDebugFlagsOverride = true;
    }

    static const PhysicsDebugComponentDirective kComponentOverrides[] = {
        { "--physics-debug-axes", "--physics_debug_axes", PHYSICS_DEBUG_AXES },
        { "--physics-debug-contacts", "--physics_debug_contacts", PHYSICS_DEBUG_CONTACTS },
        { "--physics-debug-sleep", "--physics_debug_sleep", PHYSICS_DEBUG_SLEEP },
        { "--physics-debug-pipeline", "--physics_debug_pipeline", PHYSICS_DEBUG_PIPELINE },
        { "--physics-debug-terrain-contact", "--physics_debug_terrain_contact", PHYSICS_DEBUG_TERRAIN_CONTACT },
    };

    for ( const PhysicsDebugComponentDirective& component : kComponentOverrides )
    {
        if ( !ApplyPhysicsDebugComponentOverride( commandLine, component.dashedName, component.underscoredName,
                                                  component.flag, out ) )
        {
            return false;
        }
    }

    const char* transparentValue = FindOptionValue( commandLine, "--physics-debug-transparent",
                                                    "--physics_debug_transparent" );

    if ( transparentValue )
    {
        if ( !ParseOptionalOnOffValue( transparentValue, out.physicsDebugTransparentOverride ) )
        {
            return FailCommandLineParse( "--physics-debug-transparent expects optional on|off." );
        }

        out.hasPhysicsDebugTransparentOverride = true;
    }

    static const PhysicsDebugFloatDirective kFloatOverrides[] = {
        { "--physics-debug-alpha", "--physics_debug_alpha", &ParsedArgs::hasPhysicsDebugAlphaOverride,
          &ParsedArgs::physicsDebugAlphaOverride, 0.05f, 1.0f, "--physics-debug-alpha expects 0.05..1.0.", true },
        { "--physics-debug-contact-linger", "--physics_debug_contact_linger",
          &ParsedArgs::hasPhysicsDebugContactLingerOverride, &ParsedArgs::physicsDebugContactLingerOverride, 0.0f, 5.0f,
          "--physics-debug-contact-linger expects 0.0..5.0 seconds.", false },
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
bool ParseSceneArgs( const CommandLineView& commandLine, std::vector<std::string>& sceneList, bool& isSuiteOrSceneMode )
{
    const char* suiteArg = FindOptionValue( commandLine, "--suite" );
    const char* sceneArg = FindOptionValue( commandLine, "--scene" );
    const bool heroArg = HasOption( commandLine, "--hero" );
    const bool demoHeroArg = HasOption( commandLine, "--demohero" ) || HasOption( commandLine, "--demo-hero" );

    if ( ( suiteArg && sceneArg ) || ( heroArg && ( suiteArg || sceneArg ) ) ||
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

        // Resolve a suite JSON path from either a file token or a repository-relative path.
        const std::string suitePath = ResolveSuiteLaunchPath( suiteArg );
        std::ifstream suiteFile( suitePath );

        if ( !suiteFile )
        {
            return FailCommandLineParse( "--suite could not open '%s'.", suitePath.c_str() );
        }

        Json suite = Json::parse( suiteFile, nullptr, false );

        if ( suite.is_discarded() )
        {
            return FailCommandLineParse( "--suite invalid JSON in '%s'.", suitePath.c_str() );
        }

        if ( !suite.is_object() )
        {
            return FailCommandLineParse( "--suite '%s' root must be an object.", suitePath.c_str() );
        }

        const auto formatIt = suite.find( "format" );

        if ( formatIt == suite.end() || !formatIt->is_string() || formatIt->get<std::string>() != "skullbonez.suite.json" )
        {
            return FailCommandLineParse( "--suite '%s' must declare format skullbonez.suite.json.", suitePath.c_str() );
        }

        const auto scenesIt = suite.find( "scenes" );

        if ( scenesIt == suite.end() || !scenesIt->is_array() )
        {
            return FailCommandLineParse( "--suite '%s' must contain a scenes array.", suitePath.c_str() );
        }

        for ( const Json& scene : *scenesIt )
        {
            if ( !scene.is_string() )
            {
                return FailCommandLineParse( "--suite '%s' scenes entries must be strings.", suitePath.c_str() );
            }

            sceneList.push_back( ResolveSceneLaunchPath( scene.get<std::string>().c_str() ) );
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
RunStartupOverrides BuildRunStartupOverrides( const ParsedArgs& args )
{
    RunStartupOverrides overrides;
    RunLaunchOptions& launch = overrides.launch;
    launch.timeScaleOverride = args.timeScaleOverride;
    launch.fixedStep = args.fixedStep;
    launch.seedOverride = args.seedOverride;
    launch.noWater = args.noWater;
    launch.noSleep = args.noSleep;
    launch.hasTornadoOverride = args.hasTornadoOverride;
    launch.tornadoEnabled = args.tornadoEnabled;
    launch.tornadoVectors = args.tornadoVectors;
    launch.hasCinematicRenderingOverride = args.hasCinematicRenderingOverride;
    launch.cinematicRendering = args.cinematicRendering;
    launch.hasCinematicShadowsOverride = args.hasCinematicShadowsOverride;
    launch.cinematicShadows = args.cinematicShadows;
    launch.demoHeroStyle = args.demoHeroStyle;
    launch.dumpTextureAssets = args.dumpAssets;
    launch.interactiveSceneRun = args.interactiveRun;
    launch.frameCountOverride = args.frameCountOverride;
    launch.uiStress = args.uiStress;
    launch.uiStressSeed = args.uiStressSeed;
    launch.uiStressActions = args.uiStressActions;
    launch.graphicsStress = args.graphicsStress;
    launch.graphicsStressSeed = args.graphicsStressSeed;
    launch.graphicsStressActions = args.graphicsStressActions;
    launch.graphicsStressSceneIntervalFrames = args.graphicsStressSceneIntervalFrames;
    launch.graphicsStressMemoryIntervalFrames = args.graphicsStressMemoryIntervalFrames;
    launch.replayGuideArcsAtStartup = args.replayGuideArcsAtStartup;
    launch.allocationGuardMode = args.allocationGuardMode;
    launch.generatedObjectTypeOverride = args.objectTypeOverride;
    launch.hasPhysicsDebugFlagsOverride = args.hasPhysicsDebugFlagsOverride;
    launch.physicsDebugFlagsOverride = args.physicsDebugFlagsOverride;
    launch.hasPhysicsDebugTransparentOverride = args.hasPhysicsDebugTransparentOverride;
    launch.physicsDebugTransparentOverride = args.physicsDebugTransparentOverride;
    launch.hasPhysicsDebugAlphaOverride = args.hasPhysicsDebugAlphaOverride;
    launch.physicsDebugAlphaOverride = args.physicsDebugAlphaOverride;
    launch.hasPhysicsDebugContactLingerOverride = args.hasPhysicsDebugContactLingerOverride;
    launch.physicsDebugContactLingerOverride = args.physicsDebugContactLingerOverride;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    launch.developmentUiMode = args.developmentUiMode;
    launch.developmentUiModeExplicit = args.developmentUiModeExplicit;
#endif
    strcpy_s( launch.predictTargetName, sizeof( launch.predictTargetName ), args.predictTargetName );
    launch.predictHorizonSeconds = args.predictHorizonSeconds;
    launch.predictPauseOnStart = args.predictPauseOnStart;
    overrides.liveStyleControlDirectory = args.liveStyleControlDir[0] != '\0' ? args.liveStyleControlDir : nullptr;
    overrides.mainMemoryDumpPath = args.memoryDumpPath[0] != '\0' ? args.memoryDumpPath : nullptr;
    overrides.interactionScriptPath = args.interactionScriptPath[0] != '\0' ? args.interactionScriptPath : nullptr;
    overrides.interactionReportPath = args.interactionReportPath[0] != '\0' ? args.interactionReportPath : nullptr;
    overrides.interactionRecordPath = args.interactionRecordPath[0] != '\0' ? args.interactionRecordPath : nullptr;
    const bool replayDefaultAllowed = !args.isSuiteOrSceneMode || args.interactiveRun || args.liveStyleControlDir[0] != '\0';

    const bool replayEnabled = args.replayExplicit ? args.replayRecording : ( args.replayRecording && replayDefaultAllowed );

    overrides.configureReplayRecording = replayEnabled || args.replayHashLogPath[0] != '\0';
    overrides.replayRecordingEnabled = true;
    overrides.replayRetentionSeconds = args.replaySeconds;
    overrides.replayHashLogPath = args.replayHashLogPath[0] != '\0' ? args.replayHashLogPath : nullptr;
    overrides.replayLoadPath = args.replayLoad ? args.replayLoadPath : nullptr;
    overrides.replayLoadProbe = args.replayLoadProbe;
    overrides.hasInitialOverlayMode = args.showProfiler;
    overrides.initialOverlayMode = args.showProfiler ? OverlayMode::Timers : OverlayMode::None;
    overrides.hideTopText = args.hideTopText;
    overrides.showBroadphaseVisualizer = args.showBroadphaseVisualizer;
#ifdef _DEBUG
    overrides.replayScrubProbe = args.replayScrubProbe;
    overrides.replayScrubProbeNormalized = args.replayScrubProbeNormalized;
    overrides.replayRestoreProbe = args.replayRestoreProbe;
    overrides.replayRestoreProbeNormalized = args.replayRestoreProbeNormalized;
    overrides.replaySaveProbe = args.replaySaveProbe;
    overrides.replaySaveProbePath = args.replaySaveProbe ? args.replaySaveProbePath : nullptr;
    overrides.replayRestoreFileProbePath = args.replayRestoreFileProbe ? args.replayRestoreFileProbePath : nullptr;
    overrides.replayRestoreTargetFileProbePath = args.replayRestoreTargetFileProbe ? args.replayRestoreTargetFileProbePath
                                                                                   : nullptr;

    overrides.replayRestoreBranchFileProbePath = args.replayRestoreBranchFileProbe ? args.replayRestoreBranchFileProbePath
                                                                                   : nullptr;

    overrides.replayRestoreFailureFileProbePath = args.replayRestoreFailureFileProbe ? args.replayRestoreFailureFileProbePath
                                                                                     : nullptr;

    overrides.physicsRegressionLogPath = args.physicsRegressionLogOverride[0] != '\0' ? args.physicsRegressionLogOverride
                                                                                      : nullptr;

    overrides.physicsCollisionTimeLogPath = args.physicsCollisionTimeLogOverride[0] != '\0'
                                                ? args.physicsCollisionTimeLogOverride
                                                : nullptr;

    overrides.physicsDiagnosticsPath = args.physicsDiagnosticsPath[0] != '\0' ? args.physicsDiagnosticsPath : nullptr;
    overrides.physicsDiagnosticsFixedStepForced = args.fixedStepForcedByPhysicsDiagnostics;
#endif
    return overrides;
}
bool ApplyRunCliValueDirectives( const CommandLineView& commandLine, ParsedArgs& out )
{
    // clang-format off
    // Keep the compatibility inventory compact: row order is behavior-bearing,
    // and each option remains visually adjacent to its exact callback.
    static const RunCliValueDirective kValues[] = {
        { "--seed",
          nullptr,
          []( const char* value, ParsedArgs& args ) -> bool
          {
              unsigned int seed = 0;

              if ( !ParseUnsignedCommandLineToken( value, seed ) || seed == 0 )
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

              if ( !ParseIntCommandLineToken( value, frames ) || frames <= 0 )
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
              CoreAllocation::RuntimeAllocationGuardMode mode = CoreAllocation::RuntimeAllocationGuardMode::Off;

              if ( !ParseAllocationGuardCommandLineToken( value, mode ) )
              {
                  return FailCommandLineParse( "--allocation-guard expects off|measure|gameplay." );
              }

              args.allocationGuardMode = mode;
              fprintf( stdout, "[allocation-guard] Requested mode: %s\n", CoreAllocation::RuntimeAllocationGuardModeName( mode ) );
              return true;
          } },
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
        { "--dev-ui",
          "--dev_ui",
          []( const char* value, ParsedArgs& args ) -> bool
          {
              if ( !value || IsOptionValueMissing( value ) )
              {
                  return FailCommandLineParse( "--dev-ui expects legacy|imgui; the two surfaces are mutually exclusive." );
              }

              if ( strcmp( value, "legacy" ) == 0 )
              {
                  args.developmentUiMode = DevelopmentUiMode::Legacy;
              }
              else if ( strcmp( value, "imgui" ) == 0 )
              {
                  args.developmentUiMode = DevelopmentUiMode::ImGui;
              }
              else
              {
                  return FailCommandLineParse( "--dev-ui expects legacy|imgui; the two surfaces are mutually exclusive." );
              }

              args.developmentUiModeExplicit = true;
              fprintf( stdout, "[dev-ui] Mode: %s\n", value );
              return true;
          } },
#endif
        { "--live-style-control", "--style-harness", ApplyLiveStyleControlDir },
        { "--live_style_control", "--style_harness", ApplyLiveStyleControlDir },
        { "--scene-snapshot-out", "--scene_snapshot_out", ApplySceneSnapshotOutPath },
        { "--memory-dump", "--memory_dump", ApplyMemoryDumpPath },
        { "--interaction-script", "--interaction_script", ApplyInteractionScriptPath },
        { "--interaction-report", "--interaction_report", ApplyInteractionReportPath },
        { "--record-automation", "--record_automation", ApplyInteractionRecordPath },
        { "--record-interaction", "--record_interaction", ApplyInteractionRecordPath },
        { "--predict", nullptr, ApplyPredictTargetName },
        { "--predict-seconds", "--predict_seconds", ApplyPredictHorizonSeconds },
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

              if ( !ParseIntCommandLineToken( value, seconds ) || seconds < 1 || seconds > 600 )
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
                  return FailCommandLineParse( "--replay-scrub-probe expects a normalized position in the range 0..0.995." );
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
                  return FailCommandLineParse( "--replay-restore-probe expects a normalized position in the range 0..0.995." );
              }

              args.replayRestoreProbe = true;
              args.replayRestoreProbeNormalized = normalized;
              args.replayRecording = true;
              args.replayExplicit = true;
              args.replaySeconds = (std::max)( 1, args.replaySeconds );
              args.fixedStep = true;
              args.suppressExitDialog = true;
              fprintf( stdout, "[replay] Restore probe normalized position: %.3f\n", args.replayRestoreProbeNormalized );
              return true;
          } },

        // Invariant: each replay workflow has one semantic flag. The underscore
        // spelling remains the command-line parser's universal syntax alias;
        // deleted save-test/play synonyms carried no distinct behavior.
        { "--replay-save-probe", "--replay_save_probe", ApplyReplaySaveProbePath },
        { "--replay-load", "--replay_load", ApplyReplayLoadPath },
        { "--replay-load-probe", "--replay_load_probe", ApplyReplayLoadProbePath },
        { "--replay-restore-file-probe", "--replay_restore_file_probe", ApplyReplayRestoreFileProbePath },
        { "--replay-restore-target-file-probe", "--replay_restore_target_file_probe", ApplyReplayRestoreTargetFileProbePath },
        { "--replay-restore-branch-file-probe", "--replay_restore_branch_file_probe", ApplyReplayRestoreBranchFileProbePath },
        { "--replay-restore-failure-file-probe", "--replay_restore_failure_file_probe", ApplyReplayRestoreFailureFileProbePath },
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

              if ( !ParseUnsignedCommandLineToken( value, seed ) || seed == 0 )
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

              if ( !ParseIntCommandLineToken( value, actions ) || actions <= 0 || actions > 32 )
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

              if ( !ParseUnsignedCommandLineToken( value, seed ) || seed == 0 )
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

              if ( !ParseIntCommandLineToken( value, actions ) || actions <= 0 || actions > 64 )
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

              if ( !ParseIntCommandLineToken( value, frames ) || frames <= 0 || frames > 600 )
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

              if ( !ParseIntCommandLineToken( value, frames ) || frames < 0 || frames > 36000 )
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

    // clang-format on
    for ( const RunCliValueDirective& directive : kValues )
    {
        const char* value = FindOptionValue( commandLine, directive.name );

        if ( !value && directive.alias )
        {
            value = FindOptionValue( commandLine, directive.alias );
        }

        if ( value && !directive.apply( value, out ) )
        {
            return false;
        }
    }

    return true;
}
} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore
