/*
File: TestPhysicsApi.cpp
Purpose:
  Pins the public Physics descriptor and query coordinate-frame contract.

Summary:
  These tests use rotated bodies and deliberately non-origin offsets so a
  plausible body/world-frame mix cannot pass through zero or identity values.
  Query output and point-joint behavior provide independent observable oracles
  for the frame matrix documented by PhysicsApi.h.

Invariants:
  - Shape centers and point-joint anchors are body-local values.
  - Body pose and velocity, ray/AABB inputs, and hit point/normal are world-space.
  - A non-unit ray direction changes no reported world-space distance.
  - Ragdoll point-joint solving rotates local anchors before applying inertia.

Related:
  - SkullbonezSource/Physics/PhysicsApi.h
  - SkullbonezSource/Physics/PhysicsEngine.cpp
  - SkullbonezSource/Physics/Ragdoll.cpp
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/Ragdoll.h"

#include <array>

using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
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
    const std::array<PointJointConstraint, 1> constraints = { joint };
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
    CheckVectorApprox( localAnchorResult.linearVelocity, Vector3() );
    CheckVectorApprox( localAnchorResult.angularVelocity, Vector3() );

    // A world point is numerically plausible input but means a very different
    // lever after the body-to-world rotation. Its response proves the API does
    // not silently reinterpret anchors as absolute positions.
    const PhysicsBodyHotState wrongFrameResult = SolveAnchorCase( Vector3( 10.0f, 24.0f, 30.0f ) );
    CHECK( SkullbonezCore::Math::Vector::VectorMag( wrongFrameResult.position - dynamicStart ) > 0.01f );
    CHECK( SkullbonezCore::Math::Vector::VectorMag( wrongFrameResult.angularVelocity ) > 0.01f );
}
