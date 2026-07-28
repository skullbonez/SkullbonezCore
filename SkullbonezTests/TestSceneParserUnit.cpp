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
//   - Solar body colours are authored material data, not name-based renderer policy.
//   - Solar scenes use XY orbital coordinates and oblique cameras with Z up,
//     matching the terrain-camera interaction convention after axis remapping.
//   - Deep-space presentation is authored as literal black with no second sun.
//
// Related:
//   - SkullbonezSource/Scene/AuthoredScene.h
//   - SkullbonezSource/Scene/AuthoredSceneParser.cpp
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Scene/AuthoredScene.h"
#include "../SkullbonezSource/Gameplay/TornadoField.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

using SkullbonezCore::Core::SbResult;
using SkullbonezCore::Runtime::AuthoredScene;
using SkullbonezCore::Runtime::SceneCamera;
using SkullbonezCore::Runtime::SceneObjectGroupKind;
using SkullbonezCore::Runtime::SceneObjectMaterialOverride;

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;

constexpr const char* kSmallestCommittedScenePath = "SkullbonezData/scenes/terrain_compare.scene.json";
constexpr const char* kSolarSystemScenePath = "SkullbonezData/scenes/solar_system.scene.json";
constexpr const char* kSolarSlingshotScenePath = "SkullbonezData/scenes/solar_system_mars_slingshot.scene.json";
constexpr const char* kVersionedAssetScene = R"({"format":"skullbonez.scene.json","version":2,"physics":false,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"assetLibraries":["unit_versioned.assets.json"]})";

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
    CHECK_FALSE( result.Ok() );
    CHECK( std::string( result.ErrorOwner() ) == "Scene/AuthoredSceneParser" );
    const std::string message = result.ErrorMessage();
    CHECK( message.find( expectedMessage ) != std::string::npos );
    CHECK( message.find( path ) != std::string::npos );
    CHECK( message.find( "AuthoredScene::LoadFromFile" ) != std::string::npos );
}

