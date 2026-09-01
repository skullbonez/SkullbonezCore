/*
File: SkullbonezTests/TestPhysicsApi.cpp
Purpose:
  Pins the public Physics descriptor and query coordinate-frame contract.

Summary:
  These tests use rotated bodies and deliberately non-origin offsets so a
  plausible body/world-frame mix cannot pass through zero or identity values.
  Query, point-joint, and angular-drag behavior provide independent observable
  oracles for the frame matrix documented by PhysicsApi.h. The launcher matrix
  also drives authored descriptors through PhysicsEngine so presentation names
  cannot influence motion-path classification, swept candidate selection, or
  time-of-impact response.

Invariants:
  - Shape centers and point-joint anchors are body-local values.
  - Body pose and velocity, ray/AABB inputs, and hit point/normal are world-space.
  - A non-unit ray direction changes no reported world-space distance.
  - Ragdoll point-joint solving rotates local anchors before applying inertia.
  - Angular drag clamps in body-principal axes and returns a world-space torque.
  - Every launcher slider value from 20 through 360 metres per second promotes
    at 120 Hz, and an identically shaped generic body receives the same bits.
  - The minimum-speed launcher and generic body take the same swept path through
    an identical obstacle, including candidate, TOI, and resolved velocity.

Related:
  - SkullbonezSource/Physics/PhysicsApi.h
  - SkullbonezSource/Physics/PhysicsBodyStore.cpp
  - SkullbonezSource/Physics/PhysicsEngine.cpp
  - SkullbonezSource/Physics/PhysicsMotionEligibility.h
  - SkullbonezSource/Physics/Ragdoll.cpp
  - SkullbonezSource/Runtime/Tools/RuntimeTools.cpp
*/

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/LockOrderValidator.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/BuoyancySystem.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsDiagnosticsSink.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsMotionEligibility.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/Ragdoll.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "TestColliderStoreFixtures.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

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
constexpr float FP2_LAUNCHER_SPEED_MIN = 20.0f;
constexpr float FP2_LAUNCHER_SPEED_MAX = 360.0f;
constexpr float FP2_LAUNCHER_SPEED_STEP = 5.0f;
constexpr float FP2_LAUNCHER_PROJECTILE_RADIUS = 0.85f;
constexpr float FIRST_FP2_LAUNCHER_SPEED = 105.0f;
constexpr int FIRST_FP2_LAUNCHER_SPEED_STEP = static_cast<int>( ( FIRST_FP2_LAUNCHER_SPEED - FP2_LAUNCHER_SPEED_MIN ) /
                                                                FP2_LAUNCHER_SPEED_STEP );
constexpr int LAST_LAUNCHER_SPEED_STEP = static_cast<int>( ( FP2_LAUNCHER_SPEED_MAX - FP2_LAUNCHER_SPEED_MIN ) /
                                                           FP2_LAUNCHER_SPEED_STEP );
constexpr int FP2_LAUNCHER_SPEED_COUNT = LAST_LAUNCHER_SPEED_STEP - FIRST_FP2_LAUNCHER_SPEED_STEP + 1;
constexpr int FP2_LAUNCHER_BODY_COUNT = FP2_LAUNCHER_SPEED_COUNT * 2;

static_assert( FP2_LAUNCHER_SPEED_MIN + static_cast<float>( FIRST_FP2_LAUNCHER_SPEED_STEP ) * FP2_LAUNCHER_SPEED_STEP ==
                   FIRST_FP2_LAUNCHER_SPEED,
               "The FP2 matrix must begin on a supported launcher slider step." );
static_assert( FP2_LAUNCHER_SPEED_MIN + static_cast<float>( LAST_LAUNCHER_SPEED_STEP ) * FP2_LAUNCHER_SPEED_STEP ==
                   FP2_LAUNCHER_SPEED_MAX,
               "The FP2 matrix must include the supported launcher maximum." );
static_assert( FP2_LAUNCHER_SPEED_COUNT == 52 );

