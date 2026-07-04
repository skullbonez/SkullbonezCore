/*
File: SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp
Purpose:
  Applies parsed authored scenes to runtime camera, model, and validation gate storage.

Mental model:
  Authored scene setup is scene lifecycle behavior. This helper keeps the
  existing model insertion order, material targeting rules, ragdoll constraint
  flags, and required-gate resolution while moving the construction algorithms
  out of Run.

Glossary:
  Authored scene: Parsed `.scene.json` data that explicitly names terrain,
    cameras, objects, materials, constraints, and validation gates.
  Required gate: Scene-authored condition that must be observed before a
    validation run can complete.
  Ragdoll part: One model body in the generated simple ragdoll assembly.

Invariants:
  - Scene object insertion order is validation-facing and must stay stable.
  - Authored hull tokens resolve through the editor hull asset table for
    compatibility with saved scenes.
  - Gate state is initialized here but completed by frame/runtime observation.

Related:
  - SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#include "SceneAuthoredSetup.h"
#include "SceneRuntime.h"
#include "../CameraCollection.h"
#include "../Editor/EditorHullAssets.h"
#include "../../GameObjects/GameModel.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/RotationMatrix.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/ConvexHullShape.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/Ragdoll.h"
#include "../../Scene/TestScene.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
using SkullbonezCore::Assets::ResolveEditorHullAssetPath;
using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::RotationMatrix;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsPointJointCreateDesc;
using SkullbonezCore::Physics::PointJointConstraint;
using SkullbonezCore::Physics::Ragdoll;
using SkullbonezCore::Physics::RagdollBuildOptions;

constexpr float SCENE_EDITOR_TEXTURE_MODE_INVERTED = -2.0f;

Quaternion MakeSceneEulerQuaternion( float eulerXDeg, float eulerYDeg, float eulerZDeg )
{
    static constexpr float DEG2RAD = 3.14159265f / 180.0f;
    const float xHalf = eulerXDeg * DEG2RAD * 0.5f;
    const float yHalf = eulerYDeg * DEG2RAD * 0.5f;
    const float zHalf = eulerZDeg * DEG2RAD * 0.5f;

    const Quaternion xRotation( sinf( xHalf ), 0.0f, 0.0f, cosf( xHalf ) );
    const Quaternion yRotation( 0.0f, sinf( yHalf ), 0.0f, cosf( yHalf ) );
    const Quaternion zRotation( 0.0f, 0.0f, sinf( zHalf ), cosf( zHalf ) );

    Quaternion orientation;
    orientation *= xRotation * yRotation * zRotation;
    orientation.Normalise();
    return orientation;
}

bool SceneNameEndsWithPartSuffix( const char* name, const char* suffix )
{
    if ( !name || !suffix )
    {
        return false;
    }
    const size_t nameLength = strlen( name );
    const size_t suffixLength = strlen( suffix );
    if ( nameLength <= suffixLength || name[nameLength - suffixLength - 1] != '_' )
    {
        return false;
    }
    return strcmp( name + nameLength - suffixLength, suffix ) == 0;
}

bool IsSimpleRagdollPartName( const char* name )
{
    static const char* partSuffixes[] = {
        "torso",
        "head",
        "upper_arm_l",
        "lower_arm_l",
        "upper_arm_r",
        "lower_arm_r",
        "upper_leg_l",
        "lower_leg_l",
        "upper_leg_r",
        "lower_leg_r",
    };
    for ( const char* suffix : partSuffixes )
    {
        if ( SceneNameEndsWithPartSuffix( name, suffix ) )
        {
            return true;
        }
    }
    return false;
}

bool TryGetSimpleRagdollPartPrefixLength( const char* name, const char* suffix, size_t& outPrefixLength )
{
    outPrefixLength = 0;
    if ( !SceneNameEndsWithPartSuffix( name, suffix ) )
    {
        return false;
    }

    outPrefixLength = strlen( name ) - strlen( suffix );
    return true;
}

bool IsSimpleRagdollNeckJointName( const char* bodyA, const char* bodyB )
{
    size_t torsoPrefixLength = 0;
    size_t headPrefixLength = 0;
    return TryGetSimpleRagdollPartPrefixLength( bodyA, "torso", torsoPrefixLength ) &&
           TryGetSimpleRagdollPartPrefixLength( bodyB, "head", headPrefixLength ) &&
           torsoPrefixLength == headPrefixLength && strncmp( bodyA, bodyB, torsoPrefixLength ) == 0;
}

bool IsBroadMaterialTarget( const char* target )
{
    return strcmp( target, "all" ) == 0 || strcmp( target, "balls" ) == 0 || strcmp( target, "boxes" ) == 0 ||
           strcmp( target, "hulls" ) == 0 || strcmp( target, "convex_hulls" ) == 0;
}

bool SceneMaterialTargetMatches( const SceneObjectMaterialOverride& material, const GameModel& model )
{
    // Invariant: broad scene style targets must not recolor generated ragdoll
    // body parts, but a named prefix/exact target may opt one authored ragdoll
    // into a scene-local presentation material.
    if ( IsSimpleRagdollPartName( model.GetName() ) && IsBroadMaterialTarget( material.target ) )
    {
        return false;
    }
    if ( strcmp( material.target, "all" ) == 0 )
    {
        return true;
    }
    if ( strcmp( material.target, "balls" ) == 0 )
    {
        return model.IsSphere();
    }
    if ( strcmp( material.target, "boxes" ) == 0 )
    {
        return model.IsBox();
    }
    if ( strcmp( material.target, "hulls" ) == 0 || strcmp( material.target, "convex_hulls" ) == 0 )
    {
        return model.IsConvexHull();
    }
    if ( strncmp( material.target, "prefix:", 7 ) == 0 )
    {
        const char* prefix = material.target + 7;
        return prefix[0] != '\0' && strncmp( model.GetName(), prefix, strlen( prefix ) ) == 0;
    }
    return strcmp( material.target, model.GetName() ) == 0;
}

bool SceneNameStartsWith( const char* name, const char* prefix )
{
    return name && strncmp( name, prefix, strlen( prefix ) ) == 0;
}

bool IsEditorPlacedSphereName( const char* name )
{
    return SceneNameStartsWith( name, "static_ball_" ) || SceneNameStartsWith( name, "dynamic_ball_" ) ||
           SceneNameStartsWith( name, "sleeping_ball_" ) || SceneNameStartsWith( name, "static_sphere_" ) ||
           SceneNameStartsWith( name, "dynamic_sphere_" ) || SceneNameStartsWith( name, "sleeping_sphere_" );
}

void ApplyEditorPlacedSphereMaterial( GameModel& model )
{
    if ( IsEditorPlacedSphereName( model.GetName() ) )
    {
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, SCENE_EDITOR_TEXTURE_MODE_INVERTED );
    }
}

int FindModelByName( const std::vector<GameModel>& models, const char* name )
{
    if ( !name || name[0] == '\0' )
    {
        return -1;
    }
    for ( int modelIndex = 0; modelIndex < static_cast<int>( models.size() ); ++modelIndex )
    {
        if ( strcmp( models[static_cast<size_t>( modelIndex )].GetName(), name ) == 0 )
        {
            return modelIndex;
        }
    }
    return -1;
}
} // namespace


void SceneAuthoredSetup::SetUpCameras( SceneAuthoredCameraContext context, const TestScene& scene )
{
    bool hasFreeCamera = false;
    Vector3 firstPosition( 900.0f, 110.0f, 900.0f );
    Vector3 firstView( 313.0f, 31.0f, 282.0f );
    Vector3 firstUp( 0.0f, 1.0f, 0.0f );
    for ( int i = 0; i < scene.GetCameraCount(); ++i )
    {
        const SceneCamera& cam = scene.GetCamera( i );
        uint32_t hash = HashStr( cam.name );
        if ( i == 0 )
        {
            firstPosition = cam.m_position;
            firstView = cam.view;
            firstUp = cam.up;
        }
        hasFreeCamera = hasFreeCamera || hash == CAMERA_FREE;
        context.cameras.AddCamera( cam.m_position, cam.view, cam.up, hash );
    }
    if ( !hasFreeCamera )
    {
        context.cameras.AddCamera( firstPosition, firstView, firstUp, CAMERA_FREE );
    }

    context.cameras.SetCameraXZBounds( context.terrain.GetXZBounds() );
    context.cameras.SetTerrain( &context.terrain );
    context.cameras.SetLockedMode( false );
}


void SceneAuthoredSetup::SetUpGameModels( SceneAuthoredModelContext context, const TestScene& scene )
{
    // Invariant: Model insertion order follows scene schema sections. Runtime
    // validation, saved editable scenes, and point-joint name resolution all
    // depend on this deterministic order.
    context.sceneState.modelCount = scene.GetBallCount() + scene.GetBallStateCount() + scene.GetBoxCount() +
                                    scene.GetBoxStateCount() + scene.GetConvexHullCount() +
                                    scene.GetConvexHullStateCount() +
                                    scene.GetRagdollCount() * Ragdoll::SIMPLE_PART_COUNT;
    context.physics.ClearPointJointConstraints();
    for ( int i = 0; i < scene.GetBallCount(); ++i )
    {
        const SceneBall& ball = scene.GetBall( i );

        GameModel gameModel( &context.world,
                             Vector3( ball.posX, ball.posY, ball.posZ ),
                             Vector3( ball.moment, ball.moment, ball.moment ),
                             ball.m_mass );

        gameModel.SetCoefficientRestitution( ball.restitution );
        gameModel.SetTerrain( context.terrain );
        gameModel.SetName( ball.name );
        gameModel.SetContactMaterial( ball.contactMaterial );
        gameModel.AddBoundingSphere( ball.m_radius );
        gameModel.SetFixed( ball.isFixed );
        ApplyEditorPlacedSphereMaterial( gameModel );

        // Concept: Authored init orientation is stored as Euler degrees in XYZ
        // order, while snapshot state stores the final quaternion directly.
        if ( ball.hasInitOrient )
        {
            gameModel.SetInitialOrientation( ball.eulerX, ball.eulerY, ball.eulerZ );
        }

        const bool hasInitialImpulse =
            !ball.isFixed && ( ball.forceX != 0.0f || ball.forceY != 0.0f || ball.forceZ != 0.0f );
        const PhysicsBodyHandle body = context.models.AddGameModel( std::move( gameModel ) );
        if ( hasInitialImpulse )
        {
            context.physics.SetPendingBodyImpulse( body,
                                                   Vector3( ball.forceX, ball.forceY, ball.forceZ ),
                                                   Vector3( ball.forcePosX, ball.forcePosY, ball.forcePosZ ) );
        }
    }

    // ball_state entries: full dynamic state from a snapshot
    for ( int i = 0; i < scene.GetBallStateCount(); ++i )
    {
        const SceneBallState& bs = scene.GetBallState( i );

        GameModel gameModel( &context.world,
                             Vector3( bs.posX, bs.posY, bs.posZ ),
                             Vector3( bs.inertiaX, bs.inertiaY, bs.inertiaZ ),
                             bs.mass );

        gameModel.SetCoefficientRestitution( bs.restitution );
        gameModel.SetTerrain( context.terrain );
        gameModel.SetName( bs.name );
        gameModel.SetContactMaterial( bs.contactMaterial );
        gameModel.AddBoundingSphere( bs.radius );
        gameModel.SetLinearVelocity( Vector3( bs.velX, bs.velY, bs.velZ ) );
        gameModel.SetAngularVelocity( Vector3( bs.angVelX, bs.angVelY, bs.angVelZ ) );
        gameModel.SetOrientation( Quaternion( bs.orientX, bs.orientY, bs.orientZ, bs.orientW ) );
        gameModel.SetFixed( bs.isFixed );
        ApplyEditorPlacedSphereMaterial( gameModel );

        const PhysicsBodyHandle body = context.models.AddGameModel( std::move( gameModel ) );
        if ( bs.isSleeping && !bs.isFixed )
        {
            context.physics.SeedBodyAsleep( body );
        }
    }

    // box entries: rigid box entities
    for ( int i = 0; i < scene.GetBoxCount(); ++i )
    {
        const SceneBox& box = scene.GetBox( i );

        // Box inertia: I = m/3 * (hy^2 + hz^2) etc. for half-extents
        float hx2 = box.halfX * box.halfX;
        float hy2 = box.halfY * box.halfY;
        float hz2 = box.halfZ * box.halfZ;
        float m3 = box.mass / 3.0f;
        Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

        GameModel gameModel( &context.world, Vector3( box.posX, box.posY, box.posZ ), inertia, box.mass );

        gameModel.SetCoefficientRestitution( box.restitution );
        gameModel.SetTerrain( context.terrain );
        gameModel.SetName( box.name );
        gameModel.SetContactMaterial( box.contactMaterial );
        gameModel.AddBoundingBox( Vector3( box.halfX, box.halfY, box.halfZ ) );

        if ( box.hasInitOrient )
        {
            gameModel.SetInitialOrientation( box.eulerX, box.eulerY, box.eulerZ );
        }

        if ( box.hasInitVelocity )
        {
            gameModel.SetLinearVelocity( Vector3( box.velX, box.velY, box.velZ ) );
        }

        gameModel.SetFixed( box.isFixed );

        context.models.AddGameModel( std::move( gameModel ) );
    }

    // box_state entries: full dynamic state from an editable scene snapshot
    for ( int i = 0; i < scene.GetBoxStateCount(); ++i )
    {
        const SceneBoxState& box = scene.GetBoxState( i );

        GameModel gameModel( &context.world,
                             Vector3( box.posX, box.posY, box.posZ ),
                             Vector3( box.inertiaX, box.inertiaY, box.inertiaZ ),
                             box.mass );

        gameModel.SetCoefficientRestitution( box.restitution );
        gameModel.SetTerrain( context.terrain );
        gameModel.SetName( box.name );
        gameModel.SetContactMaterial( box.contactMaterial );
        gameModel.AddBoundingBox( Vector3( box.halfX, box.halfY, box.halfZ ) );
        gameModel.SetLinearVelocity( Vector3( box.velX, box.velY, box.velZ ) );
        gameModel.SetAngularVelocity( Vector3( box.angVelX, box.angVelY, box.angVelZ ) );
        gameModel.SetOrientation( Quaternion( box.orientX, box.orientY, box.orientZ, box.orientW ) );
        gameModel.SetFixed( box.isFixed );

        const PhysicsBodyHandle body = context.models.AddGameModel( std::move( gameModel ) );
        if ( box.isSleeping && !box.isFixed )
        {
            context.physics.SeedBodyAsleep( body );
        }
    }

    // convex_hull entries: authored immutable hull assets
    for ( int i = 0; i < scene.GetConvexHullCount(); ++i )
    {
        const SceneConvexHull& hullScene = scene.GetConvexHull( i );
        const ConvexHullShape hull = ConvexHullShape::LoadFromFile( ResolveEditorHullAssetPath( hullScene.hullPath ) );
        const Vector3 inertia = hull.ComputeBoxApproxInertia( hullScene.mass );
        const Vector3 authoredPosition( hullScene.posX, hullScene.posY, hullScene.posZ );

        GameModel gameModel( &context.world, authoredPosition, inertia, hullScene.mass );

        gameModel.SetCoefficientRestitution( hullScene.restitution );
        gameModel.SetTerrain( context.terrain );
        gameModel.SetName( hullScene.name );
        gameModel.SetContactMaterial( hullScene.contactMaterial );
        gameModel.SetContactReleaseOnImpact( hullScene.contactReleaseOnImpact,
                                             hullScene.contactReleaseImpulseThreshold );
        gameModel.AddConvexHull( hull );

        if ( hullScene.hasInitOrient )
        {
            gameModel.SetInitialOrientation( hullScene.eulerX, hullScene.eulerY, hullScene.eulerZ );
        }

        Quaternion hullQuaternion = gameModel.GetOrientation();
        const RotationMatrix hullOrientation = hullQuaternion.GetOrientationMatrix();
        gameModel.SetPosition( authoredPosition + hullOrientation * hull.GetAuthoredCenterOfMass() );

        if ( hullScene.hasInitVelocity )
        {
            gameModel.SetLinearVelocity( Vector3( hullScene.velX, hullScene.velY, hullScene.velZ ) );
        }
        if ( hullScene.hasInitAngularVelocity )
        {
            gameModel.SetAngularVelocity( Vector3( hullScene.angVelX, hullScene.angVelY, hullScene.angVelZ ) );
        }

        gameModel.SetFixed( hullScene.isFixed );

        const PhysicsBodyHandle body = context.models.AddGameModel( std::move( gameModel ) );
        if ( hullScene.isSleeping && !hullScene.isFixed )
        {
            context.physics.SeedBodyAsleep( body );
        }
    }

    // Invariant: convex_hull_state entries come from editable scene snapshots.
    // The writer stores GameModel::GetPosition(), the simulated body/COM
    // position, so do not add the authored hull COM here.
    for ( int i = 0; i < scene.GetConvexHullStateCount(); ++i )
    {
        const SceneConvexHullState& hullScene = scene.GetConvexHullState( i );
        const ConvexHullShape hull = ConvexHullShape::LoadFromFile( ResolveEditorHullAssetPath( hullScene.hullPath ) );

        GameModel gameModel( &context.world,
                             Vector3( hullScene.posX, hullScene.posY, hullScene.posZ ),
                             Vector3( hullScene.inertiaX, hullScene.inertiaY, hullScene.inertiaZ ),
                             hullScene.mass );

        gameModel.SetCoefficientRestitution( hullScene.restitution );
        gameModel.SetTerrain( context.terrain );
        gameModel.SetName( hullScene.name );
        gameModel.SetContactMaterial( hullScene.contactMaterial );
        gameModel.SetContactReleaseOnImpact( hullScene.contactReleaseOnImpact,
                                             hullScene.contactReleaseImpulseThreshold );
        gameModel.AddConvexHull( hull );
        gameModel.SetLinearVelocity( Vector3( hullScene.velX, hullScene.velY, hullScene.velZ ) );
        gameModel.SetAngularVelocity( Vector3( hullScene.angVelX, hullScene.angVelY, hullScene.angVelZ ) );
        gameModel.SetOrientation(
            Quaternion( hullScene.orientX, hullScene.orientY, hullScene.orientZ, hullScene.orientW ) );
        gameModel.SetFixed( hullScene.isFixed );
        const PhysicsBodyHandle body = context.models.AddGameModel( std::move( gameModel ) );
        if ( hullScene.isSleeping && !hullScene.isFixed )
        {
            context.physics.SeedBodyAsleep( body );
        }
    }

    for ( int i = 0; i < scene.GetRagdollCount(); ++i )
    {
        const SceneRagdoll& ragdollScene = scene.GetRagdoll( i );
        RagdollBuildOptions options;
        options.namePrefix = ragdollScene.name;
        options.terrainPoint = Vector3( ragdollScene.posX, ragdollScene.posY, ragdollScene.posZ );
        options.scale = ragdollScene.scale;
        options.fixed = ragdollScene.isFixed;
        options.startsAsleep = ragdollScene.startsAsleep;
        if ( ragdollScene.hasInitOrient )
        {
            options.orientation =
                MakeSceneEulerQuaternion( ragdollScene.eulerX, ragdollScene.eulerY, ragdollScene.eulerZ );
        }
        Ragdoll::AddSimpleHumanoid( context.models, context.physics, context.world, context.terrain, options );
    }

    const std::vector<GameModel>& models = context.models.Models();
    const PhysicsBodyStore& bodyStore = context.models.GetPhysicsBodyStore();
    for ( int i = 0; i < scene.GetPointJointConstraintCount(); ++i )
    {
        const ScenePointJointConstraint& sceneJoint = scene.GetPointJointConstraint( i );
        PhysicsPointJointCreateDesc joint;
        const int bodyAIndex = FindModelByName( models, sceneJoint.bodyA );
        const int bodyBIndex = FindModelByName( models, sceneJoint.bodyB );
        if ( bodyAIndex < 0 || bodyBIndex < 0 )
        {
            fprintf( stderr,
                     "[scene] ragdoll_joint could not resolve '%s' <-> '%s'\n",
                     sceneJoint.bodyA,
                     sceneJoint.bodyB );
            continue;
        }
        joint.bodyA = bodyStore.HandleForModelIndex( bodyAIndex );
        joint.bodyB = bodyStore.HandleForModelIndex( bodyBIndex );
        joint.localAnchorA = sceneJoint.localAnchorA;
        joint.localAnchorB = sceneJoint.localAnchorB;
        joint.slack = sceneJoint.slack;
        joint.stiffness = sceneJoint.stiffness;
        joint.damping = sceneJoint.damping;
        joint.groupId = sceneJoint.groupId;
        joint.flags = sceneJoint.flags;
        if ( IsSimpleRagdollNeckJointName( sceneJoint.bodyA, sceneJoint.bodyB ) )
        {
            joint.flags |= PointJointConstraint::FLAG_LIMIT_NECK_SWING;
        }
        context.physics.CreatePointJoint( joint );
    }

    for ( int materialIndex = 0; materialIndex < scene.GetObjectMaterialOverrideCount(); ++materialIndex )
    {
        // Why: Material overrides are applied after all bodies exist so prefix
        // and exact-name targets can hit authored objects, generated ragdolls,
        // and snapshot bodies uniformly.
        const SceneObjectMaterialOverride& material = scene.GetObjectMaterialOverride( materialIndex );
        for ( int modelIndex = 0; modelIndex < context.models.GetModelCount(); ++modelIndex )
        {
            GameModel& model = context.models.GetModelAtIndex( modelIndex );
            if ( SceneMaterialTargetMatches( material, model ) )
            {
                model.SetRenderMaterial( material.material );
            }
        }
    }

    SetUpRequiredContacts( context, scene );
    SetUpRequiredBroadphaseXCells( context, scene );
}


void SceneAuthoredSetup::SetUpRequiredContacts( SceneAuthoredModelContext context, const TestScene& scene )
{
    // Lifetime: Required contacts store body indices resolved for this load.
    // Scene reloads must rebuild them because model storage is recreated.
    context.requiredContacts.clear();
    context.requiredContacts.reserve( static_cast<size_t>( scene.GetRequiredContactCount() ) );
    const std::vector<GameModel>& models = context.models.Models();

    for ( int i = 0; i < scene.GetRequiredContactCount(); ++i )
    {
        const SceneRequiredContact& contact = scene.GetRequiredContact( i );
        RunRequiredContactState state;
        strcpy_s( state.nameA, sizeof( state.nameA ), contact.nameA );
        strcpy_s( state.nameB, sizeof( state.nameB ), contact.nameB );
        state.bodyA = FindModelByName( models, state.nameA );
        state.bodyB = FindModelByName( models, state.nameB );
        if ( state.bodyA < 0 || state.bodyB < 0 )
        {
            fprintf( stderr, "[scene] required_contact could not resolve '%s' <-> '%s'\n", state.nameA, state.nameB );
        }
        context.requiredContacts.push_back( state );
    }
}


void SceneAuthoredSetup::SetUpRequiredBroadphaseXCells( SceneAuthoredModelContext context, const TestScene& scene )
{
    context.requiredBroadphaseXCells.clear();
    context.requiredBroadphaseXCells.reserve( static_cast<size_t>( scene.GetRequiredBroadphaseXCellCount() ) );
    for ( int i = 0; i < scene.GetRequiredBroadphaseXCellCount(); ++i )
    {
        const SceneRequiredBroadphaseXCells& sceneCells = scene.GetRequiredBroadphaseXCell( i );
        RunRequiredBroadphaseXCellsState state;
        state.minCellX = sceneCells.minCellX;
        state.maxCellX = sceneCells.maxCellX;
        state.cellY = sceneCells.cellY;
        state.cellZ = sceneCells.cellZ;
        context.requiredBroadphaseXCells.push_back( state );
    }
}

} // namespace Basics
} // namespace SkullbonezCore
