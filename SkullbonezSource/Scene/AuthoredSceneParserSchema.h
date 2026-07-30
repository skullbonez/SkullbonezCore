/*
File: AuthoredSceneParserSchema.h
Purpose:
  Shares parser schema primitives, declarations, and parse state across
  owner-specific AuthoredSceneParser translation units.

Summary:
  JSON validation helpers translate authored values into one AuthoredSceneParser
  instance. Domain translation units mutate that instance synchronously; this
  header owns declarations and value helpers, not a second scene model.

Glossary:
  Lane R: Recoverable parse failure returned for invalid external scene input.
  Schema domain: Cohesive authored section such as assets, bodies, simulation,
    presentation, or document composition.
  Parser failure scope: Thread-local error target used only during one bounded
    parse so legacy helper calls can stop without throwing.

Invariants:
  - One parser instance owns one AuthoredScene result and one failure state.
  - Helper definitions are inline and parser state is never shared across parses.
  - Authored field names and section order remain compatibility surfaces.

Related:
  - AuthoredSceneParser.cpp owns document composition and public entry points.
  - AuthoredSceneParserAssets.cpp owns asset-library expansion.
  - AuthoredSceneParserBodies.cpp owns body, joint, material, and group rows.
  - AuthoredSceneParserRuntime.cpp owns simulation and runtime settings.
  - AuthoredSceneParserPresentation.cpp owns UI, water, cinematic, and camera fields.
*/
#pragma once

#include "AuthoredScene.h"
#include "../Assets/AssetSystem.h"
#include "../Core/FatalError.h"
#include "../Core/SbDiagnosticStore.h"
#include "../Maths/Quaternion.h"
#include "../Physics/ConvexHullShape.h"
#include "../Physics/PhysicsMass.h"
#include "../Physics/Ragdoll.h"
#include "../Runtime/Editor/EditorHullAssets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

namespace SkullbonezCore
{
namespace Runtime
{
namespace AuthoredSceneParserDetail
{
using Json = nlohmann::ordered_json;

constexpr int kMaxStyleIncludeDepth = 8;
constexpr float kSceneDegreesToRadians = 3.14159265f / 180.0f;

// Concept: parser helpers still use the old "record failure and stop" shape,
// but the failure now becomes a Lane R SkullbonezCore::Core::SbResult at the TryLoad boundary instead
// of a C++ exception escaping through scene-load code.
struct ParserFailureState
{
    bool failed = false;
    std::string message;
};

inline thread_local ParserFailureState* s_activeParserFailure = nullptr;

class ParserFailureScope
{
  public:
    explicit ParserFailureScope( ParserFailureState& state ) : m_previous( s_activeParserFailure )
    {
        s_activeParserFailure = &state;
    }

    ~ParserFailureScope()
    {
        s_activeParserFailure = m_previous;
    }

    ParserFailureScope( const ParserFailureScope& ) = delete;
    ParserFailureScope& operator=( const ParserFailureScope& ) = delete;

