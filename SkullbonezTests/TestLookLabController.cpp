/*
File: SkullbonezTests/TestLookLabController.cpp
Purpose:
  Proves Look Lab scene resolution, snapshot publication, and lifecycle ownership.

Summary:
  CPU-only tests resolve deterministic candidates against sentinel scene values,
  publish the exact detached Scene snapshot, and drive bundle transactions in a
  temporary root without constructing simulation or renderer owners.

Glossary:
  Preservation sentinel: Deliberately unusual live value used to detect whether
    candidate resolution copied or accidentally regenerated owned scene policy.

Invariants:
  - Tests compare typed values; candidate padding is never inspected.
  - The resolution API can receive only cinematic presentation values, so it
    cannot read camera, topology, asset, physics, clock, path, or scene RNG state.
  - Transaction tests inspect atomic receipt revisions and remove their bounded
    TestOutput root before returning.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabController.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Style.cpp
  - Agentic/Reports/2026-08-01/look-lab-random-style-authoring-ll0-census.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Direction/LookLabController.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Scene/StandaloneStyleWriter.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
using namespace SkullbonezCore;

Core::CinematicRenderConfig SentinelPresentation()
{
    Core::CinematicRenderConfig presentation;
    presentation.waterMode = Core::CinematicStyleMode::Water::Off;
    presentation.basinCenterX = 101.25f;
    presentation.basinCenterZ = -202.5f;
    presentation.basinRadiusX = 303.75f;
    presentation.basinRadiusZ = 404.5f;
    presentation.basinFeather = 0.375f;
    presentation.shadow.terrainCasts = false;
    presentation.shadow.objectsCast = true;
    presentation.shadow.terrainReceives = false;
    presentation.shadow.objectsReceive = true;
    presentation.shadow.mapSize = 4096;
    presentation.shadow.pcfRadius = 3;
    presentation.shadow.depthBias = 0.0007f;
    presentation.shadow.slopeBias = 0.0013f;
    presentation.shadow.maxDistance = 3000.0f;
    return presentation;
}

std::string ReadText( const std::filesystem::path& path )
{
    std::ifstream input( path, std::ios::binary );
    return std::string( std::istreambuf_iterator<char>( input ), std::istreambuf_iterator<char>() );
}

TEST_CASE( "Look Lab scene resolution retains geometry quality and process RNG" )
{
    const Core::CinematicRenderConfig active = SentinelPresentation();
    const Runtime::LookLabCandidate raw = Runtime::GenerateLookLabCandidate( 901 );

    std::srand( 1717 );
    const int expectedFirst = std::rand();
    const int expectedSecond = std::rand();
    std::srand( 1717 );
    CHECK( std::rand() == expectedFirst );

    const Runtime::LookLabCandidate resolved = Runtime::ResolveLookLabCandidateForScene( 901, active );
    CHECK( std::rand() == expectedSecond );
    REQUIRE( Runtime::ValidateResolvedLookLabCandidate( resolved, active ) == Runtime::LookLabCandidateIssue::None );
    CHECK( resolved.cinematic.basinCenterX == active.basinCenterX );
    CHECK( resolved.cinematic.basinCenterZ == active.basinCenterZ );
    CHECK( resolved.cinematic.basinRadiusX == active.basinRadiusX );
    CHECK( resolved.cinematic.basinRadiusZ == active.basinRadiusZ );
    CHECK( resolved.cinematic.basinFeather == active.basinFeather );
    CHECK( resolved.cinematic.waterMode == Core::CinematicStyleMode::Water::Off );
    CHECK( resolved.cinematic.shadow.mapSize == active.shadow.mapSize );
    CHECK( resolved.cinematic.shadow.pcfRadius == active.shadow.pcfRadius );
    CHECK( resolved.cinematic.shadow.depthBias == active.shadow.depthBias );
    CHECK( resolved.cinematic.shadow.slopeBias == active.shadow.slopeBias );
    CHECK( resolved.cinematic.shadow.maxDistance == active.shadow.maxDistance );
    CHECK( resolved.cinematic.shadow.terrainCasts == active.shadow.terrainCasts );
    CHECK( resolved.cinematic.shadow.objectsCast == active.shadow.objectsCast );
    CHECK( resolved.cinematic.shadow.terrainReceives == active.shadow.terrainReceives );
    CHECK( resolved.cinematic.shadow.objectsReceive == active.shadow.objectsReceive );
    CHECK( resolved.cinematic.fogStart == doctest::Approx( raw.cinematic.fogStart * 2.0f ) );
    CHECK( resolved.cinematic.fogEnd == doctest::Approx( raw.cinematic.fogEnd * 2.0f ) );

    const Scene::StandaloneStyleSnapshot snapshot = Runtime::BuildLookLabStyleSnapshot( resolved );
    REQUIRE( snapshot.materialRules.size() == resolved.materialRules.size() );
    CHECK( snapshot.cinematic.basinCenterX == active.basinCenterX );
    for ( size_t index = 0; index < snapshot.materialRules.size(); ++index )
    {
        CHECK( std::strcmp( snapshot.materialRules[index].target.data(), resolved.materialRules[index].target.data() ) == 0 );
        CHECK( snapshot.materialRules[index].material.kind == resolved.materialRules[index].material.kind );
        CHECK( snapshot.materialRules[index].material.baseColor[0] == resolved.materialRules[index].material.baseColor[0] );
    }
}

TEST_CASE( "Look Lab controller publishes one candidate and clears it for scene transition" )
{
    const Core::CinematicRenderConfig active = SentinelPresentation();
    Runtime::LookLabController lookLab;

    REQUIRE( lookLab.ResolveSeed( 0xabc123ull, active ) );
    REQUIRE( lookLab.HasCandidate() );
    const Runtime::LookLabCandidate expected =
        Runtime::ResolveLookLabCandidateForScene( 0xabc123ull, SentinelPresentation() );
    CHECK( lookLab.BuildCurrentSnapshot().cinematic.exposure == expected.cinematic.exposure );
    CHECK( lookLab.Status().kind == Runtime::LookLabStatusKind::Resolved );
    CHECK( lookLab.Status().seed == 0xabc123ull );
    CHECK( lookLab.Status().fingerprint == Runtime::FingerprintLookLabCandidate( expected ) );

    const Scene::StandaloneStyleSnapshot snapshot = lookLab.BuildCurrentSnapshot();
    CHECK( snapshot.cinematic.exposure == expected.cinematic.exposure );
    CHECK( snapshot.cinematic.basinCenterX == active.basinCenterX );
    REQUIRE( snapshot.materialRules.size() == Runtime::LOOK_LAB_MATERIAL_RULE_COUNT );
    lookLab.MarkApplied();
    CHECK( lookLab.Status().kind == Runtime::LookLabStatusKind::Applied );

    // A rejected reroll reports failure but preserves the last successfully
    // resolved candidate so F11 cannot observe half-published values.
    Core::CinematicRenderConfig invalidActive = active;
    invalidActive.shadow.mapSize = 1;
    const uint64_t acceptedFingerprint = lookLab.Status().fingerprint;
    CHECK_FALSE( lookLab.ResolveSeed( 22, invalidActive ) );
    CHECK( lookLab.Status().fingerprint == acceptedFingerprint );
    CHECK( lookLab.Status().kind == Runtime::LookLabStatusKind::Rejected );
    CHECK( lookLab.Status().hasCandidate );

    lookLab.ClearForSceneTransition();
    CHECK_FALSE( lookLab.HasCandidate() );
    CHECK( lookLab.Status().kind == Runtime::LookLabStatusKind::ClearedForSceneLoad );
    CHECK( lookLab.Status().recipe == Runtime::LookLabRecipeFamily::GoldenRealism );
}

TEST_CASE( "Look Lab save transaction publishes pending final failed and cancelled receipts" )
{
    namespace fs = std::filesystem;
    Core::SbDiagnosticStore diagnostics;
    const fs::path root = fs::current_path() / "TestOutput" / "look_lab_controller_transactions";
    std::error_code filesystemError;
    fs::remove_all( root, filesystemError );
    REQUIRE_FALSE( filesystemError );

    const auto request = [&]( const char* timestamp )
    {
        Runtime::LookLabSaveRequest value;
        static std::string rootText = root.string();
        value.lookLabRoot = rootText.c_str();
        value.localTimestamp = timestamp;
        value.utcOffsetMinutes = 600;
        value.sourceScenePath = "SkullbonezData/scenes/varied.scene.json";
        value.sourceSceneDisplayName = "Varied";
        return value;
    };

    Runtime::LookLabController lookLab;
    const Runtime::LookLabSaveStartResult beforeCandidate = lookLab.BeginSave( diagnostics, request( "2026-08-01_12-00-00" ) );
    CHECK_FALSE( beforeCandidate.status.Ok() );
    CHECK_FALSE( fs::exists( root ) );

    REQUIRE( lookLab.ResolveSeed( 0x1234ull, SentinelPresentation() ) );
    const uint64_t originalFingerprint = lookLab.Status().fingerprint;
    const Runtime::LookLabSaveStartResult pending = lookLab.BeginSave( diagnostics, request( "2026-08-01_12-00-01" ) );
    REQUIRE( pending.status.Ok() );
    REQUIRE( pending.captureRequested );
    CHECK( pending.captureToken != 0 );
    CHECK( lookLab.HasPendingSave() );
    CHECK( lookLab.Status().savePending );
    const fs::path bundle = lookLab.Status().bundleDirectory.data();
    CHECK( fs::exists( bundle / "look.style.json" ) );
    CHECK( fs::exists( bundle / "look.txt" ) );
    CHECK_FALSE( fs::exists( bundle / "look.png" ) );
    CHECK( ReadText( bundle / "look.txt" ).find( "screenshot_status=pending\n" ) != std::string::npos );

    const Runtime::LookLabSaveStartResult duplicate = lookLab.BeginSave( diagnostics, request( "2026-08-01_12-00-02" ) );
    CHECK_FALSE( duplicate.status.Ok() );
    CHECK_FALSE( fs::exists( root / "2026-08-01_12-00-02_seed_0000000000001234" ) );
    CHECK_FALSE( lookLab.ResolveSeed( 99, SentinelPresentation() ) );
    CHECK( lookLab.Status().fingerprint == originalFingerprint );

    const Core::SbResult mismatched =
        lookLab.CompleteSaveCapture( diagnostics, pending.captureToken + 1u, Core::SbResult::Success() );
    CHECK_FALSE( mismatched.Ok() );
    CHECK( lookLab.HasPendingSave() );
    {
        std::ofstream screenshot( bundle / "look.png", std::ios::binary );
        screenshot.write( "PNG", 3 );
    }

    REQUIRE( lookLab.CompleteSaveCapture( diagnostics, pending.captureToken, Core::SbResult::Success() ).Ok() );
    CHECK_FALSE( lookLab.HasPendingSave() );
    CHECK( lookLab.Status().kind == Runtime::LookLabStatusKind::BundleSaved );
    CHECK( ReadText( bundle / "look.txt" ).find( "screenshot_status=saved\n" ) != std::string::npos );

    Runtime::LookLabController failedCapture;
    REQUIRE( failedCapture.ResolveSeed( 0x2234ull, SentinelPresentation() ) );
    const Runtime::LookLabSaveStartResult failing = failedCapture.BeginSave( diagnostics, request( "2026-08-01_12-00-03" ) );
    REQUIRE( failing.status.Ok() );
    const Core::SbResult captureFailure = diagnostics.Failure( "Test/Capture", "readback failed" );
    const Core::SbResult failureCompletion =
        failedCapture.CompleteSaveCapture( diagnostics, failing.captureToken, captureFailure );
    CHECK_FALSE( failureCompletion.Ok() );
    CHECK( failedCapture.Status().kind == Runtime::LookLabStatusKind::BundlePartialFailure );
    CHECK( fs::exists( fs::path( failedCapture.Status().bundleDirectory.data() ) / "look.style.json" ) );
    CHECK_FALSE( fs::exists( fs::path( failedCapture.Status().bundleDirectory.data() ) / "look.png" ) );
    CHECK( ReadText( fs::path( failedCapture.Status().bundleDirectory.data() ) / "look.txt" )
               .find( "screenshot_status=failed\n" ) != std::string::npos );

    Runtime::LookLabController cancelled;
    REQUIRE( cancelled.ResolveSeed( 0x3234ull, SentinelPresentation() ) );
    const Runtime::LookLabSaveStartResult cancelling = cancelled.BeginSave( diagnostics, request( "2026-08-01_12-00-04" ) );
    REQUIRE( cancelling.status.Ok() );
    REQUIRE( cancelled.CancelPendingSave( diagnostics, "scene transition cancelled screenshot" ).Ok() );
    CHECK( cancelled.Status().kind == Runtime::LookLabStatusKind::BundleCancelled );
    CHECK( ReadText( fs::path( cancelled.Status().bundleDirectory.data() ) / "look.txt" )
               .find( "screenshot_status=cancelled\n" ) != std::string::npos );

    Runtime::LookLabController collision;
    REQUIRE( collision.ResolveSeed( 0x3234ull, SentinelPresentation() ) );
    const Runtime::LookLabSaveStartResult collided = collision.BeginSave( diagnostics, request( "2026-08-01_12-00-04" ) );
    CHECK_FALSE( collided.status.Ok() );

    Runtime::LookLabController seedSource;
    const uint64_t firstAuthoringSeed = seedSource.NextAuthoringSeed();
    CHECK( seedSource.NextAuthoringSeed() != firstAuthoringSeed );

    fs::remove_all( root, filesystemError );
    CHECK_FALSE( filesystemError );
}
} // namespace
