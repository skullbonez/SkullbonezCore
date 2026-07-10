//
// File: SkullbonezTests/TestSceneParserUnit.cpp
// Purpose:
//   Lock the smallest authored-scene parse path and recoverable load-error contract.
//
// Mental model:
//   TestScene::LoadFromFile is a data-boundary parser. It turns committed scene
//   JSON into immutable setup records. Runtime callers use the Lane R TryLoad
//   path so malformed files return owner/message diagnostics without escaping.
//
// Glossary:
//   Authored scene: A committed `.scene.json` file used by runtime validation.
//   Parser contract: The observable success data or Lane R failure message that
//     callers depend on.
//
// Invariants:
//   - Full scene files must define at least one camera.
//   - Malformed JSON returns a path-rich Scene/TestSceneParser failure through
//     TestScene::TryLoadFromFile.
//
// Related:
//   - SkullbonezSource/Scene/TestScene.h
//   - SkullbonezSource/Scene/TestSceneParser.cpp
//   - Agentic/Plans/TODO/behavioral-test-depth.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Scene/TestScene.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

using SkullbonezCore::Basics::SbResult;
using SkullbonezCore::Basics::SceneCamera;
using SkullbonezCore::Basics::TestScene;

namespace
{
constexpr const char* kSmallestCommittedScenePath = "SkullbonezData/scenes/terrain_compare.scene.json";

struct TemporaryMalformedSceneFile
{
    const char* path = nullptr;

    TemporaryMalformedSceneFile( const char* fixturePath, const char* contents ) : path( fixturePath )
    {
        std::ofstream output( path );
        if ( !output )
        {
            throw std::runtime_error( "TestSceneParser: failed to create malformed scene fixture" );
        }
        output << contents;
    }

    ~TemporaryMalformedSceneFile()
    {
        std::remove( path );
    }
};

void CheckLoadFailure( const SbResult& result, const char* path, const char* expectedMessage )
{
    CHECK_FALSE( result.ok );
    CHECK( std::string( result.error.owner ) == "Scene/TestSceneParser" );
    const std::string message = result.error.message;
    CHECK( message.find( expectedMessage ) != std::string::npos );
    CHECK( message.find( path ) != std::string::npos );
    CHECK( message.find( "TestScene::LoadFromFile" ) != std::string::npos );
}
} // namespace


TEST_CASE( "TestSceneParser: smallest committed scene parses expected records" )
{
    const TestScene scene = TestScene::LoadFromFile( kSmallestCommittedScenePath );
    TestScene tryScene;
    const SbResult tryLoad = TestScene::TryLoadFromFile( kSmallestCommittedScenePath, tryScene );
    CHECK( tryLoad.ok );
    CHECK( tryScene.GetCameraCount() == 1 );

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


TEST_CASE( "TestSceneParser: malformed JSON reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile malformed( "unit_scene_parser_malformed.scene.json",
                                                 "{ \"format\": \"skullbonez.scene.json\", \"cameras\": [" );
    TestScene scene;
    CheckLoadFailure( TestScene::TryLoadFromFile( malformed.path, scene ), malformed.path, "Invalid JSON" );
}


TEST_CASE( "TestSceneParser: missing camera reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile missingCamera(
        "unit_scene_parser_missing_camera.scene.json",
        R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false})" );
    TestScene scene;
    CheckLoadFailure( TestScene::TryLoadFromFile( missingCamera.path, scene ),
                      missingCamera.path,
                      "at least one camera" );
}


TEST_CASE( "TestSceneParser: wrong member type reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile wrongType(
        "unit_scene_parser_wrong_type.scene.json",
        R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false,"cameras":{}})" );
    TestScene scene;
    CheckLoadFailure( TestScene::TryLoadFromFile( wrongType.path, scene ), wrongType.path, "cameras must be an array" );
}


TEST_CASE( "TestSceneParser: unknown asset instance reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile unknownAsset(
        "unit_scene_parser_unknown_asset.scene.json",
        R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"assetInstances":[{"asset":"missing.asset","name":"ghost","position":[0,0,0]}]})" );
    TestScene scene;
    CheckLoadFailure( TestScene::TryLoadFromFile( unknownAsset.path, scene ),
                      unknownAsset.path,
                      "Unknown asset instance reference" );
}


TEST_CASE( "TestSceneParser: malformed style JSON reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile malformedStyle(
        "unit_scene_parser_malformed_style.style.json",
        "{ \"format\": \"skullbonez.style.json\", \"objectMaterials\": [" );
    TestScene scene;
    CheckLoadFailure( TestScene::TryLoadStyleFromFile( malformedStyle.path, scene ),
                      malformedStyle.path,
                      "Invalid JSON" );
}
