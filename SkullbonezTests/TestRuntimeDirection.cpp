/*
File: SkullbonezTests/TestRuntimeDirection.cpp
Purpose:
  Proves bounded Demo Director and live-style control decisions.

Summary:
  Focused CPU fixtures drive the production shot-list loader/playback helpers,
  automation path admission, and live-control polling without constructing App,
  renderer, or native-window owners.

Invariants:
  - Invalid shot-list input preserves the caller's prior fixed record.
  - Playback fixtures use authored state directly and never require a camera owner.
  - Live-control path failures are detected before a bounded destination changes.
  - Physical capture lines cannot publish a bounded prefix of an overlong command.

Related:
  - SkullbonezSource/Runtime/Direction/DemoDirector.cpp
  - SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp
  - SkullbonezSource/Runtime/Direction/LiveStyleController.cpp
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Direction/DemoDirectorPersistence.h"
#include "../SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h"
#include "../SkullbonezSource/Runtime/Direction/LiveStyleController.h"
#include "../SkullbonezSource/Runtime/Automation/InteractionAutomationController.h"
#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Scene/AuthoredScene.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
using namespace SkullbonezCore;

bool WriteText( const std::filesystem::path& path, const char* text )
{
    std::ofstream output( path, std::ios::binary | std::ios::trunc );
    output << text;
    output.flush();
    return output.good();
}

Runtime::DemoDirectorPlaybackState TwoPhaseDirector()
{
    Runtime::DemoDirectorPlaybackState director;
    director.hasActiveShotList = true;
    director.activeShotList.phaseCount = 2;
    director.currentPhaseIndex = 0;
    director.activeShotList.phases[0].camera.eye = Math::Vector::Vector3( 0.0f, 0.0f, 0.0f );
    director.activeShotList.phases[0].camera.view = Math::Vector::Vector3( 0.0f, 0.0f, -1.0f );
    director.activeShotList.phases[0].camera.up = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    director.activeShotList.phases[1].camera.eye = Math::Vector::Vector3( 10.0f, 0.0f, 0.0f );
    director.activeShotList.phases[1].camera.view = Math::Vector::Vector3( 10.0f, 0.0f, -1.0f );
    director.activeShotList.phases[1].camera.up = Math::Vector::Vector3( 0.0f, -1.0f, 0.0f );
    director.blendStartPose = director.activeShotList.phases[0].camera;
    return director;
}

TEST_CASE( "Demo shot-list loader rejects nonfinite and degenerate camera input atomically" )
{
    namespace fs = std::filesystem;
    const fs::path root = fs::current_path() / "TestOutput" / "runtime_direction";
    std::error_code filesystemError;
    fs::remove_all( root, filesystemError );
    REQUIRE_FALSE( filesystemError );
    fs::create_directories( root, filesystemError );
    REQUIRE_FALSE( filesystemError );

    Runtime::DemoShotList retained;
    retained.phaseCount = 7;
    strcpy_s( retained.phases[0].name, "retained" );

    const fs::path overflow = root / "overflow.shot.json";
    REQUIRE( WriteText( overflow,
                        R"({"format":"skullbonez.shot.json","version":1,"phases":[{"name":"bad","camera":{"position":[1e39,0,0],"view":[0,0,-1],"up":[0,1,0]}}]})" ) );
    CHECK_FALSE( Runtime::LoadDemoShotList( overflow.string().c_str(), retained ) );
    CHECK( retained.phaseCount == 7 );
    CHECK( std::strcmp( retained.phases[0].name, "retained" ) == 0 );

    const fs::path degenerate = root / "degenerate.shot.json";
    REQUIRE( WriteText( degenerate,
                        R"({"format":"skullbonez.shot.json","version":1,"phases":[{"name":"bad","camera":{"position":[2,3,4],"view":[2,3,4],"up":[0,1,0]}}]})" ) );
    CHECK_FALSE( Runtime::LoadDemoShotList( degenerate.string().c_str(), retained ) );
    CHECK( retained.phaseCount == 7 );

    fs::remove_all( root, filesystemError );
    CHECK_FALSE( filesystemError );
}

TEST_CASE( "Demo playback keeps opposite up vectors continuous through midpoint" )
{
    const auto sample = []( float elapsed )
    {
        Runtime::DemoDirectorPlaybackState director = TwoPhaseDirector();
        director.activeShotList.phases[0].camera.up = Math::Vector::Vector3( 0.0f, -1.0f, 0.0f );
        director.activeShotList.phases[0].blendInSeconds = 2.0f;
        director.blendStartPose = TwoPhaseDirector().activeShotList.phases[0].camera;
        return Runtime::DemoDirectorPlayback::Tick( director, true, {}, director.blendStartPose, elapsed );
    };

    const Runtime::DemoDirectorTickResult result = sample( 1.0f );
    REQUIRE( result.applyCameraPose );
    CHECK( std::isfinite( result.cameraPose.up.x ) );
    CHECK( std::isfinite( result.cameraPose.up.y ) );
    CHECK( std::isfinite( result.cameraPose.up.z ) );
    CHECK( Math::Vector::VectorMag( result.cameraPose.up ) == doctest::Approx( 1.0f ).epsilon( 1.0e-5 ) );
    CHECK( std::fabs( result.cameraPose.up.z ) > 0.9f );

    Math::Vector::Vector3 before = sample( 0.98f ).cameraPose.up;
    Math::Vector::Vector3 after = sample( 1.02f ).cameraPose.up;
    REQUIRE( before.TryNormalise() );
    REQUIRE( after.TryNormalise() );
    CHECK( Math::Vector::Dot( before, after ) > 0.99f );
}

TEST_CASE( "Demo timer carries overshoot into the next phase and its blend" )
{
    Runtime::DemoDirectorPlaybackState director = TwoPhaseDirector();
    Runtime::DemoPhase& first = director.activeShotList.phases[0];
    first.advance = Runtime::PhaseAdvance::Timer;
    first.timerSeconds = 1.0f;
    first.blendInSeconds = 0.0f;
    director.activeShotList.phases[1].blendInSeconds = 2.0f;

    const Runtime::DemoDirectorTickResult result = Runtime::DemoDirectorPlayback::Tick(
        director, true, {}, first.camera, 1.25f );
    REQUIRE( result.applyCameraPose );
    CHECK( director.currentPhaseIndex == 1 );
    CHECK( director.phaseElapsedSeconds == doctest::Approx( 0.25f ) );
    CHECK( director.blendElapsedSeconds == doctest::Approx( 0.25f ) );
    CHECK( result.cameraPose.eye.x == doctest::Approx( 0.0f ) );

    const Runtime::DemoDirectorTickResult nextPhase = Runtime::DemoDirectorPlayback::Tick(
        director, true, {}, result.cameraPose, 0.0f );
    REQUIRE( nextPhase.applyCameraPose );
    CHECK( nextPhase.cameraPose.eye.x == doctest::Approx( 1.25f ) );
}

TEST_CASE( "Demo phase style retries after a recoverable application failure" )
{
    Runtime::DemoDirectorPlaybackState director = TwoPhaseDirector();
    director.activeShotList.phaseCount = 1;
    strcpy_s( director.activeShotList.phases[0].stylePath, "SkullbonezData/styles/retry.style.json" );

    Runtime::DemoDirectorTickResult first = Runtime::DemoDirectorPlayback::Tick(
        director, true, {}, director.blendStartPose, 0.0f );
    REQUIRE( first.applyStyle );
    Runtime::DemoDirectorPlayback::CompleteStyleApplication( director, false, "temporary read failure" );
    Runtime::DemoDirectorTickResult retry = Runtime::DemoDirectorPlayback::Tick(
        director, true, {}, director.blendStartPose, 0.0f );
    REQUIRE( retry.applyStyle );
    CHECK( std::strcmp( retry.stylePath, first.stylePath ) == 0 );

    Runtime::DemoDirectorPlayback::CompleteStyleApplication( director, true, nullptr );
    CHECK_FALSE( Runtime::DemoDirectorPlayback::Tick( director, true, {}, director.blendStartPose, 0.0f ).applyStyle );
}

TEST_CASE( "Demo playback retains long loaded paths without truncation" )
{
    Runtime::DemoDirectorPlaybackState director;
    const std::string longPath = std::string( 320u, 'p' ) + ".shot.json";
    REQUIRE( Runtime::DemoDirectorPlayback::TryRetainShotListPath( director, longPath.c_str() ) );
    CHECK( std::strcmp( director.activeShotListPath, longPath.c_str() ) == 0 );

    const std::string oversized( Runtime::DemoDirectorPlaybackState::SHOT_LIST_PATH_BYTES, 'x' );
    CHECK_FALSE( Runtime::DemoDirectorPlayback::TryRetainShotListPath( director, oversized.c_str() ) );
    CHECK( std::strcmp( director.activeShotListPath, longPath.c_str() ) == 0 );

    Runtime::RunInteractionAutomationAction action;
    REQUIRE( Runtime::TryRetainInteractionShotListPath( action, longPath ) );
    CHECK( std::strcmp( action.directorShotListPath, longPath.c_str() ) == 0 );
    CHECK_FALSE( Runtime::TryRetainInteractionShotListPath( action, oversized ) );
    CHECK( std::strcmp( action.directorShotListPath, longPath.c_str() ) == 0 );
    const std::string embeddedNull( "different\0suffix", 16u );
    CHECK_FALSE( Runtime::TryRetainInteractionShotListPath( action, embeddedNull ) );
    CHECK( std::strcmp( action.directorShotListPath, longPath.c_str() ) == 0 );

    Runtime::RunInteractionAutomationReportAction report;
    REQUIRE( Runtime::TryRetainInteractionAutomationReportTarget( report, action.directorShotListPath ) );
    CHECK( std::strcmp( report.target, longPath.c_str() ) == 0 );
}

TEST_CASE( "Live style policies retain failed stamps and reject oversized paths" )
{
    using namespace Runtime;
    CHECK( ResolveLiveStyleRetainedStamp( 17u, 29u, false ) == 17u );
    CHECK( ResolveLiveStyleRetainedStamp( 17u, 29u, true ) == 29u );

    const std::string longestDirectory( LIVE_STYLE_DIRECTORY_CAPACITY - 1u, 'd' );
    const LiveStyleControlPaths exact = ResolveLiveStyleControlPaths( longestDirectory.c_str() );
    REQUIRE( exact.valid );
    CHECK( std::strlen( exact.directory.data() ) == LIVE_STYLE_DIRECTORY_CAPACITY - 1u );
    CHECK( std::string( exact.style.data() ).ends_with( "live.style.json" ) );

    const std::string oversizedDirectory( LIVE_STYLE_DIRECTORY_CAPACITY, 'd' );
    CHECK_FALSE( ResolveLiveStyleControlPaths( oversizedDirectory.c_str() ).valid );

    char destination[12] = "unchanged";
    CHECK_FALSE( TryBuildLiveStylePath( "12345678", "tail", destination, sizeof( destination ) ) );
    CHECK( std::strcmp( destination, "unchanged" ) == 0 );

    char screenshot[LIVE_STYLE_SCREENSHOT_PATH_CAPACITY] = {};
    const std::string fittingLeaf( LIVE_STYLE_SCREENSHOT_PATH_CAPACITY - 3u, 's' );
    CHECK( TryBuildLiveStylePath( "x", fittingLeaf.c_str(), screenshot, sizeof( screenshot ) ) );
    const std::string overflowingLeaf( LIVE_STYLE_SCREENSHOT_PATH_CAPACITY - 2u, 's' );
    CHECK_FALSE( TryBuildLiveStylePath( "x", overflowingLeaf.c_str(), screenshot, sizeof( screenshot ) ) );
}

TEST_CASE( "Live style capture polling rejects an overlong physical command without publishing its prefix" )
{
    namespace fs = std::filesystem;
    const fs::path root = fs::current_path() / "TestOutput" / "runtime_direction_live_style";
    std::error_code filesystemError;
    fs::remove_all( root, filesystemError );
    REQUIRE_FALSE( filesystemError );
    fs::create_directories( root, filesystemError );
    REQUIRE_FALSE( filesystemError );

    Runtime::LiveStyleController controller;
    REQUIRE( controller.ConfigureDirectory( root.string().c_str() ) );
    const std::string overlongAbsolutePath = "C:\\" + std::string( Runtime::LIVE_STYLE_SCREENSHOT_PATH_CAPACITY, 'x' );
    REQUIRE( WriteText( root / "capture.txt", overlongAbsolutePath.c_str() ) );

    Core::SbDiagnosticStore diagnostics;
    Assets::AssetSystem assets;
    Runtime::AuthoredScene style;
    CHECK_FALSE( controller.Poll( diagnostics, assets, style ) );
    CHECK_FALSE( controller.HasPendingCapture() );
    CHECK( controller.PendingScreenshotPath()[0] == '\0' );

    fs::remove_all( root, filesystemError );
    CHECK_FALSE( filesystemError );
}
} // namespace
