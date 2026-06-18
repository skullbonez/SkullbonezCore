/*
File: Agentic/Tests/SceneParserUnitTests/SceneParserUnitTests.cpp
Purpose:
  Checks scene/style parser contracts that do not need a renderer launch.

Mental model:
  These tests protect user-facing scene authoring syntax. They load checked-in
  fixtures through the same TestScene entry points used by runtime code and then
  inspect the parsed data model.

Glossary:
  Fixture: Checked-in scene/style input file used as stable test data.
  Contract test: Focused test that protects user-visible syntax and parsed
  output rather than renderer screenshots.
  Scene authoring syntax: Text commands accepted by .scene and .style files.

Related:
  - SkullbonezSource/SkullbonezTestScene.h
  - SkullbonezSource/SkullbonezTestSceneParser.cpp
  - SkullbonezData/styles/material_authoring_contract.style
*/
#include "SkullbonezTestScene.h"

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
    explicit TestFailure( const std::string& message )
        : std::runtime_error( message )
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

void ExpectIntEqual( int actual, int expected, const char* actualExpression, const char* expectedExpression, const char* file, int line )
{
    if ( actual != expected )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " == " << expectedExpression << ", actual " << actual << ", expected " << expected;
        Fail( file, line, out.str() );
    }
}

void ExpectUIntEqual( uint32_t actual, uint32_t expected, const char* actualExpression, const char* expectedExpression, const char* file, int line )
{
    if ( actual != expected )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " == " << expectedExpression << ", actual " << actual << ", expected " << expected;
        Fail( file, line, out.str() );
    }
}

void ExpectStringEqual( const char* actual, const char* expected, const char* actualExpression, const char* expectedExpression, const char* file, int line )
{
    if ( strcmp( actual, expected ) != 0 )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " == " << expectedExpression << ", actual \"" << actual << "\", expected \"" << expected << "\"";
        Fail( file, line, out.str() );
    }
}

void ExpectFloatNear( float actual, float expected, const char* actualExpression, const char* expectedExpression, const char* file, int line )
{
    constexpr float epsilon = 0.0001f;
    if ( std::fabs( actual - expected ) > epsilon )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " near " << expectedExpression << ", actual " << actual << ", expected " << expected;
        Fail( file, line, out.str() );
    }
}

void ExpectStringContains( const std::string& actual, const char* expected, const char* actualExpression, const char* expectedExpression, const char* file, int line )
{
    if ( actual.find( expected ) == std::string::npos )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " to contain " << expectedExpression
            << ", actual \"" << actual << "\", expected substring \"" << expected << "\"";
        Fail( file, line, out.str() );
    }
}

#define EXPECT_TRUE( expression ) ExpectTrue( !!( expression ), #expression, __FILE__, __LINE__ )
#define EXPECT_INT_EQ( actual, expected ) ExpectIntEqual( static_cast<int>( actual ), static_cast<int>( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_UINT_EQ( actual, expected ) ExpectUIntEqual( static_cast<uint32_t>( actual ), static_cast<uint32_t>( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_STREQ( actual, expected ) ExpectStringEqual( ( actual ), ( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_NEAR( actual, expected ) ExpectFloatNear( ( actual ), ( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_CONTAINS( actual, expected ) ExpectStringContains( ( actual ), ( expected ), #actual, #expected, __FILE__, __LINE__ )

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
    try
    {
        (void)TestScene::LoadStyleFromFile( path );
    }
    catch ( const std::runtime_error& ex )
    {
        const std::string message = ex.what();
        EXPECT_CONTAINS( message, expectedMessage );
        return;
    }

    Fail( __FILE__, __LINE__, std::string( "expected style load failure for " ) + path );
}

void TestStyleMaterialAuthoringContract()
{
    const TestScene scene = TestScene::LoadStyleFromFile( "SkullbonezData/styles/material_authoring_contract.style" );
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
    const TestScene scene = TestScene::LoadFromFile( "SkullbonezData/scenes/material_authoring_contract.scene" );
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
    ExpectStyleLoadFails( "TestOutput/scene_parser_invalid_missing_equals.style",
                          "object_material bad metal tint=0.1,0.2,0.3 roughness\n",
                          "Invalid object_material option" );
    ExpectStyleLoadFails( "TestOutput/scene_parser_invalid_unknown_option.style",
                          "object_material bad metal tint=0.1,0.2,0.3 shininess=0.7\n",
                          "Unknown object_material option" );
    ExpectStyleLoadFails( "TestOutput/scene_parser_invalid_vec3.style",
                          "object_material bad metal tint=0.1,0.2\n",
                          "expected r,g,b" );
    ExpectStyleLoadFails( "TestOutput/scene_parser_invalid_vec3_extra.style",
                          "object_material bad metal tint=0.1,0.2,0.3,0.4\n",
                          "expected r,g,b" );
    ExpectStyleLoadFails( "TestOutput/scene_parser_invalid_vec3_trailing.style",
                          "object_material bad metal tint=0.1,0.2,0.3,\n",
                          "expected r,g,b" );
}

const TestCase kTests[] = {
    { "Style material authoring contract", TestStyleMaterialAuthoringContract },
    { "Scene material authoring sample loads", TestSceneCanLoadMaterialAuthoringSample },
    { "Material authoring rejects malformed options", TestMaterialAuthoringRejectsMalformedOptions },
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
