// File: SkullbonezTests/TestStartup.cpp
// Purpose:
//   Locks startup token parsing, frozen diagnostics, and launch-policy resolution.
//
// Summary:
//   Exercises the production command-line and launch-resolution units without
//   constructing a window, renderer, worker pool, or Run owner. Table-driven
//   failure cases assert the exact recoverable-result messages consumed by
//   automation. Recorded-manifest cases prove adjacent, digest-authenticated
//   scene resolution, while development builds lock the exclusive editor
//   selector's launch projection.
//
// Glossary:
//   Assigned option: A value supplied as --name=value rather than a later token.
//   Launch packet: ParsedArgs values projected into RunStartupOverrides.

// Invariants:
//   - Aliases, defaults, validation order, and error strings are compatibility surface.
//   - Suite tests use caller-owned temporary files and never launch the engine.
//   - Every pointer in RunStartupOverrides is checked while ParsedArgs still lives.
//
// Related:
//   - SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp
//   - SkullbonezSource/Runtime/App/StartupLaunchApplication.cpp
//   - Agentic/Reference/engine-glossary.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayCaptureLimits.h"
#include "../SkullbonezSource/Runtime/Startup/RunLaunchOptions.h"
#include "../SkullbonezSource/Runtime/Startup/StartupCommandLine.h"
#include "../SkullbonezSource/Runtime/Startup/StartupLaunchResolution.h"

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using SkullbonezCore::Core::ActiveSceneObjectCapacity;
using SkullbonezCore::Core::EngineConfig;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode;
using SkullbonezCore::Runtime::GeneratedObjectTypeOverride;
using SkullbonezCore::Runtime::RunStartupOverrides;
using namespace SkullbonezCore::Runtime::Startup;

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;

CommandLineView View( const char* text )
{
    return TokenizeCommandLine( text );
}

TEST_CASE( "Startup capacity: post-load consumers resolve the active authored override" )
{
    EngineConfig config;
    config.runtimeCapacity.sceneObjectCapacity = 6000;
    CHECK( ActiveSceneObjectCapacity( config ) == 6000 );

    config.runtimeCapacity.sceneObjectCapacity = SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS + 1;
    CHECK( ActiveSceneObjectCapacity( config ) == SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
}

void CheckRunDirectiveFailure( const char* text, const char* expected )
{
    ParsedArgs args;
    CHECK_FALSE( ApplyRunCliValueDirectives( View( text ), args ) );
    CHECK( std::strcmp( GetCommandLineError(), expected ) == 0 );
}

void CheckPhysicsDebugFailure( const char* text, const char* expected )
{
    ParsedArgs args;
    CHECK_FALSE( ParsePhysicsDebugOverrides( View( text ), args ) );
    CHECK( std::strcmp( GetCommandLineError(), expected ) == 0 );
}

void CheckFullParseFailure( const char* text, const char* expected )
{
    EngineConfig config;
    ParsedArgs args;
    CHECK_FALSE( ParseCommandLine( diagnostics, View( text ), config, args ) );
    CHECK( std::strcmp( GetCommandLineError(), expected ) == 0 );
}

std::string StartupArtifactPath( const char* leaf )
{
    std::error_code error;
    const std::filesystem::path directory = "TestOutput/startup_unit";
    std::filesystem::create_directories( directory, error );
    REQUIRE_FALSE( error );
    return ( directory / leaf ).generic_string();
}

std::string WriteSuite( const char* leaf, const char* json )
{
    const std::string path = StartupArtifactPath( leaf );
    std::ofstream output( path, std::ios::binary | std::ios::trunc );
    REQUIRE( output.is_open() );
    output << json;
    REQUIRE( output.good() );
    return path;
}
} // namespace

TEST_CASE( "Startup command line: tokenizer and option lookup preserve compatibility edges" )
{
    CHECK( TokenizeCommandLine( nullptr ).tokens.empty() );
    CHECK( View( " \t " ).tokens.empty() );

    const CommandLineView commandLine = View(
        "\t--scene \"path with spaces.scene.json\" --frames=12 loose \"unterminated tail" );
    REQUIRE( commandLine.tokens.size() == 5u );
    CHECK( commandLine.tokens[0] == "--scene" );
    CHECK( commandLine.tokens[1] == "path with spaces.scene.json" );
    CHECK( commandLine.tokens[2] == "--frames=12" );
    CHECK( commandLine.tokens[3] == "loose" );
    CHECK( commandLine.tokens[4] == "unterminated tail" );
    CHECK( std::strcmp( FindOptionValue( commandLine, "--scene" ), "path with spaces.scene.json" ) == 0 );
    CHECK( std::strcmp( FindOptionValue( commandLine, "--frames" ), "12" ) == 0 );
    CHECK( FindOptionValue( commandLine, "--missing" ) == nullptr );
    CHECK( HasOption( commandLine, "--frames" ) );
    CHECK_FALSE( HasOption( commandLine, "--frame" ) );

    const CommandLineView aliases = View( "--value --next --under=alias --value second" );
    CHECK( IsOptionValueMissing( FindOptionValue( aliases, "--value" ) ) );
    CHECK( std::strcmp( FindOptionValue( aliases, "--dash", "--under" ), "alias" ) == 0 );
    CHECK_FALSE( IsOptionValueMissing( "x" ) );
    CHECK( IsOptionValueMissing( nullptr ) );
}

