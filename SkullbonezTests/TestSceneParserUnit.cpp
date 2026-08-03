//
// File: SkullbonezTests/TestSceneParserUnit.cpp
// Purpose:
//   Lock the smallest authored-scene parse path and recoverable load-error contract.
//
// Summary:
//   AuthoredScene::TryLoadFromFile is a data-boundary parser. It turns committed
//   scene JSON into immutable setup records while malformed files return
//   owner/message diagnostics without escaping.
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
//   - Initial impulse application points are world-axis offsets from each body
//     center, never absolute world positions.
//
// Related:
//   - SkullbonezSource/Scene/AuthoredScene.h
//   - SkullbonezSource/Scene/AuthoredSceneParser.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestResultLoadFixtures.h"

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
using SkullbonezTests::ResultLoadFixtures::TryLoadAuthoredScene;

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;

constexpr const char* kSmallestCommittedScenePath = "SkullbonezData/scenes/terrain_compare.scene.json";
constexpr const char* kCardinalRollScenePath = "SkullbonezData/scenes/cardinal_roll_test.scene.json";
constexpr const char* kRagdollPlaygroundScenePath = "SkullbonezData/scenes/ragdoll_playground.scene.json";
constexpr const char* kSolarSystemScenePath = "SkullbonezData/scenes/solar_system.scene.json";
constexpr const char* kSolarSlingshotScenePath = "SkullbonezData/scenes/solar_system_mars_slingshot.scene.json";
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
    CHECK_FALSE( result.Ok() );
    CHECK( std::string( result.ErrorOwner() ) == "Scene/AuthoredSceneParser" );
    const std::string message = result.ErrorMessage();
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

std::string BuildVersionedQuaternionScene( uint32_t version )
{
    return std::string( R"({"format":"skullbonez.scene.json","version":)" ) + std::to_string( version ) +
           R"(,"physics":false,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"objects":[{"type":"ballState","sceneObjectId":12,"name":"probe","position":[0,0,0],"velocity":[0,0,0],"angularVelocity":[0,0,0],"orientation":[0.25,-0.5,0.75,-1],"radius":1,"mass":1,"restitution":0.1,"inertia":[1,1,1],"fixed":false,"sleeping":false}]})";
}

std::string BuildVersionedImpulseOffsetScene( uint32_t version, const char* offsetKey )
{
    const std::string identity = version == 1u ? "" : R"(,"sceneObjectId":71)";
    return std::string( R"({"format":"skullbonez.scene.json","version":)" ) + std::to_string( version ) +
           R"(,"physics":true,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"objects":[{"type":"ball")" +
           identity +
           R"(,"name":"impulse_probe","position":[10,20,30],"radius":1,"mass":2,"moment":3,"restitution":0.1,"force":[4,5,6],")" +
           offsetKey + R"(":[1,2,3],"fixed":false}]})";
}
} // namespace


TEST_CASE( "AuthoredSceneParser: smallest committed scene parses expected records" )
{
    AuthoredScene scene;
    REQUIRE( TryLoadAuthoredScene( diagnostics, kSmallestCommittedScenePath, scene ) );
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

TEST_CASE( "AuthoredSceneParser: initial impulse offsets are center-relative world vectors" )
{
    SUBCASE( "a nonzero authored lever arm reaches the parsed impulse record" )
    {
        AuthoredScene scene;
        REQUIRE( TryLoadAuthoredScene( diagnostics, kCardinalRollScenePath, scene ) );
        REQUIRE( scene.GetBallCount() == 4 );

        const auto& north = scene.GetBall( 0 );
        CHECK( std::string( north.name ) == "north" );
        CHECK( north.forceZ == doctest::Approx( -500.0f ) );
        CHECK( north.impulseWorldOffsetFromCenterX == doctest::Approx( 10.0f ) );
        CHECK( north.impulseWorldOffsetFromCenterY == doctest::Approx( 0.0f ) );
        CHECK( north.impulseWorldOffsetFromCenterZ == doctest::Approx( 0.0f ) );
    }

    SUBCASE( "wake_ball applies its impulse through the body center" )
    {
        AuthoredScene scene;
        REQUIRE( TryLoadAuthoredScene( diagnostics, kRagdollPlaygroundScenePath, scene ) );
        REQUIRE( scene.GetBallCount() == 1 );

        const auto& wakeBall = scene.GetBall( 0 );
        CHECK( std::string( wakeBall.name ) == "wake_ball" );
        CHECK( wakeBall.forceZ == doctest::Approx( 120.0f ) );
        CHECK( wakeBall.impulseWorldOffsetFromCenterX == doctest::Approx( 0.0f ) );
        CHECK( wakeBall.impulseWorldOffsetFromCenterY == doctest::Approx( 0.0f ) );
        CHECK( wakeBall.impulseWorldOffsetFromCenterZ == doctest::Approx( 0.0f ) );
    }
}

TEST_CASE( "AuthoredSceneParser: solar bodies publish their authored colours" )
{
    AuthoredScene scene;
    REQUIRE( TryLoadAuthoredScene( diagnostics, kSolarSystemScenePath, scene ) );
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
    AuthoredScene scene;
    REQUIRE( TryLoadAuthoredScene( diagnostics, kSolarSlingshotScenePath, scene ) );
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

TEST_CASE( "AuthoredSceneParser: quaternion representation is versioned at the scene boundary" )
{
    SUBCASE( "legacy schema is conjugated into canonical memory" )
    {
        const TemporaryMalformedSceneFile legacy( "unit_scene_parser_legacy_quaternion.scene.json",
                                                  BuildVersionedQuaternionScene( 2 ) );
        AuthoredScene scene;
        REQUIRE( TryLoadAuthoredScene( diagnostics, legacy.path, scene ) );
        REQUIRE( scene.GetBallStateCount() == 1 );
        const auto& body = scene.GetBallState( 0 );
        CHECK( body.orientX == doctest::Approx( -0.25f ) );
        CHECK( body.orientY == doctest::Approx( 0.5f ) );
        CHECK( body.orientZ == doctest::Approx( -0.75f ) );
        CHECK( body.orientW == doctest::Approx( -1.0f ) );
    }

    SUBCASE( "current schema is already canonical" )
    {
        const TemporaryMalformedSceneFile current( "unit_scene_parser_current_quaternion.scene.json",
                                                   BuildVersionedQuaternionScene( 4 ) );
        AuthoredScene scene;
        REQUIRE( TryLoadAuthoredScene( diagnostics, current.path, scene ) );
        REQUIRE( scene.GetBallStateCount() == 1 );
        const auto& body = scene.GetBallState( 0 );
        CHECK( body.orientX == doctest::Approx( 0.25f ) );
        CHECK( body.orientY == doctest::Approx( -0.5f ) );
        CHECK( body.orientZ == doctest::Approx( 0.75f ) );
        CHECK( body.orientW == doctest::Approx( -1.0f ) );
    }

    SUBCASE( "future schema fails closed" )
    {
        const TemporaryMalformedSceneFile future( "unit_scene_parser_future_quaternion.scene.json",
                                                  BuildVersionedQuaternionScene( 5 ) );
        AuthoredScene scene;
        CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, future.path, scene ), future.path,
                          "Unsupported scene schema version: 5" );
    }
}

