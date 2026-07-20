//
// File: SkullbonezTests/TestSceneParserUnit.cpp
// Purpose:
//   Lock the smallest authored-scene parse path and recoverable load-error contract.
//
// Summary:
//   AuthoredScene::LoadFromFile is a data-boundary parser. It turns committed scene
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
//   - Malformed JSON returns a path-rich Scene/AuthoredSceneParser failure through
//     AuthoredScene::TryLoadFromFile.
//
// Related:
//   - SkullbonezSource/Scene/AuthoredScene.h
//   - SkullbonezSource/Scene/AuthoredSceneParser.cpp
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Scene/AuthoredScene.h"
#include "../SkullbonezSource/Gameplay/TornadoField.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

using SkullbonezCore::Core::SbResult;
using SkullbonezCore::Runtime::SceneCamera;
using SkullbonezCore::Runtime::SceneObjectGroupKind;
using SkullbonezCore::Runtime::AuthoredScene;

namespace
{
constexpr const char* kSmallestCommittedScenePath = "SkullbonezData/scenes/terrain_compare.scene.json";
constexpr const char* kVersionedAssetScene =
    R"({"format":"skullbonez.scene.json","version":2,"physics":false,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"assetLibraries":["unit_versioned.assets.json"]})";

struct TemporaryMalformedSceneFile
{
    const char* path = nullptr;

    TemporaryMalformedSceneFile( const char* fixturePath, const std::string& contents ) : path( fixturePath )
    {
        std::ofstream output( path );
        if ( !output )
        {
            throw std::runtime_error( "AuthoredSceneParser: failed to create malformed scene fixture" );
        }
        output << contents;
    }

    ~TemporaryMalformedSceneFile()
    {
        std::remove( path );
    }
};

void CheckLoadFailure( const SkullbonezCore::Core::SbResult& result, const char* path, const char* expectedMessage )
{
    CHECK_FALSE( result.ok );
    CHECK( std::string( result.error.owner ) == "Scene/AuthoredSceneParser" );
    const std::string message = result.error.message;
    CHECK( message.find( expectedMessage ) != std::string::npos );
    CHECK( message.find( path ) != std::string::npos );
    CHECK( message.find( "AuthoredScene::LoadFromFile" ) != std::string::npos );
}

std::string BuildOverCapacityTornadoScene()
{
    std::string scene =
        R"({"format":"skullbonez.scene.json","version":2,"physics":true,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"tornadoSystem":{"vortices":[)";
    for ( std::size_t index = 0; index <= SkullbonezCore::Gameplay::MAX_TORNADO_ACTIVE_FORCE_FIELDS; ++index )
    {
        if ( index != 0u )
        {
            scene += ',';
        }
        scene += R"({"center":[0,0,0]})";
    }
    scene += "]}}";
    return scene;
}
} // namespace


TEST_CASE( "AuthoredSceneParser: smallest committed scene parses expected records" )
{
    const AuthoredScene scene = AuthoredScene::LoadFromFile( kSmallestCommittedScenePath );
    AuthoredScene tryScene;
    const SkullbonezCore::Core::SbResult tryLoad = AuthoredScene::TryLoadFromFile( kSmallestCommittedScenePath, tryScene );
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


TEST_CASE( "AuthoredSceneParser: malformed JSON reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile malformed( "unit_scene_parser_malformed.scene.json",
                                                 "{ \"format\": \"skullbonez.scene.json\", \"cameras\": [" );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( malformed.path, scene ), malformed.path, "Invalid JSON" );
}


TEST_CASE( "AuthoredSceneParser: legacy releasable trees resolve stable root ids" )
{
    const AuthoredScene scene = AuthoredScene::LoadFromFile( "SkullbonezData/scenes/nature_hull_assets.scene.json" );
    int groupedHullCount = 0;
    for ( int index = 0; index < scene.GetConvexHullCount(); ++index )
    {
        const auto& hull = scene.GetConvexHull( index );
        if ( hull.group.kind != SceneObjectGroupKind::ReleasableTree )
        {
            continue;
        }
        ++groupedHullCount;
        CHECK( hull.group.rootObjectId.IsValid() );

        bool foundRoot = false;
        for ( int candidate = 0; candidate < scene.GetConvexHullCount(); ++candidate )
        {
            if ( scene.GetConvexHull( candidate ).sceneObjectId.value == hull.group.rootObjectId.value )
            {
                foundRoot = true;
                break;
            }
        }
        CHECK( foundRoot );
    }
    CHECK( groupedHullCount > 0 );
}