std::string BuildOverCapacityTornadoScene()
{
    std::string scene = R"({"format":"skullbonez.scene.json","version":2,"physics":true,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"tornadoSystem":{"vortices":[)";

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
    const AuthoredScene scene = AuthoredScene::LoadFromFile( diagnostics, kSmallestCommittedScenePath );
    AuthoredScene tryScene;
    const SkullbonezCore::Core::SbResult tryLoad = AuthoredScene::TryLoadFromFile( diagnostics, kSmallestCommittedScenePath,
                                                                                   tryScene );

    CHECK( tryLoad.Ok() );
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

TEST_CASE( "AuthoredSceneParser: solar bodies publish their authored colours" )
{
    const AuthoredScene scene = AuthoredScene::LoadFromFile( diagnostics, kSolarSystemScenePath );
    REQUIRE( scene.GetCameraCount() == 1 );
    const SceneCamera& camera = scene.GetCamera( 0 );
    CHECK( camera.m_position.x == doctest::Approx( 0.0f ) );
    CHECK( camera.m_position.y == doctest::Approx( -260.0f ) );
    CHECK( camera.m_position.z == doctest::Approx( 100.0f ) );
    CHECK( camera.view.z == doctest::Approx( 0.0f ) );
    CHECK( camera.up.x == doctest::Approx( 0.0f ) );
    CHECK( camera.up.y == doctest::Approx( 0.0f ) );
    CHECK( camera.up.z == doctest::Approx( 1.0f ) );

    const auto& cinematic = scene.GetCinematicRenderConfig();
    CHECK( cinematic.skyMode == SkullbonezCore::Core::CinematicStyleMode::Sky::DeepSpace );
    CHECK( cinematic.sunColorR == doctest::Approx( 0.0f ) );
    CHECK( cinematic.sunColorG == doctest::Approx( 0.0f ) );
    CHECK( cinematic.sunColorB == doctest::Approx( 0.0f ) );
    CHECK( cinematic.sunIntensity == doctest::Approx( 0.0f ) );
    CHECK_FALSE( cinematic.shadow.enabled );
    CHECK( scene.GetWaterReflectionMode() == 2 );

    REQUIRE( scene.GetBallStateCount() == 4 );

    for ( int index = 0; index < scene.GetBallStateCount(); ++index )
    {
        const auto& body = scene.GetBallState( index );
        CHECK( body.posZ == doctest::Approx( 0.0f ) );
        CHECK( body.velZ == doctest::Approx( 0.0f ) );
    }

    REQUIRE( scene.GetObjectMaterialOverrideCount() == 4 );

    struct ExpectedColour
    {
        const char* target;
        float r;
        float g;
        float b;
    };
    const ExpectedColour expected[] = {
        { "sun", 1.0f, 0.82f, 0.08f },
        { "earth", 0.12f, 0.82f, 0.22f },
        { "mars", 0.92f, 0.12f, 0.06f },
        { "ship", 1.0f, 1.0f, 1.0f },
    };

    for ( int index = 0; index < scene.GetObjectMaterialOverrideCount(); ++index )
    {
        const SceneObjectMaterialOverride& material = scene.GetObjectMaterialOverride( index );
        REQUIRE( std::string( material.target ) == expected[index].target );
        CHECK( material.material.baseColor[0] == doctest::Approx( expected[index].r ) );
        CHECK( material.material.baseColor[1] == doctest::Approx( expected[index].g ) );
        CHECK( material.material.baseColor[2] == doctest::Approx( expected[index].b ) );
    }
}

TEST_CASE( "AuthoredSceneParser: Mars slingshot scene contains the complete major-moon system on XY" )
{
    const AuthoredScene scene = AuthoredScene::LoadFromFile( diagnostics, kSolarSlingshotScenePath );
    const char* expectedNames[] = {
        "sun",     "mercury",   "venus",   "earth",  "mars",    "jupiter", "saturn",   "uranus",
        "neptune", "moon",      "phobos",  "deimos", "io",      "europa",  "ganymede", "callisto",
        "mimas",   "enceladus", "tethys",  "dione",  "rhea",    "titan",   "iapetus",  "miranda",
        "ariel",   "umbriel",   "titania", "oberon", "proteus", "triton",  "nereid",   "rocket",
    };

    REQUIRE( scene.GetCameraCount() == 2 );

    for ( int index = 0; index < scene.GetCameraCount(); ++index )
    {
        const SceneCamera& camera = scene.GetCamera( index );
        CHECK( camera.m_position.y < camera.view.y );
        CHECK( camera.m_position.z > camera.view.z );
        CHECK( camera.up.x == doctest::Approx( 0.0f ) );
        CHECK( camera.up.y == doctest::Approx( 0.0f ) );
        CHECK( camera.up.z == doctest::Approx( 1.0f ) );
    }

    CHECK( scene.GetCinematicRenderConfig().skyMode == SkullbonezCore::Core::CinematicStyleMode::Sky::DeepSpace );
    CHECK_FALSE( scene.GetCinematicRenderConfig().shadow.enabled );
    CHECK( scene.GetWaterReflectionMode() == 2 );
    REQUIRE( scene.GetBallStateCount() == static_cast<int>( std::size( expectedNames ) ) );

    for ( int index = 0; index < scene.GetBallStateCount(); ++index )
    {
        const auto& body = scene.GetBallState( index );
        CHECK( std::string( body.name ) == expectedNames[index] );
        CHECK( body.posZ == doctest::Approx( 0.0f ) );
        CHECK( body.velZ == doctest::Approx( 0.0f ) );
    }

    const auto& rocket = scene.GetBallState( scene.GetBallStateCount() - 1 );
    CHECK( rocket.posX == doctest::Approx( 77.594884f ) );
    CHECK( rocket.posY == doctest::Approx( -7.997296f ) );
    CHECK( rocket.velX == doctest::Approx( 0.127265f ) );
    CHECK( rocket.velY == doctest::Approx( 27.470485f ) );
}


TEST_CASE( "AuthoredSceneParser: malformed JSON reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile malformed( "unit_scene_parser_malformed.scene.json",
                                                 "{ \"format\": \"skullbonez.scene.json\", \"cameras\": [" );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, malformed.path, scene ), malformed.path, "Invalid JSON" );
}