TEST_CASE( "AuthoredSceneParser: impulse offset spelling is versioned at the scene boundary" )
{
    for ( uint32_t version = 1u; version <= 3u; ++version )
    {
        const TemporaryMalformedSceneFile legacy( "unit_scene_parser_legacy_impulse_offset.scene.json",
                                                  BuildVersionedImpulseOffsetScene( version, "forcePosition" ) );
        AuthoredScene scene;
        REQUIRE( TryLoadAuthoredScene( diagnostics, legacy.path, scene ) );
        REQUIRE( scene.GetBallCount() == 1 );
        CHECK( scene.GetSchemaVersion() == version );
        CHECK( scene.GetBall( 0 ).impulseWorldOffsetFromCenterX == doctest::Approx( 1.0f ) );
        CHECK( scene.GetBall( 0 ).impulseWorldOffsetFromCenterY == doctest::Approx( 2.0f ) );
        CHECK( scene.GetBall( 0 ).impulseWorldOffsetFromCenterZ == doctest::Approx( 3.0f ) );
    }

    SUBCASE( "version 4 accepts only the explicit center-relative world offset" )
    {
        const TemporaryMalformedSceneFile current( "unit_scene_parser_current_impulse_offset.scene.json",
                                                   BuildVersionedImpulseOffsetScene( 4u, "impulseWorldOffsetFromCenter" ) );
        AuthoredScene scene;
        REQUIRE( TryLoadAuthoredScene( diagnostics, current.path, scene ) );
        REQUIRE( scene.GetBallCount() == 1 );
        CHECK( scene.GetSchemaVersion() == 4u );
        CHECK( scene.GetBall( 0 ).impulseWorldOffsetFromCenterX == doctest::Approx( 1.0f ) );
        CHECK( scene.GetBall( 0 ).impulseWorldOffsetFromCenterY == doctest::Approx( 2.0f ) );
        CHECK( scene.GetBall( 0 ).impulseWorldOffsetFromCenterZ == doctest::Approx( 3.0f ) );
    }

    SUBCASE( "version 4 rejects the retired absolute-position spelling" )
    {
        const TemporaryMalformedSceneFile retired( "unit_scene_parser_retired_impulse_offset.scene.json",
                                                   BuildVersionedImpulseOffsetScene( 4u, "forcePosition" ) );
        AuthoredScene scene;
        CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, retired.path, scene ), retired.path,
                          "forcePosition is retired in scene schema version 4" );
    }

    SUBCASE( "legacy versions reject the version 4 spelling" )
    {
        const TemporaryMalformedSceneFile futureField( "unit_scene_parser_future_impulse_offset.scene.json",
                                                       BuildVersionedImpulseOffsetScene( 3u,
                                                                                         "impulseWorldOffsetFromCenter" ) );
        AuthoredScene scene;
        CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, futureField.path, scene ), futureField.path,
                          "impulseWorldOffsetFromCenter requires scene schema version 4" );
    }
}


TEST_CASE( "AuthoredSceneParser: legacy releasable trees resolve stable root ids" )
{
    AuthoredScene scene;
    REQUIRE( TryLoadAuthoredScene( diagnostics, "SkullbonezData/scenes/nature_hull_assets.scene.json", scene ) );

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
    const TemporaryMalformedSceneFile unknownAsset(
        "unit_scene_parser_unknown_asset.scene.json",
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