TEST_CASE( "Startup command line: primitive value parsers reject partial writes and overflow" )
{
    float floating = 9.0f;
    CHECK( ParseFloatToken( "-1.25", floating ) );
    CHECK( floating == doctest::Approx( -1.25f ) );
    CHECK_FALSE( ParseFloatToken( "1.0tail", floating ) );
    CHECK_FALSE( ParseFloatToken( "1e9999", floating ) );
    CHECK_FALSE( ParseFloatToken( "", floating ) );

    int integer = 0;
    CHECK( ParseIntCommandLineToken( "-2147483648", integer ) );
    CHECK( integer == ( std::numeric_limits<int>::min )() );
    CHECK( ParseIntCommandLineToken( "2147483647", integer ) );
    CHECK_FALSE( ParseIntCommandLineToken( "2147483648", integer ) );
    CHECK_FALSE( ParseIntCommandLineToken( "3.5", integer ) );

    unsigned int unsignedValue = 0;
    CHECK( ParseUnsignedCommandLineToken( "4294967295", unsignedValue ) );
    CHECK( unsignedValue == ( std::numeric_limits<unsigned int>::max )() );
    CHECK_FALSE( ParseUnsignedCommandLineToken( "4294967296", unsignedValue ) );

    // Invariant: startup compatibility preserves MSVC strtoul's unsigned wrap
    // for a leading minus. This assertion records the observable parser result;
    // changing it is a product-behavior change, not a comment cleanup.
    CHECK( ParseUnsignedCommandLineToken( "-1", unsignedValue ) );
    CHECK( unsignedValue == ( std::numeric_limits<unsigned int>::max )() );

    bool enabled = false;
    CHECK( ParseOptionalOnOffValue( nullptr, enabled ) );
    CHECK( enabled );
    CHECK( ParseOptionalOnOffValue( "OFF", enabled ) );
    CHECK_FALSE( enabled );
    CHECK( ParseOptionalOnOffValue( "yes", enabled ) );
    CHECK( enabled );
    CHECK( ParseOptionalOnOffValue( "-2", enabled ) );
    CHECK( enabled );
    CHECK_FALSE( ParseOptionalOnOffValue( "sometimes", enabled ) );

    RuntimeAllocationGuardMode guard = RuntimeAllocationGuardMode::Off;
    CHECK( ParseAllocationGuardCommandLineToken( "", guard ) );
    CHECK( guard == RuntimeAllocationGuardMode::Measure );
    CHECK( ParseAllocationGuardCommandLineToken( "none", guard ) );
    CHECK( guard == RuntimeAllocationGuardMode::Off );
    CHECK( ParseAllocationGuardCommandLineToken( "warnings", guard ) );
    CHECK( guard == RuntimeAllocationGuardMode::Gameplay );
    CHECK_FALSE( ParseAllocationGuardCommandLineToken( "fatal", guard ) );

    char path[4] = {};
    CHECK( CopyCommandLinePath( "abc", "--output", path, sizeof( path ) ) );
    CHECK( std::strcmp( path, "abc" ) == 0 );
    CHECK_FALSE( CopyCommandLinePath( "abcd", "--output", path, sizeof( path ) ) );
    CHECK( std::strcmp( GetCommandLineError(), "--output path is too long." ) == 0 );
    CHECK_FALSE( CopyCommandLinePath( "", "--output", path, sizeof( path ) ) );
    CHECK( std::strcmp( GetCommandLineError(), "--output requires an output path." ) == 0 );
}

#if !defined( SKULLBONEZ_SKARNESS )
TEST_CASE( "Startup command line: non-Skarness builds reject Skarness session flags" )
{
    constexpr const char* expected = "--skarness is available only in Debug and Automation builds.";
    CheckRunDirectiveFailure( "--skarness TestOutput/skarness/session", expected );
    CheckRunDirectiveFailure( "--skarness-manual TestOutput/skarness/session", expected );
    CheckRunDirectiveFailure( "--skarness_manual TestOutput/skarness/session", expected );
}
#endif

