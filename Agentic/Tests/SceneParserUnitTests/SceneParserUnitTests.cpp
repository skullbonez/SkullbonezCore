/*
File: Agentic/Tests/SceneParserUnitTests/SceneParserUnitTests.cpp
Purpose:
  Checks scene/style parser contracts that do not need a renderer launch.

Mental model:
  These tests protect user-facing scene authoring JSON. They load checked-in
  fixtures and small TestOutput-generated fault cases through the same TestScene
  entry points used by runtime code, then inspect the parsed data model.

Glossary:
  Fixture: Checked-in scene/style input file used as stable test data.
  Contract test: Focused test that protects user-visible syntax and parsed
  output rather than renderer screenshots.
  Scene authoring JSON: Structured fields accepted by .scene.json and .style.json files.
  Scene object id: Stable nonzero physics identity explicitly authored in v2
    scenes or deterministically upgraded from v1 input.
  Lane R result: Recoverable parser failure returned by TestScene::TryLoad*
    entry points with owner/message diagnostics.

Invariants:
  Checked-in and generated fixtures must stay small enough for unit tests.
  Generated malformed inputs live only under ignored TestOutput.
  Tests inspect parsed data directly instead of depending on renderer output or
  runtime launch state.

Related:
  - SkullbonezSource/Scene/TestScene.h
  - SkullbonezSource/Scene/TestSceneParser.cpp
  - SkullbonezData/styles/material_authoring_contract.style.json
*/
#include "Scene/TestScene.h"

#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Rendering;

namespace
{

struct TestFailure : public std::runtime_error
{
    explicit TestFailure( const std::string& message ) : std::runtime_error( message )
    {
    }
};

void Fail( const char* file, int line, const std::string& message )
{
    std::ostringstream out;
    out << file << "(" << line << "): " << message;
    throw TestFailure( out.str() );
}

void ExpectTrue( bool value, const char* expression, const char* file, int line )
{
    if ( !value )
    {
        Fail( file, line, std::string( "expected true: " ) + expression );
    }
}

void ExpectIntEqual( int actual,
                     int expected,
                     const char* actualExpression,
                     const char* expectedExpression,
                     const char* file,
                     int line )
{
    if ( actual != expected )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " == " << expectedExpression << ", actual " << actual << ", expected "
            << expected;
        Fail( file, line, out.str() );
    }
}

void ExpectUIntEqual( uint32_t actual,
                      uint32_t expected,
                      const char* actualExpression,
                      const char* expectedExpression,
                      const char* file,
                      int line )
{
    if ( actual != expected )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " == " << expectedExpression << ", actual " << actual << ", expected "
            << expected;
        Fail( file, line, out.str() );
    }
}

void ExpectStringEqual( const char* actual,
                        const char* expected,
                        const char* actualExpression,
                        const char* expectedExpression,
                        const char* file,
                        int line )
{
    if ( strcmp( actual, expected ) != 0 )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " == " << expectedExpression << ", actual \"" << actual
            << "\", expected \"" << expected << "\"";
        Fail( file, line, out.str() );
    }
}

void ExpectFloatNear( float actual,
                      float expected,
                      const char* actualExpression,
                      const char* expectedExpression,
                      const char* file,
                      int line )
{
    constexpr float epsilon = 0.0001f;
    if ( !std::isfinite( actual ) || !std::isfinite( expected ) || std::fabs( actual - expected ) > epsilon )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " near " << expectedExpression << ", actual " << actual
            << ", expected " << expected;
        Fail( file, line, out.str() );
    }
}

void ExpectStringContains( const std::string& actual,
                           const char* expected,
                           const char* actualExpression,
                           const char* expectedExpression,
                           const char* file,
                           int line )
{
    if ( actual.find( expected ) == std::string::npos )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " to contain " << expectedExpression << ", actual \"" << actual
            << "\", expected substring \"" << expected << "\"";
        Fail( file, line, out.str() );
    }
}

