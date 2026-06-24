/*
File: SkullbonezSource/Scene/TestSceneParser.cpp
Purpose:
  Loads JSON scene and style descriptions into TestScene data.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Scene, style, and suite files are JSON documents.
  - Command-line scene/style field names are user-facing compatibility surface.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "TestScene.h"
#include "../Assets/AssetSystem.h"
#include "../Physics/ConvexHullShape.h"
#include "../Runtime/Editor/EditorHullAssets.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
using Json = nlohmann::ordered_json;

constexpr int kMaxStyleIncludeDepth = 8;

float LoadConvexHullDefaultMass( const char* hullPath )
{
    const Math::CollisionDetection::ConvexHullShape hull =
        Math::CollisionDetection::ConvexHullShape::LoadFromFile( Assets::ResolveEditorHullAssetPath( hullPath ) );
    return hull.GetDefaultMass();
}

struct SceneIntOption
{
    const char* name;
    int value;
};

template <size_t N> bool TryParseIntOption( const std::string& token, const SceneIntOption ( &options )[N], int& out )
{
    for ( const SceneIntOption& option : options )
    {
        if ( token == option.name )
        {
            out = option.value;
            return true;
        }
    }
    return false;
}

std::string Lowercase( std::string value )
{
    std::transform( value.begin(),
                    value.end(),
                    value.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return value;
}

bool EndsWith( const std::string& value, const char* suffix )
{
    const size_t valueLen = value.size();
    const size_t suffixLen = strlen( suffix );
    return valueLen >= suffixLen && value.compare( valueLen - suffixLen, suffixLen, suffix ) == 0;
}

bool IsSceneNameDigit( char c )
{
    return c >= '0' && c <= '9';
}


bool TryGetEditorTreeInstancePrefixLengthAnyPart( const char* name, size_t& outPrefixLength )
{
    outPrefixLength = 0;
    if ( !name || name[0] == '\0' )
    {
        return false;
    }

    const size_t nameLength = strlen( name );
    size_t marker = nameLength;
    for ( size_t i = 0; i + 5 < nameLength; ++i )
    {
        if ( name[i] == '_' && IsSceneNameDigit( name[i + 1] ) && IsSceneNameDigit( name[i + 2] ) &&
             IsSceneNameDigit( name[i + 3] ) && name[i + 4] == '_' )
        {
            marker = i;
        }
    }

    if ( marker == nameLength )
    {
        return false;
    }

    outPrefixLength = marker + 5;
    return true;
}


bool EditorTreeNamesShareInstancePrefix( const char* a, const char* b, size_t prefixLength )
{
    return a && b && strncmp( a, b, prefixLength ) == 0;
}


template <typename THull> void ApplyRootedTreeCompatibilityClearanceToHulls( std::vector<THull>& hulls )
{
    for ( const THull& root : hulls )
    {
        const float liftY = SkullbonezCore::Assets::EditorTreeRootedAboveRootLiftY( root.name );
        const float legacyRootToTrunkY = SkullbonezCore::Assets::EditorTreeRootedLegacyRootToTrunkDeltaY( root.name );
        const SkullbonezCore::Assets::EditorHullAsset rootAsset =
            SkullbonezCore::Assets::EditorHullAssetFromToken( root.hullPath );
        if ( liftY <= 0.0f || legacyRootToTrunkY <= 0.0f ||
             ( rootAsset != SkullbonezCore::Assets::EditorHullAsset::TREE_ROOT_SMALL &&
               rootAsset != SkullbonezCore::Assets::EditorHullAsset::TREE_ROOT_LARGE ) )
        {
            continue;
        }

        size_t prefixLength = 0;
        if ( !TryGetEditorTreeInstancePrefixLengthAnyPart( root.name, prefixLength ) )
        {
            continue;
        }

        const THull* trunk = nullptr;
        for ( const THull& candidate : hulls )
        {
            const SkullbonezCore::Assets::EditorHullAsset asset =
                SkullbonezCore::Assets::EditorHullAssetFromToken( candidate.hullPath );
            if ( ( asset == SkullbonezCore::Assets::EditorHullAsset::TREE_TRUNK_SMALL_FACETED ||
                   asset == SkullbonezCore::Assets::EditorHullAsset::TREE_TRUNK_FACETED ) &&
                 EditorTreeNamesShareInstancePrefix( root.name, candidate.name, prefixLength ) )
            {
                trunk = &candidate;
                break;
            }
        }

        if ( !trunk || trunk->posY - root.posY >= legacyRootToTrunkY + liftY * 0.5f )
        {
            continue;
        }

        for ( THull& candidate : hulls )
        {
            const SkullbonezCore::Assets::EditorHullAsset asset =
                SkullbonezCore::Assets::EditorHullAssetFromToken( candidate.hullPath );
            if ( candidate.isFixed && asset != SkullbonezCore::Assets::EditorHullAsset::TREE_ROOT_SMALL &&
                 asset != SkullbonezCore::Assets::EditorHullAsset::TREE_ROOT_LARGE &&
                 EditorTreeNamesShareInstancePrefix( root.name, candidate.name, prefixLength ) )
            {
                candidate.posY += liftY;
            }
        }
    }
}

std::string JsonTypeName( const Json& value )
{
    if ( value.is_null() )
    {
        return "null";
    }
    if ( value.is_boolean() )
    {
        return "bool";
    }
    if ( value.is_number() )
    {
        return "number";
    }
    if ( value.is_string() )
    {
        return "string";
    }
    if ( value.is_array() )
    {
        return "array";
    }
    if ( value.is_object() )
    {
        return "object";
    }
    return "value";
}

[[noreturn]] void Fail( const std::string& path, const std::string& detail )
{
    std::ostringstream message;
    message << detail << " in " << path << "  (TestScene::LoadFromFile)";
    throw std::runtime_error( message.str() );
}

void RequireObject( const Json& value, const std::string& path, const char* context )
{
    if ( !value.is_object() )
    {
        std::ostringstream message;
        message << context << " must be an object, got " << JsonTypeName( value );
        Fail( path, message.str() );
    }
}

void RequireArray( const Json& value, const std::string& path, const char* context )
{
    if ( !value.is_array() )
    {
        std::ostringstream message;
        message << context << " must be an array, got " << JsonTypeName( value );
        Fail( path, message.str() );
    }
}

const Json* FindMember( const Json& object, const char* key )
{
    if ( !object.is_object() )
    {
        return nullptr;
    }
    const auto it = object.find( key );
    return it == object.end() ? nullptr : &( *it );
}

const Json& RequireMember( const Json& object, const std::string& path, const char* context, const char* key )
{
    RequireObject( object, path, context );
    const Json* member = FindMember( object, key );
    if ( !member )
    {
        std::ostringstream message;
        message << context << " is missing required field '" << key << "'";
        Fail( path, message.str() );
    }
    return *member;
}

std::string ReadString( const Json& value, const std::string& path, const char* context )
{
    if ( !value.is_string() )
    {
        std::ostringstream message;
        message << context << " must be a string, got " << JsonTypeName( value );
        Fail( path, message.str() );
    }
    return value.get<std::string>();
}

float ReadFloat( const Json& value, const std::string& path, const char* context )
{
    if ( !value.is_number() )
    {
        std::ostringstream message;
        message << context << " must be a number, got " << JsonTypeName( value );
        Fail( path, message.str() );
    }
    return value.get<float>();
}

int ReadInt( const Json& value, const std::string& path, const char* context )
{
    if ( !value.is_number_integer() && !value.is_number_unsigned() )
    {
        std::ostringstream message;
        message << context << " must be an integer, got " << JsonTypeName( value );
        Fail( path, message.str() );
    }
    return value.get<int>();
}

unsigned int ReadUInt( const Json& value, const std::string& path, const char* context )
{
    if ( !value.is_number_integer() && !value.is_number_unsigned() )
    {
        std::ostringstream message;
        message << context << " must be an unsigned integer, got " << JsonTypeName( value );
        Fail( path, message.str() );
    }
    if ( value.is_number_unsigned() )
    {
        const unsigned long long parsed = value.get<unsigned long long>();
        if ( parsed > ( std::numeric_limits<unsigned int>::max )() )
        {
            std::ostringstream message;
            message << context << " must fit in uint32";
            Fail( path, message.str() );
        }
        return static_cast<unsigned int>( parsed );
    }

    const long long parsed = value.get<long long>();
    if ( parsed < 0 || parsed > ( std::numeric_limits<unsigned int>::max )() )
    {
        std::ostringstream message;
        message << context << " must fit in uint32";
        Fail( path, message.str() );
    }
    return static_cast<unsigned int>( parsed );
}

bool TryParseBoolWord( const std::string& value, bool& out )
{
    const std::string token = Lowercase( value );
    if ( token == "on" || token == "open" || token == "all" || token == "true" || token == "yes" )
    {
        out = true;
        return true;
    }
    if ( token == "off" || token == "closed" || token == "none" || token == "false" || token == "no" )
    {
        out = false;
        return true;
    }
    return false;
}

bool ReadBool( const Json& value, const std::string& path, const char* context )
{
    if ( value.is_boolean() )
    {
        return value.get<bool>();
    }
    if ( value.is_number_integer() || value.is_number_unsigned() )
    {
        return value.get<int>() != 0;
    }
    if ( value.is_string() )
    {
        bool parsed = false;
        if ( TryParseBoolWord( value.get<std::string>(), parsed ) )
        {
            return parsed;
        }
    }

    std::ostringstream message;
    message << context << " must be a bool, got " << JsonTypeName( value );
    Fail( path, message.str() );
}

template <size_t N> void CopyStringField( char ( &out )[N], const std::string& text )
{
    strncpy_s( out, N, text.c_str(), _TRUNCATE );
}

template <size_t N>
void ReadRequiredStringField( char ( &out )[N],
                              const Json& object,
                              const std::string& path,
                              const char* context,
                              const char* key )
{
    CopyStringField( out, ReadString( RequireMember( object, path, context, key ), path, key ) );
}

void ReadVec3( const Json& value, const std::string& path, const char* context, float& x, float& y, float& z )
{
    RequireArray( value, path, context );
    if ( value.size() != 3 )
    {
        std::ostringstream message;
        message << context << " must contain exactly 3 numbers";
        Fail( path, message.str() );
    }
    x = ReadFloat( value[0], path, context );
    y = ReadFloat( value[1], path, context );
    z = ReadFloat( value[2], path, context );
}

void ReadVec4( const Json& value, const std::string& path, const char* context, float& x, float& y, float& z, float& w )
{
    RequireArray( value, path, context );
    if ( value.size() != 4 )
    {
        std::ostringstream message;
        message << context << " must contain exactly 4 numbers";
        Fail( path, message.str() );
    }
    x = ReadFloat( value[0], path, context );
    y = ReadFloat( value[1], path, context );
    z = ReadFloat( value[2], path, context );
    w = ReadFloat( value[3], path, context );
}

Json ReadJsonFile( const std::string& path )
{
    std::ifstream input( path );
    if ( !input )
    {
        Fail( path, "Failed to open JSON file" );
    }

    try
    {
        return Json::parse( input );
    }
    catch ( const std::exception& e )
    {
        std::ostringstream message;
        message << "Invalid JSON: " << e.what();
        Fail( path, message.str() );
    }
}

int MaxConfigurableWorkerThreadCount()
{
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return (std::max)( 1, static_cast<int>( hardwareThreads ) );
}

int ParseUITab( const Json& value, const std::string& path )
{
    if ( value.is_number_integer() || value.is_number_unsigned() )
    {
        return value.get<int>();
    }

    const std::string tab = Lowercase( ReadString( value, path, "ui.tab" ) );
    static const SceneIntOption kTabs[] = {
        { "profiler", 0 },
        { "profile", 0 },
        { "overview", 0 },
        { "scene", 1 },
        { "editor", 2 },
        { "placement", 2 },
        { "physics", 3 },
        { "options", 4 },
        { "params", 4 },
        { "render", 5 },
        { "renderer", 5 },
        { "keys", 6 },
        { "controls", 6 },
        { "cinematic", 7 },
        { "cine", 7 },
        { "look", 7 },
    };

    int parsed = 0;
    if ( TryParseIntOption( tab, kTabs, parsed ) )
    {
        return parsed;
    }
    Fail( path, "ui.tab has an unknown tab name: " + tab );
}

int ParseWaterReflectionMode( const Json& value, const std::string& path )
{
    if ( value.is_number_integer() || value.is_number_unsigned() )
    {
        return value.get<int>();
    }

    const std::string mode = Lowercase( ReadString( value, path, "debug.waterReflection" ) );
    static const SceneIntOption kModes[] = {
        { "fbo", 0 },
        { "render_target", 0 },
        { "render-target", 0 },
        { "dxr", 1 },
        { "raytraced", 1 },
        { "ray_traced", 1 },
        { "none", 2 },
        { "off", 2 },
    };
    int parsed = 0;
    if ( TryParseIntOption( mode, kModes, parsed ) )
    {
        return parsed;
    }
    Fail( path, "debug.waterReflection must be fbo, dxr, or none" );
}

uint32_t ParsePhysicsDebugMode( const Json& value, const std::string& path )
{
    const std::string mode = Lowercase( ReadString( value, path, "debug.physics.mode" ) );
    if ( mode == "none" || mode == "off" )
    {
        return Physics::PHYSICS_DEBUG_NONE;
    }
    if ( mode == "axes" )
    {
        return Physics::PHYSICS_DEBUG_AXES;
    }
    if ( mode == "contacts" )
    {
        return Physics::PHYSICS_DEBUG_CONTACTS;
    }
    if ( mode == "sleep" )
    {
        return Physics::PHYSICS_DEBUG_SLEEP;
    }
    if ( mode == "pipeline" )
    {
        return Physics::PHYSICS_DEBUG_PIPELINE;
    }
    if ( mode == "terrain" || mode == "terrain_contact" || mode == "terrain-contact" || mode == "terrain_probe" ||
         mode == "terrain-probe" )
    {
        return Physics::PHYSICS_DEBUG_TERRAIN_CONTACT;
    }
    if ( mode == "all" || mode == "on" )
    {
        return Physics::PHYSICS_DEBUG_ALL;
    }
    Fail( path, "debug.physics.mode must be none, axes, contacts, sleep, pipeline, terrain, or all" );
}

float ParseMaterialModeValue( const Json& value, const std::string& path, const char* context )
{
    if ( value.is_number() )
    {
        return value.get<float>();
    }

    const std::string token = Lowercase( ReadString( value, path, context ) );
    static const SceneIntOption kMaterialModes[] = {
        { "texture",
          static_cast<int>( Rendering::RenderMaterialKindLegacyMode( Rendering::RenderMaterialKind::Textured ) ) },
        { "textured",
          static_cast<int>( Rendering::RenderMaterialKindLegacyMode( Rendering::RenderMaterialKind::Textured ) ) },
        { "beachball",
          static_cast<int>( Rendering::RenderMaterialKindLegacyMode( Rendering::RenderMaterialKind::Textured ) ) },
        { "matte", static_cast<int>( Rendering::RenderMaterialKind::Matte ) },
        { "solid", static_cast<int>( Rendering::RenderMaterialKind::Matte ) },
        { "metal", static_cast<int>( Rendering::RenderMaterialKind::Metal ) },
        { "chrome", static_cast<int>( Rendering::RenderMaterialKind::Metal ) },
        { "emissive", static_cast<int>( Rendering::RenderMaterialKind::Emissive ) },
        { "neon", static_cast<int>( Rendering::RenderMaterialKind::Emissive ) },
        { "glass", static_cast<int>( Rendering::RenderMaterialKind::Glass ) },
        { "toon", static_cast<int>( Rendering::RenderMaterialKind::Toon ) },
        { "pixar", static_cast<int>( Rendering::RenderMaterialKind::Toon ) },
        { "lowpoly", static_cast<int>( Rendering::RenderMaterialKind::LowPoly ) },
        { "low_poly", static_cast<int>( Rendering::RenderMaterialKind::LowPoly ) },
        { "shadow", static_cast<int>( Rendering::RenderMaterialKind::Shadow ) },
        { "black", static_cast<int>( Rendering::RenderMaterialKind::Shadow ) },
        { "foliage", static_cast<int>( Rendering::RenderMaterialKind::Foliage ) },
        { "leaf", static_cast<int>( Rendering::RenderMaterialKind::Foliage ) },
        { "leaves", static_cast<int>( Rendering::RenderMaterialKind::Foliage ) },
        { "bark", static_cast<int>( Rendering::RenderMaterialKind::Bark ) },
        { "trunk", static_cast<int>( Rendering::RenderMaterialKind::Bark ) },
        { "stone", static_cast<int>( Rendering::RenderMaterialKind::Stone ) },
        { "rock", static_cast<int>( Rendering::RenderMaterialKind::Stone ) },
        { "ridge", static_cast<int>( Rendering::RenderMaterialKind::Ridge ) },
        { "distant", static_cast<int>( Rendering::RenderMaterialKind::Ridge ) },
        { "shore", static_cast<int>( Rendering::RenderMaterialKind::Shore ) },
        { "sand", static_cast<int>( Rendering::RenderMaterialKind::Shore ) },
        { "pine", static_cast<int>( Rendering::RenderMaterialKind::Pine ) },
        { "conifer", static_cast<int>( Rendering::RenderMaterialKind::Pine ) },
    };

    int mode = 0;
    if ( TryParseIntOption( token, kMaterialModes, mode ) )
    {
        return static_cast<float>( mode );
    }
    Fail( path, std::string( context ) + " has an unknown material mode: " + token );
}

void SetObjectMaterialBaseColor( SceneObjectMaterialOverride& material, float r, float g, float b )
{
    const bool mirrorEmissiveToBase = material.material.kind == Rendering::RenderMaterialKind::Emissive &&
                                      material.material.emissiveColor[0] == material.material.baseColor[0] &&
                                      material.material.emissiveColor[1] == material.material.baseColor[1] &&
                                      material.material.emissiveColor[2] == material.material.baseColor[2];

    material.tintR = r;
    material.tintG = g;
    material.tintB = b;
    material.material.baseColor[0] = r;
    material.material.baseColor[1] = g;
    material.material.baseColor[2] = b;
    material.material.baseColor[3] = 1.0f;

    if ( mirrorEmissiveToBase )
    {
        material.material.emissiveColor[0] = r;
        material.material.emissiveColor[1] = g;
        material.material.emissiveColor[2] = b;
    }
}

float ReadUnitFloat( const Json& value, const std::string& path, const char* context )
{
    return std::clamp( ReadFloat( value, path, context ), 0.0f, 1.0f );
}

} // namespace

class TestSceneParser
{
  private:
    TestScene m_scene;
    std::vector<Json> m_assetDefinitions;

    std::string ResolveStylePath( const std::string& token ) const
    {
        if ( token.find( '/' ) != std::string::npos || token.find( '\\' ) != std::string::npos ||
             EndsWith( token, ".style.json" ) )
        {
            return token;
        }
        return std::string( "SkullbonezData/styles/" ) + token + ".style.json";
    }

    std::string ResolveAssetLibraryPath( const std::string& token ) const
    {
        if ( token.find( '/' ) != std::string::npos || token.find( '\\' ) != std::string::npos ||
             EndsWith( token, ".assets.json" ) )
        {
            return token;
        }

        const Assets::AssetSystem* assets = Assets::ActiveAssetSystem();
        if ( assets )
        {
            if ( const Assets::AssetLibrarySourceAsset* library = assets->FindAssetLibrarySourceAsset( token.c_str() ) )
            {
                return library->resolvedPath;
            }

            const std::string prefixedToken = std::string( "assetlib." ) + token;
            if ( const Assets::AssetLibrarySourceAsset* library =
                     assets->FindAssetLibrarySourceAsset( prefixedToken.c_str() ) )
            {
                return library->resolvedPath;
            }
        }

        return std::string( "SkullbonezData/assets/" ) + token + ".assets.json";
    }

    const Json* FindAssetDefinition( const std::string& name ) const
    {
        for ( const Json& asset : m_assetDefinitions )
        {
            const Json* assetName = FindMember( asset, "name" );
            if ( assetName && assetName->is_string() && assetName->get<std::string>() == name )
            {
                return &asset;
            }
        }
        return nullptr;
    }

    void ValidateAssetMaterial( const Json& owner, const std::string& path, const char* context ) const
    {
        const Json& material = RequireMember( owner, path, context, "material" );
        RequireObject( material, path, "asset.material" );
        if ( !FindMember( material, "mode" ) && !FindMember( material, "kind" ) )
        {
            Fail( path, "asset.material is missing required field 'mode'" );
        }
        if ( const Json* color = FindMember( material, "color" ) )
        {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            ReadVec3( *color, path, "asset.material.color", r, g, b );
        }
        if ( const Json* colour = FindMember( material, "colour" ) )
        {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            ReadVec3( *colour, path, "asset.material.colour", r, g, b );
        }
        if ( const Json* tint = FindMember( material, "tint" ) )
        {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            ReadVec3( *tint, path, "asset.material.tint", r, g, b );
        }
    }

    void ValidateConvexHullAssetFields( const Json& asset, const std::string& path, const char* context ) const
    {
        ReadString( RequireMember( asset, path, context, "hull" ), path, "asset.hull" );
        if ( const Json* mass = FindMember( asset, "mass" ) )
        {
            ReadFloat( *mass, path, "asset.mass" );
        }
        ReadFloat( RequireMember( asset, path, context, "restitution" ), path, "asset.restitution" );
        ValidateAssetMaterial( asset, path, context );
        if ( const Json* fixed = FindMember( asset, "fixed" ) )
        {
            ReadBool( *fixed, path, "asset.fixed" );
        }
        if ( const Json* sleeping = FindMember( asset, "sleeping" ) )
        {
            ReadBool( *sleeping, path, "asset.sleeping" );
        }
        if ( const Json* offset = FindMember( asset, "offset" ) )
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            ReadVec3( *offset, path, "asset.offset", x, y, z );
        }
        if ( const Json* euler = FindMember( asset, "euler" ) )
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            ReadVec3( *euler, path, "asset.euler", x, y, z );
        }
        if ( const Json* velocity = FindMember( asset, "velocity" ) )
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            ReadVec3( *velocity, path, "asset.velocity", x, y, z );
        }
        if ( const Json* release = FindMember( asset, "contactReleaseOnImpact" ) )
        {
            ReadBool( *release, path, "asset.contactReleaseOnImpact" );
        }
        if ( const Json* threshold = FindMember( asset, "contactReleaseImpulseThreshold" ) )
        {
            (void)(std::max)( 0.0f, ReadFloat( *threshold, path, "asset.contactReleaseImpulseThreshold" ) );
        }
    }

    void LoadAssetLibrary( const std::string& assetPath )
    {
        const Json root = ReadJsonFile( assetPath );
        RequireObject( root, assetPath, "asset library root" );
        const std::string actualFormat =
            ReadString( RequireMember( root, assetPath, "asset library root", "format" ), assetPath, "format" );
        if ( actualFormat != "skullbonez.asset_library.json" )
        {
            std::ostringstream message;
            message << "Expected format 'skullbonez.asset_library.json', got '" << actualFormat << "'";
            Fail( assetPath, message.str() );
        }

        const Json& assets = RequireMember( root, assetPath, "asset library root", "assets" );
        RequireArray( assets, assetPath, "assets" );
        for ( const Json& asset : assets )
        {
            RequireObject( asset, assetPath, "asset" );
            const std::string name =
                ReadString( RequireMember( asset, assetPath, "asset", "name" ), assetPath, "asset.name" );
            if ( name.empty() )
            {
                Fail( assetPath, "asset.name must not be empty" );
            }
            if ( FindAssetDefinition( name ) )
            {
                Fail( assetPath, "Duplicate asset name: " + name );
            }

            const std::string type =
                ReadString( RequireMember( asset, assetPath, "asset", "type" ), assetPath, "asset.type" );
            if ( type == "convexHull" )
            {
                ValidateConvexHullAssetFields( asset, assetPath, "asset" );
            }
            else if ( type == "compound" )
            {
                const Json& parts = RequireMember( asset, assetPath, "asset", "parts" );
                RequireArray( parts, assetPath, "asset.parts" );
                if ( parts.empty() )
                {
                    Fail( assetPath, "asset.parts must not be empty" );
                }
                for ( const Json& part : parts )
                {
                    RequireObject( part, assetPath, "asset.parts[]" );
                    const std::string partName = ReadString( RequireMember( part, assetPath, "asset.parts[]", "name" ),
                                                             assetPath,
                                                             "asset.parts[].name" );
                    if ( partName.empty() )
                    {
                        Fail( assetPath, "asset.parts[].name must not be empty" );
                    }
                    ValidateConvexHullAssetFields( part, assetPath, "asset.parts[]" );
                }
            }
            else
            {
                Fail( assetPath, "Unknown asset type: " + type );
            }

            m_assetDefinitions.push_back( asset );
        }
    }

    void LoadAssetLibraries( const Json& root, const std::string& path )
    {
        const Json* libraries = FindMember( root, "assetLibraries" );
        if ( !libraries )
        {
            return;
        }
        RequireArray( *libraries, path, "assetLibraries" );
        for ( const Json& library : *libraries )
        {
            const std::string token = ReadString( library, path, "assetLibraries" );
            LoadAssetLibrary( ResolveAssetLibraryPath( token ) );
        }
    }

    void CheckGeneratedSceneName( const std::string& name, const std::string& path, const char* context ) const
    {
        if ( name.empty() )
        {
            Fail( path, std::string( context ) + " must not be empty" );
        }
        if ( name.size() >= 64 )
        {
            Fail( path, std::string( context ) + " must be shorter than 64 characters" );
        }
    }

    std::string
    BuildAssetPartName( const std::string& instanceName, const std::string& partName, const std::string& path ) const
    {
        std::string name = instanceName;
        name += "_";
        name += partName;
        CheckGeneratedSceneName( name, path, "asset part generated name" );
        return name;
    }

    void ApplyAssetMaterialForTarget( const Json& asset, const std::string& path, const std::string& target )
    {
        const Json& source = RequireMember( asset, path, "asset", "material" );
        Json material = source;
        RequireObject( material, path, "asset.material" );
        material["target"] = target;
        ApplyObjectMaterial( material, path );
    }

    void ApplyAssetConvexHullPart( const Json& asset,
                                   const std::string& path,
                                   const std::string& objectName,
                                   float baseX,
                                   float baseY,
                                   float baseZ,
                                   float instanceEulerX,
                                   float instanceEulerY,
                                   float instanceEulerZ,
                                   bool hasInstanceEuler,
                                   bool hasFixedOverride,
                                   bool fixedOverride,
                                   bool hasSleepingOverride,
                                   bool sleepingOverride,
                                   bool hasInstanceVelocity,
                                   float instanceVelX,
                                   float instanceVelY,
                                   float instanceVelZ )
    {
        CheckGeneratedSceneName( objectName, path, "asset instance name" );

        float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;
        if ( const Json* offset = FindMember( asset, "offset" ) )
        {
            ReadVec3( *offset, path, "asset.offset", offsetX, offsetY, offsetZ );
        }

        float eulerX = instanceEulerX, eulerY = instanceEulerY, eulerZ = instanceEulerZ;
        bool hasEuler = hasInstanceEuler;
        if ( const Json* euler = FindMember( asset, "euler" ) )
        {
            float partX = 0.0f, partY = 0.0f, partZ = 0.0f;
            ReadVec3( *euler, path, "asset.euler", partX, partY, partZ );
            eulerX += partX;
            eulerY += partY;
            eulerZ += partZ;
            hasEuler = true;
        }

        float velX = instanceVelX, velY = instanceVelY, velZ = instanceVelZ;
        bool hasVelocity = hasInstanceVelocity;
        if ( const Json* velocity = FindMember( asset, "velocity" ) )
        {
            float partX = 0.0f, partY = 0.0f, partZ = 0.0f;
            ReadVec3( *velocity, path, "asset.velocity", partX, partY, partZ );
            velX += partX;
            velY += partY;
            velZ += partZ;
            hasVelocity = true;
        }

        bool fixed = false;
        if ( const Json* fixedValue = FindMember( asset, "fixed" ) )
        {
            fixed = ReadBool( *fixedValue, path, "asset.fixed" );
        }
        if ( hasFixedOverride )
        {
            fixed = fixedOverride;
        }

        bool sleeping = false;
        if ( const Json* sleepingValue = FindMember( asset, "sleeping" ) )
        {
            sleeping = ReadBool( *sleepingValue, path, "asset.sleeping" );
        }
        if ( hasSleepingOverride )
        {
            sleeping = sleepingOverride;
        }

        Json object = Json::object();
        object["name"] = objectName;
        object["hull"] = ReadString( RequireMember( asset, path, "asset", "hull" ), path, "asset.hull" );
        object["position"] = Json::array( { baseX + offsetX, baseY + offsetY, baseZ + offsetZ } );
        if ( const Json* mass = FindMember( asset, "mass" ) )
        {
            object["mass"] = ReadFloat( *mass, path, "asset.mass" );
        }
        object["restitution"] =
            ReadFloat( RequireMember( asset, path, "asset", "restitution" ), path, "asset.restitution" );
        object["fixed"] = fixed;
        object["sleeping"] = sleeping;
        if ( const Json* release = FindMember( asset, "contactReleaseOnImpact" ) )
        {
            object["contactReleaseOnImpact"] = ReadBool( *release, path, "asset.contactReleaseOnImpact" );
        }
        if ( const Json* threshold = FindMember( asset, "contactReleaseImpulseThreshold" ) )
        {
            object["contactReleaseImpulseThreshold"] =
                (std::max)( 0.0f, ReadFloat( *threshold, path, "asset.contactReleaseImpulseThreshold" ) );
        }
        if ( hasEuler )
        {
            object["euler"] = Json::array( { eulerX, eulerY, eulerZ } );
        }
        if ( hasVelocity )
        {
            object["velocity"] = Json::array( { velX, velY, velZ } );
        }

        ApplyConvexHull( object, path, false );
        ApplyAssetMaterialForTarget( asset, path, objectName );
    }

    void ApplyAssetInstance( const Json& instance, const std::string& path )
    {
        RequireObject( instance, path, "assetInstance" );
        const std::string assetName =
            ReadString( RequireMember( instance, path, "assetInstance", "asset" ), path, "assetInstance.asset" );
        const Json* asset = FindAssetDefinition( assetName );
        if ( !asset )
        {
            Fail( path, "Unknown asset instance reference: " + assetName );
        }

        const std::string instanceName =
            ReadString( RequireMember( instance, path, "assetInstance", "name" ), path, "assetInstance.name" );
        CheckGeneratedSceneName( instanceName, path, "assetInstance.name" );

        float baseX = 0.0f, baseY = 0.0f, baseZ = 0.0f;
        ReadVec3( RequireMember( instance, path, "assetInstance", "position" ),
                  path,
                  "assetInstance.position",
                  baseX,
                  baseY,
                  baseZ );

        bool hasFixedOverride = false;
        bool fixedOverride = false;
        if ( const Json* fixed = FindMember( instance, "fixed" ) )
        {
            hasFixedOverride = true;
            fixedOverride = ReadBool( *fixed, path, "assetInstance.fixed" );
        }

        bool hasSleepingOverride = false;
        bool sleepingOverride = false;
        if ( const Json* sleeping = FindMember( instance, "sleeping" ) )
        {
            hasSleepingOverride = true;
            sleepingOverride = ReadBool( *sleeping, path, "assetInstance.sleeping" );
        }

        bool hasInstanceEuler = false;
        float instanceEulerX = 0.0f, instanceEulerY = 0.0f, instanceEulerZ = 0.0f;
        if ( const Json* euler = FindMember( instance, "euler" ) )
        {
            ReadVec3( *euler, path, "assetInstance.euler", instanceEulerX, instanceEulerY, instanceEulerZ );
            hasInstanceEuler = true;
        }

        bool hasInstanceVelocity = false;
        float instanceVelX = 0.0f, instanceVelY = 0.0f, instanceVelZ = 0.0f;
        if ( const Json* velocity = FindMember( instance, "velocity" ) )
        {
            ReadVec3( *velocity, path, "assetInstance.velocity", instanceVelX, instanceVelY, instanceVelZ );
            hasInstanceVelocity = true;
        }

        const std::string type = ReadString( RequireMember( *asset, path, "asset", "type" ), path, "asset.type" );
        if ( type == "convexHull" )
        {
            ApplyAssetConvexHullPart( *asset,
                                      path,
                                      instanceName,
                                      baseX,
                                      baseY,
                                      baseZ,
                                      instanceEulerX,
                                      instanceEulerY,
                                      instanceEulerZ,
                                      hasInstanceEuler,
                                      hasFixedOverride,
                                      fixedOverride,
                                      hasSleepingOverride,
                                      sleepingOverride,
                                      hasInstanceVelocity,
                                      instanceVelX,
                                      instanceVelY,
                                      instanceVelZ );
            return;
        }

        if ( type == "compound" )
        {
            const Json& parts = RequireMember( *asset, path, "asset", "parts" );
            RequireArray( parts, path, "asset.parts" );
            for ( const Json& part : parts )
            {
                const std::string partName =
                    ReadString( RequireMember( part, path, "asset.parts[]", "name" ), path, "asset.parts[].name" );
                ApplyAssetConvexHullPart( part,
                                          path,
                                          BuildAssetPartName( instanceName, partName, path ),
                                          baseX,
                                          baseY,
                                          baseZ,
                                          instanceEulerX,
                                          instanceEulerY,
                                          instanceEulerZ,
                                          hasInstanceEuler,
                                          hasFixedOverride,
                                          fixedOverride,
                                          hasSleepingOverride,
                                          sleepingOverride,
                                          hasInstanceVelocity,
                                          instanceVelX,
                                          instanceVelY,
                                          instanceVelZ );
            }
            return;
        }

        Fail( path, "Unknown asset type: " + type );
    }

    void ApplyAssetInstances( const Json& root, const std::string& path )
    {
        const Json* instances = FindMember( root, "assetInstances" );
        if ( !instances )
        {
            return;
        }
        RequireArray( *instances, path, "assetInstances" );
        for ( const Json& instance : *instances )
        {
            ApplyAssetInstance( instance, path );
        }
    }

    void LoadStyleIncludes( const Json& root, const std::string& path, const char* memberName, int depth )
    {
        const Json* includes = FindMember( root, memberName );
        if ( !includes )
        {
            return;
        }
        RequireArray( *includes, path, memberName );
        for ( const Json& include : *includes )
        {
            const std::string token = ReadString( include, path, memberName );
            const std::string stylePath = ResolveStylePath( token );
            LoadDocumentIntoScene( stylePath, true, depth + 1 );
        }
    }

    void ApplyPlayback( const Json& playback, const std::string& path )
    {
        RequireObject( playback, path, "playback" );
        if ( const Json* frames = FindMember( playback, "frames" ) )
        {
            if ( frames->is_string() )
            {
                const std::string value = Lowercase( frames->get<std::string>() );
                if ( value != "unlimited" )
                {
                    Fail( path, "playback.frames string value must be 'unlimited'" );
                }
                m_scene.m_sceneOptions.frameCount = -1;
            }
            else
            {
                m_scene.m_sceneOptions.frameCount = ReadInt( *frames, path, "playback.frames" );
            }
        }
        if ( const Json* fixedStep = FindMember( playback, "fixedStep" ) )
        {
            m_scene.m_sceneOptions.isFixedStep = ReadBool( *fixedStep, path, "playback.fixedStep" );
        }
        if ( const Json* pauseSnapshotState = FindMember( playback, "pauseSnapshotState" ) )
        {
            m_scene.m_sceneOptions.pauseSnapshotState =
                ReadBool( *pauseSnapshotState, path, "playback.pauseSnapshotState" );
        }
        if ( const Json* exitOnComplete = FindMember( playback, "exitOnComplete" ) )
        {
            m_scene.m_sceneOptions.exitOnComplete = ReadBool( *exitOnComplete, path, "playback.exitOnComplete" );
        }
        if ( const Json* trackHeight = FindMember( playback, "trackHeight" ) )
        {
            const float value = ReadFloat( *trackHeight, path, "playback.trackHeight" );
            if ( value <= 0.0f )
            {
                Fail( path, "playback.trackHeight must be > 0" );
            }
            m_scene.m_sceneOptions.trackHeight = value;
        }
        if ( const Json* autoCycle = FindMember( playback, "autoCycleInterval" ) )
        {
            const float value = ReadFloat( *autoCycle, path, "playback.autoCycleInterval" );
            if ( value <= 0.0f )
            {
                Fail( path, "playback.autoCycleInterval must be > 0" );
            }
            m_scene.m_sceneOptions.autoCycleInterval = value;
        }
    }

    void ApplySimulation( const Json& simulation, const std::string& path )
    {
        RequireObject( simulation, path, "simulation" );
        if ( const Json* physics = FindMember( simulation, "physics" ) )
        {
            m_scene.m_sceneOptions.isPhysicsEnabled = ReadBool( *physics, path, "simulation.physics" );
        }
        if ( const Json* text = FindMember( simulation, "text" ) )
        {
            m_scene.m_sceneOptions.isTextEnabled = ReadBool( *text, path, "simulation.text" );
        }
        if ( const Json* textOnly = FindMember( simulation, "textOnly" ) )
        {
            m_scene.m_sceneOptions.isTextOnly = ReadBool( *textOnly, path, "simulation.textOnly" );
        }
        if ( const Json* seed = FindMember( simulation, "seed" ) )
        {
            m_scene.m_sceneOptions.seed = ReadUInt( *seed, path, "simulation.seed" );
        }
        if ( const Json* timeScale = FindMember( simulation, "timeScale" ) )
        {
            const float value = ReadFloat( *timeScale, path, "simulation.timeScale" );
            if ( value <= 0.0f )
            {
                Fail( path, "simulation.timeScale must be > 0" );
            }
            m_scene.m_sceneOptions.timeScale = value;
        }
        if ( const Json* solverBalls = FindMember( simulation, "solverBalls" ) )
        {
            const int value = ReadInt( *solverBalls, path, "simulation.solverBalls" );
            if ( value < 0 )
            {
                Fail( path, "simulation.solverBalls must be >= 0" );
            }
            m_scene.m_sceneOptions.solverBallCount = value;
        }
        if ( const Json* solverBoxes = FindMember( simulation, "solverBoxes" ) )
        {
            const int value = ReadInt( *solverBoxes, path, "simulation.solverBoxes" );
            if ( value < 0 )
            {
                Fail( path, "simulation.solverBoxes must be >= 0" );
            }
            m_scene.m_sceneOptions.solverBoxCount = value;
        }
        if ( const Json* modelCapacity = FindMember( simulation, "modelCapacity" ) )
        {
            const int value = ReadInt( *modelCapacity, path, "simulation.modelCapacity" );
            if ( value <= 0 || value > MAX_GAME_MODELS )
            {
                Fail( path, "simulation.modelCapacity is out of range" );
            }
            m_scene.m_sceneOptions.modelCapacity = value;
        }
        if ( const Json* workerThreads = FindMember( simulation, "workerThreads" ) )
        {
            const int value = ReadInt( *workerThreads, path, "simulation.workerThreads" );
            if ( value < -1 || value > MaxConfigurableWorkerThreadCount() )
            {
                Fail( path, "simulation.workerThreads is out of range" );
            }
            m_scene.m_sceneOptions.workerThreads = value;
        }
        if ( const Json* world = FindMember( simulation, "world" ) )
        {
            RequireObject( *world, path, "simulation.world" );
            m_scene.m_worldOverride.hasWorldOverride = true;
            m_scene.m_worldOverride.worldGravity =
                ReadFloat( RequireMember( *world, path, "simulation.world", "gravity" ),
                           path,
                           "simulation.world.gravity" );
            m_scene.m_worldOverride.worldFluidHeight =
                ReadFloat( RequireMember( *world, path, "simulation.world", "fluidHeight" ),
                           path,
                           "simulation.world.fluidHeight" );
            m_scene.m_worldOverride.worldFluidDensity =
                ReadFloat( RequireMember( *world, path, "simulation.world", "fluidDensity" ),
                           path,
                           "simulation.world.fluidDensity" );
        }
    }

    void ApplyTornadoFloat( const Json& source,
                            const std::string& path,
                            const char* memberName,
                            float& target,
                            float minimum )
    {
        if ( const Json* value = FindMember( source, memberName ) )
        {
            const std::string context = std::string( "tornadoSystem." ) + memberName;
            target = (std::max)( minimum, ReadFloat( *value, path, context.c_str() ) );
        }
    }

    void ApplyTornadoVortex( const Json& object, const std::string& path, Physics::TornadoSystemConfig& system )
    {
        RequireObject( object, path, "tornadoSystem.vortices[]" );
        Physics::TornadoVortexConfig vortex;
        vortex.field.enabled = true;
        if ( const Json* center = FindMember( object, "center" ) )
        {
            ReadVec3( *center,
                      path,
                      "tornadoSystem.vortices[].center",
                      vortex.field.center.x,
                      vortex.field.center.y,
                      vortex.field.center.z );
        }
        else
        {
            ReadVec3( RequireMember( object, path, "tornadoSystem.vortices[]", "position" ),
                      path,
                      "tornadoSystem.vortices[].position",
                      vortex.field.center.x,
                      vortex.field.center.y,
                      vortex.field.center.z );
        }

        if ( const Json* enabled = FindMember( object, "enabled" ) )
        {
            vortex.field.enabled = ReadBool( *enabled, path, "tornadoSystem.vortices[].enabled" );
        }
        if ( const Json* spawnTime = FindMember( object, "spawnTime" ) )
        {
            vortex.spawnSeconds =
                (std::max)( 0.0f, ReadFloat( *spawnTime, path, "tornadoSystem.vortices[].spawnTime" ) );
        }
        if ( const Json* spawnSeconds = FindMember( object, "spawnSeconds" ) )
        {
            vortex.spawnSeconds =
                (std::max)( 0.0f, ReadFloat( *spawnSeconds, path, "tornadoSystem.vortices[].spawnSeconds" ) );
        }
        if ( const Json* ttl = FindMember( object, "ttl" ) )
        {
            vortex.timeToLiveSeconds = (std::max)( 0.0f, ReadFloat( *ttl, path, "tornadoSystem.vortices[].ttl" ) );
        }
        if ( const Json* timeToLive = FindMember( object, "timeToLive" ) )
        {
            vortex.timeToLiveSeconds =
                (std::max)( 0.0f, ReadFloat( *timeToLive, path, "tornadoSystem.vortices[].timeToLive" ) );
        }
        if ( const Json* timeToLiveSeconds = FindMember( object, "timeToLiveSeconds" ) )
        {
            vortex.timeToLiveSeconds =
                (std::max)( 0.0f, ReadFloat( *timeToLiveSeconds, path, "tornadoSystem.vortices[].timeToLiveSeconds" ) );
        }

        ApplyTornadoFloat( object, path, "growSeconds", vortex.growSeconds, 0.0f );
        ApplyTornadoFloat( object, path, "shrinkSeconds", vortex.shrinkSeconds, 0.0f );
        ApplyTornadoFloat( object, path, "driftRadius", vortex.driftRadius, 0.0f );
        ApplyTornadoFloat( object, path, "driftSpeed", vortex.driftSpeed, 0.0f );
        ApplyTornadoFloat( object, path, "driftPhase", vortex.driftPhase, -100000.0f );
        ApplyTornadoFloat( object, path, "repulsionRadius", vortex.repulsionRadius, 0.0f );
        ApplyTornadoFloat( object, path, "repulsionStrength", vortex.repulsionStrength, 0.0f );
        ApplyTornadoFloat( object, path, "radius", vortex.field.radius, 1.0f );
        ApplyTornadoFloat( object, path, "height", vortex.field.height, 1.0f );
        ApplyTornadoFloat( object, path, "inwardAcceleration", vortex.field.inwardAcceleration, 0.0f );
        ApplyTornadoFloat( object, path, "swirlAcceleration", vortex.field.swirlAcceleration, 0.0f );
        ApplyTornadoFloat( object, path, "liftAcceleration", vortex.field.liftAcceleration, 0.0f );
        ApplyTornadoFloat( object, path, "ejectAcceleration", vortex.field.ejectAcceleration, 0.0f );
        ApplyTornadoFloat( object, path, "ejectUpAcceleration", vortex.field.ejectUpAcceleration, 0.0f );
        ApplyTornadoFloat( object, path, "ejectBand", vortex.field.ejectBand, 0.0f );
        ApplyTornadoFloat( object, path, "minCaptureSeconds", vortex.field.minCaptureSeconds, 0.0f );
        ApplyTornadoFloat( object, path, "ejectCooldownSeconds", vortex.field.ejectCooldownSeconds, 0.0f );
        ApplyTornadoFloat( object, path, "maxDeltaVelocity", vortex.field.maxDeltaVelocity, 1.0f );
        if ( const Json* vectors = FindMember( object, "visualizeVelocityField" ) )
        {
            vortex.field.visualizeVelocityField =
                ReadBool( *vectors, path, "tornadoSystem.vortices[].visualizeVelocityField" );
        }

        system.vortices.push_back( vortex );
    }

    void ApplyTornadoSystem( const Json& tornadoSystem, const std::string& path )
    {
        RequireObject( tornadoSystem, path, "tornadoSystem" );
        Physics::TornadoSystemConfig system;
        system.enabled = true;
        if ( const Json* enabled = FindMember( tornadoSystem, "enabled" ) )
        {
            system.enabled = ReadBool( *enabled, path, "tornadoSystem.enabled" );
        }
        if ( const Json* vectors = FindMember( tornadoSystem, "visualizeVelocityField" ) )
        {
            system.visualizeVelocityField = ReadBool( *vectors, path, "tornadoSystem.visualizeVelocityField" );
        }

        const Json& vortices = RequireMember( tornadoSystem, path, "tornadoSystem", "vortices" );
        RequireArray( vortices, path, "tornadoSystem.vortices" );
        for ( const Json& vortex : vortices )
        {
            ApplyTornadoVortex( vortex, path, system );
        }
        if ( system.vortices.empty() )
        {
            Fail( path, "tornadoSystem.vortices must not be empty" );
        }

        m_scene.m_tornadoSystem.hasTornadoSystem = true;
        m_scene.m_tornadoSystem.config = system;
    }

    void ApplyRuntime( const Json& runtime, const std::string& path )
    {
        RequireObject( runtime, path, "runtime" );
        if ( const Json* vsync = FindMember( runtime, "vsync" ) )
        {
            m_scene.m_runtimeOverrides.hasVsyncOverride = true;
            m_scene.m_runtimeOverrides.isVsyncEnabled = ReadBool( *vsync, path, "runtime.vsync" );
        }
        if ( const Json* pipelineSync = FindMember( runtime, "pipelineSync" ) )
        {
            m_scene.m_runtimeOverrides.hasPipelineSyncOverride = true;
            m_scene.m_runtimeOverrides.isPipelineSyncEnabled = ReadBool( *pipelineSync, path, "runtime.pipelineSync" );
        }
    }

    void ApplyCapture( const Json& capture, const std::string& path )
    {
        RequireObject( capture, path, "capture" );
        if ( const Json* screenshot = FindMember( capture, "screenshot" ) )
        {
            RequireObject( *screenshot, path, "capture.screenshot" );
            CopyStringField( m_scene.m_captureOptions.screenshotPath,
                             ReadString( RequireMember( *screenshot, path, "capture.screenshot", "path" ),
                                         path,
                                         "capture.screenshot.path" ) );
            if ( const Json* frame = FindMember( *screenshot, "frame" ) )
            {
                m_scene.m_captureOptions.screenshotFrame = ReadInt( *frame, path, "capture.screenshot.frame" );
                m_scene.m_captureOptions.screenshotMs = -1;
            }
            if ( const Json* ms = FindMember( *screenshot, "ms" ) )
            {
                m_scene.m_captureOptions.screenshotMs = ReadInt( *ms, path, "capture.screenshot.ms" );
                m_scene.m_captureOptions.screenshotFrame = -1;
            }
        }
        if ( const Json* screenshotAndExit = FindMember( capture, "screenshotAndExit" ) )
        {
            m_scene.m_sceneOptions.screenshotAndExit =
                ReadBool( *screenshotAndExit, path, "capture.screenshotAndExit" );
        }
        if ( const Json* interval = FindMember( capture, "interval" ) )
        {
            RequireObject( *interval, path, "capture.interval" );
            CopyStringField( m_scene.m_captureOptions.screenshotDir,
                             ReadString( RequireMember( *interval, path, "capture.interval", "dir" ),
                                         path,
                                         "capture.interval.dir" ) );
            const int frames = ReadInt( RequireMember( *interval, path, "capture.interval", "frames" ),
                                        path,
                                        "capture.interval.frames" );
            if ( frames <= 0 )
            {
                Fail( path, "capture.interval.frames must be > 0" );
            }
            m_scene.m_captureOptions.screenshotInterval = frames;
        }
    }

    void ApplyLogging( const Json& logging, const std::string& path )
    {
        RequireObject( logging, path, "logging" );
        if ( const Json* perfLog = FindMember( logging, "perfLog" ) )
        {
            CopyStringField( m_scene.m_loggingOptions.perfLogPath, ReadString( *perfLog, path, "logging.perfLog" ) );
        }
        if ( const Json* flush = FindMember( logging, "perfLogFlush" ) )
        {
            m_scene.m_loggingOptions.isPerfLogFlush = ReadBool( *flush, path, "logging.perfLogFlush" );
        }
        if ( const Json* interval = FindMember( logging, "perfLogFlushInterval" ) )
        {
            m_scene.m_loggingOptions.perfLogFlushInterval = ReadInt( *interval, path, "logging.perfLogFlushInterval" );
        }
    }

    void ApplyPhysicsDebug( const Json& debug, const std::string& path )
    {
        RequireObject( debug, path, "debug.physics" );
        if ( const Json* mode = FindMember( debug, "mode" ) )
        {
            m_scene.m_sceneOptions.physicsDebugFlags = ParsePhysicsDebugMode( *mode, path );
        }

        const auto applyFlag = [&]( const char* key, uint32_t flag )
        {
            if ( const Json* value = FindMember( debug, key ) )
            {
                if ( ReadBool( *value, path, key ) )
                {
                    m_scene.m_sceneOptions.physicsDebugFlags |= flag;
                }
                else
                {
                    m_scene.m_sceneOptions.physicsDebugFlags &= ~flag;
                }
            }
        };
        applyFlag( "axes", Physics::PHYSICS_DEBUG_AXES );
        applyFlag( "contacts", Physics::PHYSICS_DEBUG_CONTACTS );
        applyFlag( "sleep", Physics::PHYSICS_DEBUG_SLEEP );
        applyFlag( "pipeline", Physics::PHYSICS_DEBUG_PIPELINE );
        applyFlag( "terrainContact", Physics::PHYSICS_DEBUG_TERRAIN_CONTACT );

        if ( const Json* transparent = FindMember( debug, "transparent" ) )
        {
            m_scene.m_sceneOptions.physicsDebugTransparent =
                ReadBool( *transparent, path, "debug.physics.transparent" );
        }
        if ( const Json* alpha = FindMember( debug, "alpha" ) )
        {
            const float value = ReadFloat( *alpha, path, "debug.physics.alpha" );
            if ( value < 0.05f || value > 1.0f )
            {
                Fail( path, "debug.physics.alpha must be 0.05..1.0" );
            }
            m_scene.m_sceneOptions.physicsDebugAlpha = value;
        }
        if ( const Json* linger = FindMember( debug, "contactLinger" ) )
        {
            const float value = ReadFloat( *linger, path, "debug.physics.contactLinger" );
            if ( value < 0.0f || value > 5.0f )
            {
                Fail( path, "debug.physics.contactLinger must be 0.0..5.0" );
            }
            m_scene.m_sceneOptions.physicsDebugContactLinger = value;
        }
    }

    void ApplyDebug( const Json& debug, const std::string& path )
    {
        RequireObject( debug, path, "debug" );
        if ( const Json* collisionVisualizer = FindMember( debug, "collisionVisualizer" ) )
        {
            m_scene.m_sceneOptions.collisionVisualizer =
                ReadBool( *collisionVisualizer, path, "debug.collisionVisualizer" );
        }
        if ( const Json* broadphaseOverlay = FindMember( debug, "broadphaseOverlay" ) )
        {
            m_scene.m_sceneOptions.broadphaseOverlay = ReadBool( *broadphaseOverlay, path, "debug.broadphaseOverlay" );
        }
        if ( const Json* waterFreeze = FindMember( debug, "waterFreeze" ) )
        {
            m_scene.m_sceneOptions.waterFreezeDebug = ReadBool( *waterFreeze, path, "debug.waterFreeze" );
        }
        if ( const Json* waterFlat = FindMember( debug, "waterFlat" ) )
        {
            m_scene.m_sceneOptions.waterFlatDebug = ReadBool( *waterFlat, path, "debug.waterFlat" );
        }
        if ( const Json* waterReflection = FindMember( debug, "waterReflection" ) )
        {
            m_scene.m_sceneOptions.waterReflectionMode = ParseWaterReflectionMode( *waterReflection, path );
        }
        if ( const Json* waterHidden = FindMember( debug, "waterHidden" ) )
        {
            m_scene.m_sceneOptions.waterHidden = ReadBool( *waterHidden, path, "debug.waterHidden" );
        }
        if ( const Json* terrainHidden = FindMember( debug, "terrainHidden" ) )
        {
            m_scene.m_sceneOptions.terrainHidden = ReadBool( *terrainHidden, path, "debug.terrainHidden" );
        }
        if ( const Json* physics = FindMember( debug, "physics" ) )
        {
            ApplyPhysicsDebug( *physics, path );
        }
    }

    void ApplyTerrain( const Json& terrain, const std::string& path )
    {
        RequireObject( terrain, path, "terrain" );
        if ( const Json* flatSlope = FindMember( terrain, "flatSlope" ) )
        {
            RequireObject( *flatSlope, path, "terrain.flatSlope" );
            m_scene.m_terrainOverride.hasFlatSlope = true;
            m_scene.m_terrainOverride.flatBaseY =
                ReadFloat( RequireMember( *flatSlope, path, "terrain.flatSlope", "baseY" ),
                           path,
                           "terrain.flatSlope.baseY" );
            m_scene.m_terrainOverride.flatSlopeX =
                ReadFloat( RequireMember( *flatSlope, path, "terrain.flatSlope", "slopeX" ),
                           path,
                           "terrain.flatSlope.slopeX" );
            m_scene.m_terrainOverride.flatSlopeZ =
                ReadFloat( RequireMember( *flatSlope, path, "terrain.flatSlope", "slopeZ" ),
                           path,
                           "terrain.flatSlope.slopeZ" );
        }
    }

    void ApplyEditor( const Json& editor, const std::string& path )
    {
        RequireObject( editor, path, "editor" );
        if ( const Json* editable = FindMember( editor, "editableScene" ) )
        {
            m_scene.m_sceneOptions.editableScene = ReadBool( *editable, path, "editor.editableScene" );
        }
    }

    void ApplyUI( const Json& ui, const std::string& path )
    {
        RequireObject( ui, path, "ui" );
        SceneUIOptions& out = m_scene.m_UIOptions;
        out.hasSettings = true;

        if ( const Json* visible = FindMember( ui, "visible" ) )
        {
            out.hasVisible = true;
            out.isVisible = ReadBool( *visible, path, "ui.visible" );
        }
        if ( const Json* minimized = FindMember( ui, "minimized" ) )
        {
            out.hasMinimized = true;
            out.isMinimized = ReadBool( *minimized, path, "ui.minimized" );
        }
        if ( const Json* tab = FindMember( ui, "tab" ) )
        {
            out.hasActiveTab = true;
            out.activeTab = ParseUITab( *tab, path );
        }
        if ( const Json* rect = FindMember( ui, "rect" ) )
        {
            RequireArray( *rect, path, "ui.rect" );
            if ( rect->size() != 4 )
            {
                Fail( path, "ui.rect must contain exactly 4 integers" );
            }
            out.hasWindowRect = true;
            out.windowX = ReadInt( ( *rect )[0], path, "ui.rect[0]" );
            out.windowY = ReadInt( ( *rect )[1], path, "ui.rect[1]" );
            out.windowW = ReadInt( ( *rect )[2], path, "ui.rect[2]" );
            out.windowH = ReadInt( ( *rect )[3], path, "ui.rect[3]" );
        }
        if ( const Json* blur = FindMember( ui, "blur" ) )
        {
            out.hasBlur = true;
            out.blurEnabled = ReadBool( *blur, path, "ui.blur" );
        }
        if ( const Json* rendererCombo = FindMember( ui, "rendererCombo" ) )
        {
            out.hasRendererComboOpen = true;
            out.rendererComboOpen = ReadBool( *rendererCombo, path, "ui.rendererCombo" );
        }
        if ( const Json* waterCombo = FindMember( ui, "waterCombo" ) )
        {
            out.hasWaterComboOpen = true;
            out.waterComboOpen = ReadBool( *waterCombo, path, "ui.waterCombo" );
        }
        if ( const Json* sceneCombo = FindMember( ui, "sceneCombo" ) )
        {
            out.hasSceneComboOpen = true;
            out.sceneComboOpen = ReadBool( *sceneCombo, path, "ui.sceneCombo" );
        }
        if ( const Json* sceneFilter = FindMember( ui, "sceneFilter" ) )
        {
            out.hasSceneFilter = true;
            CopyStringField( out.sceneFilter, ReadString( *sceneFilter, path, "ui.sceneFilter" ) );
        }
        if ( const Json* profilerExpand = FindMember( ui, "profilerExpand" ) )
        {
            out.hasProfilerExpandAll = true;
            out.profilerExpandAll = ReadBool( *profilerExpand, path, "ui.profilerExpand" );
        }
        if ( const Json* timeline = FindMember( ui, "timeline" ) )
        {
            out.hasProfilerTimeline = true;
            out.profilerTimeline = ReadBool( *timeline, path, "ui.timeline" );
        }
        if ( const Json* histogram = FindMember( ui, "histogram" ) )
        {
            out.hasPerformanceHistogram = true;
            out.performanceHistogram = ReadBool( *histogram, path, "ui.histogram" );
        }
        if ( const Json* hitboxes = FindMember( ui, "hitboxes" ) )
        {
            out.hasHitboxOverlay = true;
            out.hitboxOverlay = ReadBool( *hitboxes, path, "ui.hitboxes" );
        }
        if ( const Json* scroll = FindMember( ui, "scroll" ) )
        {
            out.hasScrollY = true;
            if ( scroll->is_string() && Lowercase( scroll->get<std::string>() ) == "bottom" )
            {
                out.scrollY = 1000000.0f;
            }
            else
            {
                out.scrollY = ReadFloat( *scroll, path, "ui.scroll" );
            }
        }
        if ( const Json* mouse = FindMember( ui, "mouse" ) )
        {
            RequireArray( *mouse, path, "ui.mouse" );
            if ( mouse->size() != 2 )
            {
                Fail( path, "ui.mouse must contain exactly 2 integers" );
            }
            out.hasMouseOverride = true;
            out.mouseX = ReadInt( ( *mouse )[0], path, "ui.mouse[0]" );
            out.mouseY = ReadInt( ( *mouse )[1], path, "ui.mouse[1]" );
        }
        if ( const Json* stress = FindMember( ui, "stress" ) )
        {
            out.hasStress = true;
            out.stressEnabled = ReadBool( *stress, path, "ui.stress" );
        }
        if ( const Json* stressSeed = FindMember( ui, "stressSeed" ) )
        {
            out.hasStressSeed = true;
            out.stressSeed = ReadUInt( *stressSeed, path, "ui.stressSeed" );
        }
        if ( const Json* stressActions = FindMember( ui, "stressActions" ) )
        {
            const int actions = ReadInt( *stressActions, path, "ui.stressActions" );
            if ( actions < 0 )
            {
                Fail( path, "ui.stressActions must be >= 0" );
            }
            out.hasStressActions = true;
            out.stressActionsPerFrame = actions;
        }
        if ( const Json* testPattern = FindMember( ui, "testPattern" ) )
        {
            out.hasTestPattern = true;
            out.testPatternEnabled = ReadBool( *testPattern, path, "ui.testPattern" );
        }
    }

    void ApplyCinematicBool( const Json& cinematic, const std::string& path )
    {
        struct BoolField
        {
            const char* key;
            bool CinematicRenderConfig::* field;
            uint64_t bit;
        };
        static constexpr BoolField kFields[] = {
            { "rendering", &CinematicRenderConfig::enabled, SCENE_CINE_RENDERING },
            { "skyAtmosphere", &CinematicRenderConfig::skyAtmosphereEnabled, SCENE_CINE_SKY_ATMOSPHERE },
            { "clouds", &CinematicRenderConfig::cloudsEnabled, SCENE_CINE_CLOUDS },
            { "godRays", &CinematicRenderConfig::godRaysEnabled, SCENE_CINE_GOD_RAYS },
            { "volumetricLighting", &CinematicRenderConfig::volumetricLightingEnabled, SCENE_CINE_VOLUMETRIC_LIGHTING },
            { "bloom", &CinematicRenderConfig::bloomEnabled, SCENE_CINE_BLOOM },
            { "fog", &CinematicRenderConfig::fogEnabled, SCENE_CINE_FOG },
            { "terrainReliefEnabled", &CinematicRenderConfig::terrainReliefEnabled, SCENE_CINE_TERRAIN_RELIEF_ENABLED },
            { "shadows", &CinematicRenderConfig::shadowsEnabled, SCENE_CINE_SHADOWS },
        };

        for ( const BoolField& field : kFields )
        {
            if ( const Json* value = FindMember( cinematic, field.key ) )
            {
                const bool parsed = ReadBool( *value, path, field.key );
                m_scene.m_sceneOptions.cinematicRender.*( field.field ) = parsed;
                m_scene.m_sceneOptions.cinematicOverrideMask |= field.bit;
                if ( field.bit == SCENE_CINE_RENDERING )
                {
                    m_scene.m_sceneOptions.hasCinematicRenderingOverride = true;
                    m_scene.m_sceneOptions.cinematicRendering = parsed;
                }
            }
        }
    }

    void ApplyCinematicInt( const Json& cinematic, const std::string& path )
    {
        struct IntField
        {
            const char* key;
            int CinematicRenderConfig::* field;
            uint64_t bit;
            int minValue;
            int maxValue;
        };
        static constexpr IntField kFields[] = {
            { "shadowMapSize", &CinematicRenderConfig::shadowMapSize, SCENE_CINE_SHADOW_MAP_SIZE, 256, 8192 },
            { "shadowPcfRadius", &CinematicRenderConfig::shadowPcfRadius, SCENE_CINE_SHADOW_PCF_RADIUS, 0, 3 },
        };

        for ( const IntField& field : kFields )
        {
            if ( const Json* value = FindMember( cinematic, field.key ) )
            {
                const int parsed = ReadInt( *value, path, field.key );
                if ( parsed < field.minValue || parsed > field.maxValue )
                {
                    std::ostringstream message;
                    message << "cinematic." << field.key << " must be " << field.minValue << ".." << field.maxValue;
                    Fail( path, message.str() );
                }
                m_scene.m_sceneOptions.cinematicRender.*( field.field ) = parsed;
                m_scene.m_sceneOptions.cinematicOverrideMask |= field.bit;
            }
        }
    }

    void ApplyCinematicFloat( const Json& cinematic, const std::string& path )
    {
        struct FloatField
        {
            const char* key;
            float CinematicRenderConfig::* field;
            uint64_t bit;
            float minValue;
            float maxValue;
        };
        static constexpr FloatField kFields[] = {
            { "exposure", &CinematicRenderConfig::exposure, SCENE_CINE_EXPOSURE, 0.0f, 16.0f },
            { "gamma", &CinematicRenderConfig::gamma, SCENE_CINE_GAMMA, 0.1f, 8.0f },
            { "sunScreenX", &CinematicRenderConfig::sunScreenX, SCENE_CINE_SUN_SCREEN_X, 0.0f, 1.0f },
            { "sunScreenY", &CinematicRenderConfig::sunScreenY, SCENE_CINE_SUN_SCREEN_Y, 0.0f, 1.0f },
            { "sunColorR", &CinematicRenderConfig::sunColorR, SCENE_CINE_SUN_COLOR_R, 0.0f, 4.0f },
            { "sunColorG", &CinematicRenderConfig::sunColorG, SCENE_CINE_SUN_COLOR_G, 0.0f, 4.0f },
            { "sunColorB", &CinematicRenderConfig::sunColorB, SCENE_CINE_SUN_COLOR_B, 0.0f, 4.0f },
            { "sunIntensity", &CinematicRenderConfig::sunIntensity, SCENE_CINE_SUN_INTENSITY, 0.0f, 80.0f },
            { "skyHorizonR", &CinematicRenderConfig::skyHorizonR, SCENE_CINE_SKY_HORIZON_R, 0.0f, 4.0f },
            { "skyHorizonG", &CinematicRenderConfig::skyHorizonG, SCENE_CINE_SKY_HORIZON_G, 0.0f, 4.0f },
            { "skyHorizonB", &CinematicRenderConfig::skyHorizonB, SCENE_CINE_SKY_HORIZON_B, 0.0f, 4.0f },
            { "skyZenithR", &CinematicRenderConfig::skyZenithR, SCENE_CINE_SKY_ZENITH_R, 0.0f, 4.0f },
            { "skyZenithG", &CinematicRenderConfig::skyZenithG, SCENE_CINE_SKY_ZENITH_G, 0.0f, 4.0f },
            { "skyZenithB", &CinematicRenderConfig::skyZenithB, SCENE_CINE_SKY_ZENITH_B, 0.0f, 4.0f },
            { "skyGlowStrength", &CinematicRenderConfig::skyGlowStrength, SCENE_CINE_SKY_GLOW_STRENGTH, 0.0f, 16.0f },
            { "cloudCoverage", &CinematicRenderConfig::cloudCoverage, SCENE_CINE_CLOUD_COVERAGE, 0.0f, 1.0f },
            { "cloudSoftness", &CinematicRenderConfig::cloudSoftness, SCENE_CINE_CLOUD_SOFTNESS, 0.001f, 1.0f },
            { "cloudScale", &CinematicRenderConfig::cloudScale, SCENE_CINE_CLOUD_SCALE, 0.1f, 64.0f },
            { "cloudIntensity", &CinematicRenderConfig::cloudIntensity, SCENE_CINE_CLOUD_INTENSITY, 0.0f, 4.0f },
            { "sunShaftStrength", &CinematicRenderConfig::sunShaftStrength, SCENE_CINE_SUN_SHAFT_STRENGTH, 0.0f, 8.0f },
            { "sunShaftFalloff", &CinematicRenderConfig::sunShaftFalloff, SCENE_CINE_SUN_SHAFT_FALLOFF, 0.1f, 10.0f },
            { "volumetricStrength",
              &CinematicRenderConfig::volumetricStrength,
              SCENE_CINE_VOLUMETRIC_STRENGTH,
              0.0f,
              8.0f },
            { "volumetricDensity",
              &CinematicRenderConfig::volumetricDensity,
              SCENE_CINE_VOLUMETRIC_DENSITY,
              0.0f,
              8.0f },
            { "volumetricDecay", &CinematicRenderConfig::volumetricDecay, SCENE_CINE_VOLUMETRIC_DECAY, 0.0f, 1.0f },
            { "bloomThreshold", &CinematicRenderConfig::bloomThreshold, SCENE_CINE_BLOOM_THRESHOLD, 0.0f, 16.0f },
            { "bloomKnee", &CinematicRenderConfig::bloomKnee, SCENE_CINE_BLOOM_KNEE, 0.001f, 8.0f },
            { "bloomStrength", &CinematicRenderConfig::bloomStrength, SCENE_CINE_BLOOM_STRENGTH, 0.0f, 8.0f },
            { "bloomRadius", &CinematicRenderConfig::bloomRadius, SCENE_CINE_BLOOM_RADIUS, 0.1f, 32.0f },
            { "terrainRelief", &CinematicRenderConfig::terrainRelief, SCENE_CINE_TERRAIN_RELIEF, 0.0f, 4.0f },
            { "basinDepth", &CinematicRenderConfig::basinDepth, SCENE_CINE_BASIN_DEPTH, 0.0f, 256.0f },
            { "basinRimLift", &CinematicRenderConfig::basinRimLift, SCENE_CINE_BASIN_RIM_LIFT, 0.0f, 256.0f },
            { "shadowStrength", &CinematicRenderConfig::shadowStrength, SCENE_CINE_SHADOW_STRENGTH, 0.0f, 1.0f },
            { "shadowSoftness", &CinematicRenderConfig::shadowSoftness, SCENE_CINE_SHADOW_SOFTNESS, 0.25f, 4.0f },
            { "shadowDepthBias", &CinematicRenderConfig::shadowDepthBias, SCENE_CINE_SHADOW_DEPTH_BIAS, 0.0f, 0.05f },
            { "shadowSlopeBias", &CinematicRenderConfig::shadowSlopeBias, SCENE_CINE_SHADOW_SLOPE_BIAS, 0.0f, 0.05f },
            { "shadowMaxDistance",
              &CinematicRenderConfig::shadowMaxDistance,
              SCENE_CINE_SHADOW_MAX_DISTANCE,
              128.0f,
              10000.0f },
            { "fogColorR", &CinematicRenderConfig::fogColorR, SCENE_CINE_FOG_COLOR_R, 0.0f, 4.0f },
            { "fogColorG", &CinematicRenderConfig::fogColorG, SCENE_CINE_FOG_COLOR_G, 0.0f, 4.0f },
            { "fogColorB", &CinematicRenderConfig::fogColorB, SCENE_CINE_FOG_COLOR_B, 0.0f, 4.0f },
            { "fogStart", &CinematicRenderConfig::fogStart, SCENE_CINE_FOG_START, 0.0f, 10000.0f },
            { "fogEnd", &CinematicRenderConfig::fogEnd, SCENE_CINE_FOG_END, 0.0f, 20000.0f },
            { "fogDensity", &CinematicRenderConfig::fogDensity, SCENE_CINE_FOG_DENSITY, 0.0f, 0.1f },
            { "fogMaxOpacity", &CinematicRenderConfig::fogMaxOpacity, SCENE_CINE_FOG_MAX_OPACITY, 0.0f, 1.0f },
        };

        for ( const FloatField& field : kFields )
        {
            if ( const Json* value = FindMember( cinematic, field.key ) )
            {
                const float parsed = ReadFloat( *value, path, field.key );
                if ( parsed < field.minValue || parsed > field.maxValue )
                {
                    std::ostringstream message;
                    message << "cinematic." << field.key << " must be " << field.minValue << ".." << field.maxValue;
                    Fail( path, message.str() );
                }
                m_scene.m_sceneOptions.cinematicRender.*( field.field ) = parsed;
                m_scene.m_sceneOptions.cinematicOverrideMask |= field.bit;
                if ( field.bit == SCENE_CINE_EXPOSURE )
                {
                    m_scene.m_sceneOptions.hasCinematicExposure = true;
                    m_scene.m_sceneOptions.cinematicExposure = parsed;
                }
                else if ( field.bit == SCENE_CINE_GAMMA )
                {
                    m_scene.m_sceneOptions.hasCinematicGamma = true;
                    m_scene.m_sceneOptions.cinematicGamma = parsed;
                }
            }
        }
    }

    void ApplyCinematicVector( const Json& cinematic, const std::string& path )
    {
        CinematicRenderConfig& c = m_scene.m_sceneOptions.cinematicRender;

        if ( const Json* styleModes = FindMember( cinematic, "styleModes" ) )
        {
            RequireArray( *styleModes, path, "cinematic.styleModes" );
            if ( styleModes->size() != 4 )
            {
                Fail( path, "cinematic.styleModes must contain exactly 4 integers" );
            }
            c.skyMode = ReadInt( ( *styleModes )[0], path, "cinematic.styleModes[0]" );
            c.terrainMode = ReadInt( ( *styleModes )[1], path, "cinematic.styleModes[1]" );
            c.objectStyle = ReadInt( ( *styleModes )[2], path, "cinematic.styleModes[2]" );
            c.waterMode = ReadInt( ( *styleModes )[3], path, "cinematic.styleModes[3]" );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        }
        if ( const Json* styleGrade = FindMember( cinematic, "styleGrade" ) )
        {
            ReadVec3( *styleGrade, path, "cinematic.styleGrade", c.styleSaturation, c.styleContrast, c.styleVignette );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        }
        if ( const Json* terrainTint = FindMember( cinematic, "terrainTint" ) )
        {
            ReadVec3( *terrainTint, path, "cinematic.terrainTint", c.terrainTintR, c.terrainTintG, c.terrainTintB );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        }
        if ( const Json* terrainAccent = FindMember( cinematic, "terrainAccent" ) )
        {
            ReadVec3( *terrainAccent,
                      path,
                      "cinematic.terrainAccent",
                      c.terrainAccentR,
                      c.terrainAccentG,
                      c.terrainAccentB );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        }
        if ( const Json* terrainGrid = FindMember( cinematic, "terrainGrid" ) )
        {
            RequireArray( *terrainGrid, path, "cinematic.terrainGrid" );
            if ( terrainGrid->size() != 2 )
            {
                Fail( path, "cinematic.terrainGrid must contain exactly 2 numbers" );
            }
            c.terrainGridScale = ReadFloat( ( *terrainGrid )[0], path, "cinematic.terrainGrid[0]" );
            c.terrainGridStrength = ReadFloat( ( *terrainGrid )[1], path, "cinematic.terrainGrid[1]" );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        }
        if ( const Json* waterTint = FindMember( cinematic, "waterTint" ) )
        {
            ReadVec3( *waterTint, path, "cinematic.waterTint", c.waterTintR, c.waterTintG, c.waterTintB );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        }
        if ( const Json* waterProfile = FindMember( cinematic, "waterProfile" ) )
        {
            ReadVec3( *waterProfile,
                      path,
                      "cinematic.waterProfile",
                      c.waterAlpha,
                      c.waterReflectionStrength,
                      c.waterGlintStrength );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        }
        if ( const Json* basinMask = FindMember( cinematic, "basinMask" ) )
        {
            RequireArray( *basinMask, path, "cinematic.basinMask" );
            if ( basinMask->size() != 5 )
            {
                Fail( path, "cinematic.basinMask must contain exactly 5 numbers" );
            }
            c.basinCenterX = ReadFloat( ( *basinMask )[0], path, "cinematic.basinMask[0]" );
            c.basinCenterZ = ReadFloat( ( *basinMask )[1], path, "cinematic.basinMask[1]" );
            c.basinRadiusX = ReadFloat( ( *basinMask )[2], path, "cinematic.basinMask[2]" );
            c.basinRadiusZ = ReadFloat( ( *basinMask )[3], path, "cinematic.basinMask[3]" );
            c.basinFeather = ReadFloat( ( *basinMask )[4], path, "cinematic.basinMask[4]" );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        }
    }

    void ApplyCinematic( const Json& cinematic, const std::string& path )
    {
        RequireObject( cinematic, path, "cinematic" );
        ApplyCinematicBool( cinematic, path );
        ApplyCinematicInt( cinematic, path );
        ApplyCinematicFloat( cinematic, path );
        ApplyCinematicVector( cinematic, path );
    }

    void ApplyCamera( const Json& camera, const std::string& path )
    {
        RequireObject( camera, path, "camera" );
        if ( static_cast<int>( m_scene.m_cameras.size() ) >= TOTAL_CAMERA_COUNT )
        {
            Fail( path, "Too many cameras in scene" );
        }

        SceneCamera out = {};
        ReadRequiredStringField( out.name, camera, path, "camera", "name" );
        ReadVec3( RequireMember( camera, path, "camera", "position" ),
                  path,
                  "camera.position",
                  out.m_position.x,
                  out.m_position.y,
                  out.m_position.z );
        ReadVec3( RequireMember( camera, path, "camera", "view" ),
                  path,
                  "camera.view",
                  out.view.x,
                  out.view.y,
                  out.view.z );
        ReadVec3( RequireMember( camera, path, "camera", "up" ), path, "camera.up", out.up.x, out.up.y, out.up.z );
        m_scene.m_cameras.push_back( out );
    }

    void ApplyBall( const Json& object, const std::string& path, bool isFixed )
    {
        SceneBall ball = {};
        ReadRequiredStringField( ball.name, object, path, "ball", "name" );
        ReadVec3( RequireMember( object, path, "ball", "position" ),
                  path,
                  "ball.position",
                  ball.posX,
                  ball.posY,
                  ball.posZ );
        ball.m_radius = ReadFloat( RequireMember( object, path, "ball", "radius" ), path, "ball.radius" );
        ball.m_mass = ReadFloat( RequireMember( object, path, "ball", "mass" ), path, "ball.mass" );
        ball.moment = ReadFloat( RequireMember( object, path, "ball", "moment" ), path, "ball.moment" );
        ball.restitution = ReadFloat( RequireMember( object, path, "ball", "restitution" ), path, "ball.restitution" );
        ball.isFixed = isFixed;

        if ( const Json* fixed = FindMember( object, "fixed" ) )
        {
            ball.isFixed = ReadBool( *fixed, path, "ball.fixed" );
        }
        if ( const Json* force = FindMember( object, "force" ) )
        {
            ReadVec3( *force, path, "ball.force", ball.forceX, ball.forceY, ball.forceZ );
        }
        if ( const Json* forcePosition = FindMember( object, "forcePosition" ) )
        {
            ReadVec3( *forcePosition, path, "ball.forcePosition", ball.forcePosX, ball.forcePosY, ball.forcePosZ );
        }
        if ( const Json* euler = FindMember( object, "euler" ) )
        {
            ReadVec3( *euler, path, "ball.euler", ball.eulerX, ball.eulerY, ball.eulerZ );
            ball.hasInitOrient = true;
        }
        m_scene.m_balls.push_back( ball );
    }

    void ApplyBox( const Json& object, const std::string& path, bool isFixed )
    {
        SceneBox box = {};
        ReadRequiredStringField( box.name, object, path, "box", "name" );
        ReadVec3( RequireMember( object, path, "box", "position" ),
                  path,
                  "box.position",
                  box.posX,
                  box.posY,
                  box.posZ );
        ReadVec3( RequireMember( object, path, "box", "halfExtents" ),
                  path,
                  "box.halfExtents",
                  box.halfX,
                  box.halfY,
                  box.halfZ );
        box.mass = ReadFloat( RequireMember( object, path, "box", "mass" ), path, "box.mass" );
        box.restitution = ReadFloat( RequireMember( object, path, "box", "restitution" ), path, "box.restitution" );
        box.isFixed = isFixed;
        if ( const Json* fixed = FindMember( object, "fixed" ) )
        {
            box.isFixed = ReadBool( *fixed, path, "box.fixed" );
        }
        if ( const Json* euler = FindMember( object, "euler" ) )
        {
            ReadVec3( *euler, path, "box.euler", box.eulerX, box.eulerY, box.eulerZ );
            box.hasInitOrient = true;
        }
        if ( const Json* velocity = FindMember( object, "velocity" ) )
        {
            ReadVec3( *velocity, path, "box.velocity", box.velX, box.velY, box.velZ );
            box.hasInitVelocity = true;
        }
        m_scene.m_boxes.push_back( box );
    }

    void ApplyConvexHull( const Json& object, const std::string& path, bool isFixed )
    {
        SceneConvexHull hull = {};
        ReadRequiredStringField( hull.name, object, path, "convexHull", "name" );
        ReadRequiredStringField( hull.hullPath, object, path, "convexHull", "hull" );
        ReadVec3( RequireMember( object, path, "convexHull", "position" ),
                  path,
                  "convexHull.position",
                  hull.posX,
                  hull.posY,
                  hull.posZ );
        if ( const Json* mass = FindMember( object, "mass" ) )
        {
            hull.mass = ReadFloat( *mass, path, "convexHull.mass" );
        }
        else
        {
            hull.mass = LoadConvexHullDefaultMass( hull.hullPath );
        }
        hull.restitution =
            ReadFloat( RequireMember( object, path, "convexHull", "restitution" ), path, "convexHull.restitution" );
        hull.isFixed = isFixed;
        hull.contactReleaseOnImpact = SkullbonezCore::Assets::HullAssetTokenDefaultsToContactRelease( hull.hullPath );
        hull.contactReleaseImpulseThreshold =
            SkullbonezCore::Assets::HullAssetTokenDefaultContactReleaseThreshold( hull.hullPath );
        if ( const Json* fixed = FindMember( object, "fixed" ) )
        {
            hull.isFixed = ReadBool( *fixed, path, "convexHull.fixed" );
        }
        if ( const Json* sleeping = FindMember( object, "sleeping" ) )
        {
            hull.isSleeping = ReadBool( *sleeping, path, "convexHull.sleeping" );
        }
        if ( const Json* release = FindMember( object, "contactReleaseOnImpact" ) )
        {
            hull.contactReleaseOnImpact = ReadBool( *release, path, "convexHull.contactReleaseOnImpact" );
        }
        if ( const Json* threshold = FindMember( object, "contactReleaseImpulseThreshold" ) )
        {
            hull.contactReleaseImpulseThreshold =
                (std::max)( 0.0f, ReadFloat( *threshold, path, "convexHull.contactReleaseImpulseThreshold" ) );
        }
        if ( const Json* euler = FindMember( object, "euler" ) )
        {
            ReadVec3( *euler, path, "convexHull.euler", hull.eulerX, hull.eulerY, hull.eulerZ );
            hull.hasInitOrient = true;
        }
        if ( const Json* velocity = FindMember( object, "velocity" ) )
        {
            ReadVec3( *velocity, path, "convexHull.velocity", hull.velX, hull.velY, hull.velZ );
            hull.hasInitVelocity = true;
        }
        m_scene.m_convexHulls.push_back( hull );
    }

    void ApplyBallState( const Json& object, const std::string& path )
    {
        SceneBallState state = {};
        ReadRequiredStringField( state.name, object, path, "ballState", "name" );
        ReadVec3( RequireMember( object, path, "ballState", "position" ),
                  path,
                  "ballState.position",
                  state.posX,
                  state.posY,
                  state.posZ );
        ReadVec3( RequireMember( object, path, "ballState", "velocity" ),
                  path,
                  "ballState.velocity",
                  state.velX,
                  state.velY,
                  state.velZ );
        ReadVec3( RequireMember( object, path, "ballState", "angularVelocity" ),
                  path,
                  "ballState.angularVelocity",
                  state.angVelX,
                  state.angVelY,
                  state.angVelZ );
        ReadVec4( RequireMember( object, path, "ballState", "orientation" ),
                  path,
                  "ballState.orientation",
                  state.orientX,
                  state.orientY,
                  state.orientZ,
                  state.orientW );
        state.radius = ReadFloat( RequireMember( object, path, "ballState", "radius" ), path, "ballState.radius" );
        state.mass = ReadFloat( RequireMember( object, path, "ballState", "mass" ), path, "ballState.mass" );
        state.restitution =
            ReadFloat( RequireMember( object, path, "ballState", "restitution" ), path, "ballState.restitution" );
        ReadVec3( RequireMember( object, path, "ballState", "inertia" ),
                  path,
                  "ballState.inertia",
                  state.inertiaX,
                  state.inertiaY,
                  state.inertiaZ );
        if ( const Json* fixed = FindMember( object, "fixed" ) )
        {
            state.isFixed = ReadBool( *fixed, path, "ballState.fixed" );
        }
        if ( const Json* sleeping = FindMember( object, "sleeping" ) )
        {
            state.isSleeping = ReadBool( *sleeping, path, "ballState.sleeping" );
        }
        m_scene.m_ballStates.push_back( state );
    }

    void ApplyBoxState( const Json& object, const std::string& path )
    {
        SceneBoxState state = {};
        ReadRequiredStringField( state.name, object, path, "boxState", "name" );
        ReadVec3( RequireMember( object, path, "boxState", "position" ),
                  path,
                  "boxState.position",
                  state.posX,
                  state.posY,
                  state.posZ );
        ReadVec3( RequireMember( object, path, "boxState", "velocity" ),
                  path,
                  "boxState.velocity",
                  state.velX,
                  state.velY,
                  state.velZ );
        ReadVec3( RequireMember( object, path, "boxState", "angularVelocity" ),
                  path,
                  "boxState.angularVelocity",
                  state.angVelX,
                  state.angVelY,
                  state.angVelZ );
        ReadVec4( RequireMember( object, path, "boxState", "orientation" ),
                  path,
                  "boxState.orientation",
                  state.orientX,
                  state.orientY,
                  state.orientZ,
                  state.orientW );
        ReadVec3( RequireMember( object, path, "boxState", "halfExtents" ),
                  path,
                  "boxState.halfExtents",
                  state.halfX,
                  state.halfY,
                  state.halfZ );
        state.mass = ReadFloat( RequireMember( object, path, "boxState", "mass" ), path, "boxState.mass" );
        state.restitution =
            ReadFloat( RequireMember( object, path, "boxState", "restitution" ), path, "boxState.restitution" );
        ReadVec3( RequireMember( object, path, "boxState", "inertia" ),
                  path,
                  "boxState.inertia",
                  state.inertiaX,
                  state.inertiaY,
                  state.inertiaZ );
        state.isFixed = ReadBool( RequireMember( object, path, "boxState", "fixed" ), path, "boxState.fixed" );
        if ( const Json* sleeping = FindMember( object, "sleeping" ) )
        {
            state.isSleeping = ReadBool( *sleeping, path, "boxState.sleeping" );
        }
        m_scene.m_boxStates.push_back( state );
    }

    void ApplyConvexHullState( const Json& object, const std::string& path )
    {
        SceneConvexHullState state = {};
        ReadRequiredStringField( state.name, object, path, "convexHullState", "name" );
        ReadRequiredStringField( state.hullPath, object, path, "convexHullState", "hull" );
        ReadVec3( RequireMember( object, path, "convexHullState", "position" ),
                  path,
                  "convexHullState.position",
                  state.posX,
                  state.posY,
                  state.posZ );
        ReadVec3( RequireMember( object, path, "convexHullState", "velocity" ),
                  path,
                  "convexHullState.velocity",
                  state.velX,
                  state.velY,
                  state.velZ );
        ReadVec3( RequireMember( object, path, "convexHullState", "angularVelocity" ),
                  path,
                  "convexHullState.angularVelocity",
                  state.angVelX,
                  state.angVelY,
                  state.angVelZ );
        ReadVec4( RequireMember( object, path, "convexHullState", "orientation" ),
                  path,
                  "convexHullState.orientation",
                  state.orientX,
                  state.orientY,
                  state.orientZ,
                  state.orientW );
        state.mass =
            ReadFloat( RequireMember( object, path, "convexHullState", "mass" ), path, "convexHullState.mass" );
        state.restitution = ReadFloat( RequireMember( object, path, "convexHullState", "restitution" ),
                                       path,
                                       "convexHullState.restitution" );
        ReadVec3( RequireMember( object, path, "convexHullState", "inertia" ),
                  path,
                  "convexHullState.inertia",
                  state.inertiaX,
                  state.inertiaY,
                  state.inertiaZ );
        state.isFixed =
            ReadBool( RequireMember( object, path, "convexHullState", "fixed" ), path, "convexHullState.fixed" );
        state.contactReleaseOnImpact = SkullbonezCore::Assets::HullAssetTokenDefaultsToContactRelease( state.hullPath );
        state.contactReleaseImpulseThreshold =
            SkullbonezCore::Assets::HullAssetTokenDefaultContactReleaseThreshold( state.hullPath );
        if ( const Json* sleeping = FindMember( object, "sleeping" ) )
        {
            state.isSleeping = ReadBool( *sleeping, path, "convexHullState.sleeping" );
        }
        if ( const Json* release = FindMember( object, "contactReleaseOnImpact" ) )
        {
            state.contactReleaseOnImpact = ReadBool( *release, path, "convexHullState.contactReleaseOnImpact" );
        }
        if ( const Json* threshold = FindMember( object, "contactReleaseImpulseThreshold" ) )
        {
            state.contactReleaseImpulseThreshold =
                (std::max)( 0.0f, ReadFloat( *threshold, path, "convexHullState.contactReleaseImpulseThreshold" ) );
        }
        m_scene.m_convexHullStates.push_back( state );
    }

    void ApplyRagdoll( const Json& object, const std::string& path )
    {
        SceneRagdoll ragdoll = {};
        ReadRequiredStringField( ragdoll.name, object, path, "ragdoll", "name" );
        ReadVec3( RequireMember( object, path, "ragdoll", "position" ),
                  path,
                  "ragdoll.position",
                  ragdoll.posX,
                  ragdoll.posY,
                  ragdoll.posZ );
        ragdoll.scale = 1.0f;
        if ( const Json* scale = FindMember( object, "scale" ) )
        {
            ragdoll.scale = (std::max)( 0.25f, ReadFloat( *scale, path, "ragdoll.scale" ) );
        }
        if ( const Json* fixed = FindMember( object, "fixed" ) )
        {
            ragdoll.isFixed = ReadBool( *fixed, path, "ragdoll.fixed" );
        }
        if ( const Json* sleeping = FindMember( object, "sleeping" ) )
        {
            ragdoll.startsAsleep = ReadBool( *sleeping, path, "ragdoll.sleeping" );
        }
        if ( const Json* awake = FindMember( object, "awake" ) )
        {
            ragdoll.startsAsleep = !ReadBool( *awake, path, "ragdoll.awake" );
        }
        if ( const Json* euler = FindMember( object, "euler" ) )
        {
            ReadVec3( *euler, path, "ragdoll.euler", ragdoll.eulerX, ragdoll.eulerY, ragdoll.eulerZ );
            ragdoll.hasInitOrient = true;
        }
        m_scene.m_ragdolls.push_back( ragdoll );
    }


    void ApplyObject( const Json& object, const std::string& path )
    {
        RequireObject( object, path, "object" );
        const std::string type = ReadString( RequireMember( object, path, "object", "type" ), path, "object.type" );
        if ( type == "ball" )
        {
            ApplyBall( object, path, false );
        }
        else if ( type == "floatingBall" )
        {
            ApplyBall( object, path, true );
        }
        else if ( type == "box" )
        {
            ApplyBox( object, path, false );
        }
        else if ( type == "floatingBox" )
        {
            ApplyBox( object, path, true );
        }
        else if ( type == "convexHull" )
        {
            ApplyConvexHull( object, path, false );
        }
        else if ( type == "floatingConvexHull" )
        {
            ApplyConvexHull( object, path, true );
        }
        else if ( type == "ballState" )
        {
            ApplyBallState( object, path );
        }
        else if ( type == "boxState" )
        {
            ApplyBoxState( object, path );
        }
        else if ( type == "convexHullState" )
        {
            ApplyConvexHullState( object, path );
        }
        else if ( type == "ragdoll" )
        {
            ApplyRagdoll( object, path );
        }
        else
        {
            Fail( path, "Unknown object type: " + type );
        }
    }


    void ApplyPointJointConstraint( const Json& jointJson, const std::string& path )
    {
        RequireObject( jointJson, path, "ragdollJoint" );
        ScenePointJointConstraint joint = {};
        ReadRequiredStringField( joint.bodyA, jointJson, path, "ragdollJoint", "bodyA" );
        ReadRequiredStringField( joint.bodyB, jointJson, path, "ragdollJoint", "bodyB" );
        ReadVec3( RequireMember( jointJson, path, "ragdollJoint", "localAnchorA" ),
                  path,
                  "ragdollJoint.localAnchorA",
                  joint.localAnchorA.x,
                  joint.localAnchorA.y,
                  joint.localAnchorA.z );
        ReadVec3( RequireMember( jointJson, path, "ragdollJoint", "localAnchorB" ),
                  path,
                  "ragdollJoint.localAnchorB",
                  joint.localAnchorB.x,
                  joint.localAnchorB.y,
                  joint.localAnchorB.z );
        if ( const Json* slack = FindMember( jointJson, "slack" ) )
        {
            joint.slack = (std::max)( 0.0f, ReadFloat( *slack, path, "ragdollJoint.slack" ) );
        }
        if ( const Json* stiffness = FindMember( jointJson, "stiffness" ) )
        {
            joint.stiffness = std::clamp( ReadFloat( *stiffness, path, "ragdollJoint.stiffness" ), 0.0f, 1.0f );
        }
        if ( const Json* damping = FindMember( jointJson, "damping" ) )
        {
            joint.damping = std::clamp( ReadFloat( *damping, path, "ragdollJoint.damping" ), 0.0f, 1.0f );
        }
        if ( const Json* group = FindMember( jointJson, "groupId" ) )
        {
            joint.groupId = static_cast<uint32_t>( (std::max)( 0, ReadInt( *group, path, "ragdollJoint.groupId" ) ) );
        }
        if ( const Json* flags = FindMember( jointJson, "flags" ) )
        {
            joint.flags = static_cast<uint8_t>( std::clamp( ReadInt( *flags, path, "ragdollJoint.flags" ), 0, 255 ) );
        }
        m_scene.m_pointJointConstraints.push_back( joint );
    }

    void ApplyObjectMaterial( const Json& materialJson, const std::string& path )
    {
        RequireObject( materialJson, path, "objectMaterial" );
        SceneObjectMaterialOverride material = {};
        ReadRequiredStringField( material.target, materialJson, path, "objectMaterial", "target" );

        const Json* modeValue = FindMember( materialJson, "mode" );
        if ( !modeValue )
        {
            modeValue = FindMember( materialJson, "kind" );
        }
        if ( !modeValue )
        {
            Fail( path, "objectMaterial is missing required field 'mode'" );
        }

        float tintR = 1.0f;
        float tintG = 1.0f;
        float tintB = 1.0f;
        const Json* tint = FindMember( materialJson, "tint" );
        if ( !tint )
        {
            tint = FindMember( materialJson, "color" );
        }
        if ( !tint )
        {
            tint = FindMember( materialJson, "colour" );
        }
        if ( tint )
        {
            ReadVec3( *tint, path, "objectMaterial.color", tintR, tintG, tintB );
        }

        material.materialMode = ParseMaterialModeValue( *modeValue, path, "objectMaterial.mode" );
        material.material = Rendering::MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, material.materialMode );
        strncpy_s( material.material.name,
                   sizeof( material.material.name ),
                   Rendering::RenderMaterialKindName( material.material.kind ),
                   _TRUNCATE );
        SetObjectMaterialBaseColor( material, tintR, tintG, tintB );

        if ( const Json* roughness = FindMember( materialJson, "roughness" ) )
        {
            material.material.roughness = ReadUnitFloat( *roughness, path, "objectMaterial.roughness" );
        }
        if ( const Json* metallic = FindMember( materialJson, "metallic" ) )
        {
            material.material.metallic = ReadUnitFloat( *metallic, path, "objectMaterial.metallic" );
        }
        if ( const Json* specular = FindMember( materialJson, "specular" ) )
        {
            material.material.specular = ReadUnitFloat( *specular, path, "objectMaterial.specular" );
        }
        if ( const Json* transmission = FindMember( materialJson, "transmission" ) )
        {
            material.material.transmission = ReadUnitFloat( *transmission, path, "objectMaterial.transmission" );
        }
        if ( const Json* stylization = FindMember( materialJson, "stylization" ) )
        {
            material.material.stylization = ReadUnitFloat( *stylization, path, "objectMaterial.stylization" );
        }
        if ( const Json* emissive = FindMember( materialJson, "emissive" ) )
        {
            ReadVec3( *emissive,
                      path,
                      "objectMaterial.emissive",
                      material.material.emissiveColor[0],
                      material.material.emissiveColor[1],
                      material.material.emissiveColor[2] );
        }
        if ( const Json* strength = FindMember( materialJson, "strength" ) )
        {
            material.material.emissiveStrength =
                (std::max)( 0.0f, ReadFloat( *strength, path, "objectMaterial.strength" ) );
        }
        if ( const Json* flags = FindMember( materialJson, "flags" ) )
        {
            material.material.flags = static_cast<uint32_t>( ReadInt( *flags, path, "objectMaterial.flags" ) );
        }
        if ( const Json* name = FindMember( materialJson, "name" ) )
        {
            strncpy_s( material.material.name,
                       sizeof( material.material.name ),
                       ReadString( *name, path, "objectMaterial.name" ).c_str(),
                       _TRUNCATE );
        }

        static constexpr const char* kAllowedKeys[] = {
            "target",
            "mode",
            "kind",
            "tint",
            "color",
            "colour",
            "roughness",
            "metallic",
            "specular",
            "transmission",
            "stylization",
            "emissive",
            "strength",
            "flags",
            "name",
        };
        for ( const auto& item : materialJson.items() )
        {
            const bool known = std::any_of( std::begin( kAllowedKeys ),
                                            std::end( kAllowedKeys ),
                                            [&]( const char* key ) { return item.key() == key; } );
            if ( !known )
            {
                Fail( path, "Unknown objectMaterial field: " + item.key() );
            }
        }

        m_scene.m_objectMaterials.push_back( material );
    }

    void ApplyRequirements( const Json& requirements, const std::string& path )
    {
        RequireObject( requirements, path, "requirements" );
        if ( const Json* contacts = FindMember( requirements, "contacts" ) )
        {
            RequireArray( *contacts, path, "requirements.contacts" );
            for ( const Json& contactJson : *contacts )
            {
                RequireObject( contactJson, path, "requirements.contacts[]" );
                SceneRequiredContact contact = {};
                ReadRequiredStringField( contact.nameA, contactJson, path, "requirements.contacts[]", "a" );
                ReadRequiredStringField( contact.nameB, contactJson, path, "requirements.contacts[]", "b" );
                m_scene.m_requiredContacts.push_back( contact );
            }
        }
        if ( const Json* cells = FindMember( requirements, "broadphaseXCells" ) )
        {
            RequireArray( *cells, path, "requirements.broadphaseXCells" );
            for ( const Json& cellJson : *cells )
            {
                RequireObject( cellJson, path, "requirements.broadphaseXCells[]" );
                SceneRequiredBroadphaseXCells cell = {};
                cell.minCellX = ReadInt( RequireMember( cellJson, path, "requirements.broadphaseXCells[]", "min" ),
                                         path,
                                         "requirements.broadphaseXCells[].min" );
                cell.maxCellX = ReadInt( RequireMember( cellJson, path, "requirements.broadphaseXCells[]", "max" ),
                                         path,
                                         "requirements.broadphaseXCells[].max" );
                cell.cellY = ReadInt( RequireMember( cellJson, path, "requirements.broadphaseXCells[]", "y" ),
                                      path,
                                      "requirements.broadphaseXCells[].y" );
                cell.cellZ = ReadInt( RequireMember( cellJson, path, "requirements.broadphaseXCells[]", "z" ),
                                      path,
                                      "requirements.broadphaseXCells[].z" );
                m_scene.m_requiredBroadphaseXCells.push_back( cell );
            }
        }
    }

    void ApplySceneBody( const Json& root, const std::string& path )
    {
        LoadAssetLibraries( root, path );
        if ( const Json* playback = FindMember( root, "playback" ) )
        {
            ApplyPlayback( *playback, path );
        }
        if ( const Json* simulation = FindMember( root, "simulation" ) )
        {
            ApplySimulation( *simulation, path );
        }
        if ( const Json* tornadoSystem = FindMember( root, "tornadoSystem" ) )
        {
            ApplyTornadoSystem( *tornadoSystem, path );
        }
        if ( const Json* runtime = FindMember( root, "runtime" ) )
        {
            ApplyRuntime( *runtime, path );
        }
        if ( const Json* capture = FindMember( root, "capture" ) )
        {
            ApplyCapture( *capture, path );
        }
        if ( const Json* logging = FindMember( root, "logging" ) )
        {
            ApplyLogging( *logging, path );
        }
        if ( const Json* debug = FindMember( root, "debug" ) )
        {
            ApplyDebug( *debug, path );
        }
        if ( const Json* terrain = FindMember( root, "terrain" ) )
        {
            ApplyTerrain( *terrain, path );
        }
        if ( const Json* editor = FindMember( root, "editor" ) )
        {
            ApplyEditor( *editor, path );
        }
        if ( const Json* ui = FindMember( root, "ui" ) )
        {
            ApplyUI( *ui, path );
        }
        if ( const Json* cinematic = FindMember( root, "cinematic" ) )
        {
            ApplyCinematic( *cinematic, path );
        }
        if ( const Json* cameras = FindMember( root, "cameras" ) )
        {
            RequireArray( *cameras, path, "cameras" );
            for ( const Json& camera : *cameras )
            {
                ApplyCamera( camera, path );
            }
        }
        if ( const Json* objects = FindMember( root, "objects" ) )
        {
            RequireArray( *objects, path, "objects" );
            for ( const Json& object : *objects )
            {
                ApplyObject( object, path );
            }
        }
        if ( const Json* ragdollJoints = FindMember( root, "ragdollJoints" ) )
        {
            RequireArray( *ragdollJoints, path, "ragdollJoints" );
            for ( const Json& joint : *ragdollJoints )
            {
                ApplyPointJointConstraint( joint, path );
            }
        }
        ApplyAssetInstances( root, path );
        ApplyRootedTreeCompatibilityClearanceToHulls( m_scene.m_convexHulls );
        ApplyRootedTreeCompatibilityClearanceToHulls( m_scene.m_convexHullStates );
        if ( const Json* objectMaterials = FindMember( root, "objectMaterials" ) )
        {
            RequireArray( *objectMaterials, path, "objectMaterials" );
            for ( const Json& objectMaterial : *objectMaterials )
            {
                ApplyObjectMaterial( objectMaterial, path );
            }
        }
        if ( const Json* requirements = FindMember( root, "requirements" ) )
        {
            ApplyRequirements( *requirements, path );
        }
    }

    void LoadDocumentIntoScene( const std::string& path, bool styleOnly, int depth )
    {
        if ( depth > kMaxStyleIncludeDepth )
        {
            Fail( path, "Style include depth exceeded" );
        }

        const Json root = ReadJsonFile( path );
        RequireObject( root, path, "document root" );
        const std::string expectedFormat = styleOnly ? "skullbonez.style.json" : "skullbonez.scene.json";
        const std::string actualFormat =
            ReadString( RequireMember( root, path, "document root", "format" ), path, "format" );
        if ( actualFormat != expectedFormat )
        {
            std::ostringstream message;
            message << "Expected format '" << expectedFormat << "', got '" << actualFormat << "'";
            Fail( path, message.str() );
        }

        LoadStyleIncludes( root, path, "includes", depth );
        if ( !styleOnly )
        {
            LoadStyleIncludes( root, path, "styles", depth );
        }
        ApplySceneBody( root, path );
    }

  public:
    TestScene LoadScene( const char* path )
    {
        LoadDocumentIntoScene( path ? path : "", false, 0 );
        if ( m_scene.m_cameras.empty() )
        {
            throw std::runtime_error( "Scene JSON must define at least one camera.  (TestScene::LoadFromFile)" );
        }
        return m_scene;
    }

    TestScene LoadStyle( const char* path )
    {
        LoadDocumentIntoScene( path ? path : "", true, 0 );
        return m_scene;
    }
};

TestScene LoadTestSceneFromFileImpl( const char* path )
{
    return TestSceneParser().LoadScene( path );
}

TestScene LoadStyleSceneFromFileImpl( const char* path )
{
    return TestSceneParser().LoadStyle( path );
}
} // namespace Basics
} // namespace SkullbonezCore