TEST_CASE( "Startup launch values: every run directive family projects into owned state" )
{
    const CommandLineView commandLine = View(
        "--seed 17 --frames=9 --frame_buffers 3 --perf_log TestOutput/demohero.csv --allocation_guard gameplay "
        "--style-harness TestOutput/style --scene_snapshot_out TestOutput/scene.json "
        "--memory_dump TestOutput/memory.json --interaction_script script.json "
        "--interaction_report report.json --interaction_trace trace.jsonl "
        "--interaction_record_max_minutes 3 "
        "--replay off --replay_seconds 12 "
        "--replay_scrub_probe 0.5 --replay_restore_probe 0.75 "
        "--replay_save_probe save.skreplay --replay_load load.skreplay "
        "--replay_load_probe probe.skreplay --replay_restore_file_probe restore.skreplay "
        "--replay_restore_target_file_probe target.skreplay "
        "--replay_restore_branch_file_probe branch.skreplay "
        "--replay_restore_failure_file_probe failure.skreplay --replay_hashes hashes.csv "
        "--ui_stress on --ui_stress_seed 21 --ui_stress_actions 7 "
        "--graphics_stress on --graphics_stress_seed 22 --graphics_stress_actions 8 "
        "--graphics_stress_scene_interval 30 --graphics_stress_memory_interval 0" );

    ParsedArgs args;
    REQUIRE( ApplyRunCliValueDirectives( commandLine, args ) );

    CHECK( args.seedOverride == 17u );
    CHECK( args.frameCountOverride == 9 );
    CHECK( args.renderFrameBufferCount == 3 );
    CHECK( std::strcmp( args.perfLogPath, "TestOutput/demohero.csv" ) == 0 );
    CHECK( args.allocationGuardMode == RuntimeAllocationGuardMode::Gameplay );
    CHECK( std::strcmp( args.liveStyleControlDir, "TestOutput/style" ) == 0 );
    CHECK( std::strcmp( args.sceneSnapshotOutPath, "TestOutput/scene.json" ) == 0 );
    CHECK( std::strcmp( args.memoryDumpPath, "TestOutput/memory.json" ) == 0 );
    CHECK( std::strcmp( args.interactionScriptPath, "script.json" ) == 0 );
    CHECK( std::strcmp( args.interactionReportPath, "report.json" ) == 0 );
    CHECK( std::strcmp( args.interactionTracePath, "trace.jsonl" ) == 0 );
    CHECK( args.interactionRecordMaxMinutes == 3 );
    CHECK( args.replayRecording );
    CHECK( args.replayExplicit );
    CHECK( args.replaySeconds == 12 );
    CHECK( args.replayScrubProbe );
    CHECK( args.replayScrubProbeNormalized == doctest::Approx( 0.5f ) );
    CHECK( args.replayRestoreProbe );
    CHECK( args.replayRestoreProbeNormalized == doctest::Approx( 0.75f ) );
    CHECK( std::strcmp( args.replaySaveProbePath, "save.skreplay" ) == 0 );
    CHECK( args.replayLoad );
    CHECK( args.replayLoadProbe );
    CHECK( std::strcmp( args.replayLoadPath, "probe.skreplay" ) == 0 );
    CHECK( args.replayRestoreFileProbe );
    CHECK( args.replayRestoreTargetFileProbe );
    CHECK( args.replayRestoreBranchFileProbe );
    CHECK( args.replayRestoreFailureFileProbe );
    CHECK( std::strcmp( args.replayHashLogPath, "hashes.csv" ) == 0 );
    CHECK( args.uiStress );
    CHECK( args.uiStressSeed == 21u );
    CHECK( args.uiStressActions == 7 );
    CHECK( args.graphicsStress );
    CHECK( args.graphicsStressSeed == 22u );
    CHECK( args.graphicsStressActions == 8 );
    CHECK( args.graphicsStressSceneIntervalFrames == 30 );
    CHECK( args.graphicsStressMemoryIntervalFrames == 0 );
    CHECK( args.interactiveRun );
    CHECK( args.fixedStep );
    CHECK( args.suppressExitDialog );
}

TEST_CASE( "Startup render buffering: defaults to two and accepts only two or three" )
{
    ParsedArgs defaults;
    REQUIRE( ApplyRunCliValueDirectives( View( "--frames 1" ), defaults ) );
    CHECK( defaults.renderFrameBufferCount == 2 );

    ParsedArgs triple;
    REQUIRE( ApplyRunCliValueDirectives( View( "--frame-buffers 3" ), triple ) );
    CHECK( triple.renderFrameBufferCount == 3 );

    CheckRunDirectiveFailure( "--frame-buffers 1", "--frame-buffers expects 2 or 3." );
    CheckRunDirectiveFailure( "--frame_buffers 4", "--frame-buffers expects 2 or 3." );
}

TEST_CASE( "Startup launch values: malformed directives keep exact recoverable messages" )
{
    struct FailureCase
    {
        const char* commandLine;
        const char* message;
    };
    const FailureCase cases[] = {
        { "--seed 0", "--seed expects a positive 32-bit integer." },
        { "--frames -1", "--frames expects a positive integer." },
        { "--perf-log", "--perf-log requires an output path." },
        { "--allocation-guard fatal", "--allocation-guard expects off|measure|gameplay." },
        { "--live-style-control", "--live-style-control expects a directory path." },
        { "--scene-snapshot-out", "--scene-snapshot-out expects a file path." },
        { "--memory-dump", "--memory-dump requires an output path." },
        { "--interaction-script", "--interaction-script requires an output path." },
        { "--interaction-report", "--interaction-report requires an output path." },
        { "--interaction-record-max-minutes 0", "--interaction-record-max-minutes expects 1..60." },
        { "--interaction-record-max-minutes 61", "--interaction-record-max-minutes expects 1..60." },
        { "--replay maybe", "--replay expects optional on|off." },
        { "--replay-seconds 0", "--replay-seconds expects 1..600." },
        { "--replay-scrub-probe 0.995", "--replay-scrub-probe expects a normalized position in the range 0..0.995." },
        { "--replay-restore-probe -0.1", "--replay-restore-probe expects a normalized position in the range 0..0.995." },
        { "--replay-save-probe", "--replay-save-probe expects a file path." },
        { "--replay-load", "--replay-load expects a file path." },
        { "--replay-load-probe", "--replay-load expects a file path." },
        { "--replay-restore-file-probe", "--replay-restore-file-probe expects a file path." },
        { "--replay-restore-target-file-probe", "--replay-restore-target-file-probe expects a file path." },
        { "--replay-restore-branch-file-probe", "--replay-restore-branch-file-probe expects a file path." },
        { "--replay-restore-failure-file-probe", "--replay-restore-failure-file-probe expects a file path." },
        { "--replay-hashes", "--replay-hashes expects a file path." },
        { "--ui-stress maybe", "--ui-stress expects optional on|off." },
        { "--ui-stress-seed 0", "--ui-stress-seed expects a positive 32-bit integer." },
        { "--ui-stress-actions 33", "--ui-stress-actions expects 1..32." },
        { "--graphics-stress maybe", "--graphics-stress expects optional on|off." },
        { "--graphics-stress-seed 0", "--graphics-stress-seed expects a positive 32-bit integer." },
        { "--graphics-stress-actions 65", "--graphics-stress-actions expects 1..64." },
        { "--graphics-stress-scene-interval 601", "--graphics-stress-scene-interval expects 1..600 frames." },
        { "--graphics-stress-memory-interval -1", "--graphics-stress-memory-interval expects 0..36000 frames." },
    };

    for ( const FailureCase& failure : cases )
    {
        CAPTURE( failure.commandLine );
        CheckRunDirectiveFailure( failure.commandLine, failure.message );
    }
}

TEST_CASE( "Startup recorded interaction owns its adjacent scene launch" )
{
    WriteSuite( "saved.scene.json", "" );
    const std::string manifest = WriteSuite(
        "recorded_interaction.json",
        R"({"format":"skullbonez.interaction-recording","version":1,"complete":true,"scene":{"path":"saved.scene.json","sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},"frames":[]})" );
    ParsedArgs args;
    strcpy_s( args.interactionScriptPath, manifest.c_str() );
    REQUIRE( ResolveInteractionRecordingLaunch( args ) );
    REQUIRE( args.sceneList.size() == 1u );
    CHECK( std::filesystem::path( args.sceneList[0] ).filename() == "saved.scene.json" );
    CHECK( args.isSuiteOrSceneMode );
    CHECK( args.interactiveRun );
    CHECK( args.suppressExitDialog );
}

TEST_CASE( "Startup recorded interaction authenticates sidecars before launch publication" )
{
    WriteSuite( "mutated.scene.json", "mutated" );
    const std::string manifest = WriteSuite(
        "recorded_interaction_mutated.json",
        R"({"format":"skullbonez.interaction-recording","version":1,"complete":true,"scene":{"path":"mutated.scene.json","sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},"frames":[]})" );
    ParsedArgs args;
    strcpy_s( args.interactionScriptPath, manifest.c_str() );
    REQUIRE( ResolveInteractionRecordingLaunch( args ) );
    CHECK( args.sceneList.empty() );
    CHECK_FALSE( args.replayLoad );
    CHECK_FALSE( args.isSuiteOrSceneMode );
}

TEST_CASE( "Startup recorded interaction rejects escaping sidecar paths" )
{
    const std::string manifest = WriteSuite(
        "recorded_interaction_escape.json",
        R"({"format":"skullbonez.interaction-recording","version":1,"complete":true,"scene":{"path":"../saved.scene.json","sha256":"0000000000000000000000000000000000000000000000000000000000000000"},"frames":[]})" );
    ParsedArgs args;
    strcpy_s( args.interactionScriptPath, manifest.c_str() );
    REQUIRE( ResolveInteractionRecordingLaunch( args ) );
    CHECK( args.sceneList.empty() );
    CHECK_FALSE( args.isSuiteOrSceneMode );
}

