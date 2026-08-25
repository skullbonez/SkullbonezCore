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
//   Parser contract: The observable success data or recoverable failure message that
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
//   - Orbital stability names resolve exactly once to stable scene-object ids.
//   - Initial impulse application points are world-axis offsets from each body
//     center, never absolute world positions.
//   - Tornado scenes retain all 64 admitted fields and reject the 65th before
//     committing authored state.
//   - Fixed-cardinality values validate container shape and exact length before
//     indexing; a failed load never replaces the caller's prior scene.
//
// Related:
//   - SkullbonezSource/Scene/AuthoredScene.h
//   - SkullbonezSource/Scene/AuthoredSceneParser.cpp
//   - SkullbonezSource/Gameplay/TornadoField.h
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestResultLoadFixtures.h"

#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Scene/AuthoredScene.h"
#include "../SkullbonezSource/Gameplay/TornadoField.h"

#include <cstdio>
#include <fstream>
#include <iterator>
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
constexpr const char* kAtRestScenePath = "SkullbonezData/scenes/at_rest.scene.json";
constexpr const char* kVersionedAssetScene =
    R"({"format":"skullbonez.scene.json","version":2,"physics":false,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"assetLibraries":["unit_versioned.assets.json"]})";

struct TemporaryMalformedSceneFile
{
    const char* path = nullptr;

    TemporaryMalformedSceneFile( const char* fixturePath, const std::string& contents ) : path( fixturePath )
    {
        std::ofstream output( path );

        REQUIRE_MESSAGE( output.good(), "AuthoredSceneParser: failed to create malformed scene fixture" );
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

std::string BuildTornadoScene( std::size_t vortexCount )
{
    std::string scene =
        R"({"format":"skullbonez.scene.json","version":2,"physics":true,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"tornadoSystem":{"vortices":[)";

    for ( std::size_t index = 0; index < vortexCount; ++index )
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

std::string BuildOrbitalStabilityResolutionScene( const char* coreObjectName )
{
    return std::string(
               R"({"format":"skullbonez.scene.json","version":2,"simulation":{"physics":true,"text":false,"orbitalStability":{"escapeGraceSeconds":5,"members":[{"object":"sun","role":"primary"},{"object":")" ) +
           coreObjectName +
           R"(","role":"core","innerRadius":60,"outerRadius":100,"escapeStartRadius":90}]}},"cameras":[{"name":"main","position":[0,0,10],"view":[0,0,0],"up":[0,1,0]}],"objects":[{"type":"ballState","sceneObjectId":71,"name":"sun","position":[0,0,0],"velocity":[0,0,0],"angularVelocity":[0,0,0],"orientation":[0,0,0,1],"radius":2,"mass":100,"restitution":0,"inertia":[1,1,1],"fixed":true,"sleeping":false},{"type":"ballState","sceneObjectId":72,"name":"earth","position":[80,0,0],"velocity":[0,1,0],"angularVelocity":[0,0,0],"orientation":[0,0,0,1],"radius":1,"mass":1,"restitution":0,"inertia":[1,1,1],"fixed":false,"sleeping":false}]})";
}

// Invariant: one row binds a field's document splice, exact cardinality, and
// diagnostic noun so all three malformed-shape variants exercise the same
// parser contract.
struct FixedArrayFailureCase
{
    const char* context;
    const char* prefix;
    const char* suffix;
    std::size_t cardinality;
    const char* elementDescription;
};

std::string BuildNumberArray( std::size_t cardinality )
{
    std::string value = "[";

    for ( std::size_t index = 0u; index < cardinality; ++index )
    {
        if ( index != 0u )
        {
            value += ',';
        }

        value += '0';
    }

    value += ']';
    return value;
}

std::string BuildSameCardinalityObject( std::size_t cardinality )
{
    std::string value = "{";

    for ( std::size_t index = 0u; index < cardinality; ++index )
    {
        if ( index != 0u )
        {
            value += ',';
        }

        value += "\"v" + std::to_string( index ) + "\":0";
    }

    value += '}';
    return value;
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
    CHECK_FALSE( scene.PredictionShowsAllBodies() );
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

TEST_CASE( "AuthoredSceneParser: at-rest completion stays unlimited and names all three balls" )
{
    AuthoredScene scene;
    REQUIRE( TryLoadAuthoredScene( diagnostics, kAtRestScenePath, scene ) );
    CHECK( scene.GetFrameCount() == -1 );
    CHECK( scene.IsExitOnComplete() );
    REQUIRE( scene.GetRequiredSleepingDynamicBodyCount() == 3 );
    CHECK( std::string( scene.GetRequiredSleepingDynamicBody( 0 ).name ) == "ball_a" );
    CHECK( std::string( scene.GetRequiredSleepingDynamicBody( 1 ).name ) == "ball_b" );
    CHECK( std::string( scene.GetRequiredSleepingDynamicBody( 2 ).name ) == "ball_c" );
}

TEST_CASE( "AuthoredSceneParser: sleeping-body requirements reject non-string entries" )
{
    constexpr const char* path = "TestOutput/scene_parser_sleep_requirement_invalid.scene.json";
    constexpr const char* json =
        R"({"format":"skullbonez.scene.json","version":1,"physics":true,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"requirements":{"sleepingDynamicBodies":[7]}})";
    const TemporaryMalformedSceneFile fixture( path, json );
    AuthoredScene scene;
    const SbResult result = AuthoredScene::TryLoadFromFile( diagnostics, path, scene );
    CheckLoadFailure( result, path, "requirements.sleepingDynamicBodies[] must be a string" );
}

TEST_CASE( "AuthoredSceneParser: sleeping-body requirement names reject truncation" )
{
    constexpr const char* path = "TestOutput/scene_parser_sleep_requirement_name_too_long.scene.json";
    constexpr const char* json =
        R"({"format":"skullbonez.scene.json","version":1,"physics":true,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"requirements":{"sleepingDynamicBodies":["dynamic_body_name_that_is_deliberately_longer_than_the_fixed_authored_name_buffer"]}})";
    const TemporaryMalformedSceneFile fixture( path, json );
    AuthoredScene scene;
    const SbResult result = AuthoredScene::TryLoadFromFile( diagnostics, path, scene );
    CheckLoadFailure( result, path, "requirements.sleepingDynamicBodies[] must be shorter than 64 characters" );
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
    CHECK( scene.PredictionShowsAllBodies() );
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

    const auto& stability = scene.GetOrbitalStabilityContract();
    REQUIRE( stability.enabled );
    REQUIRE( stability.memberCount == 4u );
    CHECK( stability.escapeGraceSeconds == doctest::Approx( 5.0 ) );
    CHECK( stability.members[0].role == SkullbonezCore::Scene::OrbitalStabilityMemberRole::Primary );
    CHECK( stability.members[1].role == SkullbonezCore::Scene::OrbitalStabilityMemberRole::CoreOrbiter );
    CHECK( stability.members[2].role == SkullbonezCore::Scene::OrbitalStabilityMemberRole::CoreOrbiter );
    CHECK( stability.members[3].role == SkullbonezCore::Scene::OrbitalStabilityMemberRole::Auxiliary );

    for ( std::size_t memberIndex = 0u; memberIndex < stability.memberCount; ++memberIndex )
    {
        CHECK( stability.members[memberIndex].sceneObjectId.IsValid() );
        CHECK( std::string( stability.members[memberIndex].authoredObjectName ) ==
               scene.GetBallState( static_cast<int>( memberIndex ) ).name );
    }

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

TEST_CASE( "AuthoredSceneParser: orbital stability membership rejects missing and repeated objects" )
{
    {
        const TemporaryMalformedSceneFile missing( "unit_scene_parser_orbital_missing.scene.json",
                                                   BuildOrbitalStabilityResolutionScene( "ghost" ) );
        AuthoredScene scene;
        CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, missing.path, scene ), missing.path,
                          "does not resolve exactly once" );
    }

    {
        const TemporaryMalformedSceneFile repeated( "unit_scene_parser_orbital_repeated.scene.json",
                                                    BuildOrbitalStabilityResolutionScene( "sun" ) );
        AuthoredScene scene;
        CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, repeated.path, scene ), repeated.path,
                          "repeats one resolved object" );
    }
}

TEST_CASE( "AuthoredSceneParser: Mars slingshot scene contains the complete major-moon system on XY" )
{
    AuthoredScene scene;
    REQUIRE( TryLoadAuthoredScene( diagnostics, kSolarSlingshotScenePath, scene ) );
    CHECK( scene.PredictionShowsAllBodies() );
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

TEST_CASE( "AuthoredSceneParser: prediction path presentation is explicit and independent of mutual gravity" )
{
    const std::string
        scenePrefix = R"({"format":"skullbonez.scene.json","version":4,"simulation":{"physics":true,"text":false,)";
    const std::string sceneSuffix = R"(},"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}]})";

    SUBCASE( "mutual gravity alone retains selected causal tree presentation" )
    {
        const TemporaryMalformedSceneFile fixture(
            "unit_scene_parser_prediction_force_independence.scene.json",
            scenePrefix +
                R"("world":{"gravity":0,"fluidHeight":-1000,"fluidDensity":0,"mutualGravity":{"enabled":true,"gravitationalConstant":1,"softeningLength":0.5,"elasticCollisions":false}})" +
                sceneSuffix );
        AuthoredScene scene;
        REQUIRE( TryLoadAuthoredScene( diagnostics, fixture.path, scene ) );
        CHECK( scene.HasMutualGravityEnabled() );
        CHECK_FALSE( scene.PredictionShowsAllBodies() );
    }

    SUBCASE( "invalid presentation token fails through recoverable result" )
    {
        const TemporaryMalformedSceneFile fixture( "unit_scene_parser_prediction_path_policy.scene.json",
                                                   scenePrefix + R"("predictionPathPresentation":"everything")" +
                                                       sceneSuffix );
        AuthoredScene scene;
        CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, fixture.path, scene ), fixture.path,
                          "simulation.predictionPathPresentation" );
    }
}


TEST_CASE( "AuthoredSceneParser: malformed JSON reports recoverable load failure" )
{
    const TemporaryMalformedSceneFile malformed( "unit_scene_parser_malformed.scene.json",
                                                 "{ \"format\": \"skullbonez.scene.json\", \"cameras\": [" );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, malformed.path, scene ), malformed.path, "Invalid JSON" );
}

