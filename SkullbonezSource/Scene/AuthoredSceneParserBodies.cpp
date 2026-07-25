/*
File: AuthoredSceneParserBodies.cpp
Purpose:
  Parses bodies, state rows, joints, materials, requirements, and object-group metadata.

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
using AuthoredSceneParserDetail::CopyOptionalContactMaterial;
using AuthoredSceneParserDetail::Fail;
using AuthoredSceneParserDetail::FindMember;
using AuthoredSceneParserDetail::LoadConvexHullDefaultMass;
using AuthoredSceneParserDetail::ParseMaterialModeValue;
using AuthoredSceneParserDetail::ParserFailed;
using AuthoredSceneParserDetail::ReadBool;
using AuthoredSceneParserDetail::ReadFloat;
using AuthoredSceneParserDetail::ReadInt;
using AuthoredSceneParserDetail::ReadOptionalSceneObjectGroup;
using AuthoredSceneParserDetail::ReadRequiredStringField;
using AuthoredSceneParserDetail::ReadString;
using AuthoredSceneParserDetail::ReadUnitFloat;
using AuthoredSceneParserDetail::ReadVec3;
using AuthoredSceneParserDetail::ReadVec4;
using AuthoredSceneParserDetail::RequireArray;
using AuthoredSceneParserDetail::RequireMember;
using AuthoredSceneParserDetail::RequireObject;
using AuthoredSceneParserDetail::SetObjectMaterialBaseColor;

void AuthoredSceneParser::ApplyBall( const Json& object, const std::string& path, bool isFixed )
{
    SceneBall ball = {};
    ball.sceneObjectId = ReadSceneObjectId( object, path, "ball" );
    if ( ParserFailed() )
    {
        return;
    }
    ReadRequiredStringField( ball.name, object, path, "ball", "name" );
    if ( ParserFailed() || !RegisterSceneObjectName( ball.name, path ) )
    {
        return;
    }
    ReadVec3(
        RequireMember( object, path, "ball", "position" ),
        path,
        "ball.position",
        ball.posX,
        ball.posY,
        ball.posZ
    );

    ball.m_radius = ReadFloat( RequireMember( object, path, "ball", "radius" ), path, "ball.radius" );
    ball.m_mass = ReadFloat( RequireMember( object, path, "ball", "mass" ), path, "ball.mass" );
    ball.moment = ReadFloat( RequireMember( object, path, "ball", "moment" ), path, "ball.moment" );
    ball.restitution = ReadFloat( RequireMember( object, path, "ball", "restitution" ), path, "ball.restitution" );
    CopyOptionalContactMaterial( ball.contactMaterial, object, path, "ball.contactMaterial" );
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

void AuthoredSceneParser::ApplyBox( const Json& object, const std::string& path, bool isFixed )
{
    SceneBox box = {};
    box.sceneObjectId = ReadSceneObjectId( object, path, "box" );
    if ( ParserFailed() )
    {
        return;
    }
    ReadRequiredStringField( box.name, object, path, "box", "name" );
    if ( ParserFailed() || !RegisterSceneObjectName( box.name, path ) )
    {
        return;
    }
    ReadVec3( RequireMember( object, path, "box", "position" ), path, "box.position", box.posX, box.posY, box.posZ );
    ReadVec3(
        RequireMember( object, path, "box", "halfExtents" ),
        path,
        "box.halfExtents",
        box.halfX,
        box.halfY,
        box.halfZ
    );

    box.mass = ReadFloat( RequireMember( object, path, "box", "mass" ), path, "box.mass" );
    box.restitution = ReadFloat( RequireMember( object, path, "box", "restitution" ), path, "box.restitution" );
    CopyOptionalContactMaterial( box.contactMaterial, object, path, "box.contactMaterial" );
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

void AuthoredSceneParser::ApplyConvexHull(
    const Json& object,
    const std::string& path,
    bool isFixed,
    const Math::Orientation::Quaternion* composedOrientation
)
{
    SceneConvexHull hull = {};
    hull.sceneObjectId = ReadSceneObjectId( object, path, "convexHull" );
    if ( ParserFailed() )
    {
        return;
    }
    ReadRequiredStringField( hull.name, object, path, "convexHull", "name" );
    if ( ParserFailed() || !RegisterSceneObjectName( hull.name, path ) )
    {
        return;
    }
    ReadRequiredStringField( hull.hullPath, object, path, "convexHull", "hull" );
    ReadOptionalSceneObjectGroup( hull.group, object, path, "convexHull" );
    ReadVec3(
        RequireMember( object, path, "convexHull", "position" ),
        path,
        "convexHull.position",
        hull.posX,
        hull.posY,
        hull.posZ
    );

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
    CopyOptionalContactMaterial( hull.contactMaterial, object, path, "convexHull.contactMaterial" );
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
    if ( composedOrientation )
    {
        // Lifetime: this pointer is borrowed only for the internal asset
        // expansion call. It is not an authored JSON field or retained.
        composedOrientation->GetComponents( hull.orientX, hull.orientY, hull.orientZ, hull.orientW );
        hull.hasInitQuaternionOrient = true;
    }
    else if ( const Json* euler = FindMember( object, "euler" ) )
    {
        ReadVec3( *euler, path, "convexHull.euler", hull.eulerX, hull.eulerY, hull.eulerZ );
        hull.hasInitOrient = true;
    }
    if ( const Json* velocity = FindMember( object, "velocity" ) )
    {
        ReadVec3( *velocity, path, "convexHull.velocity", hull.velX, hull.velY, hull.velZ );
        hull.hasInitVelocity = true;
    }
    if ( const Json* angularVelocity = FindMember( object, "angularVelocity" ) )
    {
        ReadVec3( *angularVelocity, path, "convexHull.angularVelocity", hull.angVelX, hull.angVelY, hull.angVelZ );
        hull.hasInitAngularVelocity = true;
    }
    m_scene.m_convexHulls.push_back( hull );
}

void AuthoredSceneParser::ApplyBallState( const Json& object, const std::string& path )
{
    SceneBallState state = {};
    state.sceneObjectId = ReadSceneObjectId( object, path, "ballState" );
    if ( ParserFailed() )
    {
        return;
    }
    ReadRequiredStringField( state.name, object, path, "ballState", "name" );
    if ( ParserFailed() || !RegisterSceneObjectName( state.name, path ) )
    {
        return;
    }
    ReadVec3(
        RequireMember( object, path, "ballState", "position" ),
        path,
        "ballState.position",
        state.posX,
        state.posY,
        state.posZ
    );

    ReadVec3(
        RequireMember( object, path, "ballState", "velocity" ),
        path,
        "ballState.velocity",
        state.velX,
        state.velY,
        state.velZ
    );

    ReadVec3(
        RequireMember( object, path, "ballState", "angularVelocity" ),
        path,
        "ballState.angularVelocity",
        state.angVelX,
        state.angVelY,
        state.angVelZ
    );

    ReadVec4(
        RequireMember( object, path, "ballState", "orientation" ),
        path,
        "ballState.orientation",
        state.orientX,
        state.orientY,
        state.orientZ,
        state.orientW
    );

    state.radius = ReadFloat( RequireMember( object, path, "ballState", "radius" ), path, "ballState.radius" );
    state.mass = ReadFloat( RequireMember( object, path, "ballState", "mass" ), path, "ballState.mass" );
    state.restitution =
        ReadFloat( RequireMember( object, path, "ballState", "restitution" ), path, "ballState.restitution" );
    CopyOptionalContactMaterial( state.contactMaterial, object, path, "ballState.contactMaterial" );
    ReadVec3(
        RequireMember( object, path, "ballState", "inertia" ),
        path,
        "ballState.inertia",
        state.inertiaX,
        state.inertiaY,
        state.inertiaZ
    );

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

void AuthoredSceneParser::ApplyBoxState( const Json& object, const std::string& path )
{
    SceneBoxState state = {};
    state.sceneObjectId = ReadSceneObjectId( object, path, "boxState" );
    if ( ParserFailed() )
    {
        return;
    }
    ReadRequiredStringField( state.name, object, path, "boxState", "name" );
    if ( ParserFailed() || !RegisterSceneObjectName( state.name, path ) )
    {
        return;
    }
    ReadVec3(
        RequireMember( object, path, "boxState", "position" ),
        path,
        "boxState.position",
        state.posX,
        state.posY,
        state.posZ
    );

    ReadVec3(
        RequireMember( object, path, "boxState", "velocity" ),
        path,
        "boxState.velocity",
        state.velX,
        state.velY,
        state.velZ
    );

    ReadVec3(
        RequireMember( object, path, "boxState", "angularVelocity" ),
        path,
        "boxState.angularVelocity",
        state.angVelX,
        state.angVelY,
        state.angVelZ
    );

    ReadVec4(
        RequireMember( object, path, "boxState", "orientation" ),
        path,
        "boxState.orientation",
        state.orientX,
        state.orientY,
        state.orientZ,
        state.orientW
    );

    ReadVec3(
        RequireMember( object, path, "boxState", "halfExtents" ),
        path,
        "boxState.halfExtents",
        state.halfX,
        state.halfY,
        state.halfZ
    );

    state.mass = ReadFloat( RequireMember( object, path, "boxState", "mass" ), path, "boxState.mass" );
    state.restitution =
        ReadFloat( RequireMember( object, path, "boxState", "restitution" ), path, "boxState.restitution" );
    CopyOptionalContactMaterial( state.contactMaterial, object, path, "boxState.contactMaterial" );
    ReadVec3(
        RequireMember( object, path, "boxState", "inertia" ),
        path,
        "boxState.inertia",
        state.inertiaX,
        state.inertiaY,
        state.inertiaZ
    );

    state.isFixed = ReadBool( RequireMember( object, path, "boxState", "fixed" ), path, "boxState.fixed" );
    if ( const Json* sleeping = FindMember( object, "sleeping" ) )
    {
        state.isSleeping = ReadBool( *sleeping, path, "boxState.sleeping" );
    }
    m_scene.m_boxStates.push_back( state );
}

void AuthoredSceneParser::ApplyConvexHullState( const Json& object, const std::string& path )
{
    SceneConvexHullState state = {};
    state.sceneObjectId = ReadSceneObjectId( object, path, "convexHullState" );
    if ( ParserFailed() )
    {

        return;
    }
    ReadRequiredStringField( state.name, object, path, "convexHullState", "name" );
    if ( ParserFailed() || !RegisterSceneObjectName( state.name, path ) )
    {
        return;
    }
    ReadRequiredStringField( state.hullPath, object, path, "convexHullState", "hull" );
    ReadOptionalSceneObjectGroup( state.group, object, path, "convexHullState" );
    ReadVec3(
        RequireMember( object, path, "convexHullState", "position" ),
        path,
        "convexHullState.position",
        state.posX,
        state.posY,
        state.posZ
    );
    ReadVec3(
        RequireMember( object, path, "convexHullState", "velocity" ),
        path,
        "convexHullState.velocity",
        state.velX,
        state.velY,
        state.velZ
    );
    ReadVec3(
        RequireMember( object, path, "convexHullState", "angularVelocity" ),
        path,
        "convexHullState.angularVelocity",
        state.angVelX,
        state.angVelY,
        state.angVelZ
    );
    ReadVec4(
        RequireMember( object, path, "convexHullState", "orientation" ),
        path,
        "convexHullState.orientation",
        state.orientX,
        state.orientY,
        state.orientZ,
        state.orientW
    );
    state.mass = ReadFloat( RequireMember( object, path, "convexHullState", "mass" ), path, "convexHullState.mass" );
    state.restitution = ReadFloat(
        RequireMember( object, path, "convexHullState", "restitution" ),
        path,
        "convexHullState.restitution"
    );
    CopyOptionalContactMaterial( state.contactMaterial, object, path, "convexHullState.contactMaterial" );
    ReadVec3(
        RequireMember( object, path, "convexHullState", "inertia" ),
        path,
        "convexHullState.inertia",
        state.inertiaX,
        state.inertiaY,
        state.inertiaZ
    );
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

void AuthoredSceneParser::ApplyRagdoll( const Json& object, const std::string& path )
{
    SceneRagdoll ragdoll = {};
    ragdoll.firstSceneObjectId =
        ReadSceneObjectId( object, path, "ragdoll", static_cast<uint32_t>( Physics::Ragdoll::SIMPLE_PART_COUNT ) );
    if ( ParserFailed() )
    {
        return;
    }
    ReadRequiredStringField( ragdoll.name, object, path, "ragdoll", "name" );
    if ( ParserFailed() )
    {
        return;
    }
    for ( int partIndex = 0; partIndex < Physics::Ragdoll::SIMPLE_PART_COUNT; ++partIndex )
    {
        char partName[64] = {};

        if ( !Physics::Ragdoll::TryBuildSimplePartName( ragdoll.name, partIndex, partName ) )
        {
            Fail( path, "ragdoll.name produces a part name longer than 63 characters" );
            return;
        }
        if ( !RegisterSceneObjectName( partName, path ) )
        {
            return;
        }
    }
    ReadVec3(
        RequireMember( object, path, "ragdoll", "position" ),
        path,
        "ragdoll.position",
        ragdoll.posX,
        ragdoll.posY,
        ragdoll.posZ
    );

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

void AuthoredSceneParser::ApplyObject( const Json& object, const std::string& path )
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

void AuthoredSceneParser::ApplyPointJointConstraint( const Json& jointJson, const std::string& path )
{
    RequireObject( jointJson, path, "ragdollJoint" );
    ScenePointJointConstraint joint = {};
    ReadRequiredStringField( joint.bodyA, jointJson, path, "ragdollJoint", "bodyA" );
    ReadRequiredStringField( joint.bodyB, jointJson, path, "ragdollJoint", "bodyB" );
    ReadVec3(
        RequireMember( jointJson, path, "ragdollJoint", "localAnchorA" ),
        path,
        "ragdollJoint.localAnchorA",
        joint.localAnchorA.x,
        joint.localAnchorA.y,
        joint.localAnchorA.z
    );

    ReadVec3(
        RequireMember( jointJson, path, "ragdollJoint", "localAnchorB" ),
        path,
        "ragdollJoint.localAnchorB",
        joint.localAnchorB.x,
        joint.localAnchorB.y,
        joint.localAnchorB.z
    );

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

void AuthoredSceneParser::ApplyObjectMaterial( const Json& materialJson, const std::string& path )
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
        // Lane R: Fail records a recoverable authoring error instead of
        // unwinding. Return before dereferencing the absent JSON member.
        return;
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
    strncpy_s(
        material.material.name,
        sizeof( material.material.name ),
        Rendering::RenderMaterialKindName( material.material.kind ),
        _TRUNCATE
    );
    SetObjectMaterialBaseColor( material, tintR, tintG, tintB );

    if ( const Json* alpha = FindMember( materialJson, "alpha" ) )
    {
        material.material.baseColor[3] = ReadUnitFloat( *alpha, path, "objectMaterial.alpha" );
    }

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
        ReadVec3(
            *emissive,
            path,
            "objectMaterial.emissive",
            material.material.emissiveColor[0],
            material.material.emissiveColor[1],
            material.material.emissiveColor[2]
        );
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
        strncpy_s(
            material.material.name,
            sizeof( material.material.name ),
            ReadString( *name, path, "objectMaterial.name" ).c_str(),
            _TRUNCATE
        );
    }

    static constexpr const char* kAllowedKeys[] = {
        "target",   "mode",     "kind",         "tint",        "color",    "colour",   "alpha", "roughness",
        "metallic", "specular", "transmission", "stylization", "emissive", "strength", "flags", "name",
    };

    for ( const auto& item : materialJson.items() )
    {
        const bool known = std::any_of(
            std::begin( kAllowedKeys ),
            std::end( kAllowedKeys ),
            [&]( const char* key ) { return item.key() == key; }
        );

        if ( !known )
        {
            Fail( path, "Unknown objectMaterial field: " + item.key() );
        }
    }

    m_scene.m_objectMaterials.push_back( material );
}

void AuthoredSceneParser::ApplyRequirements( const Json& requirements, const std::string& path )
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

            cell.minCellX = ReadInt(
                RequireMember( cellJson, path, "requirements.broadphaseXCells[]", "min" ),
                path,
                "requirements.broadphaseXCells[].min"
            );

            cell.maxCellX = ReadInt(
                RequireMember( cellJson, path, "requirements.broadphaseXCells[]", "max" ),
                path,
                "requirements.broadphaseXCells[].max"
            );

            cell.cellY = ReadInt(
                RequireMember( cellJson, path, "requirements.broadphaseXCells[]", "y" ),
                path,
                "requirements.broadphaseXCells[].y"
            );

            cell.cellZ = ReadInt(
                RequireMember( cellJson, path, "requirements.broadphaseXCells[]", "z" ),
                path,
                "requirements.broadphaseXCells[].z"
            );

            m_scene.m_requiredBroadphaseXCells.push_back( cell );
        }
    }
}


} // namespace Runtime
} // namespace SkullbonezCore
