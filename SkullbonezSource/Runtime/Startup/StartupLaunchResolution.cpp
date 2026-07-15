/*
File: StartupLaunchResolution.cpp
Purpose:
  Owns scene/suite path resolution and the launch-policy packet passed to Run.

Summary:
  This unit resolves authored launch tokens, visualization-only physics-debug
  overrides, suite JSON, and the final RunStartupOverrides value without
  retaining command-line storage.

Glossary:
  Launch token: CLI value naming a scene, suite, built-in hero, or generated
    demo mode.
  Launch policy: Caller-owned values that configure Run before its first frame.
  Physics-debug override: Visualization-only startup request that must not alter
    solver state.

Invariants:
  - Scene paths, suite order, diagnostics, defaults, and failure strings remain
    byte-identical to the pre-split Init.cpp.
  - ParsedArgs is borrowed synchronously; returned Run values own or borrow only
    the same storage lifetime as before extraction.
  - Resolution performs no window, renderer, worker, or Run construction.

Related:
  - StartupLaunchResolution.h
  - StartupCommandLine.h
  - Agentic/Reports/2026-07-15/init-startup-decomposition-map.md
*/
#include "StartupLaunchResolution.h"

#include "StartupCommandLine.h"

#include "../../Core/Common.h"
#include "../RunLaunchOptions.h"
#include "../WindowConstants.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <io.h>
#include <string>
#include <vector>

using namespace SkullbonezCore::Physics;

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

bool ApplyPhysicsDebugComponentOverride( const CommandLineView& commandLine,
                                         const char* dashedName,
                                         const char* underscoredName,
                                         uint32_t flag,
                                         ParsedArgs& out )
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

bool ApplyPhysicsDebugFloatOverride( const CommandLineView& commandLine,
                                     const PhysicsDebugFloatDirective& directive,
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

} // anonymous namespace

bool ParsePhysicsDebugOverrides( const CommandLineView& commandLine, ParsedArgs& out )
{
    const char* modeValue = FindOptionValue( commandLine, "--physics-debug", "--physics_debug" );
    if ( modeValue )
    {
        if ( !ParsePhysicsDebugMode( modeValue, out.physicsDebugFlagsOverride ) )
        {
            return FailCommandLineParse(
                "--physics-debug expects none|axes|contacts|sleep|pipeline|terrain|all|on|off." );
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
        if ( !ApplyPhysicsDebugComponentOverride( commandLine,
                                                  component.dashedName,
                                                  component.underscoredName,
                                                  component.flag,
                                                  out ) )
        {
            return false;
        }
    }

    const char* transparentValue =
        FindOptionValue( commandLine, "--physics-debug-transparent", "--physics_debug_transparent" );
    if ( transparentValue )
    {
        if ( !ParseOptionalOnOffValue( transparentValue, out.physicsDebugTransparentOverride ) )
        {
            return FailCommandLineParse( "--physics-debug-transparent expects optional on|off." );
        }
        out.hasPhysicsDebugTransparentOverride = true;
    }

    static const PhysicsDebugFloatDirective kFloatOverrides[] = {
        { "--physics-debug-alpha",
          "--physics_debug_alpha",
          &ParsedArgs::hasPhysicsDebugAlphaOverride,
          &ParsedArgs::physicsDebugAlphaOverride,
          0.05f,
          1.0f,
          "--physics-debug-alpha expects 0.05..1.0.",
          true },
        { "--physics-debug-contact-linger",
          "--physics_debug_contact_linger",
          &ParsedArgs::hasPhysicsDebugContactLingerOverride,
          &ParsedArgs::physicsDebugContactLingerOverride,
          0.0f,
          5.0f,
          "--physics-debug-contact-linger expects 0.0..5.0 seconds.",
          false },
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
        fprintf( stdout,
                 "[physics-debug] Transparent bodies: %s\n",
                 out.physicsDebugTransparentOverride ? "on" : "off" );
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
        if ( formatIt == suite.end() || !formatIt->is_string() ||
             formatIt->get<std::string>() != "skullbonez.suite.json" )
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
    launch.noContactAudio = args.noContactAudio;
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

    overrides.liveStyleControlDirectory = args.liveStyleControlDir[0] != '\0' ? args.liveStyleControlDir : nullptr;
    overrides.mainMemoryDumpPath = args.memoryDumpPath[0] != '\0' ? args.memoryDumpPath : nullptr;
    overrides.interactionScriptPath = args.interactionScriptPath[0] != '\0' ? args.interactionScriptPath : nullptr;
    overrides.interactionReportPath = args.interactionReportPath[0] != '\0' ? args.interactionReportPath : nullptr;

    const bool replayDefaultAllowed =
        !args.isSuiteOrSceneMode || args.interactiveRun || args.liveStyleControlDir[0] != '\0';
    const bool replayEnabled =
        args.replayExplicit ? args.replayRecording : ( args.replayRecording && replayDefaultAllowed );
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
    overrides.replayRestoreTargetFileProbePath =
        args.replayRestoreTargetFileProbe ? args.replayRestoreTargetFileProbePath : nullptr;
    overrides.replayRestoreBranchFileProbePath =
        args.replayRestoreBranchFileProbe ? args.replayRestoreBranchFileProbePath : nullptr;
    overrides.replayRestoreFailureFileProbePath =
        args.replayRestoreFailureFileProbe ? args.replayRestoreFailureFileProbePath : nullptr;
    overrides.physicsRegressionLogPath =
        args.physicsRegressionLogOverride[0] != '\0' ? args.physicsRegressionLogOverride : nullptr;
    overrides.physicsCollisionTimeLogPath =
        args.physicsCollisionTimeLogOverride[0] != '\0' ? args.physicsCollisionTimeLogOverride : nullptr;
    overrides.physicsDiagnosticsPath = args.physicsDiagnosticsPath[0] != '\0' ? args.physicsDiagnosticsPath : nullptr;
    overrides.physicsDiagnosticsFixedStepForced = args.fixedStepForcedByPhysicsDiagnostics;
#endif

    return overrides;
}

} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore
