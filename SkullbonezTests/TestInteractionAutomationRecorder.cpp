/*
File: SkullbonezTests/TestInteractionAutomationRecorder.cpp
Purpose:
  Proves full-frame recording, lifecycle-prefix safety, and atomic manifest publication.

Summary:
  Tests drive the production recorder with complete detached input samples,
  verify JSON round trips at normalized boundaries, and prove scene replacement
  discards the transition-triggering pending turn.

Invariants:
  - F8 is absent from the serialized key snapshot even if its release is late.
  - A committed manifest references an existing adjacent scene sidecar.
  - Distinct manifests in one directory retain distinct immutable sidecars.
  - Stop/save failures and invalid duration configuration are recoverable.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.h
  - SkullbonezSource/Runtime/Input/InputRouter.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Core/PlatformWin32.h"
#include "../SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.h"

#pragma warning( push, 0 )
#include "../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using Json = nlohmann::ordered_json;
using namespace SkullbonezCore::Runtime;

namespace
{
std::filesystem::path PrepareRecorderTest( const char* name )
{
    const std::filesystem::path directory = std::filesystem::path( "TestOutput" ) / "interaction_recorder_tests" / name;
    std::error_code ignored;
    std::filesystem::remove_all( directory, ignored );
    std::filesystem::create_directories( directory );
    return directory / "interaction.json";
}

void WriteText( const std::filesystem::path& path, const char* text )
{
    std::ofstream output( path, std::ios::binary );
    output << text;
}

void WriteSceneSidecar( const InteractionAutomationRecorder& recorder,
                        const char* text = "{\"format\":\"test.scene\"}\n" )
{
    WriteText( recorder.ScenePath(), text );
}

std::string ReadText( const std::filesystem::path& path )
{
    std::ifstream input( path, std::ios::binary );
    return { std::istreambuf_iterator<char>( input ), std::istreambuf_iterator<char>() };
}

Json ReadJson( const std::filesystem::path& path )
{
    std::ifstream input( path );
    return Json::parse( input );
}
} // namespace

TEST_CASE( "Interaction recorder gives every manifest immutable sidecar names" )
{
    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    const std::filesystem::path directory = PrepareRecorderTest( "sidecar_identity" ).parent_path();
    const std::filesystem::path firstManifest = directory / "first.json";
    const std::filesystem::path secondManifest = directory / "second.json";

    InteractionAutomationRecorder firstRecorder;
    REQUIRE( firstRecorder.Arm( diagnostics, firstManifest.string().c_str(), 1 ).Ok() );
    const std::filesystem::path firstScene = firstRecorder.ScenePath();
    const std::filesystem::path firstReplay = firstRecorder.ReplayPath();
    WriteSceneSidecar( firstRecorder, "{\"format\":\"first.scene\"}\n" );
    WriteText( firstReplay, "first replay sentinel\n" );
    REQUIRE( firstRecorder.BeginAtBoundary( diagnostics, 1280, 720, 1u, {}, true ).Ok() );
    REQUIRE( firstRecorder.StopAndSave( diagnostics, "operator" ).Ok() );
    const std::string firstManifestBytes = ReadText( firstManifest );

    InteractionAutomationRecorder secondRecorder;
    REQUIRE( secondRecorder.Arm( diagnostics, secondManifest.string().c_str(), 1 ).Ok() );
    const std::filesystem::path secondScene = secondRecorder.ScenePath();
    const std::filesystem::path secondReplay = secondRecorder.ReplayPath();
    CHECK( firstScene != secondScene );
    CHECK( firstReplay != secondReplay );
    WriteSceneSidecar( secondRecorder, "{\"format\":\"second.scene\"}\n" );
    WriteText( secondReplay, "second replay sentinel\n" );
    REQUIRE( secondRecorder.BeginAtBoundary( diagnostics, 1280, 720, 2u, {}, true ).Ok() );
    REQUIRE( secondRecorder.StopAndSave( diagnostics, "operator" ).Ok() );

    CHECK( ReadText( firstManifest ) == firstManifestBytes );
    CHECK( ReadText( firstScene ) == "{\"format\":\"first.scene\"}\n" );
    CHECK( ReadText( firstReplay ) == "first replay sentinel\n" );
    CHECK( ReadText( secondScene ) == "{\"format\":\"second.scene\"}\n" );
    CHECK( ReadText( secondReplay ) == "second replay sentinel\n" );

    const Json firstRoot = ReadJson( firstManifest );
    const Json secondRoot = ReadJson( secondManifest );
    CHECK( firstRoot["scene"]["path"] == firstScene.filename().string() );
    CHECK( firstRoot["replay"]["path"] == firstReplay.filename().string() );
    CHECK( secondRoot["scene"]["path"] == secondScene.filename().string() );
    CHECK( secondRoot["replay"]["path"] == secondReplay.filename().string() );
}

TEST_CASE( "Interaction recorder refuses to replace any reserved recording artifact" )
{
    const std::filesystem::path directory = PrepareRecorderTest( "sidecar_collision" ).parent_path();
    const std::array<const char*, 5> reservedSuffixes = {
        ".partial", ".scene.json", ".scene.json.partial", ".replay.skreplay", ".replay.skreplay.partial"
    };

    for ( std::size_t index = 0; index < reservedSuffixes.size(); ++index )
    {
        SkullbonezCore::Core::SbDiagnosticStore diagnostics;
        const std::filesystem::path manifest = directory / ( "collision-" + std::to_string( index ) + ".json" );
        const std::filesystem::path reserved = manifest.string() + reservedSuffixes[index];
        WriteText( reserved, "immutable evidence\n" );

        InteractionAutomationRecorder recorder;
        CHECK_FALSE( recorder.Arm( diagnostics, manifest.string().c_str(), 1 ).Ok() );
        CHECK( ReadText( reserved ) == "immutable evidence\n" );
        CHECK_FALSE( std::filesystem::exists( manifest ) );
    }
}

TEST_CASE( "Interaction recorder round trips a complete resolution-independent device frame" )
{
    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    InteractionAutomationRecorder recorder;
    const std::filesystem::path manifest = PrepareRecorderTest( "full_frame" );
    REQUIRE( recorder.Arm( diagnostics, manifest.string().c_str(), 1 ).Ok() );
    CHECK( recorder.Status().frameCapacity == InteractionAutomationRecorder::FRAMES_PER_MINUTE );
    WriteSceneSidecar( recorder );

    InteractionRecordingBaseline baseline;
    baseline.cameraMode = 2;
    baseline.uiVisible = true;
    baseline.uiMinimized = true;
    REQUIRE( recorder.BeginAtBoundary( diagnostics, 1920, 1080, 41u, baseline, false ).Ok() );

    std::array<uint64_t, 4> words = {};
    words[static_cast<std::size_t>( 'W' ) / 64u] |= uint64_t { 1 } << ( static_cast<unsigned int>( 'W' ) & 63u );
    words[static_cast<std::size_t>( VK_SHIFT ) / 64u] |=
        uint64_t { 1 } << ( static_cast<unsigned int>( VK_SHIFT ) & 63u );
    words[static_cast<std::size_t>( VK_F8 ) / 64u] |= uint64_t { 1 } << ( static_cast<unsigned int>( VK_F8 ) & 63u );
    InteractionAutomationInputSample input;
    input.keyWords = words;
    input.clientX = 0;
    input.clientY = 1079;
    input.hasClientPosition = true;
    input.appFocused = true;
    input.leftDown = true;
    input.middleDown = true;
    input.wheelDelta = -120;
    input.rawMouseX = 17;
    input.rawMouseY = -9;
    recorder.CapturePendingTurn( 1.0 / 120.0, 1920, 1080, input );
    REQUIRE( recorder.AdvanceBoundary( diagnostics, 41u ).Ok() );
    REQUIRE( recorder.StopAndSave( diagnostics, "operator" ).Ok() );

    const Json root = ReadJson( manifest );
    CHECK( root["format"] == "skullbonez.interaction-recording" );
    CHECK( root["complete"] == true );
    CHECK( root["turnCount"] == 1u );
    CHECK( root["frames"][0]["pointer"][0].get<float>() == doctest::Approx( 0.0f ) );
    CHECK( root["frames"][0]["pointer"][1].get<float>() == doctest::Approx( 1.0f ) );
    CHECK( root["frames"][0]["middle"] == true );
    CHECK( root["frames"][0]["wheel"] == -120 );
    CHECK( root["frames"][0]["rawMouse"][0] == 17 );
    CHECK( root["frames"][0]["rawMouse"][1] == -9 );
    CHECK( root["baseline"]["replay"]["liveAdvanceHeld"] == false );
    CHECK( root["baseline"]["replay"]["causeInspection"]["mode"] == 0 );
    CHECK( root["baseline"]["replay"]["causeInspection"]["selectedRow"] == -1 );
    const std::string f8Word = root["frames"][0]["keys"][static_cast<std::size_t>( VK_F8 ) / 64u].get<std::string>();
    const uint64_t serialized = std::stoull( f8Word, nullptr, 16 );
    CHECK( ( serialized & ( uint64_t { 1 } << ( static_cast<unsigned int>( VK_F8 ) & 63u ) ) ) == 0u );
    CHECK( std::filesystem::is_regular_file( manifest.parent_path() / root["scene"]["path"].get<std::string>() ) );
}

TEST_CASE( "Interaction recorder saves only the valid prefix when the scene generation changes" )
{
    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    InteractionAutomationRecorder recorder;
    const std::filesystem::path manifest = PrepareRecorderTest( "scene_prefix" );
    REQUIRE( recorder.Arm( diagnostics, manifest.string().c_str(), 1 ).Ok() );
    WriteSceneSidecar( recorder );
    REQUIRE( recorder.BeginAtBoundary( diagnostics, 1280, 720, 7u, {}, false ).Ok() );

    InteractionAutomationInputSample transitionInput;
    transitionInput.appFocused = true;
    transitionInput.leftDown = true;
    recorder.CapturePendingTurn( 1.0 / 60.0, 1280, 720, transitionInput );
    REQUIRE( recorder.AdvanceBoundary( diagnostics, 8u ).Ok() );

    const Json root = ReadJson( manifest );
    CHECK( root["stopReason"] == "scene_transition" );
    CHECK( root["turnCount"] == 0u );
    CHECK( root["frames"].empty() );
}

TEST_CASE( "Interaction recorder preserves an unavailable pointer instead of serializing a corner" )
{
    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    InteractionAutomationRecorder recorder;
    const std::filesystem::path manifest = PrepareRecorderTest( "absent_pointer" );
    REQUIRE( recorder.Arm( diagnostics, manifest.string().c_str(), 1 ).Ok() );
    WriteSceneSidecar( recorder );
    REQUIRE( recorder.BeginAtBoundary( diagnostics, 1280, 720, 8u, {}, false ).Ok() );

    InteractionAutomationInputSample input;
    input.appFocused = true;
    input.hasClientPosition = false;
    input.clientX = 511;
    input.clientY = 271;
    recorder.CapturePendingTurn( 1.0 / 60.0, 1280, 720, input, "ui:corner-control" );
    REQUIRE( recorder.AdvanceBoundary( diagnostics, 8u ).Ok() );
    REQUIRE( recorder.StopAndSave( diagnostics, "operator" ).Ok() );

    const Json root = ReadJson( manifest );
    REQUIRE( root["frames"].size() == 1u );
    CHECK_FALSE( root["frames"][0].contains( "pointer" ) );
    CHECK_FALSE( root["frames"][0].contains( "semanticAnchor" ) );
}

TEST_CASE( "Interaction recorder rejects duration limits outside one to sixty minutes" )
{
    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    InteractionAutomationRecorder recorder;
    CHECK_FALSE( recorder.Arm( diagnostics, "TestOutput/interaction_recorder_tests/invalid/interaction.json", 0 ).Ok() );
    CHECK( recorder.Status().state == InteractionRecordingState::Failed );
}

TEST_CASE( "Interaction recorder grows only when a configured second minute is needed" )
{
    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    InteractionAutomationRecorder recorder;
    const std::filesystem::path manifest = PrepareRecorderTest( "minute_growth" );
    REQUIRE( recorder.Arm( diagnostics, manifest.string().c_str(), 2 ).Ok() );
    WriteSceneSidecar( recorder );
    REQUIRE( recorder.BeginAtBoundary( diagnostics, 1280, 720, 9u, {}, false ).Ok() );

    InteractionAutomationInputSample input;
    input.appFocused = true;

    for ( std::size_t turn = 0; turn <= InteractionAutomationRecorder::FRAMES_PER_MINUTE; ++turn )
    {
        recorder.CapturePendingTurn( 0.0, 1280, 720, input );
        REQUIRE( recorder.AdvanceBoundary( diagnostics, 9u ).Ok() );
    }

    CHECK( recorder.Status().frameCapacity == 2u * InteractionAutomationRecorder::FRAMES_PER_MINUTE );
    REQUIRE( recorder.StopAndSave( diagnostics, "operator" ).Ok() );
}

TEST_CASE( "Interaction recorder saves automatically at the accumulated duration limit" )
{
    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    InteractionAutomationRecorder recorder;
    const std::filesystem::path manifest = PrepareRecorderTest( "duration_limit" );
    REQUIRE( recorder.Arm( diagnostics, manifest.string().c_str(), 1 ).Ok() );
    WriteSceneSidecar( recorder );
    REQUIRE( recorder.BeginAtBoundary( diagnostics, 1280, 720, 11u, {}, false ).Ok() );

    InteractionAutomationInputSample input;
    input.appFocused = true;

    for ( int turn = 0; turn < 1'300 && recorder.IsRecording(); ++turn )
    {
        recorder.CapturePendingTurn( 0.05, 1280, 720, input );
        REQUIRE( recorder.AdvanceBoundary( diagnostics, 11u ).Ok() );
    }

    REQUIRE( recorder.Status().state == InteractionRecordingState::Saved );
    CHECK( std::strcmp( recorder.Status().stopReason, "duration_limit" ) == 0 );
    CHECK( recorder.Status().elapsedSeconds == doctest::Approx( 60.0 ) );
    CHECK( std::filesystem::is_regular_file( manifest ) );
}
