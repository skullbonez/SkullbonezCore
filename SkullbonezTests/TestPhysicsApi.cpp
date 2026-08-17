/*
File: TestPhysicsApi.cpp
Purpose:
  Pins the public Physics descriptor and query coordinate-frame contract.

Summary:
  These tests use rotated bodies and deliberately non-origin offsets so a
  plausible body/world-frame mix cannot pass through zero or identity values.
  Query, point-joint, and angular-drag behavior provide independent observable
  oracles for the frame matrix documented by PhysicsApi.h.

Invariants:
  - Shape centers and point-joint anchors are body-local values.
  - Body pose and velocity, ray/AABB inputs, and hit point/normal are world-space.
  - A non-unit ray direction changes no reported world-space distance.
  - Ragdoll point-joint solving rotates local anchors before applying inertia.
  - Angular drag clamps in body-principal axes and returns a world-space torque.

Related:
  - SkullbonezSource/Physics/PhysicsApi.h
  - SkullbonezSource/Physics/PhysicsBodyStore.cpp
  - SkullbonezSource/Physics/PhysicsEngine.cpp
  - SkullbonezSource/Physics/Ragdoll.cpp
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/BuoyancySystem.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/Ragdoll.h"
#include "TestColliderStoreFixtures.h"

#include <algorithm>
#include <array>

using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::RotationMatrix;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Physics::BuoyancyBodyFacts;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::MakePhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsAuthoredBodyRegistration;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyHotState;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsBroadphaseCellQueryDesc;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsPointJointCreateDesc;
using SkullbonezCore::Physics::PhysicsRayCastDesc;
using SkullbonezCore::Physics::PhysicsRayCastHit;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Physics::PointJointConstraint;
using SkullbonezCore::Physics::Ragdoll;

namespace
{
constexpr float HALF_PI_RADIANS = 1.57079632679489661923f;

void CheckVectorApprox( const Vector3& actual, const Vector3& expected )
{
    CHECK( actual.x == doctest::Approx( expected.x ).epsilon( 0.00001 ) );
    CHECK( actual.y == doctest::Approx( expected.y ).epsilon( 0.00001 ) );
    CHECK( actual.z == doctest::Approx( expected.z ).epsilon( 0.00001 ) );
}

void CheckVectorExact( const Vector3& actual, const Vector3& expected )
{
    CHECK( actual.x == expected.x );
    CHECK( actual.y == expected.y );
    CHECK( actual.z == expected.z );
}

Vector3 RunAngularDragCase( const CollisionShape& shape, const Vector3& bodyPrincipalInertia,
                            const Quaternion& orientation, const Vector3& worldAngularVelocity,
                            float dragCoefficient, float gasDensity, float deltaSeconds, bool usesWorldInertia )
{
    PhysicsBodyStore bodies;
    ColliderStore colliders;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodies.ReserveCapacity( 1u );
        colliders.ReserveCapacity( 1u );
        colliders.ReserveShapeCapacity( 1u, 1u, 0u );
    }

    PhysicsBodyCreateRecord body;
    body.cold.sceneObjectId = MakePhysicsSceneObjectId( 71u );
    body.cold.mass = 1.0f;
    body.cold.rotationalInertia = bodyPrincipalInertia;
    body.cold.angularVelocityLimit = 100.0f;
    body.cold.usesWorldInertia = usesWorldInertia;
    body.hot.orientation = orientation;
    body.hot.angularVelocity = worldAngularVelocity;
    body.hot.inverseMass = 1.0f;
    body.hot.inverseRotationalInertia = Vector3( 1.0f / bodyPrincipalInertia.x, 1.0f / bodyPrincipalInertia.y,
                                                 1.0f / bodyPrincipalInertia.z );
    body.hot.boundingRadius = GetShapeBoundingRadius( shape );
    const auto bodyHandle = bodies.CreateBodyRecord( body );
    REQUIRE( bodyHandle.IsValid() );

    ColliderRecord collider;
    collider.body = bodyHandle;
    collider.sceneObjectId = body.cold.sceneObjectId;
    collider.shapeKind = std::holds_alternative<BoundingBox>( shape ) ? ColliderShapeKind::Box : ColliderShapeKind::Sphere;
    collider.boundingRadius = body.hot.boundingRadius;
    collider.dragCoefficient = dragCoefficient;
    REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, shape ).IsValid() );

    BuoyancyBodyFacts buoyancyFacts;
    buoyancyFacts.dragCoefficient = dragCoefficient;
    PhysicsWorldForces forces;
    forces.fluidSurfaceHeight = -1000.0f;
    forces.gasDensity = gasDensity;
    REQUIRE( bodies.ApplyForces( forces, colliders, {}, buoyancyFacts, 0, deltaSeconds ) );
    return SkullbonezCore::Physics::PhysicsBodyAngularVelocity( bodies.HotFields(), 0u );
}

float ClampDragAxisReference( float bodyTorque, float bodyAngularVelocity, float bodyInertia, float deltaSeconds )
{
    const float maxDampingTorque = fabsf( bodyAngularVelocity ) * bodyInertia / deltaSeconds;
    return (std::clamp)( bodyTorque, -maxDampingTorque, maxDampingTorque );
}

PhysicsBodyHotState SolveAnchorCase( const Vector3& anchorForBodyA )
{
    PhysicsBodyStore bodies;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodies.ReserveCapacity( 2u );
    }

    PhysicsBodyCreateRecord fixedBody;
    fixedBody.cold.mass = 1.0f;
    fixedBody.cold.rotationalInertia = Vector3( 3.0f, 5.0f, 7.0f );
    fixedBody.cold.usesWorldInertia = true;
    fixedBody.hot.position = Vector3( 10.0f, 20.0f, 30.0f );
    fixedBody.hot.orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), HALF_PI_RADIANS );
    fixedBody.hot.inverseRotationalInertia = Vector3( 1.0f / 3.0f, 0.2f, 1.0f / 7.0f );
    fixedBody.hot.fixed = true;
    const auto fixedHandle = bodies.CreateBodyRecord( fixedBody );

    PhysicsBodyCreateRecord dynamicBody;
    dynamicBody.cold.mass = 1.0f;
    dynamicBody.cold.rotationalInertia = Vector3( 2.0f, 4.0f, 8.0f );
    dynamicBody.cold.usesWorldInertia = true;
    dynamicBody.hot.position = Vector3( 10.0f, 24.0f, 29.0f );
    dynamicBody.hot.orientation.RotateAboutAxis( Vector3( 1.0f, 0.0f, 0.0f ), HALF_PI_RADIANS );
    dynamicBody.hot.inverseRotationalInertia = Vector3( 0.5f, 0.25f, 0.125f );
    dynamicBody.hot.inverseMass = 1.0f;
    const auto dynamicHandle = bodies.CreateBodyRecord( dynamicBody );

    PointJointConstraint joint;
    joint.SetBodies( fixedHandle, dynamicHandle );
    joint.localAnchorA = anchorForBodyA;
    joint.localAnchorB = Vector3( 0.0f, 1.0f, 0.0f );
    joint.slack = 0.001f;
    std::array<PointJointConstraint, 1> constraints = { joint };
    const std::array<uint8_t, 2> sleepState = { 0u, 0u };
    CHECK( Ragdoll::SolvePointJoints( bodies, constraints, sleepState, 1.0f / 120.0f ) );

    const int dynamicRow = bodies.ModelIndexForHandle( dynamicHandle );
    REQUIRE( dynamicRow >= 0 );
    return SkullbonezCore::Physics::LoadPhysicsBodyHotState( bodies.HotFields(), static_cast<std::size_t>( dynamicRow ) );
}
} // namespace

TEST_CASE( "Physics API frames: body-local shape offsets project into world queries" )
{
    PhysicsEngine engine;
    const Vector3 bodyPosition( 10.0f, 20.0f, 30.0f );
    const Vector3 localShapeOffset( 4.0f, 0.0f, 0.0f );
    const Vector3 linearVelocity( 1.0f, 2.0f, 3.0f );
    const Vector3 angularVelocity( 0.25f, 0.5f, 0.75f );
    const Vector3 bodyPrincipalInertia( 2.0f, 3.0f, 4.0f );
    const CollisionShape shape = BoundingSphere( 0.5f, localShapeOffset, 0.4f );
    Quaternion orientation;
    orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), HALF_PI_RADIANS );

    const auto bodyDesc = MakePhysicsBodyCreateDesc( MakePhysicsSceneObjectId( 41u ), shape, bodyPosition, orientation,
                                                     linearVelocity, angularVelocity, bodyPrincipalInertia, 2.0f, 0.1f,
                                                     PhysicsBodyMotionKind::Dynamic, "physics-api-frame-body" );

    auto colliderDesc = MakeColliderCreateDesc( shape, bodyDesc.restitution, 0u );
    PhysicsAuthoredBodyRegistration registration;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        engine.ReserveAuthoredBodyCapacity( 1u, 1u );
        registration = engine.RegisterAuthoredBody( bodyDesc, colliderDesc );
    }

    REQUIRE( registration.IsValid() );
    const PhysicsBodyStore& bodies = PhysicsEngine::ReadBodies( engine );
    const int bodyRow = bodies.ModelIndexForHandle( registration.body );
    REQUIRE( bodyRow >= 0 );
    const PhysicsBodyHotState hot = SkullbonezCore::Physics::LoadPhysicsBodyHotState( bodies.HotFields(),
                                                                                      static_cast<std::size_t>( bodyRow ) );

    CheckVectorApprox( hot.position, bodyPosition );
    CheckVectorApprox( hot.linearVelocity, linearVelocity );
    CheckVectorApprox( hot.angularVelocity, angularVelocity );
    CheckVectorApprox( bodies.RecordForHandle( registration.body )->rotationalInertia, bodyPrincipalInertia );

    const Vector3 expectedWorldShapeCenter( 10.0f, 24.0f, 30.0f );
    PhysicsRayCastDesc ray;
    ray.origin = Vector3( 10.0f, 24.0f, 20.0f );
    ray.direction = Vector3( 0.0f, 0.0f, 2.0f );
    ray.maxDistance = 20.0f;
    const PhysicsRayCastHit hit = engine.RayCast( ray );
    REQUIRE( hit.hit );
    CHECK( hit.body == registration.body );
    CHECK( hit.collider == registration.collider );
    CHECK( hit.distance == doctest::Approx( 5.5f ).epsilon( 0.00001 ) );
    CheckVectorApprox( hit.point, Vector3( 10.0f, 24.0f, 25.5f ) );
    CheckVectorApprox( hit.normal, Vector3( 0.0f, 0.0f, -1.0f ) );

    PhysicsBroadphaseCellQueryDesc worldQuery;
    const Vector3 queryHalfExtents( 0.05f, 0.05f, 0.05f );
    worldQuery.min = expectedWorldShapeCenter - queryHalfExtents;
    worldQuery.max = expectedWorldShapeCenter + queryHalfExtents;
    const auto worldResults = engine.QueryBroadphaseCells( worldQuery );
    REQUIRE( worldResults.bodyCount == 1u );
    REQUIRE( worldResults.bodies != nullptr );
    CHECK( worldResults.bodies[0] == registration.body );

    PhysicsBroadphaseCellQueryDesc unrotatedOffsetQuery;
    const Vector3 plausibleWrongWorldCenter = bodyPosition + localShapeOffset;
    unrotatedOffsetQuery.min = plausibleWrongWorldCenter - queryHalfExtents;
    unrotatedOffsetQuery.max = plausibleWrongWorldCenter + queryHalfExtents;
    CHECK( engine.QueryBroadphaseCells( unrotatedOffsetQuery ).bodyCount == 0u );
}

TEST_CASE( "Physics API frames: point-joint anchors are body-local rather than world positions" )
{
    const Vector3 dynamicStart( 10.0f, 24.0f, 29.0f );
    const PhysicsBodyHotState localAnchorResult = SolveAnchorCase( Vector3( 4.0f, 0.0f, 0.0f ) );
    CheckVectorApprox( localAnchorResult.position, dynamicStart );
    CheckVectorApprox( localAnchorResult.linearVelocity, ZERO_VECTOR );
    CheckVectorApprox( localAnchorResult.angularVelocity, ZERO_VECTOR );

    // A world point is numerically plausible input but means a very different
    // lever after the body-to-world rotation. Its response proves the API does
    // not silently reinterpret anchors as absolute positions.
    const PhysicsBodyHotState wrongFrameResult = SolveAnchorCase( Vector3( 10.0f, 24.0f, 30.0f ) );
    CHECK( SkullbonezCore::Math::Vector::VectorMag( wrongFrameResult.position - dynamicStart ) > 0.01f );
    CHECK( SkullbonezCore::Math::Vector::VectorMag( wrongFrameResult.angularVelocity ) > 0.01f );
}

TEST_CASE( "Physics API frames: anisotropic angular drag clamps in body-principal axes" )
{
    constexpr float deltaSeconds = 0.5f;
    constexpr float dragCoefficient = 10.0f;
    constexpr float gasDensity = 1.0f;
    const Vector3 bodyPrincipalInertia( 1.0f, 10.0f, 100.0f );
    const Vector3 initialWorldAngularVelocity( 1.0f, 2.0f, 0.5f );
    const CollisionShape shape = BoundingBox( Vector3( 1.0f, 0.5f, 0.25f ), ZERO_VECTOR );
    Quaternion orientation;
    orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), 0.7853981633974483f );
    const RotationMatrix rotation = orientation.GetOrientationMatrix();

    const float radius = GetShapeBoundingRadius( shape );
    const float dragScale = dragCoefficient * gasDensity * radius * radius * radius;
    const Vector3 initialBodyAngularVelocity = rotation.TransposeMultiply( initialWorldAngularVelocity );
    Vector3 bodyTorque = initialBodyAngularVelocity * ( -dragScale );
    bodyTorque.x = ClampDragAxisReference( bodyTorque.x, initialBodyAngularVelocity.x, bodyPrincipalInertia.x,
                                           deltaSeconds );
    bodyTorque.y = ClampDragAxisReference( bodyTorque.y, initialBodyAngularVelocity.y, bodyPrincipalInertia.y,
                                           deltaSeconds );
    bodyTorque.z = ClampDragAxisReference( bodyTorque.z, initialBodyAngularVelocity.z, bodyPrincipalInertia.z,
                                           deltaSeconds );
    const Vector3 expectedBodyAngularVelocity =
        initialBodyAngularVelocity + Vector3( bodyTorque.x * deltaSeconds / bodyPrincipalInertia.x,
                                              bodyTorque.y * deltaSeconds / bodyPrincipalInertia.y,
                                              bodyTorque.z * deltaSeconds / bodyPrincipalInertia.z );
    const Vector3 expectedWorldAngularVelocity = rotation * expectedBodyAngularVelocity;

    const Vector3 actual = RunAngularDragCase( shape, bodyPrincipalInertia, orientation, initialWorldAngularVelocity,
                                               dragCoefficient, gasDensity, deltaSeconds, true );
    CheckVectorApprox( actual, expectedWorldAngularVelocity );
    CHECK( fabsf( rotation.TransposeMultiply( actual ).x ) < 0.00001f );
}

TEST_CASE( "Physics API frames: isotropic angular drag retains exact world-path arithmetic" )
{
    constexpr float deltaSeconds = 0.25f;
    constexpr float dragCoefficient = 20.0f;
    constexpr float gasDensity = 2.0f;
    const Vector3 isotropicInertia( 4.0f, 4.0f, 4.0f );
    const Vector3 initialWorldAngularVelocity( 1.25f, -0.5f, 0.75f );
    const CollisionShape shape = BoundingSphere( 1.0f, ZERO_VECTOR, 0.0f );
    Quaternion orientation;
    orientation.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), 0.6f );
    const RotationMatrix rotation = orientation.GetOrientationMatrix();

    const float radius = GetShapeBoundingRadius( shape );
    const float dragScale = dragCoefficient * gasDensity * radius * radius * radius;
    Vector3 worldTorque = initialWorldAngularVelocity * ( -dragScale );
    worldTorque.x = ClampDragAxisReference( worldTorque.x, initialWorldAngularVelocity.x, isotropicInertia.x,
                                            deltaSeconds );
    worldTorque.y = ClampDragAxisReference( worldTorque.y, initialWorldAngularVelocity.y, isotropicInertia.y,
                                            deltaSeconds );
    worldTorque.z = ClampDragAxisReference( worldTorque.z, initialWorldAngularVelocity.z, isotropicInertia.z,
                                            deltaSeconds );
    const Vector3 bodyImpulse = rotation.TransposeMultiply( worldTorque * deltaSeconds );
    const Vector3 bodyDelta( bodyImpulse.x / isotropicInertia.x, bodyImpulse.y / isotropicInertia.y,
                             bodyImpulse.z / isotropicInertia.z );
    const Vector3 expected = initialWorldAngularVelocity + rotation * bodyDelta;

    const Vector3 actual = RunAngularDragCase( shape, isotropicInertia, orientation, initialWorldAngularVelocity,
                                               dragCoefficient, gasDensity, deltaSeconds, false );
    CheckVectorExact( actual, expected );
}