  private:
    ParserFailureState* m_previous;
};

inline bool ParserFailed() noexcept
{
    return s_activeParserFailure && s_activeParserFailure->failed;
}

inline SkullbonezCore::Core::SbResult ParserFailureResult( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                           const ParserFailureState& state )
{

    if ( !state.failed )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    return diagnostics.Failure( "Scene/AuthoredSceneParser", "%s", state.message.c_str() );
}

inline Math::Orientation::Quaternion MakeSceneEulerQuaternion( float eulerXDeg, float eulerYDeg, float eulerZDeg )
{
    const float xHalf = eulerXDeg * kSceneDegreesToRadians * 0.5f;
    const float yHalf = eulerYDeg * kSceneDegreesToRadians * 0.5f;
    const float zHalf = eulerZDeg * kSceneDegreesToRadians * 0.5f;

    // Invariant: authored Euler degrees keep their established world-space
    // meaning while live quaternions use canonical Hamilton components.
    const Math::Orientation::Quaternion xRotation( -sinf( xHalf ), 0.0f, 0.0f, cosf( xHalf ) );
    const Math::Orientation::Quaternion yRotation( 0.0f, -sinf( yHalf ), 0.0f, cosf( yHalf ) );
    const Math::Orientation::Quaternion zRotation( 0.0f, 0.0f, -sinf( zHalf ), cosf( zHalf ) );

    Math::Orientation::Quaternion orientation;
    orientation *= xRotation * yRotation * zRotation;
    orientation.Normalise();
    return orientation;
}

inline Json QuaternionToJson( const Math::Orientation::Quaternion& orientation )
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
    orientation.GetComponents( x, y, z, w );
    return Json::array( { x, y, z, w } );
}

inline Json Vector3ToJson( const Math::Vector::Vector3& value )
{
    return Json::array( { value.x, value.y, value.z } );
}

inline void Fail( const std::string& path, const std::string& detail );

inline float LoadConvexHullDefaultMass( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* hullPath )
{
    const char* resolvedPath = Assets::ResolveEditorHullAssetPath( hullPath );
    Math::CollisionDetection::ConvexHullShape hull;
    const SkullbonezCore::Core::SbResult
        loadResult = Math::CollisionDetection::ConvexHullShape::TryLoadFromFile( diagnostics, resolvedPath, hull );

    if ( !loadResult.Ok() )
    {
        Fail( resolvedPath ? resolvedPath : "", loadResult.ErrorMessage() );
    }

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

inline std::string Lowercase( std::string value )
{
    std::transform( value.begin(), value.end(), value.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return value;
}

inline bool EndsWith( const std::string& value, const char* suffix )
{
    const size_t valueLen = value.size();
    const size_t suffixLen = strlen( suffix );
    return valueLen >= suffixLen && value.compare( valueLen - suffixLen, suffixLen, suffix ) == 0;
}

inline bool IsSceneNameDigit( char c )
{
    return c >= '0' && c <= '9';
}


inline bool IsSceneTreePartNameSuffix( const char* suffix )
{

    if ( !suffix || suffix[0] == '\0' )
    {
        return false;
    }

    return strcmp( suffix, "trunk" ) == 0 || strcmp( suffix, "low" ) == 0 || strcmp( suffix, "mid" ) == 0 ||
           strcmp( suffix, "top" ) == 0 || strncmp( suffix, "needle_", 7 ) == 0;
}


inline bool TryGetSceneTreeInstancePrefixLength( const char* name, size_t& outPrefixLength )
{
    outPrefixLength = 0;

    if ( !name || name[0] == '\0' )
    {
        return false;
    }

    if ( strstr( name, "_tree_" ) == nullptr && strncmp( name, "tree_", 5 ) != 0 )
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

    if ( marker != nameLength )
    {
        const size_t prefixLength = marker + 5;

        if ( !IsSceneTreePartNameSuffix( name + prefixLength ) )
        {
            return false;
        }

        outPrefixLength = prefixLength;
        return true;
    }

    for ( size_t i = 0; i + 1 < nameLength; ++i )
    {

        if ( name[i] == '_' && IsSceneTreePartNameSuffix( name + i + 1 ) )
        {
            outPrefixLength = i + 1;
            return true;
        }
    }

    return false;
}


inline bool TryGetEditorTreeInstancePrefixLengthAnyPart( const char* name, size_t& outPrefixLength )
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


inline bool EditorTreeNamesShareInstancePrefix( const char* a, const char* b, size_t prefixLength )
{
    return a && b && strncmp( a, b, prefixLength ) == 0;
}


inline bool IsReleasableTreeSceneHull( const char* hullPath )
{
    return SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease( SkullbonezCore::Assets::EditorHullAssetFromToken( hullPath ) );
}


template <typename THull> void AssignReleasableTreeGroupsToHulls( std::vector<THull>& hulls )
{

    for ( int i = 0; i < static_cast<int>( hulls.size() ); ++i )
    {
        THull& hull = hulls[static_cast<std::size_t>( i )];

        if ( hull.group.kind == SceneObjectGroupKind::None || hull.group.rootObjectName[0] == '\0' )
        {
            continue;
        }

        for ( int candidateIndex = 0; candidateIndex < static_cast<int>( hulls.size() ); ++candidateIndex )
        {
            const THull& candidate = hulls[static_cast<std::size_t>( candidateIndex )];

            if ( strcmp( candidate.name, hull.group.rootObjectName ) == 0 )
            {
                hull.group.rootObjectId = candidate.sceneObjectId;
                break;
            }
        }
    }

    for ( int i = 0; i < static_cast<int>( hulls.size() ); ++i )
    {
        THull& hull = hulls[static_cast<std::size_t>( i )];

        if ( hull.group.kind != SceneObjectGroupKind::None )
        {
            continue;
        }

        // Why: legacy scene files encoded tree grouping through generated
        // display names. Parse that compatibility surface once during scene
        // import, then runtime construction can pass explicit group descriptors
        // into the model collection without a collection-side name scan.
        size_t sourcePrefixLength = 0;

        if ( !IsReleasableTreeSceneHull( hull.hullPath ) ||
             !TryGetSceneTreeInstancePrefixLength( hull.name, sourcePrefixLength ) )
        {
            continue;
        }

        SceneObjectGroupMetadata group;
        group.kind = SceneObjectGroupKind::ReleasableTree;
        group.rootObjectId = hull.sceneObjectId;
        group.partIndex = 0;
        strncpy_s( group.rootObjectName, hull.name, _TRUNCATE );

        for ( int previousIndex = 0; previousIndex < i; ++previousIndex )
        {
            const THull& previous = hulls[static_cast<std::size_t>( previousIndex )];

            if ( previous.group.kind != SceneObjectGroupKind::ReleasableTree )
            {
                continue;
            }

            size_t previousPrefixLength = 0;

            if ( !TryGetSceneTreeInstancePrefixLength( previous.name, previousPrefixLength ) ||
                 previousPrefixLength != sourcePrefixLength ||
                 !EditorTreeNamesShareInstancePrefix( previous.name, hull.name, sourcePrefixLength ) )
            {
                continue;
            }

            group.rootObjectId = previous.group.rootObjectId.IsValid() ? previous.group.rootObjectId
                                                                       : previous.sceneObjectId;
            strncpy_s( group.rootObjectName,
                       previous.group.rootObjectName[0] != '\0' ? previous.group.rootObjectName : previous.name, _TRUNCATE );
            group.partIndex = (std::max)( group.partIndex, previous.group.partIndex + 1 );
        }

        hull.group = group;
    }
}


template <typename THull> void ValidateReleasableTreeGroups( const std::vector<THull>& hulls, const std::string& path )
{

    for ( const THull& hull : hulls )
    {

        if ( hull.group.kind == SceneObjectGroupKind::None )
        {
            continue;
        }

        if ( !hull.group.rootObjectId.IsValid() )
        {
            Fail( path, std::string( "objectGroup root '" ) + hull.group.rootObjectName + "' for '" + hull.name +
                            "' does not name an object in the same hull section" );
            return;
        }

        const THull* root = nullptr;

        for ( const THull& candidate : hulls )
        {

            if ( candidate.sceneObjectId.value == hull.group.rootObjectId.value )
            {
                root = &candidate;
                break;
            }
        }

        // Lane R: a parsed scene must never publish group metadata that can
        // only fail after earlier entities have already entered runtime stores.

        if ( !root || root->group.kind != hull.group.kind || root->group.rootObjectId.value != root->sceneObjectId.value ||
             root->group.partIndex != 0 )
        {
            Fail( path, std::string( "objectGroup root '" ) + hull.group.rootObjectName + "' for '" + hull.name +
                            "' is not a compatible part-zero group root" );
            return;
        }
    }
}


template <typename THull> void ApplyRootedTreeCompatibilityClearanceToHulls( std::vector<THull>& hulls )
{

    // Why: Older rooted-tree scenes placed trunk foliage too low relative to the
    // root hull. Apply the compatibility lift only to matching fixed parts so
    // saved legacy scenes keep their intended clearance without moving roots.

    for ( const THull& root : hulls )
    {
        const float liftY = SkullbonezCore::Assets::EditorTreeRootedAboveRootLiftY( root.name );
        const float legacyRootToTrunkY = SkullbonezCore::Assets::EditorTreeRootedLegacyRootToTrunkDeltaY( root.name );
        const SkullbonezCore::Assets::EditorHullAsset rootAsset = SkullbonezCore::Assets::EditorHullAssetFromToken( root.hullPath );

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
            const SkullbonezCore::Assets::EditorHullAsset asset = SkullbonezCore::Assets::EditorHullAssetFromToken( candidate.hullPath );

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
            const SkullbonezCore::Assets::EditorHullAsset asset = SkullbonezCore::Assets::EditorHullAssetFromToken( candidate.hullPath );

            if ( candidate.isFixed && asset != SkullbonezCore::Assets::EditorHullAsset::TREE_ROOT_SMALL &&
                 asset != SkullbonezCore::Assets::EditorHullAsset::TREE_ROOT_LARGE &&
                 EditorTreeNamesShareInstancePrefix( root.name, candidate.name, prefixLength ) )
            {
                candidate.posY += liftY;
            }
        }
    }
}

inline std::string JsonTypeName( const Json& value )
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

inline const Json& EmptyJson()
{
    static const Json empty = Json::object();
    return empty;
}

inline void Fail( const std::string& path, const std::string& detail )
{

    // Concept: Parser failures include the file path and logical context because
    // scene JSON is edited by humans and validation scripts.
    std::ostringstream message;
    message << detail << " in " << path << "  (AuthoredScene::LoadFromFile)";

    if ( s_activeParserFailure && !s_activeParserFailure->failed )
    {
        s_activeParserFailure->failed = true;
        s_activeParserFailure->message = message.str();
    }
}

inline void RequireObject( const Json& value, const std::string& path, const char* context )
{

    if ( !value.is_object() )
    {
        std::ostringstream message;
        message << context << " must be an object, got " << JsonTypeName( value );
        Fail( path, message.str() );
    }
}

inline void RequireArray( const Json& value, const std::string& path, const char* context )
{

    if ( !value.is_array() )
    {
        std::ostringstream message;
        message << context << " must be an array, got " << JsonTypeName( value );
        Fail( path, message.str() );
    }
}

inline const Json* FindMember( const Json& object, const char* key )
{

    if ( !object.is_object() )
    {
        return nullptr;
    }

    const auto it = object.find( key );
    return it == object.end() ? nullptr : &( *it );
}

inline const Json& RequireMember( const Json& object, const std::string& path, const char* context, const char* key )
{
    RequireObject( object, path, context );
    const Json* member = FindMember( object, key );

    if ( !member )
    {
        std::ostringstream message;
        message << context << " is missing required field '" << key << "'";
        Fail( path, message.str() );
        return EmptyJson();
    }

    return *member;
}

inline std::string ReadString( const Json& value, const std::string& path, const char* context )
{

    if ( !value.is_string() )
    {
        std::ostringstream message;
        message << context << " must be a string, got " << JsonTypeName( value );
        Fail( path, message.str() );
        return std::string();
    }

    return value.get<std::string>();
}

inline float ReadFloat( const Json& value, const std::string& path, const char* context )
{

    if ( !value.is_number() )
    {
        std::ostringstream message;
        message << context << " must be a number, got " << JsonTypeName( value );
        Fail( path, message.str() );
        return 0.0f;
    }

    return value.get<float>();
}

inline int ReadInt( const Json& value, const std::string& path, const char* context )
{

    if ( !value.is_number_integer() && !value.is_number_unsigned() )

    {
        std::ostringstream message;
        message << context << " must be an integer, got " << JsonTypeName( value );
        Fail( path, message.str() );
        return 0;
    }

    return value.get<int>();
}

inline unsigned int ReadUInt( const Json& value, const std::string& path, const char* context )
{

    if ( !value.is_number_integer() && !value.is_number_unsigned() )
    {
        std::ostringstream message;
        message << context << " must be an unsigned integer, got " << JsonTypeName( value );
        Fail( path, message.str() );
        return 0u;
    }

    if ( value.is_number_unsigned() )
    {
        const unsigned long long parsed = value.get<unsigned long long>();

        if ( parsed > ( std::numeric_limits<unsigned int>::max )() )
        {
            std::ostringstream message;
            message << context << " must fit in uint32";
            Fail( path, message.str() );
            return 0u;
        }

        return static_cast<unsigned int>( parsed );
    }

    const long long parsed = value.get<long long>();

    if ( parsed < 0 || parsed > ( std::numeric_limits<unsigned int>::max )() )
    {
        std::ostringstream message;
        message << context << " must fit in uint32";
        Fail( path, message.str() );
        return 0u;
    }

    return static_cast<unsigned int>( parsed );
}

inline bool TryParseBoolWord( const std::string& value, bool& out )
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

inline bool ReadBool( const Json& value, const std::string& path, const char* context )
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
    return false;
}

template <size_t N> void CopyStringField( char ( &out )[N], const std::string& text )
{
    strncpy_s( out, N, text.c_str(), _TRUNCATE );
}

template <size_t N>
inline bool CopyCheckedStringField( char ( &out )[N], const std::string& text, const std::string& path, const char* context )
{

    if ( text.size() >= N )
    {
        std::ostringstream message;
        message << context << " must be shorter than " << N << " characters";
        Fail( path, message.str() );
        return false;
    }

    strcpy_s( out, N, text.c_str() );
    return true;
}

template <size_t N>
inline void ReadRequiredStringField( char ( &out )[N], const Json& object, const std::string& path, const char* context,
                                     const char* key )
{
    const std::string text = ReadString( RequireMember( object, path, context, key ), path, key );

    if ( ParserFailed() )
    {
        return;
    }

    (void)CopyCheckedStringField( out, text, path, key );
}

inline SceneObjectGroupKind ReadSceneObjectGroupKind( const Json& value, const std::string& path, const char* context )
{
    const std::string kind = Lowercase( ReadString( value, path, context ) );

    if ( kind == "releasabletree" || kind == "releasable_tree" )
    {
        return SceneObjectGroupKind::ReleasableTree;
    }

    if ( kind == "none" )
    {
        return SceneObjectGroupKind::None;
    }

    Fail( path, "Unknown scene object group kind: " + kind );
    return SceneObjectGroupKind::None;
}

inline void ReadOptionalSceneObjectGroup( SceneObjectGroupMetadata& group, const Json& object, const std::string& path,
                                          const char* objectContext )
{

    // Concept: objectGroup JSON uses the authored root name as its file-facing
    // reference. After expansion, the parser resolves that name once to the
    // root's stable scene object id; runtime grouping never stores the row.
    const Json* groupJson = FindMember( object, "objectGroup" );

    if ( !groupJson )
    {
        return;
    }

    RequireObject( *groupJson, path, "objectGroup" );
    group.kind = ReadSceneObjectGroupKind( RequireMember( *groupJson, path, "objectGroup", "kind" ), path,
                                           "objectGroup.kind" );

    if ( group.kind == SceneObjectGroupKind::None )
    {
        return;
    }

    ReadRequiredStringField( group.rootObjectName, *groupJson, path, "objectGroup", "root" );
    group.partIndex = (std::max)( 0, ReadInt( RequireMember( *groupJson, path, "objectGroup", "part" ), path,
                                              "objectGroup.part" ) );

    if ( group.rootObjectName[0] == '\0' )
    {
        Fail( path, std::string( objectContext ) + ".objectGroup.root must not be empty" );
        return;
    }
}

inline void ReadVec3( const Json& value, const std::string& path, const char* context, float& x, float& y, float& z )
{
    RequireArray( value, path, context );

    if ( value.size() != 3 )
    {
        std::ostringstream message;
        message << context << " must contain exactly 3 numbers";
        Fail( path, message.str() );
        return;
    }

    x = ReadFloat( value[0], path, context );
    y = ReadFloat( value[1], path, context );
    z = ReadFloat( value[2], path, context );
}

inline void ReadVec4( const Json& value, const std::string& path, const char* context, float& x, float& y, float& z,
                      float& w )
{
    RequireArray( value, path, context );

    if ( value.size() != 4 )
    {
        std::ostringstream message;
        message << context << " must contain exactly 4 numbers";
        Fail( path, message.str() );
        return;
    }

    x = ReadFloat( value[0], path, context );
    y = ReadFloat( value[1], path, context );
    z = ReadFloat( value[2], path, context );
    w = ReadFloat( value[3], path, context );
}

inline Json ReadJsonFile( const std::string& path )
{
    std::ifstream input( path );

    if ( !input )
    {
        Fail( path, "Failed to open JSON file" );
        return Json::object();
    }

    Json root = Json::parse( input, nullptr, false );

    if ( root.is_discarded() )
    {

        // Lane R: malformed authored JSON is external input, so the parser
        // records a recoverable failure without requiring exception support.
        Fail( path, "Invalid JSON" );
        return Json::object();
    }

    return root;
}

inline int MaxConfigurableWorkerThreadCount()
{
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return (std::max)( 1, static_cast<int>( hardwareThreads ) );
}

inline int ParseUITab( const Json& value, const std::string& path )
{

    if ( value.is_number_integer() || value.is_number_unsigned() )
    {
        return value.get<int>();
    }

    const std::string tab = Lowercase( ReadString( value, path, "ui.tab" ) );

    // Invariant: these ordinals mirror UI::InGameUITab. UI capture scenes store
    // tab names as authoring text, but the runtime state still consumes the enum
    // ordinal.
    static const SceneIntOption kTabs[] = {
        { "profiler", 0 },  { "profile", 0 }, { "overview", 0 },       { "scene", 1 },          { "editor", 2 },
        { "placement", 2 }, { "physics", 3 }, { "options", 4 },        { "params", 4 },         { "render", 5 },
        { "renderer", 5 },  { "targets", 6 }, { "render_targets", 6 }, { "render-targets", 6 }, { "keys", 7 },
        { "controls", 7 },  { "sky", 8 },     { "atmosphere", 8 },     { "cinematic", 9 },      { "cine", 9 },
        { "look", 9 },      { "memory", 10 }, { "allocations", 10 },
    };

    int parsed = 0;

    if ( TryParseIntOption( tab, kTabs, parsed ) )
    {
        return parsed;
    }

    Fail( path, "ui.tab has an unknown tab name: " + tab );
    return 0;
}

inline int ParseWaterReflectionMode( const Json& value, const std::string& path )
{

    if ( value.is_number_integer() || value.is_number_unsigned() )
    {
        return value.get<int>();
    }

    const std::string mode = Lowercase( ReadString( value, path, "debug.waterReflection" ) );
    static const SceneIntOption kModes[] = {
        { "fbo", 0 },       { "render_target", 0 }, { "render-target", 0 }, { "dxr", 1 },
        { "raytraced", 1 }, { "ray_traced", 1 },    { "none", 2 },          { "off", 2 },
    };
    int parsed = 0;

    if ( TryParseIntOption( mode, kModes, parsed ) )
    {
        return parsed;
    }

    Fail( path, "debug.waterReflection must be fbo, dxr, or none" );
    return 0;
}

inline uint32_t ParsePhysicsDebugMode( const Json& value, const std::string& path )
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
    return Physics::PHYSICS_DEBUG_NONE;
}

inline float ParseMaterialModeValue( const Json& value, const std::string& path, const char* context )
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
    return 0.0f;
}