// Invariant: these body constants mirror RuntimeTools' launcher projectile.
// The paired rows differ in presentation name and position only, leaving shape
// and velocity as identical Physics classification inputs.
void AddLauncherClassificationBody( PhysicsEngine& engine, uint32_t sceneObjectId, float speed, float z,
                                    const char* diagnosticName )
{
    constexpr float projectileRadius = FP2_LAUNCHER_PROJECTILE_RADIUS;
    constexpr float projectileMass = 6.0f;
    constexpr float projectileRestitution = 0.42f;
    constexpr float projectileMoment = 0.4f * projectileMass * projectileRadius * projectileRadius;
    const CollisionShape shape = BoundingSphere( projectileRadius, ZERO_VECTOR, 0.0f );
    auto body = MakePhysicsBodyCreateDesc( MakePhysicsSceneObjectId( sceneObjectId ), shape, Vector3( 0.0f, 0.0f, z ),
                                           SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                           Vector3( speed, 0.0f, 0.0f ), ZERO_VECTOR,
                                           Vector3( projectileMoment, projectileMoment, projectileMoment ), projectileMass,
                                           projectileRestitution, PhysicsBodyMotionKind::Dynamic, diagnosticName );
    body.angularVelocityLimit = 1000.0f;
    auto collider = MakeColliderCreateDesc( shape, projectileRestitution, 0u, "unit" );
    collider.sceneObjectId = body.sceneObjectId;
    REQUIRE( engine.RegisterAuthoredBody( body, collider ).IsValid() );
}

void AddLauncherCollisionWall( PhysicsEngine& engine, uint32_t sceneObjectId, float x, float z, const char* diagnosticName )
{
    constexpr float projectileRestitution = 0.42f;
    const CollisionShape shape = BoundingBox( Vector3( 0.05f, 2.0f, 1.0f ), ZERO_VECTOR );
    auto body = MakePhysicsBodyCreateDesc( MakePhysicsSceneObjectId( sceneObjectId ), shape, Vector3( x, 0.0f, z ),
                                           SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION, ZERO_VECTOR, ZERO_VECTOR,
                                           Vector3( 1.0f, 1.0f, 1.0f ), 1.0f, projectileRestitution,
                                           PhysicsBodyMotionKind::Fixed, diagnosticName );
    auto collider = MakeColliderCreateDesc( shape, projectileRestitution, 0u, "unit" );
    collider.sceneObjectId = body.sceneObjectId;
    REQUIRE( engine.RegisterAuthoredBody( body, collider ).IsValid() );
}

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

Vector3 RunAngularDragCase( const CollisionShape& shape, const Vector3& bodyPrincipalInertia, const Quaternion& orientation,
                            const Vector3& worldAngularVelocity, float dragCoefficient, float gasDensity, float deltaSeconds,
                            bool usesWorldInertia )
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
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
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

TEST_CASE( "Physics launcher classification: every supported fast speed promotes independent of name" )
{
    SkullbonezCore::Core::EngineConfig config;
    config.physicsExecution.parallel = false;
    config.physicsExecution.parallelApplyForces = false;
    config.physicsExecution.parallelMutualGravity = false;
    config.physicsExecution.parallelExternalForceFields = false;
    config.physicsExecution.parallelNarrowphase = false;
    config.physicsExecution.parallelTerrainDetect = false;
    config.physicsExecution.parallelIntegrate = false;
    config.bodySimulation.velocityLimit = 1000.0f;

    // Lifetime: PhysicsEngine retains the terrain view, so declaration order
    // destroys the engine before its deep no-contact terrain owner.
    SkullbonezCore::Geometry::Terrain terrain( -100000.0f, 0.0f, 0.0f, config );
    PhysicsEngine engine;
    engine.ApplyRuntimeConfig( config );
    engine.SetTerrainView( terrain.PhysicsView() );
    engine.SetSleepEnabled( false );

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        engine.ReserveAuthoredBodyCapacity( static_cast<std::size_t>( FP2_LAUNCHER_BODY_COUNT ),
                                            static_cast<std::size_t>( FP2_LAUNCHER_BODY_COUNT ) );

        for ( int speedStep = FIRST_FP2_LAUNCHER_SPEED_STEP; speedStep <= LAST_LAUNCHER_SPEED_STEP; ++speedStep )
        {
            const int speedOffset = speedStep - FIRST_FP2_LAUNCHER_SPEED_STEP;
            const int firstBodyRow = speedOffset * 2;
            const float speed = FP2_LAUNCHER_SPEED_MIN + static_cast<float>( speedStep ) * FP2_LAUNCHER_SPEED_STEP;
            AddLauncherClassificationBody( engine, 12000u + static_cast<uint32_t>( firstBodyRow ), speed,
                                           static_cast<float>( firstBodyRow ) * 4.0f, "launcher_projectile" );
            AddLauncherClassificationBody( engine, 12001u + static_cast<uint32_t>( firstBodyRow ), speed,
                                           static_cast<float>( firstBodyRow + 1 ) * 4.0f, "generic_fast_ball" );
        }
    }

    REQUIRE( PhysicsEngine::ReadBodies( engine ).Count() == FP2_LAUNCHER_BODY_COUNT );
    SkullbonezCore::Threading::LockOrderValidator lockOrderValidator;
    SkullbonezCore::Threading::WorkerPool workerPool( lockOrderValidator );
    PhysicsWorldForces noForces;
    noForces.angularDragMultiplier = 0.0f;
    engine.Step( PHYSICS_FIXED_DT, noForces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );

    const auto diagnostics = engine.GetDiagnosticsView();
    REQUIRE( diagnostics.motionEligibilityState.size() == static_cast<std::size_t>( FP2_LAUNCHER_BODY_COUNT ) );
    CHECK( diagnostics.motionEligibilityStats.evaluatedBodies == FP2_LAUNCHER_BODY_COUNT );
    CHECK( diagnostics.motionEligibilityStats.promotedBodies == FP2_LAUNCHER_BODY_COUNT );
    CHECK( diagnostics.motionEligibilityStats.discreteBodies == 0 );

    const auto hot = PhysicsEngine::ReadBodies( engine ).HotFields();

    for ( int speedStep = FIRST_FP2_LAUNCHER_SPEED_STEP; speedStep <= LAST_LAUNCHER_SPEED_STEP; ++speedStep )
    {
        const int speedOffset = speedStep - FIRST_FP2_LAUNCHER_SPEED_STEP;
        const std::size_t launcherRow = static_cast<std::size_t>( speedOffset * 2 );
        const std::size_t genericRow = launcherRow + 1u;
        const float speed = FP2_LAUNCHER_SPEED_MIN + static_cast<float>( speedStep ) * FP2_LAUNCHER_SPEED_STEP;
        const float expectedTravel = speed * PHYSICS_FIXED_DT;
        CAPTURE( speed );

        CHECK( expectedTravel > FP2_LAUNCHER_PROJECTILE_RADIUS );
        CHECK( hot.positionX[launcherRow] == doctest::Approx( expectedTravel ) );
        CHECK( hot.positionX[genericRow] == doctest::Approx( expectedTravel ) );
        CHECK( diagnostics.motionEligibilityState[launcherRow] ==
               SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted );
        CHECK( diagnostics.motionEligibilityState[genericRow] ==
               SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted );
        CHECK( diagnostics.motionEligibilityState[genericRow] == diagnostics.motionEligibilityState[launcherRow] );
    }
}