TEST_CASE( "AuthoredSceneParser: missing behavior-group root is a recoverable parse failure" )
{
    const TemporaryMalformedSceneFile missingGroupRoot(
        "unit_scene_parser_missing_group_root.scene.json",
        R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"objects":[{"type":"convexHull","name":"tree_child","hull":"pyramid","position":[0,0,0],"restitution":0.1,"objectGroup":{"kind":"releasableTree","root":"missing_root","part":1}}]})" );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( missingGroupRoot.path, scene ),
                      missingGroupRoot.path,
                      "does not name an object" );
}


TEST_CASE( "AuthoredSceneParser: missing camera reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile missingCamera(
        "unit_scene_parser_missing_camera.scene.json",
        R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false})" );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( missingCamera.path, scene ),
                      missingCamera.path,
                      "at least one camera" );
}


TEST_CASE( "AuthoredSceneParser: tornado fields over the fixed gameplay capacity fail recoverably" )
{
    const TemporaryMalformedSceneFile overCapacity( "unit_scene_parser_tornado_capacity.scene.json",
                                                     BuildOverCapacityTornadoScene() );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( overCapacity.path, scene ),
                      overCapacity.path,
                      "Gameplay.TornadoGameplay tornadoSystem.vortices requested 65, capacity is 64" );
}


TEST_CASE( "AuthoredSceneParser: wrong member type reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile wrongType(
        "unit_scene_parser_wrong_type.scene.json",
        R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false,"cameras":{}})" );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( wrongType.path, scene ), wrongType.path, "cameras must be an array" );
}


TEST_CASE( "AuthoredSceneParser: unknown asset instance reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile unknownAsset(
        "unit_scene_parser_unknown_asset.scene.json",
        R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"assetInstances":[{"asset":"missing.asset","name":"ghost","position":[0,0,0]}]})" );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( unknownAsset.path, scene ),
                      unknownAsset.path,
                      "Unknown asset instance reference" );
}


TEST_CASE( "AuthoredSceneParser: legacy and current asset-library versions load" )
{
    const TemporaryMalformedSceneFile sceneFile( "unit_versioned_asset.scene.json", kVersionedAssetScene );

    {
        const TemporaryMalformedSceneFile legacyLibrary( "unit_versioned.assets.json",
                                                         R"({"format":"skullbonez.asset_library.json","assets":[]})" );
        AuthoredScene scene;
        CHECK( AuthoredScene::TryLoadFromFile( sceneFile.path, scene ).ok );
    }
    {
        const TemporaryMalformedSceneFile currentLibrary(
            "unit_versioned.assets.json",
            R"({"format":"skullbonez.asset_library.json","version":1,"assets":[]})" );
        AuthoredScene scene;
        CHECK( AuthoredScene::TryLoadFromFile( sceneFile.path, scene ).ok );
    }
}


TEST_CASE( "AuthoredSceneParser: future asset-library version is a named recoverable failure" )
{
    const TemporaryMalformedSceneFile sceneFile( "unit_versioned_asset.scene.json", kVersionedAssetScene );
    const TemporaryMalformedSceneFile futureLibrary(
        "unit_versioned.assets.json",
        R"({"format":"skullbonez.asset_library.json","version":2,"assets":[]})" );

    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( sceneFile.path, scene ),
                      futureLibrary.path,
                      "version 2 is newer than current version 1" );
}


TEST_CASE( "AuthoredSceneParser: malformed style JSON reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile malformedStyle(
        "unit_scene_parser_malformed_style.style.json",
        "{ \"format\": \"skullbonez.style.json\", \"objectMaterials\": [" );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadStyleFromFile( malformedStyle.path, scene ),
                      malformedStyle.path,
                      "Invalid JSON" );
}