inline void SetObjectMaterialBaseColor( SceneObjectMaterialOverride& material, float r, float g, float b )
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

inline std::string ReadContactMaterialToken( const Json& value, const std::string& path, const char* context )
{
    const std::string token = Lowercase( ReadString( value, path, context ) );

    if ( token.empty() || token.size() >= 32 )
    {
        Fail( path, std::string( context ) + " must be 1-31 characters" );
        return std::string();
    }

    return token;
}

inline std::string ReadInferredContactMaterial( const Json& object, const std::string& path, const char* context )
{

    if ( const Json* material = FindMember( object, "contactMaterial" ) )
    {
        return ReadContactMaterialToken( *material, path, context );
    }

    const Json* renderMaterial = FindMember( object, "material" );

    if ( renderMaterial && renderMaterial->is_object() )
    {

        // Why: asset libraries already tag render materials by substance; using
        // that token keeps gameplay contact policies material-aware without duplicating JSON.

        if ( const Json* mode = FindMember( *renderMaterial, "mode" ); mode && mode->is_string() )
        {
            return ReadContactMaterialToken( *mode, path, "asset.material.mode" );
        }

        if ( const Json* kind = FindMember( *renderMaterial, "kind" ); kind && kind->is_string() )
        {
            return ReadContactMaterialToken( *kind, path, "asset.material.kind" );
        }
    }

    return "default";
}

inline void CopyOptionalContactMaterial( char ( &out )[32], const Json& object, const std::string& path,
                                         const char* context )
{
    const Json* material = FindMember( object, "contactMaterial" );

    if ( !material )
    {
        strncpy_s( out, sizeof( out ), "default", _TRUNCATE );
        return;
    }

    const std::string token = ReadContactMaterialToken( *material, path, context );
    strncpy_s( out, sizeof( out ), token.c_str(), _TRUNCATE );
}

inline float ReadUnitFloat( const Json& value, const std::string& path, const char* context )
{
    return std::clamp( ReadFloat( value, path, context ), 0.0f, 1.0f );
}

} // namespace AuthoredSceneParserDetail

class AuthoredSceneParser
{
  private:
    using Json = AuthoredSceneParserDetail::Json;
    using ParserFailureState = AuthoredSceneParserDetail::ParserFailureState;

    struct ParsedAssetDefinition
    {
        Json value;
        uint32_t libraryRefIndex = 0; // Exact library token/path owner retained beside copied JSON.
    };

    struct AssetInstanceExpansion
    {
        Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 authoredEuler = Math::Vector::ZERO_VECTOR;
        Math::Orientation::Quaternion orientation;
        Math::Vector::Vector3 velocity = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
        uint32_t overrideMask = 0;
        bool fixed = false;
        bool sleeping = false;

