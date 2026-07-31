/*
File: AuthoredSceneParser.cpp
Purpose:
  Owns scene document composition, schema identity upgrades, and public parse entry points.

Summary:
  This translation unit handles one schema domain while mutating the single
  AuthoredSceneParser result. Shared validation and failure policy live in
  AuthoredSceneParserSchema.h; top-level document order stays in AuthoredSceneParser.cpp.

Invariants:
  - Authored JSON field names remain command-line and scene-file compatibility.
  - Parser failure stops further mutation and is returned without an engine throw.
  - Stable scene identities and source ordering are preserved exactly.

Related:
  - AuthoredSceneParserSchema.h declares shared parser state and helpers.
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md owns this decomposition.
  - Agentic/Reference/engine-glossary.md
*/
#include "AuthoredSceneParserSchema.h"

namespace SkullbonezCore
{
namespace Runtime
{
using AuthoredSceneParserDetail::ApplyRootedTreeCompatibilityClearanceToHulls;
using AuthoredSceneParserDetail::AssignReleasableTreeGroupsToHulls;
using AuthoredSceneParserDetail::EndsWith;
using AuthoredSceneParserDetail::Fail;
using AuthoredSceneParserDetail::FindMember;
using AuthoredSceneParserDetail::kMaxStyleIncludeDepth;
using AuthoredSceneParserDetail::ParserFailed;
using AuthoredSceneParserDetail::ParserFailureResult;
using AuthoredSceneParserDetail::ParserFailureScope;
using AuthoredSceneParserDetail::ReadJsonFile;
using AuthoredSceneParserDetail::ReadString;
using AuthoredSceneParserDetail::ReadUInt;
using AuthoredSceneParserDetail::RequireArray;
using AuthoredSceneParserDetail::RequireMember;
using AuthoredSceneParserDetail::RequireObject;
using AuthoredSceneParserDetail::ValidateReleasableTreeGroups;

Physics::PhysicsSceneObjectId AuthoredSceneParser::RegisterSceneObjectIdRange( uint32_t first, uint32_t count,
                                                                               const std::string& path )
{
    Physics::PhysicsSceneObjectId result { first };

    if ( first == 0 )
    {
        Fail( path, "sceneObjectId must be nonzero" );
        return {};
    }

    const uint32_t maxId = ( std::numeric_limits<uint32_t>::max )();

    if ( count == 0 || count - 1u > maxId - first )
    {
        Fail( path, "sceneObjectId range exceeds uint32" );
        return {};
    }

    // Invariant: duplicate detection covers derived ragdoll parts as well as
    // directly authored rows, so no two bodies can enter creation with the
    // same persistent identity.

    for ( uint32_t offset = 0; offset < count; ++offset )
    {
        const uint32_t candidate = first + offset;

        if ( std::find( m_sceneObjectIds.begin(), m_sceneObjectIds.end(), candidate ) != m_sceneObjectIds.end() )
        {
            Fail( path, "Duplicate sceneObjectId: " + std::to_string( candidate ) );
            return {};
        }
    }

    for ( uint32_t offset = 0; offset < count; ++offset )
    {
        m_sceneObjectIds.push_back( first + offset );
    }

    return result;
}

Physics::PhysicsSceneObjectId AuthoredSceneParser::ReadSceneObjectId( const Json& object, const std::string& path,
                                                                      const char* context, uint32_t count )
{
    uint32_t first = 0;

    if ( m_currentDocumentVersion >= 2 && FindMember( object, "sceneObjectId" ) )
    {
        first = ReadUInt( RequireMember( object, path, context, "sceneObjectId" ), path, "sceneObjectId" );
    }
    else if ( m_currentDocumentVersion == 2 )
    {
        first = ReadUInt( RequireMember( object, path, context, "sceneObjectId" ), path, "sceneObjectId" );
    }
    else
    {

        if ( m_currentDocumentVersion == 1 && FindMember( object, "sceneObjectId" ) )
        {
            Fail( path, "sceneObjectId requires scene schema version 2 or later" );
            return {};
        }

        // Compatibility: v3 files migrated from v1 retain their compact
        // identity shape. The shared post-parse pass fills only absent ids in
        // historical section order; writer-made v3 files keep explicit ids.
        return {};
    }

    if ( ParserFailed() )
    {
        return {};
    }

    return RegisterSceneObjectIdRange( first, count, path );
}

Physics::PhysicsSceneObjectId AuthoredSceneParser::AllocateVersion1SceneObjectIdRange( uint32_t& next, uint32_t count,
                                                                                       const std::string& path )
{
    const uint32_t maxId = ( std::numeric_limits<uint32_t>::max )();

    while ( next != 0 && count > 0 && count - 1u <= maxId - next )
    {
        bool available = true;

        for ( uint32_t offset = 0; offset < count; ++offset )
        {

            if ( std::find( m_sceneObjectIds.begin(), m_sceneObjectIds.end(), next + offset ) != m_sceneObjectIds.end() )
            {
                next += offset + 1u;
                available = false;
                break;
            }
        }

        if ( available )
        {
            const Physics::PhysicsSceneObjectId result = RegisterSceneObjectIdRange( next, count, path );
            next += count;
            return result;
        }
    }

    Fail( path, "Version 1 sceneObjectId upgrade exhausted uint32" );
    return {};
}

void AuthoredSceneParser::UpgradeVersion1SceneObjectIds( const std::string& path )
{
    uint32_t next = 1;
    auto assignRows = [&]( auto& rows )
    {

        for ( auto& row : rows )
        {

            if ( !row.sceneObjectId.IsValid() )
            {
                row.sceneObjectId = AllocateVersion1SceneObjectIdRange( next, 1, path );

                if ( ParserFailed() )
                {
                    return;
                }
            }
        }
    };

    // Compatibility: this is the exact section order used by authored
    // creation before stable ids moved into the parsed scene record.
    assignRows( m_scene.m_balls );
    assignRows( m_scene.m_ballStates );
    assignRows( m_scene.m_boxes );
    assignRows( m_scene.m_boxStates );
    assignRows( m_scene.m_convexHulls );
    assignRows( m_scene.m_convexHullStates );

    for ( SceneRagdoll& ragdoll : m_scene.m_ragdolls )
    {

        if ( !ragdoll.firstSceneObjectId.IsValid() )
        {
            ragdoll.firstSceneObjectId = AllocateVersion1SceneObjectIdRange( next,

                                                                             static_cast<uint32_t>( Physics::Ragdoll::SIMPLE_PART_COUNT ),
                                                                             path );

            if ( ParserFailed() )
            {
                return;
            }
        }
    }

    for ( SceneAssetPartRef& part : m_scene.m_assetParts )
    {

        switch ( part.source )
        {
        case SceneAssetPartSource::BallState:
            part.sceneObjectId = m_scene.m_ballStates[part.sourceIndex].sceneObjectId;
            break;
        case SceneAssetPartSource::BoxState:
            part.sceneObjectId = m_scene.m_boxStates[part.sourceIndex].sceneObjectId;
            break;
        case SceneAssetPartSource::ConvexHull:
            part.sceneObjectId = m_scene.m_convexHulls[part.sourceIndex].sceneObjectId;
            break;
        case SceneAssetPartSource::ConvexHullState:
            part.sceneObjectId = m_scene.m_convexHullStates[part.sourceIndex].sceneObjectId;
            break;
        }
    }

    for ( SceneAssetInstanceRecord& instance : m_scene.m_assetInstances )
    {

        if ( instance.partCount > 0 )
        {
            instance.rootSceneObjectId = m_scene.m_assetParts[instance.firstPart].sceneObjectId;
        }
    }

    // Version-1 compatibility grouping runs once before legacy ids exist so
    // it can preserve authored ordering. Resolve the retained root names to
    // stable ids after the upgrade pass assigns every hull identity.
    AssignReleasableTreeGroupsToHulls( m_scene.m_convexHulls );
    AssignReleasableTreeGroupsToHulls( m_scene.m_convexHullStates );
}

const AuthoredSceneParser::Json* AuthoredSceneParser::ReadAssetPartIdentity( const Json& instance, const std::string& path,
                                                                             uint32_t partIndex, uint32_t expectedPartCount,
                                                                             const std::string& expectedPartName )
{
    const Json* parts = FindMember( instance, "parts" );

    if ( m_currentDocumentVersion == 1 )
    {

        if ( parts )
        {
            Fail( path, "assetInstance.parts requires scene schema version 2" );
        }

        return nullptr;
    }

    if ( !parts )
    {

        if ( m_currentDocumentVersion == 2 )
        {
            Fail( path, "assetInstance is missing required field 'parts'" );
        }

        // Compatibility: a migrated v1 instance in schema v3 keeps its
        // recipe-authored part identities and receives ids after parsing.
        return nullptr;
    }

    RequireArray( *parts, path, "assetInstance.parts" );

    if ( ParserFailed() )
    {
        return nullptr;
    }

    if ( parts->size() != expectedPartCount )
    {
        Fail( path, "assetInstance.parts count does not match asset recipe: expected " +
                        std::to_string( expectedPartCount ) + ", got " + std::to_string( parts->size() ) );

        return nullptr;
    }

    const Json& identity = ( *parts )[partIndex];
    RequireObject( identity, path, "assetInstance.parts[]" );
    const std::string name = ReadString( RequireMember( identity, path, "assetInstance.parts[]", "name" ), path,
                                         "assetInstance.parts[].name" );

    if ( ParserFailed() )
    {
        return nullptr;
    }

    if ( name != expectedPartName )
    {
        Fail( path, "assetInstance.parts[] name mismatch: expected '" + expectedPartName + "', got '" + name + "'" );
        return nullptr;
    }

    (void)RequireMember( identity, path, "assetInstance.parts[]", "sceneObjectId" );
    return ParserFailed() ? nullptr : &identity;
}

std::string AuthoredSceneParser::ResolveStylePath( const std::string& token ) const
{

    if ( token.find( '/' ) != std::string::npos || token.find( '\\' ) != std::string::npos ||
         EndsWith( token, ".style.json" ) )
    {
        return token;
    }

    return std::string( "SkullbonezData/styles/" ) + token + ".style.json";
}

void AuthoredSceneParser::LoadStyleIncludes( const Json& root, const std::string& path, const char* memberName, int depth )
{
    const Json* includes = FindMember( root, memberName );

    if ( !includes )
    {
        return;
    }

    RequireArray( *includes, path, memberName );

    if ( ParserFailed() )
    {
        return;
    }

    for ( const Json& include : *includes )
    {
        const std::string token = ReadString( include, path, memberName );

        if ( ParserFailed() )
        {
            return;
        }

        const std::string stylePath = ResolveStylePath( token );
        LoadDocumentIntoScene( stylePath, true, depth + 1 );

        if ( ParserFailed() )
        {
            return;
        }
    }
}

void AuthoredSceneParser::ApplySceneBody( const Json& root, const std::string& path )
{
    LoadAssetLibraries( root, path );

    if ( ParserFailed() )
    {
        return;
    }

    if ( const Json* playback = FindMember( root, "playback" ) )
    {
        ApplyPlayback( *playback, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* simulation = FindMember( root, "simulation" ) )
    {
        ApplySimulation( *simulation, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* tornadoSystem = FindMember( root, "tornadoSystem" ) )
    {
        ApplyTornadoSystem( *tornadoSystem, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* runtime = FindMember( root, "runtime" ) )
    {
        ApplyRuntime( *runtime, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* capture = FindMember( root, "capture" ) )
    {
        ApplyCapture( *capture, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* logging = FindMember( root, "logging" ) )
    {
        ApplyLogging( *logging, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* debug = FindMember( root, "debug" ) )
    {
        ApplyDebug( *debug, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* terrain = FindMember( root, "terrain" ) )
    {
        ApplyTerrain( *terrain, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* editor = FindMember( root, "editor" ) )
    {
        ApplyEditor( *editor, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* ui = FindMember( root, "ui" ) )
    {
        ApplyUI( *ui, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* cinematic = FindMember( root, "cinematic" ) )
    {
        ApplyCinematic( *cinematic, path );

        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* cameras = FindMember( root, "cameras" ) )
    {
        RequireArray( *cameras, path, "cameras" );

        if ( ParserFailed() )
        {
            return;
        }

        for ( const Json& camera : *cameras )
        {
            ApplyCamera( camera, path );

            if ( ParserFailed() )
            {
                return;
            }
        }
    }

    if ( const Json* objects = FindMember( root, "objects" ) )
    {
        RequireArray( *objects, path, "objects" );

        if ( ParserFailed() )
        {
            return;
        }

        for ( const Json& object : *objects )
        {
            ApplyObject( object, path );

            if ( ParserFailed() )
            {
                return;
            }
        }
    }

    if ( const Json* ragdollJoints = FindMember( root, "ragdollJoints" ) )
    {
        RequireArray( *ragdollJoints, path, "ragdollJoints" );

        if ( ParserFailed() )
        {
            return;
        }

        for ( const Json& joint : *ragdollJoints )
        {
            ApplyPointJointConstraint( joint, path );

            if ( ParserFailed() )
            {
                return;
            }
        }
    }

    ApplyAssetInstances( root, path );

    if ( ParserFailed() )
    {
        return;
    }

    ApplyRootedTreeCompatibilityClearanceToHulls( m_scene.m_convexHulls );
    ApplyRootedTreeCompatibilityClearanceToHulls( m_scene.m_convexHullStates );
    AssignReleasableTreeGroupsToHulls( m_scene.m_convexHulls );
    AssignReleasableTreeGroupsToHulls( m_scene.m_convexHullStates );

    if ( const Json* objectMaterials = FindMember( root, "objectMaterials" ) )
    {
        RequireArray( *objectMaterials, path, "objectMaterials" );

        if ( ParserFailed() )
        {
            return;
        }

        for ( const Json& objectMaterial : *objectMaterials )
        {
            ApplyObjectMaterial( objectMaterial, path );

            if ( ParserFailed() )
            {
                return;
            }
        }
    }

    if ( const Json* requirements = FindMember( root, "requirements" ) )
    {
        ApplyRequirements( *requirements, path );

        if ( ParserFailed() )
        {
            return;
        }
    }
}

void AuthoredSceneParser::LoadDocumentIntoScene( const std::string& path, bool styleOnly, int depth )
{

    if ( depth > kMaxStyleIncludeDepth )
    {
        Fail( path, "Style include depth exceeded" );
        return;
    }

    const Json root = ReadJsonFile( path );

    if ( ParserFailed() )
    {
        return;
    }

    RequireObject( root, path, "document root" );

    if ( ParserFailed() )
    {
        return;
    }

    const std::string expectedFormat = styleOnly ? "skullbonez.style.json" : "skullbonez.scene.json";
    const std::string actualFormat = ReadString( RequireMember( root, path, "document root", "format" ), path, "format" );

    if ( ParserFailed() )
    {
        return;
    }

    if ( actualFormat != expectedFormat )
    {
        std::ostringstream message;
        message << "Expected format '" << expectedFormat << "', got '" << actualFormat << "'";
        Fail( path, message.str() );
        return;
    }

    const uint32_t documentVersion = ReadUInt( RequireMember( root, path, "document root", "version" ), path, "version" );

    if ( ParserFailed() )
    {
        return;
    }

    if ( documentVersion < 1 || documentVersion > 3 )
    {
        Fail( path, "Unsupported scene schema version: " + std::to_string( documentVersion ) );
        return;
    }

    if ( !styleOnly && depth == 0 )
    {
        m_scene.m_schemaVersion = documentVersion;
    }

    LoadStyleIncludes( root, path, "includes", depth );

    if ( ParserFailed() )
    {
        return;
    }

    if ( !styleOnly )
    {
        LoadStyleIncludes( root, path, "styles", depth );

        if ( ParserFailed() )
        {
            return;
        }
    }

    // Why: includes can use a different supported schema. Restore the parent
    // document version at its body boundary so identity requirements are
    // decided by the file that actually authored each object.
    const uint32_t previousDocumentVersion = m_currentDocumentVersion;
    m_currentDocumentVersion = documentVersion;
    ApplySceneBody( root, path );
    m_currentDocumentVersion = previousDocumentVersion;
}

// Lifetime: the parser only borrows the asset registry during this parse.
// A null registry keeps standalone tools on the historical path fallback.
AuthoredSceneParser::AuthoredSceneParser( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                          const Assets::AssetSystem* assets )
    : m_resultDiagnostics( resultDiagnostics ), m_assets( assets )
{
}

SkullbonezCore::Core::SbResult AuthoredSceneParser::TryLoadScene( const char* path, AuthoredScene& outScene )
{
    return TryLoadDocument( path, false, outScene );
}

SkullbonezCore::Core::SbResult AuthoredSceneParser::TryLoadStyle( const char* path, AuthoredScene& outScene )
{
    return TryLoadDocument( path, true, outScene );
}


SkullbonezCore::Core::SbResult AuthoredSceneParser::TryLoadDocument( const char* path, bool styleOnly,
                                                                     AuthoredScene& outScene )
{
    m_scene = AuthoredScene();
    m_assetDefinitions.clear();
    m_sceneObjectNames.clear();
    m_sceneObjectIds.clear();
    m_currentDocumentVersion = 1;
    m_failure = ParserFailureState {};

    {
        ParserFailureScope failureScope( m_failure );
        LoadDocumentIntoScene( path ? path : "", styleOnly, 0 );

        if ( !ParserFailed() )
        {
            UpgradeVersion1SceneObjectIds( path ? path : "" );
        }

        if ( !ParserFailed() )
        {

            // Invariant: explicit and legacy group names are resolved only
            // after includes and version-1 ids have reached their final form.
            ValidateReleasableTreeGroups( m_scene.m_convexHulls, path ? path : "" );
            ValidateReleasableTreeGroups( m_scene.m_convexHullStates, path ? path : "" );
        }

        if ( !ParserFailed() && !styleOnly && m_scene.m_cameras.empty() )
        {
            Fail( path ? path : "", "Scene JSON must define at least one camera." );
        }
    }

    if ( m_failure.failed )
    {
        return ParserFailureResult( m_resultDiagnostics, m_failure );
    }

    outScene = m_scene;
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult TryLoadAuthoredSceneFromFileImpl( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                                 const char* path, const Assets::AssetSystem* assets,
                                                                 AuthoredScene& outScene )
{
    return AuthoredSceneParser( resultDiagnostics, assets ).TryLoadScene( path, outScene );
}

SkullbonezCore::Core::SbResult TryLoadStyleSceneFromFileImpl( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                              const char* path, const Assets::AssetSystem* assets,
                                                              AuthoredScene& outScene )
{
    return AuthoredSceneParser( resultDiagnostics, assets ).TryLoadStyle( path, outScene );
}
} // namespace Runtime
} // namespace SkullbonezCore