#define EXPECT_TRUE( expression ) ExpectTrue( !!( expression ), #expression, __FILE__, __LINE__ )
#define EXPECT_INT_EQ( actual, expected )                                                                              \
    ExpectIntEqual( static_cast<int>( actual ), static_cast<int>( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_UINT_EQ( actual, expected )                                                                             \
    ExpectUIntEqual( static_cast<uint32_t>( actual ),                                                                  \
                     static_cast<uint32_t>( expected ),                                                                \
                     #actual,                                                                                          \
                     #expected,                                                                                        \
                     __FILE__,                                                                                         \
                     __LINE__ )
#define EXPECT_STREQ( actual, expected )                                                                               \
    ExpectStringEqual( ( actual ), ( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_NEAR( actual, expected )                                                                                \
    ExpectFloatNear( ( actual ), ( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_CONTAINS( actual, expected )                                                                            \
    ExpectStringContains( ( actual ), ( expected ), #actual, #expected, __FILE__, __LINE__ )

struct TestCase
{
    const char* name = "";
    void ( *run )() = nullptr;
};

void ExpectMaterialKind( const SceneObjectMaterialOverride& material, RenderMaterialKind expected )
{
    EXPECT_INT_EQ( static_cast<int>( material.material.kind ), static_cast<int>( expected ) );
    EXPECT_NEAR( material.materialMode, RenderMaterialKindLegacyMode( expected ) );
}

void WriteTextFile( const char* path, const char* contents )
{
    std::ofstream out( path, std::ios::binary );
    if ( !out )
    {
        Fail( __FILE__, __LINE__, std::string( "failed to create test file: " ) + path );
    }
    out << contents;
}

void ExpectStyleLoadFails( const char* path, const char* contents, const char* expectedMessage )
{
    WriteTextFile( path, contents );
    TestScene scene;
    const SbResult result = TestScene::TryLoadStyleFromFile( path, scene );
    if ( !result.ok )
    {
        EXPECT_STREQ( result.error.owner, "Scene/TestSceneParser" );
        const std::string message = result.error.message;
        EXPECT_CONTAINS( message, expectedMessage );
        return;
    }

    Fail( __FILE__, __LINE__, std::string( "expected style load failure for " ) + path );
}

void ExpectSceneLoadFails( const char* path, const char* expectedMessage )
{
    TestScene scene = TestScene::LoadFromFile( "SkullbonezData/scenes/material_authoring_contract.scene.json" );
    const int originalCameraCount = scene.GetCameraCount();
    const int originalBallCount = scene.GetBallCount();
    const int originalMaterialCount = scene.GetObjectMaterialOverrideCount();

    const SbResult result = TestScene::TryLoadFromFile( path, scene );
    EXPECT_TRUE( !result.ok );
    EXPECT_STREQ( result.error.owner, "Scene/TestSceneParser" );
    EXPECT_CONTAINS( std::string( result.error.message ), expectedMessage );

    // Invariant: a failed parse never publishes its partially appended rows to
    // the caller's previously valid scene.
    EXPECT_INT_EQ( scene.GetCameraCount(), originalCameraCount );
    EXPECT_INT_EQ( scene.GetBallCount(), originalBallCount );
    EXPECT_INT_EQ( scene.GetObjectMaterialOverrideCount(), originalMaterialCount );
    EXPECT_STREQ( scene.GetBall( 0 ).name, "contract_probe" );
}

void TestStyleMaterialAuthoringContract()
{
    const TestScene scene =
        TestScene::LoadStyleFromFile( "SkullbonezData/styles/material_authoring_contract.style.json" );
    EXPECT_INT_EQ( scene.GetObjectMaterialOverrideCount(), 4 );

    const SceneObjectMaterialOverride& metal = scene.GetObjectMaterialOverride( 0 );
    EXPECT_STREQ( metal.target, "prefix:contract_metal" );
    ExpectMaterialKind( metal, RenderMaterialKind::Metal );
    EXPECT_STREQ( metal.material.name, "contract_metal" );
    EXPECT_NEAR( metal.tintR, 0.82f );
    EXPECT_NEAR( metal.tintG, 0.84f );
    EXPECT_NEAR( metal.tintB, 0.87f );
    EXPECT_NEAR( metal.material.baseColor[0], 0.82f );
    EXPECT_NEAR( metal.material.baseColor[1], 0.84f );
    EXPECT_NEAR( metal.material.baseColor[2], 0.87f );
    EXPECT_NEAR( metal.material.roughness, 0.18f );
    EXPECT_NEAR( metal.material.metallic, 1.0f );
    EXPECT_NEAR( metal.material.specular, 0.91f );

    const SceneObjectMaterialOverride& emitter = scene.GetObjectMaterialOverride( 1 );
    EXPECT_STREQ( emitter.target, "exact_emitter" );
    ExpectMaterialKind( emitter, RenderMaterialKind::Emissive );
    EXPECT_STREQ( emitter.material.name, "contract_emitter" );
    EXPECT_NEAR( emitter.tintR, 0.10f );
    EXPECT_NEAR( emitter.tintG, 0.20f );
    EXPECT_NEAR( emitter.tintB, 0.35f );
    EXPECT_NEAR( emitter.material.emissiveColor[0], 1.0f );
    EXPECT_NEAR( emitter.material.emissiveColor[1], 0.25f );
    EXPECT_NEAR( emitter.material.emissiveColor[2], 0.05f );
    EXPECT_NEAR( emitter.material.emissiveStrength, 3.5f );
    EXPECT_NEAR( emitter.material.roughness, 0.4f );
    EXPECT_UINT_EQ( emitter.material.flags, 7u );

    const SceneObjectMaterialOverride& legacyWithOptions = scene.GetObjectMaterialOverride( 2 );
    EXPECT_STREQ( legacyWithOptions.target, "balls" );
    ExpectMaterialKind( legacyWithOptions, RenderMaterialKind::Toon );
    EXPECT_NEAR( legacyWithOptions.tintR, 0.12f );
    EXPECT_NEAR( legacyWithOptions.tintG, 0.24f );
    EXPECT_NEAR( legacyWithOptions.tintB, 0.36f );
    EXPECT_NEAR( legacyWithOptions.material.stylization, 0.66f );

    const SceneObjectMaterialOverride& clamped = scene.GetObjectMaterialOverride( 3 );
    EXPECT_STREQ( clamped.target, "prefix:contract_clamp" );
    ExpectMaterialKind( clamped, RenderMaterialKind::Toon );
    EXPECT_STREQ( clamped.material.name, "contract_clamp" );
    EXPECT_NEAR( clamped.material.roughness, 0.0f );
    EXPECT_NEAR( clamped.material.metallic, 1.0f );
    EXPECT_NEAR( clamped.material.specular, 1.0f );
    EXPECT_NEAR( clamped.material.transmission, 0.0f );
    EXPECT_NEAR( clamped.material.stylization, 1.0f );
}

void TestSceneCanLoadMaterialAuthoringSample()
{
    const TestScene scene = TestScene::LoadFromFile( "SkullbonezData/scenes/material_authoring_contract.scene.json" );
    EXPECT_INT_EQ( scene.GetCameraCount(), 1 );
    EXPECT_INT_EQ( scene.GetBallCount(), 1 );
    EXPECT_TRUE( !scene.IsPhysicsEnabled() );
    EXPECT_INT_EQ( scene.GetFrameCount(), 1 );
    EXPECT_INT_EQ( scene.GetObjectMaterialOverrideCount(), 5 );

    const SceneObjectMaterialOverride& directOverride = scene.GetObjectMaterialOverride( 4 );
    EXPECT_STREQ( directOverride.target, "contract_probe" );
    ExpectMaterialKind( directOverride, RenderMaterialKind::Glass );
    EXPECT_STREQ( directOverride.material.name, "contract_glass" );
    EXPECT_NEAR( directOverride.tintR, 0.58f );
    EXPECT_NEAR( directOverride.tintG, 0.78f );
    EXPECT_NEAR( directOverride.tintB, 0.95f );
    EXPECT_NEAR( directOverride.material.transmission, 0.81f );
    EXPECT_NEAR( directOverride.material.roughness, 0.09f );
}

void TestMaterialAuthoringRejectsMalformedOptions()
{
    ExpectStyleLoadFails(
        "TestOutput/scene_parser_invalid_missing_mode.style.json",
        R"({"format":"skullbonez.style.json","version":1,"objectMaterials":[{"target":"bad","tint":[0.1,0.2,0.3]}]})",
        "objectMaterial is missing required field 'mode'" );
    ExpectStyleLoadFails(
        "TestOutput/scene_parser_invalid_unknown_field.style.json",
        R"({"format":"skullbonez.style.json","version":1,"objectMaterials":[{"target":"bad","mode":"metal","tint":[0.1,0.2,0.3],"shininess":0.7}]})",
        "Unknown objectMaterial field: shininess" );
    ExpectStyleLoadFails(
        "TestOutput/scene_parser_invalid_vec3.style.json",
        R"({"format":"skullbonez.style.json","version":1,"objectMaterials":[{"target":"bad","mode":"metal","tint":[0.1,0.2]}]})",
        "objectMaterial.color must contain exactly 3 numbers" );
    ExpectStyleLoadFails(
        "TestOutput/scene_parser_invalid_vec3_extra.style.json",
        R"({"format":"skullbonez.style.json","version":1,"objectMaterials":[{"target":"bad","mode":"metal","tint":[0.1,0.2,0.3,0.4]}]})",
        "objectMaterial.color must contain exactly 3 numbers" );
    ExpectStyleLoadFails(
        "TestOutput/scene_parser_invalid_vec3_type.style.json",
        R"({"format":"skullbonez.style.json","version":1,"objectMaterials":[{"target":"bad","mode":"metal","tint":"0.1,0.2,0.3"}]})",
        "objectMaterial.color must be an array" );
}

void TestAssetInstanceProvenanceAndTransformComposition()
{
    // Why: parent Y rotation plus child X rotation is deliberately
    // non-commuting, and the three offsets exercise all axes. Euler addition
    // or an unrotated child offset cannot satisfy the expected rows below.
    static constexpr const char* paddingLibraryPath = "TestOutput/scene_parser_asset_padding.assets.json";
    static constexpr const char* assetLibraryPath = "TestOutput/scene_parser_asset_provenance.assets.json";
    static constexpr const char* scenePath = "TestOutput/scene_parser_asset_provenance.scene.json";

    WriteTextFile( paddingLibraryPath,
                   R"({
  "format": "skullbonez.asset_library.json",
  "version": 1,
  "assets": [{
    "name": "contract.padding",
    "type": "compound",
    "parts": [
      {"name":"sphere","type":"sphere","radius":1,"mass":1,"restitution":0.1,"material":{"mode":"glass"}},
      {"name":"box","type":"box","halfExtents":[1,1,1],"mass":1,"restitution":0.1,"material":{"mode":"metal"}},
      {"name":"hull","type":"convexHull","hull":"wedge","mass":1,"restitution":0.1,"material":{"mode":"toon"}}
    ]
  }]
})" );
    WriteTextFile( assetLibraryPath,
                   R"({
  "format": "skullbonez.asset_library.json",
  "version": 1,
  "assets": [{
    "name": "contract.mixed",
    "type": "compound",
    "parts": [
      {"name":"box_part","type":"box","halfExtents":[1,2,3],"offset":[1,0,0],"euler":[90,0,0],"mass":2,"restitution":0.2,"material":{"mode":"metal"}},
      {"name":"sphere_part","type":"sphere","radius":1.5,"offset":[0,1,0],"euler":[90,0,0],"mass":3,"restitution":0.3,"material":{"mode":"glass"}},
      {"name":"hull_part","type":"convexHull","hull":"wedge","offset":[0,0,1],"euler":[90,0,0],"mass":4,"restitution":0.4,"material":{"mode":"toon"}}
    ]
  }]
})" );
    WriteTextFile( scenePath,
                   R"({
  "format": "skullbonez.scene.json",
  "version": 1,
  "assetLibraries": ["padding", "contract"],
  "cameras": [{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "assetInstances": [
    {"asset":"contract.padding","name":"padding","position":[-20,0,0]},
    {
      "asset": "contract.mixed",
      "name": "probe",
      "position": [10,20,30],
      "euler": [0,90,0],
      "fixed": true,
      "sleeping": true,
      "velocity": [1,2,3],
      "angularVelocity": [4,5,6]
    }
  ]
})" );

    SkullbonezCore::Assets::AssetSystem assets( "" );
    const SkullbonezCore::Assets::AssetId registeredPaddingLibraryId =
        assets.RegisterAssetLibrarySourceAsset( "assetlib.padding", paddingLibraryPath ).id;
    const SkullbonezCore::Assets::AssetLibrarySourceAsset& registeredLibrary =
        assets.RegisterAssetLibrarySourceAsset( "assetlib.contract", assetLibraryPath );
    const TestScene scene = TestScene::LoadFromFile( scenePath, assets );
    EXPECT_INT_EQ( scene.GetAssetLibraryCount(), 2 );
    EXPECT_INT_EQ( scene.GetAssetInstanceCount(), 2 );
    EXPECT_INT_EQ( scene.GetAssetPartCount(), 6 );

    const SceneAssetLibraryRef& paddingLibrary = scene.GetAssetLibrary( 0 );
    EXPECT_STREQ( paddingLibrary.token, "padding" );
    EXPECT_STREQ( paddingLibrary.resolvedPath, paddingLibraryPath );
    EXPECT_UINT_EQ( paddingLibrary.resolvedAssetId, registeredPaddingLibraryId );
    const SceneAssetLibraryRef& library = scene.GetAssetLibrary( 1 );
    EXPECT_STREQ( library.token, "contract" );
    EXPECT_STREQ( library.resolvedPath, assetLibraryPath );
    EXPECT_UINT_EQ( library.resolvedAssetId, registeredLibrary.id );

    const SceneAssetInstanceRecord& instance = scene.GetAssetInstance( 1 );
    EXPECT_STREQ( instance.assetName, "contract.mixed" );
    EXPECT_STREQ( instance.instanceName, "probe" );
    EXPECT_UINT_EQ( instance.libraryRefIndex, 1u );
    EXPECT_UINT_EQ( instance.firstPart, 3u );
    EXPECT_UINT_EQ( instance.partCount, 3u );
    EXPECT_UINT_EQ( instance.overrideMask,
                    SCENE_ASSET_OVERRIDE_FIXED | SCENE_ASSET_OVERRIDE_SLEEPING | SCENE_ASSET_OVERRIDE_EULER |
                        SCENE_ASSET_OVERRIDE_VELOCITY | SCENE_ASSET_OVERRIDE_ANGULAR_VELOCITY );
    EXPECT_NEAR( instance.posX, 10.0f );
    EXPECT_NEAR( instance.posY, 20.0f );
    EXPECT_NEAR( instance.posZ, 30.0f );
    EXPECT_NEAR( instance.eulerY, 90.0f );
    EXPECT_NEAR( instance.orientY, 0.70710678f );
    EXPECT_NEAR( instance.orientW, 0.70710678f );
    EXPECT_NEAR( instance.velX, 1.0f );
    EXPECT_NEAR( instance.velY, 2.0f );
    EXPECT_NEAR( instance.velZ, 3.0f );
    EXPECT_NEAR( instance.angVelX, 4.0f );
    EXPECT_NEAR( instance.angVelY, 5.0f );
    EXPECT_NEAR( instance.angVelZ, 6.0f );
    EXPECT_TRUE( instance.fixed );
    EXPECT_TRUE( instance.sleeping );

    const SceneAssetPartRef& boxPart = scene.GetAssetPart( 3 );
    EXPECT_STREQ( boxPart.partName, "box_part" );
    EXPECT_STREQ( boxPart.objectName, "probe_box_part" );
    EXPECT_INT_EQ( boxPart.source, SceneAssetPartSource::BoxState );
    EXPECT_UINT_EQ( boxPart.partIndex, 0u );
    EXPECT_UINT_EQ( boxPart.sourceIndex, 1u );
    EXPECT_NEAR( boxPart.posX, 10.0f );
    EXPECT_NEAR( boxPart.posY, 20.0f );
    EXPECT_NEAR( boxPart.posZ, 31.0f );
    EXPECT_NEAR( boxPart.orientX, 0.5f );
    EXPECT_NEAR( boxPart.orientY, 0.5f );
    EXPECT_NEAR( boxPart.orientZ, 0.5f );
    EXPECT_NEAR( boxPart.orientW, 0.5f );

    const SceneAssetPartRef& spherePart = scene.GetAssetPart( 4 );
    EXPECT_STREQ( spherePart.partName, "sphere_part" );
    EXPECT_INT_EQ( spherePart.source, SceneAssetPartSource::BallState );
    EXPECT_UINT_EQ( spherePart.partIndex, 1u );
    EXPECT_UINT_EQ( spherePart.sourceIndex, 1u );
    EXPECT_NEAR( spherePart.posX, 10.0f );
    EXPECT_NEAR( spherePart.posY, 21.0f );
    EXPECT_NEAR( spherePart.posZ, 30.0f );

    const SceneAssetPartRef& hullPart = scene.GetAssetPart( 5 );
    EXPECT_STREQ( hullPart.partName, "hull_part" );
    EXPECT_INT_EQ( hullPart.source, SceneAssetPartSource::ConvexHull );
    EXPECT_UINT_EQ( hullPart.partIndex, 2u );
    EXPECT_UINT_EQ( hullPart.sourceIndex, 1u );
    EXPECT_NEAR( hullPart.posX, 9.0f );
    EXPECT_NEAR( hullPart.posY, 20.0f );
    EXPECT_NEAR( hullPart.posZ, 30.0f );

    const SceneBoxState& box = scene.GetBoxState( 1 );
    EXPECT_NEAR( box.posZ, boxPart.posZ );
    EXPECT_NEAR( box.orientZ, boxPart.orientZ );
    EXPECT_NEAR( box.velX, 1.0f );
    EXPECT_NEAR( box.angVelZ, 6.0f );
    EXPECT_TRUE( box.isFixed );
    EXPECT_TRUE( box.isSleeping );
    const SceneBallState& sphere = scene.GetBallState( 1 );
    EXPECT_NEAR( sphere.posY, spherePart.posY );
    EXPECT_NEAR( sphere.orientZ, spherePart.orientZ );
    EXPECT_NEAR( sphere.velY, 2.0f );
    EXPECT_NEAR( sphere.angVelX, 4.0f );
    const SceneConvexHull& hull = scene.GetConvexHull( 1 );
    EXPECT_TRUE( hull.hasInitQuaternionOrient );
    EXPECT_TRUE( !hull.hasInitOrient );
    EXPECT_NEAR( hull.posX, hullPart.posX );
    EXPECT_NEAR( hull.orientX, hullPart.orientX );
    EXPECT_NEAR( hull.orientY, hullPart.orientY );
    EXPECT_NEAR( hull.orientZ, hullPart.orientZ );
    EXPECT_NEAR( hull.orientW, hullPart.orientW );
    EXPECT_NEAR( hull.velZ, 3.0f );
    EXPECT_NEAR( hull.angVelY, 5.0f );
}

void TestAssetIdentityFailuresAreRecoverable()
{
    static constexpr const char* libraryA = "TestOutput/scene_parser_identity_a.assets.json";
    static constexpr const char* libraryB = "TestOutput/scene_parser_identity_b.assets.json";
    static constexpr const char* scenePath = "TestOutput/scene_parser_identity_failure.scene.json";

    const char* duplicateAsset = R"({
  "format":"skullbonez.asset_library.json","version":1,
  "assets":[{"name":"duplicate","type":"sphere","radius":1,"mass":1,"restitution":0.1,"material":{"mode":"glass"}}]
})";
    WriteTextFile( libraryA, duplicateAsset );
    WriteTextFile( libraryB, duplicateAsset );
    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":1,
  "assetLibraries":["TestOutput/scene_parser_identity_a.assets.json","TestOutput/scene_parser_identity_b.assets.json"],
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}]
})" );
    ExpectSceneLoadFails( scenePath, "Duplicate asset name: duplicate" );

    WriteTextFile( libraryA,
                   R"({
  "format":"skullbonez.asset_library.json","version":1,
  "assets":[{"name":"duplicate.parts","type":"compound","parts":[
    {"name":"same","type":"sphere","radius":1,"mass":1,"restitution":0.1,"material":{"mode":"glass"}},
    {"name":"same","type":"box","halfExtents":[1,1,1],"mass":1,"restitution":0.1,"material":{"mode":"metal"}}
  ]}]
})" );
    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":1,
  "assetLibraries":["TestOutput/scene_parser_identity_a.assets.json"],
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}]
})" );
    ExpectSceneLoadFails( scenePath, "Duplicate asset part name: same" );

    WriteTextFile( libraryA,
                   R"({
  "format":"skullbonez.asset_library.json","version":1,
  "assets":[{"name":"single","type":"sphere","radius":1,"mass":1,"restitution":0.1,"material":{"mode":"glass"}}]
})" );
    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":1,
  "assetLibraries":["TestOutput/scene_parser_identity_a.assets.json"],
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "assetInstances":[
    {"asset":"single","name":"repeated","position":[0,0,0]},
    {"asset":"single","name":"repeated","position":[2,0,0]}
  ]
})" );
    ExpectSceneLoadFails( scenePath, "Duplicate asset instance name: repeated" );

    WriteTextFile( libraryA,
                   R"({
  "format":"skullbonez.asset_library.json","version":1,
  "assets":[{"name":"partial","type":"compound","parts":[
    {"name":"good","type":"sphere","radius":1,"mass":1,"restitution":0.1,"material":{"mode":"glass"}},
    {"name":"collide","type":"box","halfExtents":[1,1,1],"mass":1,"restitution":0.1,"material":{"mode":"metal"}}
  ]}]
})" );
    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":1,
  "assetLibraries":["TestOutput/scene_parser_identity_a.assets.json"],
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "objects":[{"type":"ball","name":"inst_collide","position":[0,0,0],"radius":1,"mass":1,"moment":0.4,"restitution":0.1}],
  "assetInstances":[{"asset":"partial","name":"inst","position":[0,0,0]}]
})" );
    ExpectSceneLoadFails( scenePath, "Duplicate scene object name: inst_collide" );

    WriteTextFile( libraryA,
                   R"({
  "format":"skullbonez.asset_library.json","version":1,
  "assets":[{"name":"head.asset","type":"sphere","radius":1,"mass":1,"restitution":0.1,"material":{"mode":"glass"}}]
})" );
    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":1,
  "assetLibraries":["TestOutput/scene_parser_identity_a.assets.json"],
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "objects":[{"type":"ragdoll","name":"hero","position":[0,0,0]}],
  "assetInstances":[{"asset":"head.asset","name":"hero_head","position":[0,0,0]}]
})" );
    ExpectSceneLoadFails( scenePath, "Duplicate scene object name: hero_head" );

    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":1,
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "objects":[
    {"type":"ragdoll","name":"twins","position":[0,0,0]},
    {"type":"ragdoll","name":"twins","position":[2,0,0]}
  ]
    })" );
    ExpectSceneLoadFails( scenePath, "Duplicate scene object name: twins_torso" );

    const std::string longRagdollPrefix( 53, 'r' );
    const std::string longRagdollScene =
        std::string( R"({
  "format":"skullbonez.scene.json","version":1,
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "objects":[{"type":"ragdoll","name":")" ) +
        longRagdollPrefix + R"(","position":[0,0,0]}]
})";
    WriteTextFile( scenePath, longRagdollScene.c_str() );
    ExpectSceneLoadFails( scenePath, "ragdoll.name produces a part name longer than 63 characters" );
}

