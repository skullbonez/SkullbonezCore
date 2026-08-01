/*
File: SkullbonezTests/TestLookLabController.cpp
Purpose:
  Proves Look Lab scene resolution, snapshot publication, and lifecycle ownership.

Summary:
  CPU-only tests resolve deterministic candidates against sentinel scene values,
  publish the exact detached Scene snapshot, and verify bounded controller state
  and lifecycle clearing without constructing simulation or renderer owners.

Glossary:
  Preservation sentinel: Deliberately unusual live value used to detect whether
    candidate resolution copied or accidentally regenerated owned scene policy.

Invariants:
  - Tests compare typed values; candidate padding is never inspected.
  - The resolution API can receive only cinematic presentation values, so it
    cannot read camera, topology, asset, physics, clock, path, or scene RNG state.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabController.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Style.cpp
  - Agentic/Reports/2026-08-01/look-lab-random-style-authoring-ll0-census.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Direction/LookLabController.h"
#include "../SkullbonezSource/Scene/StandaloneStyleWriter.h"

#include <cstdlib>
#include <cstring>

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

TEST_CASE( "Look Lab controller publishes one candidate and clears it on scene load" )
{
    const Core::CinematicRenderConfig active = SentinelPresentation();
    Runtime::LookLabController lookLab;

    REQUIRE( lookLab.ResolveSeed( 0xabc123ull, active ) );
    REQUIRE( lookLab.HasCandidate() );
    const Runtime::LookLabCandidate* candidate = lookLab.CurrentCandidate();
    REQUIRE( candidate != nullptr );
    CHECK( Runtime::EncodeLookLabCandidateCanonical( *candidate ) ==
           Runtime::EncodeLookLabCandidateCanonical( Runtime::ResolveLookLabCandidateForScene( 0xabc123ull,
                                                                                               SentinelPresentation() ) ) );
    CHECK( lookLab.Status().kind == Runtime::LookLabStatusKind::Resolved );
    CHECK( lookLab.Status().seed == 0xabc123ull );
    CHECK( lookLab.Status().fingerprint == Runtime::FingerprintLookLabCandidate( *candidate ) );

    const Scene::StandaloneStyleSnapshot snapshot = lookLab.BuildCurrentSnapshot();
    CHECK( snapshot.cinematic.exposure == candidate->cinematic.exposure );
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
    REQUIRE( lookLab.CurrentCandidate() != nullptr );
    CHECK( Runtime::FingerprintLookLabCandidate( *lookLab.CurrentCandidate() ) == acceptedFingerprint );
    CHECK( lookLab.Status().kind == Runtime::LookLabStatusKind::Rejected );
    CHECK( lookLab.Status().hasCandidate );

    lookLab.ClearForSceneTransition();
    CHECK_FALSE( lookLab.HasCandidate() );
    CHECK( lookLab.Status().kind == Runtime::LookLabStatusKind::ClearedForSceneLoad );
    CHECK( lookLab.Status().recipe == Runtime::LookLabRecipeFamily::GoldenRealism );

    REQUIRE( lookLab.ResolveSeed( 0xabc123ull, active ) );

    Runtime::SceneLifecyclePacket cleared;
    cleared.generation = 1;
    cleared.event = Runtime::SceneRuntimeLifecycleEvent::AfterSceneCleared;
    lookLab.ObserveSceneLifecycle( cleared );
    CHECK_FALSE( lookLab.HasCandidate() );
    CHECK( lookLab.Status().kind == Runtime::LookLabStatusKind::ClearedForSceneLoad );
    CHECK( lookLab.Status().seed == 0 );

    // Repeated observations of one generation are inert.
    lookLab.ObserveSceneLifecycle( cleared );
    CHECK_FALSE( lookLab.HasCandidate() );
}
} // namespace