TEST_CASE( "Physics launcher collision path: generic and launcher names produce identical swept TOI" )
{
    SkullbonezCore::Core::EngineConfig config;
    config.physicsExecution.parallel = false;
    config.physicsExecution.parallelApplyForces = false;
    config.physicsExecution.parallelMutualGravity = false;
    config.physicsExecution.parallelExternalForceFields = false;
    config.physicsExecution.parallelNarrowphase = false;
    config.physicsExecution.parallelTerrainDetect = false;
    config.physicsExecution.parallelIntegrate = false;
    config.bodySimulation.velocityLimit = 1000.0f;

    constexpr float laneSeparation = 8.0f;
    constexpr float wallCenterX = 1.05f;
    constexpr float speed = FIRST_FP2_LAUNCHER_SPEED;
    constexpr int launcherBodyRow = 0;
    constexpr int launcherWallRow = 1;
    constexpr int genericBodyRow = 2;
    constexpr int genericWallRow = 3;

    // Lifetime: PhysicsEngine retains the terrain view, so declaration order
    // destroys the engine before its deep no-contact terrain owner.
    SkullbonezCore::Geometry::Terrain terrain( -100000.0f, 0.0f, 0.0f, config );
    PhysicsEngine engine;
    engine.ApplyRuntimeConfig( config );
    engine.SetTerrainView( terrain.PhysicsView() );
    engine.SetSleepEnabled( false );
    engine.SetPipelineTraceFullRecordConsumerActive( true );

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        engine.ReserveAuthoredBodyCapacity( 4u, 2u, 2u );
        AddLauncherClassificationBody( engine, 13000u, speed, 0.0f, "launcher_projectile" );
        AddLauncherCollisionWall( engine, 13001u, wallCenterX, 0.0f, "launcher_collision_wall" );
        AddLauncherClassificationBody( engine, 13002u, speed, laneSeparation, "generic_fast_ball" );
        AddLauncherCollisionWall( engine, 13003u, wallCenterX, laneSeparation, "generic_collision_wall" );
    }

    SkullbonezCore::Threading::LockOrderValidator lockOrderValidator;
    SkullbonezCore::Threading::WorkerPool workerPool( lockOrderValidator );
    PhysicsWorldForces noForces;
    noForces.angularDragMultiplier = 0.0f;
    engine.Step( PHYSICS_FIXED_DT, noForces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );

    const auto diagnostics = engine.GetDiagnosticsView();
    REQUIRE( diagnostics.motionEligibilityState.size() == 4u );
    CHECK( diagnostics.motionEligibilityState[launcherBodyRow] ==
           SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted );
    CHECK( diagnostics.motionEligibilityState[genericBodyRow] == diagnostics.motionEligibilityState[launcherBodyRow] );

    const auto pairMatches = []( int actualA, int actualB, int expectedA, int expectedB )
    { return ( actualA == expectedA && actualB == expectedB ) || ( actualA == expectedB && actualB == expectedA ); };
    const auto hasCandidatePair = [&]( int bodyA, int bodyB )
    {
        return std::ranges::any_of( diagnostics.candidatePairs, [&]( const auto& candidate )
                                    { return pairMatches( candidate.first, candidate.second, bodyA, bodyB ); } );
    };
    const auto findSweptHit = [&]( int bodyA, int bodyB )
    {
        return std::ranges::find_if( diagnostics.physicsPipelineTrace,
                                     [&]( const auto& record )
                                     {
                                         return record.stage ==
                                                    SkullbonezCore::Physics::PhysicsPipelineStage::SweptObjectHit &&
                                                pairMatches( record.bodyA, record.bodyB, bodyA, bodyB );
                                     } );
    };

    CHECK( hasCandidatePair( launcherBodyRow, launcherWallRow ) );
    CHECK( hasCandidatePair( genericBodyRow, genericWallRow ) );
    const auto launcherHit = findSweptHit( launcherBodyRow, launcherWallRow );
    const auto genericHit = findSweptHit( genericBodyRow, genericWallRow );
    REQUIRE( launcherHit != diagnostics.physicsPipelineTrace.end() );
    REQUIRE( genericHit != diagnostics.physicsPipelineTrace.end() );
    CHECK( launcherHit->scalarA > 0.0f );
    CHECK( launcherHit->scalarA < PHYSICS_FIXED_DT );
    CHECK( genericHit->scalarA == launcherHit->scalarA );
    CHECK( genericHit->scalarB == launcherHit->scalarB );

    // Invariant: lane translation is the only authored difference. Equal TOI
    // records must therefore resolve to equal x/y poses and all velocity axes.
    const auto hot = PhysicsEngine::ReadBodies( engine ).HotFields();
    CHECK( hot.positionX[genericBodyRow] == hot.positionX[launcherBodyRow] );
    CHECK( hot.positionY[genericBodyRow] == hot.positionY[launcherBodyRow] );
    CHECK( hot.positionZ[genericBodyRow] - laneSeparation == hot.positionZ[launcherBodyRow] );
    CHECK( hot.linearVelocityX[genericBodyRow] == hot.linearVelocityX[launcherBodyRow] );
    CHECK( hot.linearVelocityY[genericBodyRow] == hot.linearVelocityY[launcherBodyRow] );
    CHECK( hot.linearVelocityZ[genericBodyRow] == hot.linearVelocityZ[launcherBodyRow] );
    CHECK( hot.linearVelocityX[launcherBodyRow] < 0.0f );
}

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
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
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
    CHECK( hit.distance == doctest::Approx( 9.5f ).epsilon( 0.00001 ) );
    CheckVectorApprox( hit.point, Vector3( 10.0f, 24.0f, 29.5f ) );
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