void TestSceneObjectIdsAreSchemaVersionedAndStable()
{
    static constexpr const char* libraryPath = "TestOutput/scene_parser_stable_ids.assets.json";
    static constexpr const char* scenePath = "TestOutput/scene_parser_stable_ids.scene.json";
    WriteTextFile( libraryPath,
                   R"({
  "format":"skullbonez.asset_library.json","version":1,
  "assets":[{"name":"mixed","type":"compound","parts":[
    {"name":"box_part","type":"box","halfExtents":[1,1,1],"mass":1,"restitution":0.1,"material":{"mode":"metal"}},
    {"name":"sphere_part","type":"sphere","radius":1,"mass":1,"restitution":0.1,"material":{"mode":"glass"}}
  ]}]
})" );

    // Version 1 upgrades once in the historical runtime section order. IDs then
    // live in parsed rows, so future creation-loop changes cannot reinterpret
    // legacy scene identity.
    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":1,
  "assetLibraries":["TestOutput/scene_parser_stable_ids.assets.json"],
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "objects":[
    {"type":"box","name":"authored_box","position":[0,0,0],"halfExtents":[1,1,1],"mass":1,"restitution":0.1},
    {"type":"ball","name":"authored_ball","position":[2,0,0],"radius":1,"mass":1,"moment":0.4,"restitution":0.1}
  ],
  "assetInstances":[{"asset":"mixed","name":"asset","position":[4,0,0]}]
})" );
    const TestScene upgraded = TestScene::LoadFromFile( scenePath );
    EXPECT_UINT_EQ( upgraded.GetSchemaVersion(), 1u );
    EXPECT_UINT_EQ( upgraded.GetBall( 0 ).sceneObjectId.value, 1u );
    EXPECT_UINT_EQ( upgraded.GetBallState( 0 ).sceneObjectId.value, 2u );
    EXPECT_UINT_EQ( upgraded.GetBox( 0 ).sceneObjectId.value, 3u );
    EXPECT_UINT_EQ( upgraded.GetBoxState( 0 ).sceneObjectId.value, 4u );
    EXPECT_UINT_EQ( upgraded.GetAssetPart( 0 ).sceneObjectId.value, 4u );
    EXPECT_UINT_EQ( upgraded.GetAssetPart( 1 ).sceneObjectId.value, 2u );
    EXPECT_UINT_EQ( upgraded.GetAssetInstance( 0 ).rootSceneObjectId.value, 4u );

    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":2,
  "assetLibraries":["TestOutput/scene_parser_stable_ids.assets.json"],
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "objects":[
    {"type":"box","sceneObjectId":900,"name":"authored_box","position":[0,0,0],"halfExtents":[1,1,1],"mass":1,"restitution":0.1},
    {"type":"ball","sceneObjectId":7,"name":"authored_ball","position":[2,0,0],"radius":1,"mass":1,"moment":0.4,"restitution":0.1}
  ],
  "assetInstances":[{
    "asset":"mixed","name":"asset","position":[4,0,0],
    "parts":[{"name":"box_part","sceneObjectId":300},{"name":"sphere_part","sceneObjectId":42}]
  }]
})" );
    const TestScene explicitIds = TestScene::LoadFromFile( scenePath );
    EXPECT_UINT_EQ( explicitIds.GetSchemaVersion(), 2u );
    EXPECT_UINT_EQ( explicitIds.GetBox( 0 ).sceneObjectId.value, 900u );
    EXPECT_UINT_EQ( explicitIds.GetBall( 0 ).sceneObjectId.value, 7u );
    EXPECT_UINT_EQ( explicitIds.GetAssetPart( 0 ).sceneObjectId.value, 300u );
    EXPECT_UINT_EQ( explicitIds.GetAssetPart( 1 ).sceneObjectId.value, 42u );
    EXPECT_UINT_EQ( explicitIds.GetBoxState( 0 ).sceneObjectId.value, 300u );
    EXPECT_UINT_EQ( explicitIds.GetBallState( 0 ).sceneObjectId.value, 42u );
    EXPECT_UINT_EQ( explicitIds.GetAssetInstance( 0 ).rootSceneObjectId.value, 300u );

    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":2,
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "objects":[{"type":"ball","name":"missing","position":[0,0,0],"radius":1,"mass":1,"moment":0.4,"restitution":0.1}]
})" );
    ExpectSceneLoadFails( scenePath, "ball is missing required field 'sceneObjectId'" );

    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":1,
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "objects":[{"type":"ball","sceneObjectId":12,"name":"wrong_version","position":[0,0,0],"radius":1,"mass":1,"moment":0.4,"restitution":0.1}]
})" );
    ExpectSceneLoadFails( scenePath, "sceneObjectId requires scene schema version 2" );

    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":2,
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "objects":[{"type":"ball","sceneObjectId":0,"name":"zero","position":[0,0,0],"radius":1,"mass":1,"moment":0.4,"restitution":0.1}]
})" );
    ExpectSceneLoadFails( scenePath, "sceneObjectId must be nonzero" );

    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":2,
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "objects":[
    {"type":"ball","sceneObjectId":77,"name":"first","position":[0,0,0],"radius":1,"mass":1,"moment":0.4,"restitution":0.1},
    {"type":"box","sceneObjectId":77,"name":"second","position":[2,0,0],"halfExtents":[1,1,1],"mass":1,"restitution":0.1}
  ]
})" );
    ExpectSceneLoadFails( scenePath, "Duplicate sceneObjectId: 77" );

    WriteTextFile( scenePath,
                   R"({
  "format":"skullbonez.scene.json","version":2,
  "assetLibraries":["TestOutput/scene_parser_stable_ids.assets.json"],
  "cameras":[{"name":"camera","position":[0,0,-10],"view":[0,0,0],"up":[0,1,0]}],
  "assetInstances":[{"asset":"mixed","name":"asset","position":[0,0,0],"parts":[
    {"name":"box_part","sceneObjectId":55},{"name":"sphere_part","sceneObjectId":55}
  ]}]
})" );
    ExpectSceneLoadFails( scenePath, "Duplicate sceneObjectId: 55" );
}

const TestCase kTests[] = {
    { "Style material authoring contract", TestStyleMaterialAuthoringContract },
    { "Scene material authoring sample loads", TestSceneCanLoadMaterialAuthoringSample },
    { "Material authoring rejects malformed options", TestMaterialAuthoringRejectsMalformedOptions },
    { "Asset instance provenance and transform composition", TestAssetInstanceProvenanceAndTransformComposition },
    { "Asset identity failures are recoverable", TestAssetIdentityFailuresAreRecoverable },
    { "Scene object ids are schema-versioned and stable", TestSceneObjectIdsAreSchemaVersionedAndStable },
};

} // namespace

int main()
{
    int failures = 0;

    for ( const TestCase& test : kTests )
    {
        try
        {
            test.run();
            std::cout << "PASS: " << test.name << "\n";
        }
        catch ( const std::exception& ex )
        {
            ++failures;
            std::cerr << "FAIL: " << test.name << "\n";
            std::cerr << "      " << ex.what() << "\n";
        }
    }

    if ( failures != 0 )
    {
        std::cerr << failures << " scene parser unit test(s) failed.\n";
        return 1;
    }

    std::cout << "PASS: all scene parser unit tests passed.\n";
    return 0;
}
