/*
File: AuthoredSceneParserAssets.cpp
Purpose:
  Validates asset libraries and expands authored asset instances with provenance.

Summary:
  This translation unit handles one schema domain while mutating the single
  AuthoredSceneParser result. Shared validation and failure policy live in
  AuthoredSceneParserSchema.h; top-level document order stays in AuthoredSceneParser.cpp.

Glossary:
  Schema domain: Cohesive authored section translated without creating another
    scene owner or intermediate model.
  Lane R: Recoverable invalid-input result accumulated by the active parser.

Invariants:
  - Authored JSON field names remain command-line and scene-file compatibility.
  - Parser failure stops further mutation and is returned without an engine throw.
  - Stable scene identities and source ordering are preserved exactly.

Related:
  - AuthoredSceneParserSchema.h declares shared parser state and helpers.
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md owns this decomposition.
*/
#include "AuthoredSceneParserSchema.h"

namespace SkullbonezCore
{
namespace Runtime
{
using AuthoredSceneParserDetail::CopyCheckedStringField;
using AuthoredSceneParserDetail::EndsWith;
using AuthoredSceneParserDetail::Fail;
using AuthoredSceneParserDetail::FindMember;
using AuthoredSceneParserDetail::MakeSceneEulerQuaternion;
using AuthoredSceneParserDetail::ParserFailed;
using AuthoredSceneParserDetail::QuaternionToJson;
using AuthoredSceneParserDetail::ReadBool;
using AuthoredSceneParserDetail::ReadFloat;
using AuthoredSceneParserDetail::ReadInferredContactMaterial;
using AuthoredSceneParserDetail::ReadJsonFile;
using AuthoredSceneParserDetail::ReadString;
using AuthoredSceneParserDetail::ReadUInt;
using AuthoredSceneParserDetail::ReadVec3;
using AuthoredSceneParserDetail::RequireArray;
using AuthoredSceneParserDetail::RequireMember;
using AuthoredSceneParserDetail::RequireObject;
using AuthoredSceneParserDetail::Vector3ToJson;

namespace
{
constexpr uint32_t ASSET_LIBRARY_FORMAT_VERSION = 1;
}

const Assets::AssetLibrarySourceAsset* AuthoredSceneParser::FindRegisteredAssetLibrary( const std::string& token ) const
{
    if ( !m_assets.assets || token.find( '/' ) != std::string::npos || token.find( '\\' ) != std::string::npos ||
         EndsWith( token, ".assets.json" ) )
    {
        return nullptr;
    }

    if ( const Assets::AssetLibrarySourceAsset* library =
             m_assets.assets->FindAssetLibrarySourceAsset( token.c_str() ) )
    {
        return library;
    }

    const std::string prefixedToken = std::string( "assetlib." ) + token;
    return m_assets.assets->FindAssetLibrarySourceAsset( prefixedToken.c_str() );
}

std::string AuthoredSceneParser::ResolveAssetLibraryPath( const std::string& token ) const
{
    if ( token.find( '/' ) != std::string::npos || token.find( '\\' ) != std::string::npos ||
         EndsWith( token, ".assets.json" ) )
    {
        return token;
    }

    if ( const Assets::AssetLibrarySourceAsset* library = FindRegisteredAssetLibrary( token ) )
    {
        return library->resolvedPath;
    }

    return std::string( "SkullbonezData/assets/" ) + token + ".assets.json";
}

const AuthoredSceneParser::ParsedAssetDefinition*
AuthoredSceneParser::FindAssetDefinition( const std::string& name ) const
{
    for ( const ParsedAssetDefinition& asset : m_assetDefinitions )
    {
        const Json* assetName = FindMember( asset.value, "name" );
        if ( assetName && assetName->is_string() && assetName->get<std::string>() == name )
        {
            return &asset;
        }
    }
    return nullptr;
}

bool AuthoredSceneParser::RegisterSceneObjectName( const char* name, const std::string& path )
{
    // Invariant: display-name material targeting and current runtime lookup
    // are unambiguous only when every parsed shape row owns a unique name.
    const std::string candidate = name ? name : "";
    if ( std::find( m_sceneObjectNames.begin(), m_sceneObjectNames.end(), candidate ) != m_sceneObjectNames.end() )
    {
        Fail( path, "Duplicate scene object name: " + candidate );
        return false;
    }
    m_sceneObjectNames.push_back( candidate );
    return true;
}

void AuthoredSceneParser::ValidateAssetMaterial( const Json& owner, const std::string& path, const char* context ) const
{
    const Json& material = RequireMember( owner, path, context, "material" );
    RequireObject( material, path, "asset.material" );
    if ( !FindMember( material, "mode" ) && !FindMember( material, "kind" ) )
    {
        Fail( path, "asset.material is missing required field 'mode'" );
        return;
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

void AuthoredSceneParser::ValidateAssetCommonPhysicsFields( const Json& asset,
                                                            const std::string& path,
                                                            const char* context,
                                                            bool requireMass ) const
{
    if ( const Json* mass = FindMember( asset, "mass" ) )
    {
        const float value = ReadFloat( *mass, path, "asset.mass" );
        if ( value <= 0.0f )
        {
            Fail( path, "asset.mass must be greater than zero" );
            return;
        }
    }
    else if ( requireMass )
    {
        RequireMember( asset, path, context, "mass" );
    }
    ReadFloat( RequireMember( asset, path, context, "restitution" ), path, "asset.restitution" );
    ValidateAssetMaterial( asset, path, context );
    (void)ReadInferredContactMaterial( asset, path, "asset.contactMaterial" );
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
    if ( const Json* angularVelocity = FindMember( asset, "angularVelocity" ) )
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        ReadVec3( *angularVelocity, path, "asset.angularVelocity", x, y, z );
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

std::string
AuthoredSceneParser::ReadAssetPrimitiveType( const Json& asset, const std::string& path, const char* context ) const
{
    // Concept: old compound parts used a bare `hull` member. Keep that as a
    // convex-hull shorthand while new container parts name their primitive.
    if ( const Json* type = FindMember( asset, "type" ) )
    {
        const std::string primitiveType = ReadString( *type, path, "asset.primitive.type" );
        if ( primitiveType == "convexHull" || primitiveType == "box" || primitiveType == "sphere" )
        {
            return primitiveType;
        }
        Fail( path, "Unknown asset primitive type: " + primitiveType );
        return std::string();
    }
    if ( FindMember( asset, "hull" ) )
    {
        return "convexHull";
    }
    Fail( path, std::string( context ) + " must declare type or hull" );
    return std::string();
}

void AuthoredSceneParser::ValidateAssetBoxFields( const Json& asset,
                                                  const std::string& path,
                                                  const char* context ) const
{
    float halfX = 0.0f;
    float halfY = 0.0f;
    float halfZ = 0.0f;
    ReadVec3( RequireMember( asset, path, context, "halfExtents" ), path, "asset.halfExtents", halfX, halfY, halfZ );
    if ( halfX <= 0.0f || halfY <= 0.0f || halfZ <= 0.0f )
    {
        Fail( path, "asset.halfExtents values must be greater than zero" );
        return;
    }
    ValidateAssetCommonPhysicsFields( asset, path, context, true );
}

void AuthoredSceneParser::ValidateAssetSphereFields( const Json& asset,
                                                     const std::string& path,
                                                     const char* context ) const
{
    const float radius = ReadFloat( RequireMember( asset, path, context, "radius" ), path, "asset.radius" );
    if ( radius <= 0.0f )
    {
        Fail( path, "asset.radius must be greater than zero" );
        return;
    }
    if ( const Json* moment = FindMember( asset, "moment" ) )
    {
        const float value = ReadFloat( *moment, path, "asset.moment" );
        if ( value <= 0.0f )
        {
            Fail( path, "asset.moment must be greater than zero" );
            return;
        }
    }
    ValidateAssetCommonPhysicsFields( asset, path, context, true );
}

void AuthoredSceneParser::ValidateConvexHullAssetFields( const Json& asset,
                                                         const std::string& path,
                                                         const char* context ) const
{
    ReadString( RequireMember( asset, path, context, "hull" ), path, "asset.hull" );
    ValidateAssetCommonPhysicsFields( asset, path, context, false );
}

void AuthoredSceneParser::ValidateAssetPrimitiveFields( const Json& asset,
                                                        const std::string& path,
                                                        const char* context ) const
{
    const std::string primitiveType = ReadAssetPrimitiveType( asset, path, context );
    if ( primitiveType == "convexHull" )
    {
        ValidateConvexHullAssetFields( asset, path, context );
        return;
    }
    if ( primitiveType == "box" )
    {
        ValidateAssetBoxFields( asset, path, context );
        return;
    }
    if ( primitiveType == "sphere" )
    {
        ValidateAssetSphereFields( asset, path, context );
        return;
    }
    Fail( path, "Unknown asset primitive type: " + primitiveType );
    return;
}

void AuthoredSceneParser::UpgradeAssetLibraryV0ToV1( Json& root, const std::string& path )
{
    // Version 0 is the unversioned asset-library grammar. Its fields already
    // have v1 meaning, so the deterministic upgrade is an explicit stamp with
    // no recipe reordering or value changes.
    (void)path;
    root["version"] = ASSET_LIBRARY_FORMAT_VERSION;
}

void AuthoredSceneParser::LoadAssetLibrary( const std::string& assetPath, uint32_t libraryRefIndex )
{
    Json root = ReadJsonFile( assetPath );
    RequireObject( root, assetPath, "asset library root" );
    const std::string actualFormat =
        ReadString( RequireMember( root, assetPath, "asset library root", "format" ), assetPath, "format" );
    if ( actualFormat != "skullbonez.asset_library.json" )
    {
        std::ostringstream message;
        message << "Expected format 'skullbonez.asset_library.json', got '" << actualFormat << "'";
        Fail( assetPath, message.str() );
        return;
    }

    const Json* versionMember = FindMember( root, "version" );
    const uint32_t loadedVersion = versionMember ? ReadUInt( *versionMember, assetPath, "version" ) : 0;
    if ( ParserFailed() )
    {
        return;
    }
    if ( loadedVersion > ASSET_LIBRARY_FORMAT_VERSION )
    {
        Fail( assetPath,
              "Asset library format version " + std::to_string( loadedVersion ) + " is newer than current version " +
                  std::to_string( ASSET_LIBRARY_FORMAT_VERSION ) );
        return;
    }
    if ( loadedVersion == 0 )
    {
        UpgradeAssetLibraryV0ToV1( root, assetPath );
    }

    const Json& assets = RequireMember( root, assetPath, "asset library root", "assets" );
    RequireArray( assets, assetPath, "assets" );
    if ( ParserFailed() )
    {
        return;
    }
    for ( const Json& asset : assets )
    {
        RequireObject( asset, assetPath, "asset" );
        if ( ParserFailed() )
        {
            return;
        }
        const std::string name =
            ReadString( RequireMember( asset, assetPath, "asset", "name" ), assetPath, "asset.name" );
        if ( name.empty() )
        {
            Fail( assetPath, "asset.name must not be empty" );
            return;
        }
        if ( FindAssetDefinition( name ) )
        {
            Fail( assetPath, "Duplicate asset name: " + name );
            return;
        }

        const std::string type =
            ReadString( RequireMember( asset, assetPath, "asset", "type" ), assetPath, "asset.type" );
        if ( type == "convexHull" || type == "box" || type == "sphere" )
        {
            ValidateAssetPrimitiveFields( asset, assetPath, "asset" );
        }
        else if ( type == "compound" )
        {
            const Json& parts = RequireMember( asset, assetPath, "asset", "parts" );
            RequireArray( parts, assetPath, "asset.parts" );
            if ( parts.empty() )
            {
                Fail( assetPath, "asset.parts must not be empty" );
                return;
            }
            std::vector<std::string> partNames;
            for ( const Json& part : parts )
            {
                RequireObject( part, assetPath, "asset.parts[]" );
                if ( ParserFailed() )
                {
                    return;
                }
                const std::string partName = ReadString( RequireMember( part, assetPath, "asset.parts[]", "name" ),
                                                         assetPath,
                                                         "asset.parts[].name" );
                if ( partName.empty() )
                {
                    Fail( assetPath, "asset.parts[].name must not be empty" );
                    return;
                }
                if ( std::find( partNames.begin(), partNames.end(), partName ) != partNames.end() )
                {
                    Fail( assetPath, "Duplicate asset part name: " + partName );
                    return;
                }
                partNames.push_back( partName );
                ValidateAssetPrimitiveFields( part, assetPath, "asset.parts[]" );
                if ( ParserFailed() )
                {
                    return;
                }
            }
        }
        else
        {
            Fail( assetPath, "Unknown asset type: " + type );
            return;
        }

        m_assetDefinitions.push_back( ParsedAssetDefinition{ asset, libraryRefIndex } );
    }
}

void AuthoredSceneParser::LoadAssetLibraries( const Json& root, const std::string& path )
{
    const Json* libraries = FindMember( root, "assetLibraries" );
    if ( !libraries )
    {
        return;
    }
    RequireArray( *libraries, path, "assetLibraries" );
    if ( ParserFailed() )
    {
        return;
    }
    for ( const Json& library : *libraries )
    {
        const std::string token = ReadString( library, path, "assetLibraries" );
        const std::string resolvedPath = ResolveAssetLibraryPath( token );
        const Assets::AssetLibrarySourceAsset* registeredLibrary = FindRegisteredAssetLibrary( token );

        SceneAssetLibraryRef reference;
        if ( !CopyCheckedStringField( reference.token, token, path, "assetLibraries[]" ) ||
             !CopyCheckedStringField( reference.resolvedPath, resolvedPath, path, "resolved asset-library path" ) )
        {
            return;
        }
        reference.resolvedAssetId = registeredLibrary ? registeredLibrary->id : 0;
        const uint32_t libraryRefIndex = static_cast<uint32_t>( m_scene.m_assetLibraries.size() );
        m_scene.m_assetLibraries.push_back( reference );

        LoadAssetLibrary( resolvedPath, libraryRefIndex );
        if ( ParserFailed() )
        {
            return;
        }
    }
}

void AuthoredSceneParser::CheckGeneratedSceneName( const std::string& name,
                                                   const std::string& path,
                                                   const char* context ) const
{
    if ( name.empty() )
    {
        Fail( path, std::string( context ) + " must not be empty" );
        return;
    }
    if ( name.size() >= 64 )
    {
        Fail( path, std::string( context ) + " must be shorter than 64 characters" );
        return;
    }
}

std::string AuthoredSceneParser::BuildAssetPartName( const std::string& instanceName,
                                                     const std::string& partName,
                                                     const std::string& path ) const
{
    std::string name = instanceName;
    name += "_";
    name += partName;
    CheckGeneratedSceneName( name, path, "asset part generated name" );
    if ( ParserFailed() )
    {
        return std::string();
    }
    return name;
}

void AuthoredSceneParser::ApplyAssetMaterialForTarget( const Json& asset,
                                                       const std::string& path,
                                                       const std::string& target )
{
    const Json& source = RequireMember( asset, path, "asset", "material" );
    Json material = source;
    RequireObject( material, path, "asset.material" );
    if ( ParserFailed() )
    {
        return;
    }
    material["target"] = target;
    ApplyObjectMaterial( material, path );
}

void AuthoredSceneParser::RecordAssetPart( const std::string& path,
                                           const std::string& partName,
                                           const std::string& objectName,
                                           Physics::PhysicsSceneObjectId sceneObjectId,
                                           uint32_t partIndex,
                                           SceneAssetPartSource source,
                                           uint32_t sourceIndex,
                                           const Math::Vector::Vector3& worldPosition,
                                           const Math::Orientation::Quaternion& worldOrientation )
{
    SceneAssetPartRef part;
    if ( !CopyCheckedStringField( part.partName, partName, path, "asset part name" ) ||
         !CopyCheckedStringField( part.objectName, objectName, path, "expanded asset object name" ) )
    {
        return;
    }
    part.sceneObjectId = sceneObjectId;
    part.partIndex = partIndex;
    // Invariant: source names the exact shape vector; sourceIndex is
    // captured before Apply* appends and recorded only after it succeeds.
    part.sourceIndex = sourceIndex;
    part.source = source;
    part.posX = worldPosition.x;
    part.posY = worldPosition.y;
    part.posZ = worldPosition.z;
    worldOrientation.GetComponents( part.orientX, part.orientY, part.orientZ, part.orientW );
    m_scene.m_assetParts.push_back( part );
}

void AuthoredSceneParser::ApplyAssetPrimitivePart( const Json& asset,
                                                   const std::string& path,
                                                   const std::string& objectName,
                                                   const std::string& partName,
                                                   uint32_t partIndex,
                                                   const AssetInstanceExpansion& instance,
                                                   const Json* authoredPartIdentity )
{
    std::string effectiveObjectName = objectName;
    const Json* liveStateType = authoredPartIdentity ? FindMember( *authoredPartIdentity, "type" ) : nullptr;
    if ( authoredPartIdentity )
    {
        if ( const Json* authoredObjectName = FindMember( *authoredPartIdentity, "objectName" ) )
        {
            effectiveObjectName = ReadString( *authoredObjectName, path, "assetInstance.parts[].objectName" );
        }
    }
    CheckGeneratedSceneName( effectiveObjectName, path, "asset instance part objectName" );
    if ( ParserFailed() )
    {
        return;
    }

    const std::string primitiveType = ReadAssetPrimitiveType( asset, path, "asset" );
    if ( ParserFailed() )
    {
        return;
    }

    Math::Vector::Vector3 localOffset = Math::Vector::ZERO_VECTOR;
    if ( const Json* offset = FindMember( asset, "offset" ) )
    {
        ReadVec3( *offset, path, "asset.offset", localOffset.x, localOffset.y, localOffset.z );
        if ( ParserFailed() )
        {
            return;
        }
    }

    Math::Orientation::Quaternion partOrientation;
    bool hasPartEuler = false;
    if ( const Json* euler = FindMember( asset, "euler" ) )
    {
        Math::Vector::Vector3 partEuler = Math::Vector::ZERO_VECTOR;
        ReadVec3( *euler, path, "asset.euler", partEuler.x, partEuler.y, partEuler.z );
        if ( ParserFailed() )
        {
            return;
        }
        partOrientation = MakeSceneEulerQuaternion( partEuler.x, partEuler.y, partEuler.z );
        hasPartEuler = true;
    }

    Math::Vector::Vector3 velocity = instance.velocity;
    bool hasVelocity = instance.HasOverride( SCENE_ASSET_OVERRIDE_VELOCITY );
    if ( const Json* velocityValue = FindMember( asset, "velocity" ) )
    {
        Math::Vector::Vector3 partVelocity = Math::Vector::ZERO_VECTOR;
        ReadVec3( *velocityValue, path, "asset.velocity", partVelocity.x, partVelocity.y, partVelocity.z );
        if ( ParserFailed() )
        {

            return;
        }
        velocity += partVelocity;
        hasVelocity = true;
    }

    Math::Vector::Vector3 angularVelocity = instance.angularVelocity;
    bool hasAngularVelocity = instance.HasOverride( SCENE_ASSET_OVERRIDE_ANGULAR_VELOCITY );
    if ( const Json* angularVelocityValue = FindMember( asset, "angularVelocity" ) )
    {
        Math::Vector::Vector3 partAngularVelocity = Math::Vector::ZERO_VECTOR;
        ReadVec3( *angularVelocityValue,
                  path,
                  "asset.angularVelocity",
                  partAngularVelocity.x,
                  partAngularVelocity.y,
                  partAngularVelocity.z );
        if ( ParserFailed() )
        {
            return;
        }
        angularVelocity += partAngularVelocity;
        hasAngularVelocity = true;
    }

    bool fixed = false;
    if ( const Json* fixedValue = FindMember( asset, "fixed" ) )
    {
        fixed = ReadBool( *fixedValue, path, "asset.fixed" );
        if ( ParserFailed() )
        {
            return;
        }
    }
    if ( instance.HasOverride( SCENE_ASSET_OVERRIDE_FIXED ) )
    {
        fixed = instance.fixed;
    }

    bool sleeping = false;
    if ( const Json* sleepingValue = FindMember( asset, "sleeping" ) )
    {
        sleeping = ReadBool( *sleepingValue, path, "asset.sleeping" );
        if ( ParserFailed() )
        {
            return;
        }
    }
    if ( instance.HasOverride( SCENE_ASSET_OVERRIDE_SLEEPING ) )
    {
        sleeping = instance.sleeping;
    }

    // Concept: an asset part is a child transform. Rotate its local offset
    // by the instance orientation, then compose part rotation after the
    // instance rotation using the engine's documented quaternion order.
    Math::Orientation::Quaternion instanceOrientation = instance.orientation;
    const Math::Vector::Vector3 worldPosition =
        instance.position + instanceOrientation.GetOrientationMatrix() * localOffset;
    Math::Orientation::Quaternion worldOrientation = instance.orientation * partOrientation;
    worldOrientation.Normalise();
    const bool hasOrientation = instance.HasOverride( SCENE_ASSET_OVERRIDE_EULER ) || hasPartEuler;

    Json object = Json::object();
    object["name"] = effectiveObjectName;
    object["position"] = Vector3ToJson( worldPosition );
    object["fixed"] = fixed;
    object["contactMaterial"] = ReadInferredContactMaterial( asset, path, "asset.contactMaterial" );
    if ( authoredPartIdentity )
    {
        object["sceneObjectId"] =
            ReadUInt( RequireMember( *authoredPartIdentity, path, "assetInstance.parts[]", "sceneObjectId" ),
                      path,
                      "assetInstance.parts[].sceneObjectId" );
        if ( ParserFailed() )
        {
            return;
        }
    }

    // Concept: schema-v2 saved asset parts may carry a full live-state
    // packet. Identity-only part records still expand the recipe exactly as
    // before, while a typed packet replaces every independently simulated
    // body/collider field without reapplying the stale instance transform.
    if ( liveStateType )
    {
        const std::string expectedStateType = primitiveType == "sphere" ? "ballState"
                                              : primitiveType == "box"  ? "boxState"
                                                                        : "convexHullState";
        const std::string authoredStateType = ReadString( *liveStateType, path, "assetInstance.parts[].type" );
        if ( ParserFailed() )
        {
            return;
        }
        if ( authoredStateType != expectedStateType )
        {
            Fail( path,
                  "assetInstance.parts[] live type mismatch: expected '" + expectedStateType + "', got '" +
                      authoredStateType + "'" );
            return;
        }
    }

    const auto applyLiveState = [&]()
    {
        if ( !liveStateType )
        {
            return;
        }
        static constexpr const char* kLiveStateFields[] = {
            "position",
            "velocity",
            "angularVelocity",
            "orientation",
            "radius",
            "halfExtents",
            "hull",
            "mass",
            "restitution",
            "contactMaterial",
            "inertia",
            "fixed",
            "sleeping",
            "contactReleaseOnImpact",
            "contactReleaseImpulseThreshold",
            "objectGroup",
        };
        for ( const char* field : kLiveStateFields )
        {
            if ( const Json* value = FindMember( *authoredPartIdentity, field ) )
            {
                object[field] = *value;
            }
        }
    };

    if ( primitiveType == "convexHull" )
    {
        const uint32_t sourceIndex =
            static_cast<uint32_t>( liveStateType ? m_scene.m_convexHullStates.size() : m_scene.m_convexHulls.size() );
        object["hull"] = ReadString( RequireMember( asset, path, "asset", "hull" ), path, "asset.hull" );
        if ( ParserFailed() )
        {
            return;
        }
        if ( const Json* mass = FindMember( asset, "mass" ) )
        {
            object["mass"] = ReadFloat( *mass, path, "asset.mass" );
            if ( ParserFailed() )
            {
                return;
            }
        }
        object["restitution"] =
            ReadFloat( RequireMember( asset, path, "asset", "restitution" ), path, "asset.restitution" );
        if ( ParserFailed() )
        {
            return;
        }
        object["sleeping"] = sleeping;
        if ( const Json* release = FindMember( asset, "contactReleaseOnImpact" ) )
        {
            object["contactReleaseOnImpact"] = ReadBool( *release, path, "asset.contactReleaseOnImpact" );
            if ( ParserFailed() )
            {
                return;
            }
        }
        if ( const Json* threshold = FindMember( asset, "contactReleaseImpulseThreshold" ) )
        {
            object["contactReleaseImpulseThreshold"] =
                (std::max)( 0.0f, ReadFloat( *threshold, path, "asset.contactReleaseImpulseThreshold" ) );
            if ( ParserFailed() )
            {
                return;
            }
        }
        if ( hasVelocity )
        {
            object["velocity"] = Vector3ToJson( velocity );
        }
        if ( hasAngularVelocity )
        {
            object["angularVelocity"] = Vector3ToJson( angularVelocity );
        }
        applyLiveState();

        if ( liveStateType )
        {
            ApplyConvexHullState( object, path );
        }
        else
        {
            ApplyConvexHull( object, path, false, hasOrientation ? &worldOrientation : nullptr );
        }
        ApplyAssetMaterialForTarget( asset, path, effectiveObjectName );
        if ( ParserFailed() )
        {
            return;
        }
        if ( liveStateType )
        {
            const SceneConvexHullState& state = m_scene.m_convexHullStates[sourceIndex];
            RecordAssetPart(
                path,
                partName,
                effectiveObjectName,
                state.sceneObjectId,
                partIndex,
                SceneAssetPartSource::ConvexHullState,
                sourceIndex,
                Math::Vector::Vector3( state.posX, state.posY, state.posZ ),
                Math::Orientation::Quaternion( state.orientX, state.orientY, state.orientZ, state.orientW ) );
        }
        else
        {
            RecordAssetPart( path,
                             partName,
                             effectiveObjectName,
                             m_scene.m_convexHulls[sourceIndex].sceneObjectId,
                             partIndex,
                             SceneAssetPartSource::ConvexHull,
                             sourceIndex,
                             worldPosition,
                             worldOrientation );
        }
        return;
    }

    // Why: primitive container parts need state records so asset-authored
    // sleep, velocity, angular velocity, and orientation survive expansion.
    object["velocity"] = Vector3ToJson( hasVelocity ? velocity : Math::Vector::ZERO_VECTOR );
    object["angularVelocity"] = Vector3ToJson( hasAngularVelocity ? angularVelocity : Math::Vector::ZERO_VECTOR );
    object["orientation"] = QuaternionToJson( worldOrientation );
    object["mass"] = ReadFloat( RequireMember( asset, path, "asset", "mass" ), path, "asset.mass" );
    object["restitution"] =
        ReadFloat( RequireMember( asset, path, "asset", "restitution" ), path, "asset.restitution" );
    if ( ParserFailed() )
    {
        return;
    }
    object["sleeping"] = sleeping;

    if ( primitiveType == "box" )
    {
        const uint32_t sourceIndex = static_cast<uint32_t>( m_scene.m_boxStates.size() );
        Math::Vector::Vector3 halfExtents;
        ReadVec3( RequireMember( asset, path, "asset", "halfExtents" ),
                  path,
                  "asset.halfExtents",
                  halfExtents.x,
                  halfExtents.y,
                  halfExtents.z );
        if ( ParserFailed() )
        {
            return;
        }
        const float mass = object["mass"].get<float>();
        object["halfExtents"] = Vector3ToJson( halfExtents );
        object["inertia"] = Vector3ToJson( Physics::CalculateBoxInertiaForHalfExtents( halfExtents, mass ) );
        applyLiveState();
        ApplyBoxState( object, path );
        ApplyAssetMaterialForTarget( asset, path, effectiveObjectName );
        if ( ParserFailed() )
        {
            return;
        }
        const SceneBoxState& state = m_scene.m_boxStates[sourceIndex];
        RecordAssetPart( path,
                         partName,
                         effectiveObjectName,
                         state.sceneObjectId,
                         partIndex,
                         SceneAssetPartSource::BoxState,
                         sourceIndex,
                         Math::Vector::Vector3( state.posX, state.posY, state.posZ ),
                         Math::Orientation::Quaternion( state.orientX, state.orientY, state.orientZ, state.orientW ) );
        return;
    }

    if ( primitiveType == "sphere" )
    {
        const uint32_t sourceIndex = static_cast<uint32_t>( m_scene.m_ballStates.size() );
        const float radius = ReadFloat( RequireMember( asset, path, "asset", "radius" ), path, "asset.radius" );
        if ( ParserFailed() )
        {
            return;
        }
        const float mass = object["mass"].get<float>();
        object["radius"] = radius;
        object["inertia"] = Vector3ToJson( Physics::CalculateSphereInertia( radius, mass ) );
        applyLiveState();
        ApplyBallState( object, path );
        ApplyAssetMaterialForTarget( asset, path, effectiveObjectName );
        if ( ParserFailed() )
        {
            return;
        }
        const SceneBallState& state = m_scene.m_ballStates[sourceIndex];
        RecordAssetPart( path,
                         partName,
                         effectiveObjectName,
                         state.sceneObjectId,
                         partIndex,
                         SceneAssetPartSource::BallState,
                         sourceIndex,
                         Math::Vector::Vector3( state.posX, state.posY, state.posZ ),
                         Math::Orientation::Quaternion( state.orientX, state.orientY, state.orientZ, state.orientW ) );
        return;
    }

    Fail( path, "Unknown asset primitive type: " + primitiveType );
    return;
}

void AuthoredSceneParser::ApplyAssetInstance( const Json& instance, const std::string& path )
{
    RequireObject( instance, path, "assetInstance" );
    if ( ParserFailed() )
    {
        return;
    }
    const std::string assetName =
        ReadString( RequireMember( instance, path, "assetInstance", "asset" ), path, "assetInstance.asset" );
    if ( ParserFailed() )
    {
        return;
    }
    const ParsedAssetDefinition* assetDefinition = FindAssetDefinition( assetName );
    if ( !assetDefinition )
    {
        Fail( path, "Unknown asset instance reference: " + assetName );
        return;
    }
    const Json& asset = assetDefinition->value;

    const std::string instanceName =
        ReadString( RequireMember( instance, path, "assetInstance", "name" ), path, "assetInstance.name" );
    if ( ParserFailed() )
    {
        return;
    }
    CheckGeneratedSceneName( instanceName, path, "assetInstance.name" );
    if ( ParserFailed() )
    {
        return;
    }
    for ( const SceneAssetInstanceRecord& existing : m_scene.m_assetInstances )
    {
        if ( instanceName == existing.instanceName )
        {
            Fail( path, "Duplicate asset instance name: " + instanceName );
            return;
        }
    }

    AssetInstanceExpansion expansion;
    ReadVec3( RequireMember( instance, path, "assetInstance", "position" ),
              path,
              "assetInstance.position",
              expansion.position.x,
              expansion.position.y,
              expansion.position.z );
    if ( ParserFailed() )
    {
        return;
    }

    if ( const Json* fixed = FindMember( instance, "fixed" ) )
    {
        expansion.overrideMask |= SCENE_ASSET_OVERRIDE_FIXED;
        expansion.fixed = ReadBool( *fixed, path, "assetInstance.fixed" );
        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* sleeping = FindMember( instance, "sleeping" ) )
    {
        expansion.overrideMask |= SCENE_ASSET_OVERRIDE_SLEEPING;
        expansion.sleeping = ReadBool( *sleeping, path, "assetInstance.sleeping" );
        if ( ParserFailed() )
        {
            return;
        }
    }

    if ( const Json* euler = FindMember( instance, "euler" ) )
    {
        ReadVec3( *euler,
                  path,
                  "assetInstance.euler",
                  expansion.authoredEuler.x,
                  expansion.authoredEuler.y,
                  expansion.authoredEuler.z );
        if ( ParserFailed() )
        {
            return;
        }
        expansion.overrideMask |= SCENE_ASSET_OVERRIDE_EULER;
        expansion.orientation =
            MakeSceneEulerQuaternion( expansion.authoredEuler.x, expansion.authoredEuler.y, expansion.authoredEuler.z );
    }

    if ( const Json* velocity = FindMember( instance, "velocity" ) )
    {
        ReadVec3( *velocity,
                  path,
                  "assetInstance.velocity",
                  expansion.velocity.x,
                  expansion.velocity.y,
                  expansion.velocity.z );
        if ( ParserFailed() )
        {
            return;
        }
        expansion.overrideMask |= SCENE_ASSET_OVERRIDE_VELOCITY;
    }

    if ( const Json* angularVelocity = FindMember( instance, "angularVelocity" ) )
    {
        ReadVec3( *angularVelocity,
                  path,
                  "assetInstance.angularVelocity",
                  expansion.angularVelocity.x,
                  expansion.angularVelocity.y,
                  expansion.angularVelocity.z );
        if ( ParserFailed() )
        {
            return;
        }
        expansion.overrideMask |= SCENE_ASSET_OVERRIDE_ANGULAR_VELOCITY;
    }

    SceneAssetInstanceRecord record;
    if ( !CopyCheckedStringField( record.assetName, assetName, path, "assetInstance.asset" ) ||
         !CopyCheckedStringField( record.instanceName, instanceName, path, "assetInstance.name" ) )
    {
        return;
    }
    record.libraryRefIndex = assetDefinition->libraryRefIndex;
    record.firstPart = static_cast<uint32_t>( m_scene.m_assetParts.size() );
    record.overrideMask = expansion.overrideMask;
    record.posX = expansion.position.x;
    record.posY = expansion.position.y;
    record.posZ = expansion.position.z;
    record.eulerX = expansion.authoredEuler.x;
    record.eulerY = expansion.authoredEuler.y;
    record.eulerZ = expansion.authoredEuler.z;
    expansion.orientation.GetComponents( record.orientX, record.orientY, record.orientZ, record.orientW );
    record.velX = expansion.velocity.x;
    record.velY = expansion.velocity.y;
    record.velZ = expansion.velocity.z;
    record.angVelX = expansion.angularVelocity.x;
    record.angVelY = expansion.angularVelocity.y;
    record.angVelZ = expansion.angularVelocity.z;
    record.fixed = expansion.fixed;
    record.sleeping = expansion.sleeping;

    const std::string type = ReadString( RequireMember( asset, path, "asset", "type" ), path, "asset.type" );
    if ( ParserFailed() )
    {
        return;
    }
    if ( type == "convexHull" || type == "box" || type == "sphere" )
    {
        const Json* identity = ReadAssetPartIdentity( instance, path, 0, 1, assetName );
        if ( ParserFailed() )
        {
            return;
        }
        ApplyAssetPrimitivePart( asset, path, instanceName, assetName, 0, expansion, identity );
    }
    else if ( type == "compound" )
    {
        const Json& parts = RequireMember( asset, path, "asset", "parts" );
        RequireArray( parts, path, "asset.parts" );
        if ( ParserFailed() )
        {
            return;
        }
        uint32_t partIndex = 0;
        const uint32_t partCount = static_cast<uint32_t>( parts.size() );
        for ( const Json& part : parts )
        {
            const std::string partName =
                ReadString( RequireMember( part, path, "asset.parts[]", "name" ), path, "asset.parts[].name" );
            if ( ParserFailed() )
            {
                return;
            }
            const Json* identity = ReadAssetPartIdentity( instance, path, partIndex, partCount, partName );
            if ( ParserFailed() )
            {
                return;
            }
            ApplyAssetPrimitivePart( part,
                                     path,
                                     BuildAssetPartName( instanceName, partName, path ),
                                     partName,
                                     partIndex,
                                     expansion,
                                     identity );
            if ( ParserFailed() )
            {
                return;
            }
            ++partIndex;
        }
    }
    else
    {
        Fail( path, "Unknown asset type: " + type );
        return;
    }

    if ( ParserFailed() )
    {
        return;
    }
    // Invariant: publish the instance range only after every expanded shape
    // and ordered part reference has succeeded. Failed parses discard the
    // private AuthoredScene and never expose a partial range to the caller.
    record.partCount = static_cast<uint32_t>( m_scene.m_assetParts.size() ) - record.firstPart;
    if ( record.partCount > 0 )
    {
        record.rootSceneObjectId = m_scene.m_assetParts[record.firstPart].sceneObjectId;
    }
    m_scene.m_assetInstances.push_back( record );
}

void AuthoredSceneParser::ApplyAssetInstances( const Json& root, const std::string& path )
{
    const Json* instances = FindMember( root, "assetInstances" );
    if ( !instances )
    {
        return;
    }
    RequireArray( *instances, path, "assetInstances" );
    if ( ParserFailed() )
    {
        return;
    }
    for ( const Json& instance : *instances )
    {
        ApplyAssetInstance( instance, path );
        if ( ParserFailed() )
        {
            return;
        }
    }
}


} // namespace Runtime
} // namespace SkullbonezCore