TEST_CASE( "Physics broadphase fixed step rotates body-local collider centers" )
{
    auto bodies = std::make_unique<PhysicsBodyStore>();
    auto colliders = std::make_unique<ColliderStore>();
    auto broadphase = std::make_unique<SkullbonezCore::Physics::PhysicsBroadphaseStage>();
    auto diagnostics = std::make_unique<SkullbonezCore::Physics::PhysicsStepDiagnostics>();
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodies->ReserveCapacity( 2u );
        colliders->ReserveCapacity( 2u );
        colliders->ReserveShapeCapacity( 2u, 0u, 0u );
        broadphase->ReserveSceneCapacity( 2u );
        diagnostics->ReserveSceneCapacity( 2u );
    }

    const CollisionShape offsetShape = BoundingSphere( 0.5f, Vector3( 10.0f, 0.0f, 0.0f ) );
    const CollisionShape originShape = BoundingSphere( 0.5f, ZERO_VECTOR );
    const CollisionShape shapes[] = { offsetShape, originShape };
    const Vector3 bodyPositions[] = { ZERO_VECTOR, Vector3( 0.0f, 10.0f, 0.0f ) };

    for ( int bodyIndex = 0; bodyIndex < 2; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.sceneObjectId = MakePhysicsSceneObjectId( static_cast<uint32_t>( 81 + bodyIndex ) );
        body.cold.mass = 1.0f;
        body.hot.position = bodyPositions[bodyIndex];
        body.hot.inverseMass = 1.0f;
        body.hot.boundingRadius = SkullbonezCore::Math::CollisionDetection::GetShapeBodyOriginBoundingRadius(
            shapes[bodyIndex] );
        const auto bodyHandle = bodies->CreateBodyRecord( body );
        REQUIRE( bodyHandle.IsValid() );
        ColliderRecord collider;
        collider.body = bodyHandle;
        collider.sceneObjectId = body.cold.sceneObjectId;
        collider.boundingRadius = body.hot.boundingRadius;
        REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( *colliders, collider, shapes[bodyIndex] )
                     .IsValid() );
    }

    diagnostics->BeginStep( 2 );
    const std::array<uint8_t, 2> sleepState = { 0u, 0u };
    const std::array<int, 2> awakeBodies = { 0, 1 };
    const std::array<uint8_t, 2> motionEligibilityState = {};
    const std::array<float, 2> angularBroadphaseExpansion = { 0.0f, 0.0f };
    SkullbonezCore::Physics::BroadphaseSettings settings;
    settings.cellSize = 2.0f;
    broadphase->ApplyRuntimeSettings( settings );
    const SkullbonezCore::Physics::BroadphaseBodyActivityView activity( 2, sleepState, awakeBodies, motionEligibilityState,
                                                                        angularBroadphaseExpansion );
    const SkullbonezCore::Physics::BroadphaseSweepContactEnvelope envelope( 1.0f / 120.0f, 0.0f, 0.05f );
    const auto unrotatedPairs = broadphase->Run( *bodies, *colliders, {}, activity, envelope,
                                                 diagnostics->MutablePipelineTraceRecorder() );
    CHECK( unrotatedPairs.empty() );

    auto offsetBody = SkullbonezCore::Physics::LoadPhysicsBodyHotState( bodies->HotFields(), 0u );
    offsetBody.orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), HALF_PI_RADIANS );
    SkullbonezCore::Physics::StorePhysicsBodyHotState( bodies->MutableHotFields(), 0u, offsetBody );
    broadphase->InvalidateBodyTopology();
    diagnostics->BeginStep( 2 );
    const auto pairs = broadphase->Run( *bodies, *colliders, {}, activity, envelope,
                                        diagnostics->MutablePipelineTraceRecorder() );
    REQUIRE( pairs.size() == 1u );
    CHECK( pairs[0] == std::make_pair( 0, 1 ) );
}