TEST_CASE( "Startup physics debug: component, float, and optional switches compose deterministically" )
{
    ParsedArgs args;
    REQUIRE( ParsePhysicsDebugOverrides( View( "--physics_debug all --physics-debug-axes off --physics_debug_contacts on "
                                               "--physics-debug-sleep=off --physics-debug-pipeline "
                                               "--physics-debug-terrain-contact=0 "
                                               "--physics-debug-transparent --physics-debug-alpha 0.5 "
                                               "--physics_debug_contact_linger 1.25" ),
                                         args ) );

    CHECK( args.hasPhysicsDebugFlagsOverride );
    CHECK( ( args.physicsDebugFlagsOverride & SkullbonezCore::Physics::PHYSICS_DEBUG_AXES ) == 0u );
    CHECK( ( args.physicsDebugFlagsOverride & SkullbonezCore::Physics::PHYSICS_DEBUG_CONTACTS ) != 0u );
    CHECK( ( args.physicsDebugFlagsOverride & SkullbonezCore::Physics::PHYSICS_DEBUG_SLEEP ) == 0u );
    CHECK( ( args.physicsDebugFlagsOverride & SkullbonezCore::Physics::PHYSICS_DEBUG_PIPELINE ) != 0u );
    CHECK( ( args.physicsDebugFlagsOverride & SkullbonezCore::Physics::PHYSICS_DEBUG_TERRAIN_CONTACT ) == 0u );
    CHECK( args.hasPhysicsDebugTransparentOverride );
    CHECK( args.physicsDebugTransparentOverride );
    CHECK( args.hasPhysicsDebugAlphaOverride );
    CHECK( args.physicsDebugAlphaOverride == doctest::Approx( 0.5f ) );
    CHECK( args.hasPhysicsDebugContactLingerOverride );
    CHECK( args.physicsDebugContactLingerOverride == doctest::Approx( 1.25f ) );

    CheckPhysicsDebugFailure( "--physics-debug unknown",
                              "--physics-debug expects none|axes|contacts|sleep|pipeline|terrain|all|on|off." );

    CheckPhysicsDebugFailure( "--physics-debug-axes maybe", "--physics-debug-axes expects optional on|off." );
    CheckPhysicsDebugFailure( "--physics-debug-transparent maybe", "--physics-debug-transparent expects optional on|off." );
    CheckPhysicsDebugFailure( "--physics-debug-alpha 0.01", "--physics-debug-alpha expects 0.05..1.0." );
    CheckPhysicsDebugFailure( "--physics-debug-contact-linger 5.1",
                              "--physics-debug-contact-linger expects 0.0..5.0 seconds." );
}

TEST_CASE( "Startup launch resolution: generated, hero, named, and explicit scene paths are distinct" )
{
    std::vector<std::string> scenes;
    bool suiteOrScene = false;
    REQUIRE( ParseSceneArgs( View( "" ), scenes, suiteOrScene ) );
    REQUIRE( scenes.size() == 1u );
    CHECK( scenes[0].empty() );
    CHECK_FALSE( suiteOrScene );

    scenes.clear();
    REQUIRE( ParseSceneArgs( View( "--hero" ), scenes, suiteOrScene ) );
    REQUIRE( scenes.size() == 1u );
    CHECK( scenes[0] == "SkullbonezData/scenes/concept_12_low_poly_art_style.scene.json" );
    CHECK( suiteOrScene );

    scenes.clear();
    suiteOrScene = false;
    REQUIRE( ParseSceneArgs( View( "--scene prediction_ragdoll_wall_200" ), scenes, suiteOrScene ) );
    CHECK( scenes[0] == "SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json" );

    scenes.clear();
    const std::string explicitPath = StartupArtifactPath( "scene with spaces.scene.json" );
    REQUIRE( ParseSceneArgs( View( ( "--scene \"" + explicitPath + "\"" ).c_str() ), scenes, suiteOrScene ) );
    CHECK( scenes[0] == explicitPath );

    scenes.clear();
    REQUIRE( ParseSceneArgs( View( "--scene low-poly-hero" ), scenes, suiteOrScene ) );
    CHECK( scenes[0] == "SkullbonezData/scenes/concept_12_low_poly_art_style.scene.json" );
}