        bool HasOverride( uint32_t bit ) const
        {
            return ( overrideMask & bit ) != 0;
        }
    };

    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    AuthoredScene m_scene;
    const Assets::AssetSystem* m_assets = nullptr;
    ParserFailureState m_failure;
    std::vector<ParsedAssetDefinition> m_assetDefinitions;
    std::vector<std::string> m_sceneObjectNames;
    std::vector<uint32_t> m_sceneObjectIds;
    uint32_t m_currentDocumentVersion = 1;
    Physics::PhysicsSceneObjectId RegisterSceneObjectIdRange( uint32_t first, uint32_t count, const std::string& path );
    Physics::PhysicsSceneObjectId ReadSceneObjectId( const Json& object, const std::string& path, const char* context,
                                                     uint32_t count = 1 );
    Physics::PhysicsSceneObjectId AllocateVersion1SceneObjectIdRange( uint32_t& next, uint32_t count,
                                                                      const std::string& path );
    void UpgradeVersion1SceneObjectIds( const std::string& path );
    const Json* ReadAssetPartIdentity( const Json& instance, const std::string& path, uint32_t partIndex,
                                       uint32_t expectedPartCount, const std::string& expectedPartName );
    std::string ResolveStylePath( const std::string& token ) const;
    const Assets::AssetLibrarySourceAsset* FindRegisteredAssetLibrary( const std::string& token ) const;
    std::string ResolveAssetLibraryPath( const std::string& token ) const;
    const ParsedAssetDefinition* FindAssetDefinition( const std::string& name ) const;
    bool RegisterSceneObjectName( const char* name, const std::string& path );
    void ValidateAssetMaterial( const Json& owner, const std::string& path, const char* context ) const;
    void ValidateAssetCommonPhysicsFields( const Json& asset, const std::string& path, const char* context,
                                           bool requireMass ) const;
    std::string ReadAssetPrimitiveType( const Json& asset, const std::string& path, const char* context ) const;
    void ValidateAssetBoxFields( const Json& asset, const std::string& path, const char* context ) const;
    void ValidateAssetSphereFields( const Json& asset, const std::string& path, const char* context ) const;
    void ValidateConvexHullAssetFields( const Json& asset, const std::string& path, const char* context ) const;
    void ValidateAssetPrimitiveFields( const Json& asset, const std::string& path, const char* context ) const;
    void UpgradeAssetLibraryV0ToV1( Json& root, const std::string& path );
    void LoadAssetLibrary( const std::string& assetPath, uint32_t libraryRefIndex );
    void LoadAssetLibraries( const Json& root, const std::string& path );
    void CheckGeneratedSceneName( const std::string& name, const std::string& path, const char* context ) const;
    std::string BuildAssetPartName( const std::string& instanceName, const std::string& partName,
                                    const std::string& path ) const;
    void ApplyAssetMaterialForTarget( const Json& asset, const std::string& path, const std::string& target );
    void RecordAssetPart( const std::string& path, const std::string& partName, const std::string& objectName,
                          Physics::PhysicsSceneObjectId sceneObjectId, uint32_t partIndex, SceneAssetPartSource source,
                          uint32_t sourceIndex, const Math::Vector::Vector3& worldPosition,
                          const Math::Orientation::Quaternion& worldOrientation );
    void ApplyAssetPrimitivePart( const Json& asset, const std::string& path, const std::string& objectName,
                                  const std::string& partName, uint32_t partIndex, const AssetInstanceExpansion& instance,
                                  const Json* authoredPartIdentity );
    void ApplyAssetInstance( const Json& instance, const std::string& path );
    void ApplyAssetInstances( const Json& root, const std::string& path );
    void LoadStyleIncludes( const Json& root, const std::string& path, const char* memberName, int depth );
    void ApplyPlayback( const Json& playback, const std::string& path );
    Physics::MutualGravitySettings ReadMutualGravitySettings( const Json& mutualGravity, const std::string& path );
    void ApplySimulation( const Json& simulation, const std::string& path );
    void ApplyTornadoFloat( const Json& source, const std::string& path, const char* memberName, float& target,
                            float minimum );
    void ApplyTornadoVortex( const Json& object, const std::string& path, AuthoredTornadoSystemConfig& system );
    void ApplyTornadoSystem( const Json& tornadoSystem, const std::string& path );
    void ApplyRuntime( const Json& runtime, const std::string& path );
    void ApplyCapture( const Json& capture, const std::string& path );
    void ApplyLogging( const Json& logging, const std::string& path );
    void ApplyPhysicsDebug( const Json& debug, const std::string& path );
    void ApplyDebug( const Json& debug, const std::string& path );
    void ApplyTerrain( const Json& terrain, const std::string& path );
    void ApplyEditor( const Json& editor, const std::string& path );
    void ApplyUI( const Json& ui, const std::string& path );
    void ApplyCinematicBool( const Json& cinematic, const std::string& path );
    void ApplyCinematicInt( const Json& cinematic, const std::string& path );
    void ApplyCinematicFloat( const Json& cinematic, const std::string& path );
    void ApplyCinematicVector( const Json& cinematic, const std::string& path );
    void ApplyCinematic( const Json& cinematic, const std::string& path );
    void ApplyCamera( const Json& camera, const std::string& path );
    void ApplyBall( const Json& object, const std::string& path, bool isFixed );
    void ApplyBox( const Json& object, const std::string& path, bool isFixed );
    void ApplyConvexHull( const Json& object, const std::string& path, bool isFixed,
                          const Math::Orientation::Quaternion* composedOrientation = nullptr );
    void ApplyBallState( const Json& object, const std::string& path );
    void ApplyBoxState( const Json& object, const std::string& path );
    void ApplyConvexHullState( const Json& object, const std::string& path );
    void ApplyRagdoll( const Json& object, const std::string& path );
    void ApplyObject( const Json& object, const std::string& path );
    void ApplyPointJointConstraint( const Json& jointJson, const std::string& path );
    void ApplyObjectMaterial( const Json& materialJson, const std::string& path );
    void ApplyRequirements( const Json& requirements, const std::string& path );
    void ApplySceneBody( const Json& root, const std::string& path );
    void LoadDocumentIntoScene( const std::string& path, bool styleOnly, int depth );
    SkullbonezCore::Core::SbResult TryLoadDocument( const char* path, bool styleOnly, AuthoredScene& outScene );

  public:
    AuthoredSceneParser( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, const Assets::AssetSystem* assets );
    SkullbonezCore::Core::SbResult TryLoadScene( const char* path, AuthoredScene& outScene );
    SkullbonezCore::Core::SbResult TryLoadStyle( const char* path, AuthoredScene& outScene );
};
} // namespace Runtime
} // namespace SkullbonezCore