TEST_CASE( "Physics API frames: point-joint anchors are body-local rather than world positions" )
{
    const Vector3 dynamicStart( 10.0f, 24.0f, 29.0f );
    const PhysicsBodyHotState localAnchorResult = SolveAnchorCase( Vector3( 4.0f, 0.0f, 0.0f ) );
    CheckVectorApprox( localAnchorResult.position, dynamicStart );
    CheckVectorApprox( localAnchorResult.linearVelocity, ZERO_VECTOR );
    CheckVectorApprox( localAnchorResult.angularVelocity, ZERO_VECTOR );

    // Hazard: a world point is numerically plausible input but means a very different
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
    const Vector3 expectedBodyAngularVelocity = initialBodyAngularVelocity +
                                                Vector3( bodyTorque.x * deltaSeconds / bodyPrincipalInertia.x,
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
    worldTorque.x = ClampDragAxisReference( worldTorque.x, initialWorldAngularVelocity.x, isotropicInertia.x, deltaSeconds );
    worldTorque.y = ClampDragAxisReference( worldTorque.y, initialWorldAngularVelocity.y, isotropicInertia.y, deltaSeconds );
    worldTorque.z = ClampDragAxisReference( worldTorque.z, initialWorldAngularVelocity.z, isotropicInertia.z, deltaSeconds );
    const Vector3 bodyImpulse = rotation.TransposeMultiply( worldTorque * deltaSeconds );
    const Vector3 bodyDelta( bodyImpulse.x / isotropicInertia.x, bodyImpulse.y / isotropicInertia.y,
                             bodyImpulse.z / isotropicInertia.z );
    const Vector3 expected = initialWorldAngularVelocity + rotation * bodyDelta;

    const Vector3 actual = RunAngularDragCase( shape, isotropicInertia, orientation, initialWorldAngularVelocity,
                                               dragCoefficient, gasDensity, deltaSeconds, false );
    CheckVectorExact( actual, expected );
}