TEST_CASE( "Startup launch resolution: suite schema and mutual-exclusion failures are frozen" )
{
    const std::string valid = WriteSuite( "valid.suite.json",
                                          R"({"format":"skullbonez.suite.json","scenes":["hero","missing.scene.json"]})" );

    std::vector<std::string> scenes;
    bool suiteOrScene = false;
    REQUIRE( ParseSceneArgs( View( ( "--suite " + valid ).c_str() ), scenes, suiteOrScene ) );
    REQUIRE( scenes.size() == 2u );
    CHECK( scenes[0] == "SkullbonezData/scenes/concept_12_low_poly_art_style.scene.json" );
    CHECK( scenes[1] == "missing.scene.json" );
    CHECK( suiteOrScene );

    struct SuiteFailure
    {
        const char* leaf;
        const char* json;
        const char* suffix;
    };
    const SuiteFailure failures[] = {
        { "invalid-json.suite.json", "{", "invalid JSON in" },
        { "array-root.suite.json", "[]", "root must be an object." },
        { "missing-format.suite.json", R"({"scenes":[]})", "must declare format skullbonez.suite.json." },
        { "missing-scenes.suite.json", R"({"format":"skullbonez.suite.json"})", "must contain a scenes array." },
        { "bad-entry.suite.json", R"({"format":"skullbonez.suite.json","scenes":[1]})", "scenes entries must be strings." },
    };

    for ( const SuiteFailure& failure : failures )
    {
        const std::string path = WriteSuite( failure.leaf, failure.json );
        scenes.clear();
        CHECK_FALSE( ParseSceneArgs( View( ( "--suite " + path ).c_str() ), scenes, suiteOrScene ) );
        CHECK( std::strstr( GetCommandLineError(), failure.suffix ) != nullptr );
    }

    scenes.clear();
    CHECK_FALSE( ParseSceneArgs( View( "--scene" ), scenes, suiteOrScene ) );
    CHECK( std::strcmp( GetCommandLineError(), "--scene requires a path." ) == 0 );
    scenes.clear();
    CHECK_FALSE( ParseSceneArgs( View( "--suite does-not-exist" ), scenes, suiteOrScene ) );
    CHECK( std::strcmp( GetCommandLineError(), "--suite could not open 'does-not-exist'." ) == 0 );
    scenes.clear();
    CHECK_FALSE( ParseSceneArgs( View( "--hero --scene hero" ), scenes, suiteOrScene ) );
    CHECK( std::strcmp( GetCommandLineError(), "--demohero, --hero, --suite, and --scene are mutually exclusive." ) == 0 );
    scenes.clear();
    CHECK_FALSE( ParseSceneArgs( View( "--demo-hero --hero" ), scenes, suiteOrScene ) );
    CHECK( std::strcmp( GetCommandLineError(), "--demohero, --hero, --suite, and --scene are mutually exclusive." ) == 0 );
}