TEST_CASE( "AuthoredSceneParser: fixed-cardinality fields fail recoverably before indexed access" )
{
    // Concept: these eight fields cover both shared tuple readers and every
    // direct fixed-width UI/cinematic access. An object with the expected
    // member count is the negative control for validating size without type.
    static constexpr FixedArrayFailureCase cases[] = {
        { "camera.position",
          R"({"format":"skullbonez.scene.json","version":4,"physics":false,"text":false,"cameras":[{"name":"main","position":)",
          R"(,"view":[0,0,1],"up":[0,1,0]}]})", 3u, "numbers" },
        { "ballState.orientation",
          R"({"format":"skullbonez.scene.json","version":4,"physics":false,"text":false,"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}],"objects":[{"type":"ballState","sceneObjectId":12,"name":"probe","position":[0,0,0],"velocity":[0,0,0],"angularVelocity":[0,0,0],"orientation":)",
          R"(,"radius":1,"mass":1,"restitution":0.1,"inertia":[1,1,1],"fixed":false,"sleeping":false}]})", 4u,
          "numbers" },
        { "ui.rect",
          R"({"format":"skullbonez.scene.json","version":4,"physics":false,"text":false,"ui":{"rect":)",
          R"(},"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}]})", 4u, "integers" },
        { "ui.mouse",
          R"({"format":"skullbonez.scene.json","version":4,"physics":false,"text":false,"ui":{"mouse":)",
          R"(},"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}]})", 2u, "integers" },
        { "cinematic.shadowParticipation",
          R"({"format":"skullbonez.scene.json","version":4,"physics":false,"text":false,"cinematic":{"shadowParticipation":)",
          R"(},"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}]})", 4u, "booleans" },
        { "cinematic.styleModes",
          R"({"format":"skullbonez.scene.json","version":4,"physics":false,"text":false,"cinematic":{"styleModes":)",
          R"(},"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}]})", 4u, "integers" },
        { "cinematic.terrainGrid",
          R"({"format":"skullbonez.scene.json","version":4,"physics":false,"text":false,"cinematic":{"terrainGrid":)",
          R"(},"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}]})", 2u, "numbers" },
        { "cinematic.basinMask",
          R"({"format":"skullbonez.scene.json","version":4,"physics":false,"text":false,"cinematic":{"basinMask":)",
          R"(},"cameras":[{"name":"main","position":[0,0,0],"view":[0,0,1],"up":[0,1,0]}]})", 5u, "numbers" },
    };

    for ( const FixedArrayFailureCase& failureCase : cases )
    {
        for ( int variant = 0; variant < 3; ++variant )
        {
            CAPTURE( failureCase.context );
            CAPTURE( variant );
            const std::size_t arraySize = variant == 1 ? failureCase.cardinality - 1u : failureCase.cardinality + 1u;
            const std::string malformedValue =
                variant == 0 ? BuildSameCardinalityObject( failureCase.cardinality ) : BuildNumberArray( arraySize );
            const std::string document = failureCase.prefix + malformedValue + failureCase.suffix;
            constexpr const char* path = "TestOutput/scene_parser_fixed_array_invalid.scene.json";
            const TemporaryMalformedSceneFile fixture( path, document );
            AuthoredScene scene;
            REQUIRE( TryLoadAuthoredScene( diagnostics, kSmallestCommittedScenePath, scene ) );
            REQUIRE( scene.GetCameraCount() == 1 );
            const SceneCamera retainedCamera = scene.GetCamera( 0 );

            const SbResult result = AuthoredScene::TryLoadFromFile( diagnostics, path, scene );
            const std::string expectedMessage =
                variant == 0 ? std::string( failureCase.context ) + " must be an array, got object"
                             : std::string( failureCase.context ) + " must contain exactly " +
                                   std::to_string( failureCase.cardinality ) + ' ' + failureCase.elementDescription;
            CheckLoadFailure( result, path, expectedMessage.c_str() );

            // Invariant: TryLoad assigns only after a complete parse. A
            // recoverable shape error must leave the caller's scene untouched.
            REQUIRE( scene.GetCameraCount() == 1 );
            CHECK( scene.GetCamera( 0 ).m_position.x == retainedCamera.m_position.x );
            CHECK( scene.GetCamera( 0 ).m_position.y == retainedCamera.m_position.y );
            CHECK( scene.GetCamera( 0 ).m_position.z == retainedCamera.m_position.z );
        }
    }
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
                                                    BuildTornadoScene(
                                                        SkullbonezCore::Gameplay::MAX_TORNADO_ACTIVE_FORCE_FIELDS + 1u ) );
    AuthoredScene scene;
    CheckLoadFailure( AuthoredScene::TryLoadFromFile( diagnostics, overCapacity.path, scene ), overCapacity.path,
                      "Gameplay.TornadoGameplay tornadoSystem.vortices requested 65, capacity is 64" );
}


TEST_CASE( "AuthoredSceneParser: exact tornado gameplay capacity retains every field" )
{
    const TemporaryMalformedSceneFile exactCapacity( "unit_scene_parser_tornado_exact_capacity.scene.json",
                                                     BuildTornadoScene(
                                                         SkullbonezCore::Gameplay::MAX_TORNADO_ACTIVE_FORCE_FIELDS ) );
    AuthoredScene scene;
    const auto result = AuthoredScene::TryLoadFromFile( diagnostics, exactCapacity.path, scene );

    REQUIRE( result.Ok() );
    REQUIRE( scene.HasTornadoSystem() );
    CHECK( scene.GetTornadoSystemConfig().vortices.size() == SkullbonezCore::Gameplay::MAX_TORNADO_ACTIVE_FORCE_FIELDS );
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
