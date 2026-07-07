//
// File: SkullbonezTests/TestSceneParserUnit.cpp
// Purpose:
//   Lock the smallest authored-scene parse path and current malformed JSON error contract.
//
// Mental model:
//   TestScene::LoadFromFile is a data-boundary parser. It turns committed scene
//   JSON into immutable setup records, and today parser failures are surfaced as
//   std::runtime_error messages that include both the path and parse context.
//
// Glossary:
//   Authored scene: A committed `.scene.json` file used by runtime validation.
//   Parser contract: The currently observable success data or failure message
//     that callers depend on until plan 05 moves this lane to SbResult.
//
// Invariants:
//   - Full scene files must define at least one camera.
//   - Malformed JSON currently throws with path-rich TestScene::LoadFromFile context.
//
// Related:
//   - SkullbonezSource/Scene/TestScene.h
//   - SkullbonezSource/Scene/TestSceneParser.cpp
//   - fable_plans/01-unit-test-pyramid-progress.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Scene/TestScene.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

using SkullbonezCore::Basics::SceneCamera;
using SkullbonezCore::Basics::TestScene;

namespace
{
constexpr const char* kSmallestCommittedScenePath = "SkullbonezData/scenes/terrain_compare.scene.json";

struct TemporaryMalformedSceneFile
{
    const char* path = "unit_scene_parser_malformed.scene.json";

    TemporaryMalformedSceneFile()
    {
        std::ofstream output( path );
        if ( !output )
        {
            throw std::runtime_error( "TestSceneParser: failed to create malformed scene fixture" );
        }
        output << "{ \"format\": \"skullbonez.scene.json\", \"cameras\": [";
    }

    ~TemporaryMalformedSceneFile()
    {
        std::remove( path );
    }
};

void CheckMalformedJsonThrowsParserError( const char* path )
{
    bool threw = false;
    try
    {
        (void)TestScene::LoadFromFile( path );
    }
    catch ( const std::runtime_error& e )
    {
        threw = true;
        const std::string message = e.what();
        CHECK( message.find( "Invalid JSON" ) != std::string::npos );
        CHECK( message.find( path ) != std::string::npos );
        CHECK( message.find( "TestScene::LoadFromFile" ) != std::string::npos );
    }
    CHECK( threw );
}
} // namespace


TEST_CASE( "TestSceneParser: smallest committed scene parses expected records" )
{
    const TestScene scene = TestScene::LoadFromFile( kSmallestCommittedScenePath );

    CHECK( scene.GetCameraCount() == 1 );
    CHECK( scene.GetBallCount() == 0 );
    CHECK( scene.GetBoxCount() == 0 );
    CHECK( scene.GetConvexHullCount() == 0 );
    CHECK_FALSE( scene.IsPhysicsEnabled() );
    CHECK_FALSE( scene.IsTextEnabled() );
    CHECK( scene.IsWaterHidden() );
    CHECK( scene.GetScreenshotFrame() == 5 );
    CHECK( std::string( scene.GetScreenshotPath() ) == "TestOutput/terrain_cap.bmp" );

    const SceneCamera& camera = scene.GetCamera( 0 );
    CHECK( std::string( camera.name ) == "main" );
    CHECK( camera.m_position.x == doctest::Approx( 200.0f ) );
    CHECK( camera.m_position.y == doctest::Approx( 500.0f ) );
    CHECK( camera.m_position.z == doctest::Approx( 1800.0f ) );
    CHECK( camera.view.x == doctest::Approx( 1400.0f ) );
    CHECK( camera.view.y == doctest::Approx( 0.0f ) );
    CHECK( camera.view.z == doctest::Approx( 200.0f ) );
}


TEST_CASE( "TestSceneParser: malformed JSON reports the current throwing contract" )
{
    // Why: plan 05 will update this expectation when parser failures move from
    // exceptions to SbResult, so this test names the current behavior directly.
    const TemporaryMalformedSceneFile malformed;
    CheckMalformedJsonThrowsParserError( malformed.path );
}