TEST_CASE( "Startup launch packet: replay defaults and borrowed paths follow parsed ownership" )
{
    ParsedArgs args;
    args.timeScaleOverride = 2.0f;
    args.fixedStep = true;
    args.seedOverride = 99u;
    args.noWater = true;
    args.noSleep = true;
    args.hasTornadoOverride = true;
    args.tornadoEnabled = true;
    args.tornadoVectors = true;
    args.hasCinematicRenderingOverride = true;
    args.cinematicRendering = true;
    args.hasCinematicShadowsOverride = true;
    args.cinematicShadows = true;
    args.demoHeroStyle = true;
    args.dumpAssets = true;
    args.interactiveRun = true;
    args.frameCountOverride = 10;
    strcpy_s( args.perfLogPath, "profile.csv" );
    args.uiStress = true;
    args.graphicsStress = true;
    args.replayGuideArcsAtStartup = true;
    args.allocationGuardMode = RuntimeAllocationGuardMode::Measure;
    args.objectTypeOverride = GeneratedObjectTypeOverride::AllBoxes;
    args.hasPhysicsDebugFlagsOverride = true;
    args.physicsDebugFlagsOverride = SkullbonezCore::Physics::PHYSICS_DEBUG_ALL;
    args.hasPhysicsDebugTransparentOverride = true;
    args.physicsDebugTransparentOverride = true;
    args.hasPhysicsDebugAlphaOverride = true;
    args.physicsDebugAlphaOverride = 0.5f;
    args.hasPhysicsDebugContactLingerOverride = true;
    args.physicsDebugContactLingerOverride = 1.0f;
    args.replaySeconds = 14;
    args.replayExplicit = true;
    args.replayRecording = true;
    args.showProfiler = true;
    args.hideTopText = true;
    args.showBroadphaseVisualizer = true;
    strcpy_s( args.liveStyleControlDir, "style" );
    strcpy_s( args.memoryDumpPath, "memory.json" );
    strcpy_s( args.interactionScriptPath, "script.json" );
    strcpy_s( args.interactionReportPath, "report.json" );
    strcpy_s( args.interactionTracePath, "trace.jsonl" );
    strcpy_s( args.replayHashLogPath, "hashes.csv" );
    args.replayLoad = true;
    strcpy_s( args.replayLoadPath, "load.skreplay" );

    const RunStartupOverrides overrides = BuildRunStartupOverrides( args );
    CHECK( overrides.launch.timeScaleOverride == doctest::Approx( 2.0f ) );
    CHECK( overrides.launch.fixedStep );
    CHECK( overrides.launch.seedOverride == 99u );
    CHECK( overrides.launch.noWater );
    CHECK( overrides.launch.noSleep );
    CHECK( overrides.launch.tornadoEnabled );
    CHECK( overrides.launch.tornadoVectors );
    CHECK( overrides.launch.cinematicRendering );
    CHECK( overrides.launch.cinematicShadows );
    CHECK( overrides.launch.demoHeroStyle );
    CHECK( overrides.launch.dumpTextureAssets );
    CHECK( overrides.launch.interactiveSceneRun );
    CHECK( overrides.launch.frameCountOverride == 10 );
    CHECK( std::strcmp( overrides.launch.perfLogPath, "profile.csv" ) == 0 );
    CHECK( overrides.launch.uiStress );
    CHECK( overrides.launch.graphicsStress );
    CHECK( overrides.launch.replayGuideArcsAtStartup );
    CHECK( overrides.launch.generatedObjectTypeOverride == GeneratedObjectTypeOverride::AllBoxes );
    CHECK( overrides.launch.hasPhysicsDebugFlagsOverride );
    CHECK( overrides.launch.physicsDebugTransparentOverride );
    CHECK( overrides.launch.physicsDebugAlphaOverride == doctest::Approx( 0.5f ) );
    CHECK( overrides.launch.physicsDebugContactLingerOverride == doctest::Approx( 1.0f ) );
    CHECK( std::strcmp( overrides.liveStyleControlDirectory, "style" ) == 0 );
    CHECK( std::strcmp( overrides.mainMemoryDumpPath, "memory.json" ) == 0 );
    CHECK( std::strcmp( overrides.interactionScriptPath, "script.json" ) == 0 );
    CHECK( std::strcmp( overrides.interactionReportPath, "report.json" ) == 0 );
    CHECK( std::strcmp( overrides.interactionTracePath, "trace.jsonl" ) == 0 );
    CHECK( overrides.configureReplayRecording );
    CHECK( overrides.replayRetentionSeconds == 14 );
    CHECK( std::strcmp( overrides.replayHashLogPath, "hashes.csv" ) == 0 );
    CHECK( std::strcmp( overrides.replayLoadPath, "load.skreplay" ) == 0 );
    CHECK( overrides.hasInitialOverlayMode );
    CHECK( overrides.hideTopText );
    CHECK( overrides.showBroadphaseVisualizer );

    ParsedArgs suiteDefaults;
    suiteDefaults.isSuiteOrSceneMode = true;
    const RunStartupOverrides suiteOverrides = BuildRunStartupOverrides( suiteDefaults );
    CHECK_FALSE( suiteOverrides.configureReplayRecording );
    CHECK( suiteOverrides.replayRetentionSeconds == SkullbonezCore::Runtime::REPLAY_PAST_BUFFER_SECONDS );
    suiteDefaults.interactiveRun = true;
    CHECK( BuildRunStartupOverrides( suiteDefaults ).configureReplayRecording );
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
TEST_CASE( "Startup development UI: one startup-selected surface owns focus and GameUI remains the default" )
{
    using SkullbonezCore::Runtime::DevelopmentUiMode;
    using SkullbonezCore::Runtime::DevelopmentUiModeShowsGameUI;
    using SkullbonezCore::Runtime::DevelopmentUiModeShowsImGui;

    struct ModeCase
    {
        const char* commandLine;
        DevelopmentUiMode expected;
    };
    const ModeCase cases[] = {
        { "--dev-ui game", DevelopmentUiMode::GameUI },
        { "--dev_ui=imgui", DevelopmentUiMode::ImGui },
    };

    for ( const ModeCase& modeCase : cases )
    {
        CAPTURE( modeCase.commandLine );
        ParsedArgs args;
        REQUIRE( ApplyRunCliValueDirectives( View( modeCase.commandLine ), args ) );
        CHECK( args.developmentUiMode == modeCase.expected );
        CHECK( args.developmentUiModeExplicit );

        const RunStartupOverrides overrides = BuildRunStartupOverrides( args );
        CHECK( overrides.launch.developmentUiMode == modeCase.expected );
        CHECK( overrides.launch.developmentUiModeExplicit );
        CHECK( DevelopmentUiModeShowsGameUI( modeCase.expected ) != DevelopmentUiModeShowsImGui( modeCase.expected ) );
    }

    ParsedArgs omitted;
    REQUIRE( ApplyRunCliValueDirectives( View( "--frames 2" ), omitted ) );
    CHECK( omitted.developmentUiMode == DevelopmentUiMode::GameUI );
    CHECK_FALSE( omitted.developmentUiModeExplicit );
    CHECK_FALSE( BuildRunStartupOverrides( omitted ).launch.developmentUiModeExplicit );
}

TEST_CASE( "Startup development UI: invalid mode keeps the frozen recoverable diagnostic" )
{
    constexpr const char* expected = "--dev-ui expects game|imgui; the two surfaces are mutually exclusive.";
    CheckRunDirectiveFailure( "--dev-ui unknown", expected );
    CheckRunDirectiveFailure( "--dev-ui both", expected );
    CheckRunDirectiveFailure( "--dev-ui", expected );
}
#endif

TEST_CASE( "Startup full parse: config, flags, aliases, and launch values compose once" )
{
    std::string text = "--renderer d3d12 --vsync off --time-scale 2 --tornado=off --tornado-vector-field on "
                       "--cinematic-rendering off --shadow-maps off --workers 0 --model-capacity 32 "
                       "--physics-parallel off "
                       "--parallel-shadow-prep on --hold=off "
                       "--seed 17 --frames 3 --all-boxes --physics-debug contacts --physics-debug-alpha .5 "
                       "--fixed-step --no-water --no-sleep --load-scenes-only "
                       "--demo-hero --show-profiler --no-top-text --automation-hidden-window --broadphase-overlay "
                       "--guide-arcs "
                       "--dump-config --dump-assets --workers-self-test";

#ifdef _DEBUG
    text += " --physics-diag TestOutput/startup_unit/physics.ndjson --replay-scrub-test --replay-restore-test";
#endif
    EngineConfig config;
    ParsedArgs args;
    REQUIRE( ParseCommandLine( diagnostics, View( text.c_str() ), config, args ) );
    REQUIRE( args.sceneList.size() == 1u );
    CHECK( args.sceneList[0].empty() );
    CHECK( args.timeScaleOverride == doctest::Approx( 2.0f ) );
    CHECK( args.fixedStep );
    CHECK( args.noWater );
    CHECK( args.noSleep );
    CHECK( args.sceneLoadOnly );
    CHECK( args.demoHeroStyle );
    CHECK( args.showProfiler );
    CHECK( args.hideTopText );
    CHECK( args.automationWindowHidden );
    CHECK( args.showBroadphaseVisualizer );
    CHECK( args.replayGuideArcsAtStartup );
    CHECK( args.dumpConfig );
    CHECK( args.dumpAssets );
    CHECK( args.workerSelfTest );
    CHECK( args.objectTypeOverride == GeneratedObjectTypeOverride::AllBoxes );
    CHECK( args.hasPhysicsDebugFlagsOverride );
    CHECK( args.physicsDebugAlphaOverride == doctest::Approx( 0.5f ) );
    CHECK_FALSE( config.runtimeRender.vsyncEnabled );
    CHECK( config.runtimeCapacity.workerThreads == 0 );
    CHECK( config.runtimeCapacity.sceneObjectCapacity == 32 );
    CHECK_FALSE( config.physicsExecution.parallel );
    CHECK( config.runtimeRender.shadowParallelPrep );
#ifdef _DEBUG
    CHECK( args.physicsDiagnosticsRequested );
    CHECK_FALSE( args.renderFrameLockstepForcedByPhysicsDiagnostics );
    CHECK( args.replayScrubProbe );
    CHECK( args.replayRestoreProbe );
#endif
}

#ifdef _DEBUG
TEST_CASE( "Startup physics diagnostics explicitly requests render-frame lockstep" )
{
    EngineConfig config;
    ParsedArgs args;
    REQUIRE(
        ParseCommandLine( diagnostics, View( "--physics-diag TestOutput/startup_unit/physics.ndjson" ), config, args ) );
    CHECK( args.physicsDiagnosticsRequested );
    CHECK( args.fixedStep );
    CHECK( args.renderFrameLockstepForcedByPhysicsDiagnostics );
}
#endif

TEST_CASE( "Startup full parse: validation precedence publishes frozen messages" )
{
    CheckFullParseFailure( "--scene", "--scene requires a path." );
    CheckFullParseFailure( "--renderer gl", "--renderer expects dx12. GL and DX11 are retired runtime choices." );
    CheckFullParseFailure( "--vsync maybe", "--vsync expects on|off." );
    CheckFullParseFailure( "--switch-interval 1",
                           "--switch-interval is retired because DX12 is the only runtime renderer." );

    CheckFullParseFailure( "--time-scale 0", "--time-scale expects a positive float." );
    CheckFullParseFailure( "--model-capacity 0", "--model-capacity expects 1..8192." );
    CheckFullParseFailure( "--physics-parallel maybe", "--physics-parallel expects optional on|off." );
    CheckFullParseFailure( "--shadow-parallel-prep maybe", "--shadow-parallel-prep expects optional on|off." );
    CheckFullParseFailure( "--interactive maybe", "--interactive expects optional on|off." );
    CheckFullParseFailure( "--all-balls --all-boxes", "--all-balls and --all-boxes are mutually exclusive." );

    const int maxWorkers = SkullbonezCore::Threading::WorkerPool::MaxThreadCount();
    char workersMessage[128] = {};
    snprintf( workersMessage, sizeof( workersMessage ), "--workers expects -1, 0, or 1..%d.", maxWorkers );
    CheckFullParseFailure( "--workers 999999", workersMessage );
#ifndef _DEBUG
    CheckFullParseFailure( "--physics-diag trace.ndjson",
                           "--physics-diag is only supported in Debug builds. Recompile with the Debug configuration "
                           "to use queryable physics diagnostics." );

    CheckFullParseFailure( "--replay-save-probe save.skreplay", "--replay-save-probe is only supported in Debug builds." );
#endif
}