TEST_CASE( "AuthoredSceneParser: legacy releasable trees resolve stable root ids" )
{
    const AuthoredScene scene = AuthoredScene::LoadFromFile( diagnostics,
                                                             "SkullbonezData/scenes/nature_hull_assets.scene.json" );

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
    const TemporaryMalformedSceneFile missingGroupRoot( "unit_scene_parser_missing_group_root.scene.json",
                                                        R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"objects":[{"type":"convexHull","name":"tree_child","hull":"pyramid","position":[0,0,0],"restitution":0.1,"objectGroup":{"kind":"releasableTree","root":"missing_root","part":1}}]})" );
                                                        AuthoredScene scene;
                                                        CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, missingGroupRoot.path, scene ), missingGroupRoot.path,
                                                        "does not name an object" );
}


TEST_CASE( "AuthoredSceneParser: missing camera reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile
        missingCamera( "unit_scene_parser_missing_camera.scene.json",
                       R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false})" );

    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, missingCamera.path, scene ), missingCamera.path,
                      "at least one camera" );
}


TEST_CASE( "AuthoredSceneParser: tornado fields over the fixed gameplay capacity fail recoverably" )
{
    const TemporaryMalformedSceneFile overCapacity( "unit_scene_parser_tornado_capacity.scene.json",
                                                    BuildOverCapacityTornadoScene() );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, overCapacity.path, scene ), overCapacity.path,
                      "Gameplay.TornadoGameplay tornadoSystem.vortices requested 65, capacity is 64" );
}


TEST_CASE( "AuthoredSceneParser: wrong member type reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile
        wrongType( "unit_scene_parser_wrong_type.scene.json",
                   R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false,"cameras":{}})" );

    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, wrongType.path, scene ), wrongType.path,
                      "cameras must be an array" );
}


TEST_CASE( "AuthoredSceneParser: unknown asset instance reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile unknownAsset( "unit_scene_parser_unknown_asset.scene.json",
                                                    R"({"format":"skullbonez.scene.json","version":1,"physics":false,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"assetInstances":[{"asset":"missing.asset","name":"ghost","position":[0,0,0]}]})" );
                                                    AuthoredScene scene;
                                                    CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, unknownAsset.path, scene ), unknownAsset.path,
                                                    "Unknown asset instance reference" );
}


TEST_CASE( "AuthoredSceneParser: legacy and current asset-library versions load" )
{
    const TemporaryMalformedSceneFile sceneFile( "unit_versioned_asset.scene.json", kVersionedAssetScene );

    {
        const TemporaryMalformedSceneFile legacyLibrary( "unit_versioned.assets.json",
                                                         R"({"format":"skullbonez.asset_library.json","assets":[]})" );
        AuthoredScene scene;
        CHECK( AuthoredScene::TryLoadFromFile( diagnostics, sceneFile.path, scene ).Ok() );
    }
    {
        const TemporaryMalformedSceneFile
            currentLibrary( "unit_versioned.assets.json",
                            R"({"format":"skullbonez.asset_library.json","version":1,"assets":[]})" );

        AuthoredScene scene;
        CHECK( AuthoredScene::TryLoadFromFile( diagnostics, sceneFile.path, scene ).Ok() );
    }
}


TEST_CASE( "AuthoredSceneParser: future asset-library version is a named recoverable failure" )
{
    const TemporaryMalformedSceneFile sceneFile( "unit_versioned_asset.scene.json", kVersionedAssetScene );
    const TemporaryMalformedSceneFile
        futureLibrary( "unit_versioned.assets.json",
                       R"({"format":"skullbonez.asset_library.json","version":2,"assets":[]})" );

    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, sceneFile.path, scene ), futureLibrary.path,
                      "version 2 is newer than current version 1" );
}


TEST_CASE( "AuthoredSceneParser: malformed style JSON reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile malformedStyle( "unit_scene_parser_malformed_style.style.json",
                                                      "{ \"format\": \"skullbonez.style.json\", \"objectMaterials\": [" );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadStyleFromFile( diagnostics, malformedStyle.path, scene ), malformedStyle.path,
                      "Invalid JSON" );
}
