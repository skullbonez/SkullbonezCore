/*
File: TestRagdoll.cpp
Purpose:
  Pins deterministic ragdoll neck swing correction and point-joint warm starting.

Summary:
  Focused value cases exercise the vector-angle decision, while a point-joint
  host case proves the solver invokes that policy. Together they cover rounded
  dot clamping, aligned and opposed endpoints, fallback-axis selection, and the
  per-step correction cap. A loaded multi-link chain compares retained and
  deliberately cleared joint impulses under a zero-slack load so numerical
  solver sag remains measured independently of authored extension.

Glossary:
  Neck swing correction: Bounded rotation that returns the head-up vector
    toward the torso-up vector after it exceeds the authored cone.

Invariants:
  - Dot and cross inputs represent the same normalized head/torso vector pair;
    endpoint tests model the small dot excursions floating-point rounding adds.
  - Expected constants are independent literals, not aliases of production
    policy values.
  - The loaded-chain comparison differs only in whether each constraint's
    handle-keyed accumulated impulse survives the next fixed step; zero slack
    prevents authored extension from being misreported as solver softness.
  - These tests pin candidate behavior and do not authorize golden refreshes.

Related:
  - SkullbonezSource/Physics/Ragdoll.h
  - SkullbonezSource/Maths/DeterministicMath.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/Ragdoll.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>


using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::LoadPhysicsBodyHotState;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PointJointConstraint;
using SkullbonezCore::Physics::Ragdoll;
using SkullbonezCore::Physics::StorePhysicsBodyHotState;

namespace
{
constexpr float kMaximumCorrectionRadians = 0.20f;
constexpr int kLoadedChainBodyCount = 10;

struct LoadedChainMeasurement
{
    float finalBottomSag = 0.0f;
    float maximumBottomSag = 0.0f;
    float topJointAccumulatedImpulse = 0.0f;
};

void CheckVectorExact( const Vector3& actual, const Vector3& expected )
{
    CHECK( actual.x == expected.x );
    CHECK( actual.y == expected.y );
    CHECK( actual.z == expected.z );
}

LoadedChainMeasurement RunLoadedChain( bool clearWarmStartEveryStep )
{
    PhysicsBodyStore bodies;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodies.ReserveCapacity( static_cast<std::size_t>( kLoadedChainBodyCount + 1 ) );
    }

    std::array<SkullbonezCore::Physics::PhysicsBodyHandle, kLoadedChainBodyCount + 1> handles = {};
    PhysicsBodyCreateRecord anchor;
    anchor.cold.mass = 1.0f;
    anchor.hot.fixed = true;
    handles[0] = bodies.CreateBodyRecord( anchor );

    for ( int bodyIndex = 1; bodyIndex <= kLoadedChainBodyCount; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.position = Vector3( 0.0f, -static_cast<float>( bodyIndex ), 0.0f );
        body.hot.inverseMass = 1.0f;
        body.hot.inverseRotationalInertia = Vector3( 1.0f, 1.0f, 1.0f );
        handles[static_cast<std::size_t>( bodyIndex )] = bodies.CreateBodyRecord( body );
    }

    std::array<PointJointConstraint, kLoadedChainBodyCount> constraints = {};

    for ( int jointIndex = 0; jointIndex < kLoadedChainBodyCount; ++jointIndex )
    {
        PointJointConstraint& joint = constraints[static_cast<std::size_t>( jointIndex )];
        joint.SetBodies( handles[static_cast<std::size_t>( jointIndex )],
                         handles[static_cast<std::size_t>( jointIndex + 1 )] );
        joint.localAnchorA = Vector3( 0.0f, -0.5f, 0.0f );
        joint.localAnchorB = Vector3( 0.0f, 0.5f, 0.0f );
        joint.slack = 0.0f;
        joint.stiffness = 0.22f;
        joint.damping = 0.35f;
    }

    constexpr float stepSeconds = 1.0f / 120.0f;
    constexpr float gravity = -9.81f;
    const std::array<uint8_t, kLoadedChainBodyCount + 1> sleepState = {};
    LoadedChainMeasurement measurement;

    for ( int frame = 0; frame < 240; ++frame )
    {
        if ( clearWarmStartEveryStep )
        {
            for ( PointJointConstraint& joint : constraints )
            {
                joint.accumulatedImpulse = Vector3( 0.0f, 0.0f, 0.0f );
            }
        }

        for ( int bodyIndex = 1; bodyIndex <= kLoadedChainBodyCount; ++bodyIndex )
        {
            const std::size_t row = static_cast<std::size_t>( bodyIndex );
            auto hot = LoadPhysicsBodyHotState( bodies.HotFields(), row );
            hot.linearVelocity.y += gravity * stepSeconds;
            StorePhysicsBodyHotState( bodies.MutableHotFields(), row, hot );
        }

        REQUIRE( Ragdoll::SolvePointJoints( bodies, constraints, sleepState, stepSeconds ) );

        for ( int bodyIndex = 1; bodyIndex <= kLoadedChainBodyCount; ++bodyIndex )
        {
            const std::size_t row = static_cast<std::size_t>( bodyIndex );
            auto hot = LoadPhysicsBodyHotState( bodies.HotFields(), row );
            hot.position += hot.linearVelocity * stepSeconds;
            StorePhysicsBodyHotState( bodies.MutableHotFields(), row, hot );
        }

        const auto bottom = LoadPhysicsBodyHotState( bodies.HotFields(), kLoadedChainBodyCount );
        const float bottomSag = std::abs( bottom.position.y + static_cast<float>( kLoadedChainBodyCount ) );
        measurement.maximumBottomSag = (std::max)( measurement.maximumBottomSag, bottomSag );
        measurement.finalBottomSag = bottomSag;
    }

    measurement.topJointAccumulatedImpulse = VectorMag( constraints.front().accumulatedImpulse );
    return measurement;
}
} // namespace


TEST_CASE( "Ragdoll neck swing: rounded dot endpoints stay finite" )
{
    const float aboveOne = std::nextafter( 1.0f, std::numeric_limits<float>::infinity() );
    const float belowNegativeOne = std::nextafter( -1.0f, -std::numeric_limits<float>::infinity() );
    const Vector3 fallbackAxis( 1.0f, 0.0f, 0.0f );
    Vector3 correctionAxis;
    float correctionAngle = -1.0f;

    CHECK_FALSE( Ragdoll::TryBuildNeckSwingCorrection( aboveOne, Vector3( 0.0f, 0.0f, 0.0f ), fallbackAxis,
                                                       correctionAxis, correctionAngle ) );
    CHECK( correctionAngle == -1.0f );

    REQUIRE( Ragdoll::TryBuildNeckSwingCorrection( belowNegativeOne, Vector3( 0.0f, 0.0f, 0.0f ), fallbackAxis,
                                                   correctionAxis, correctionAngle ) );
    CHECK( std::isfinite( correctionAngle ) );
    CHECK( correctionAngle == doctest::Approx( kMaximumCorrectionRadians ) );
    CheckVectorExact( correctionAxis, fallbackAxis );
}


TEST_CASE( "Ragdoll neck swing: aligned vectors need no correction and opposed vectors use fallback" )
{
    const Vector3 fallbackAxis( 0.0f, 0.0f, 1.0f );
    Vector3 correctionAxis;
    float correctionAngle = 0.0f;

    CHECK_FALSE( Ragdoll::TryBuildNeckSwingCorrection( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ), fallbackAxis,
                                                       correctionAxis, correctionAngle ) );

    REQUIRE( Ragdoll::TryBuildNeckSwingCorrection( -1.0f, Vector3( 0.0f, 0.0f, 0.0f ), fallbackAxis,
                                                   correctionAxis, correctionAngle ) );
    CheckVectorExact( correctionAxis, fallbackAxis );
    CHECK( correctionAngle == doctest::Approx( kMaximumCorrectionRadians ) );
}


TEST_CASE( "Ragdoll neck swing: cross axis is reused for capped and ordinary corrections" )
{
    const Vector3 correctionCross( 0.0f, 0.0f, 1.0f );
    Vector3 correctionAxis;
    float correctionAngle = 0.0f;

    REQUIRE( Ragdoll::TryBuildNeckSwingCorrection( 0.0f, correctionCross, Vector3( 1.0f, 0.0f, 0.0f ),
                                                   correctionAxis, correctionAngle ) );
    CheckVectorExact( correctionAxis, correctionCross );
    CHECK( correctionAngle == doctest::Approx( kMaximumCorrectionRadians ) );

    // A 35-degree separation exceeds the 30-degree cone by five degrees. This
    // independent geometric case proves the ordinary path does not always emit
    // the per-step cap used by the larger endpoint cases.
    const Vector3 subCapCross( 0.0f, 0.0f, 0.57357644f );
    REQUIRE( Ragdoll::TryBuildNeckSwingCorrection( 0.81915206f, subCapCross,
                                                   Vector3( 1.0f, 0.0f, 0.0f ), correctionAxis,
                                                   correctionAngle ) );
    CheckVectorExact( correctionAxis, correctionCross );
    CHECK( correctionAngle < kMaximumCorrectionRadians );
    CHECK( std::abs( correctionAngle - 0.08726646f ) <= 0.00005f );
}


TEST_CASE( "Ragdoll neck swing: point-joint host applies correction and damping" )
{
    PhysicsBodyStore bodies;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodies.ReserveCapacity( 2u );
    }

    PhysicsBodyCreateRecord torso;
    torso.cold.mass = 1.0f;
    torso.hot.fixed = true;
    const auto torsoHandle = bodies.CreateBodyRecord( torso );

    PhysicsBodyCreateRecord head;
    head.cold.mass = 1.0f;
    head.hot.inverseMass = 1.0f;
    head.hot.inverseRotationalInertia = Vector3( 1.0f, 1.0f, 1.0f );
    head.hot.orientation.RotateAboutAxis( Vector3( 1.0f, 0.0f, 0.0f ), 3.14159265f );
    head.hot.angularVelocity = Vector3( 1.0f, 2.0f, 3.0f );
    const auto headHandle = bodies.CreateBodyRecord( head );

    PointJointConstraint neck;
    neck.SetBodies( torsoHandle, headHandle );
    neck.flags = PointJointConstraint::FLAG_LIMIT_NECK_SWING;
    std::array<PointJointConstraint, 1> constraints = { neck };
    const std::array<uint8_t, 2> sleepState = { 0u, 0u };

    REQUIRE( Ragdoll::SolvePointJoints( bodies, constraints, sleepState, 1.0f / 120.0f ) );
    const int headIndex = bodies.ModelIndexForHandle( headHandle );
    REQUIRE( headIndex >= 0 );
    const auto corrected = LoadPhysicsBodyHotState( bodies.HotFields(), static_cast<std::size_t>( headIndex ) );
    const Vector3 correctedUp = corrected.orientation.GetOrientationMatrix() * Vector3( 0.0f, 1.0f, 0.0f );

    CHECK( correctedUp.y > -1.0f );
    CHECK( corrected.angularVelocity.x == doctest::Approx( 0.45f ) );
    CHECK( corrected.angularVelocity.y == doctest::Approx( 0.90f ) );
    CHECK( corrected.angularVelocity.z == doctest::Approx( 1.35f ) );
}


TEST_CASE( "Ragdoll point joint: retained impulse reduces zero-slack loaded-chain sag" )
{
    const LoadedChainMeasurement cold = RunLoadedChain( true );
    const LoadedChainMeasurement warm = RunLoadedChain( false );

    CAPTURE( cold.finalBottomSag );
    CAPTURE( cold.maximumBottomSag );
    CAPTURE( warm.finalBottomSag );
    CAPTURE( warm.maximumBottomSag );
    CAPTURE( warm.topJointAccumulatedImpulse );
    CHECK( warm.topJointAccumulatedImpulse > 0.0f );
    // Invariant: clearing each cache isolates the current solver
    // without temporal warm starting. Retention must materially reduce both settled and
    // peak numerical stretch, not merely perturb the trajectory.
    CHECK( warm.finalBottomSag <= cold.finalBottomSag * 0.75f );
    CHECK( warm.maximumBottomSag <= cold.maximumBottomSag * 0.90f );
}

TEST_CASE( "Ragdoll point joint: coincident anchors constrain off-axis relative velocity" )
{
    PhysicsBodyStore bodies;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodies.ReserveCapacity( 2u );
    }
    PhysicsBodyCreateRecord anchor;
    anchor.hot.fixed = true;
    const auto fixedHandle = bodies.CreateBodyRecord( anchor );
    PhysicsBodyCreateRecord moving;
    moving.cold.mass = 1.0f;
    moving.hot.inverseMass = 1.0f;
    moving.hot.inverseRotationalInertia = Vector3( 1.0f, 1.0f, 1.0f );
    moving.hot.linearVelocity = Vector3( 0.0f, 2.0f, -3.0f );
    const auto movingHandle = bodies.CreateBodyRecord( moving );
    PointJointConstraint joint;
    joint.SetBodies( fixedHandle, movingHandle );
    joint.slack = 0.0f;
    joint.damping = 0.0f;
    std::array<PointJointConstraint, 1> joints = { joint };
    const std::array<uint8_t, 2> sleep = {};
    REQUIRE( Ragdoll::SolvePointJoints( bodies, joints, sleep, 1.0f / 120.0f ) );
    const auto result = LoadPhysicsBodyHotState( bodies.HotFields(), 1u );
    CHECK( result.linearVelocity.x == doctest::Approx( 0.0f ).epsilon( 0.00001f ) );
    CHECK( result.linearVelocity.y == doctest::Approx( 0.0f ).epsilon( 0.00001f ) );
    CHECK( result.linearVelocity.z == doctest::Approx( 0.0f ).epsilon( 0.00001f ) );
}

TEST_CASE( "Ragdoll point joint: coupled anchor response is rotation invariant" )
{
    for ( const bool fixedA : { false, true } )
    {
        Vector3 referenceImpulse( 0.0f, 0.0f, 0.0f );
        for ( int rotated = 0; rotated < 2; ++rotated )
        {
            PhysicsBodyStore bodies;
            {
                SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
                    SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
                bodies.ReserveCapacity( 2u );
            }
            auto orientation = SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION;
            if ( rotated ) { orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), 1.5707963267948966f ); }
            const auto rotation = orientation.GetOrientationMatrix();
            PhysicsBodyCreateRecord a;
            a.cold.mass = 2.0f;
            a.cold.usesWorldInertia = true;
            a.hot.fixed = fixedA;
            a.hot.inverseMass = 0.5f;
            a.hot.orientation = orientation;
            a.hot.inverseRotationalInertia = Vector3( 0.3f, 0.8f, 0.7f );
            PhysicsBodyCreateRecord b;
            b.cold.mass = 1.0f;
            b.cold.usesWorldInertia = true;
            b.hot.inverseMass = 1.0f;
            b.hot.orientation = orientation;
            b.hot.position = rotation * Vector3( 0.1f, -0.6f, 0.2f );
            b.hot.linearVelocity = rotation * Vector3( 1.0f, 2.0f, -1.0f );
            b.hot.angularVelocity = rotation * Vector3( 0.2f, -0.1f, 0.3f );
            b.hot.inverseRotationalInertia = Vector3( 0.5f, 0.4f, 0.9f );
            PointJointConstraint joint;
            const auto handleA = bodies.CreateBodyRecord( a );
            const auto handleB = bodies.CreateBodyRecord( b );
            joint.SetBodies( handleA, handleB );
            joint.localAnchorA = Vector3( 0.4f, 0.2f, -0.3f );
            joint.localAnchorB = Vector3( 0.3f, 0.8f, -0.5f );
            joint.slack = 0.0f;
            joint.stiffness = 0.0f;
            joint.damping = 0.0f;
            std::array<PointJointConstraint, 1> joints = { joint };
            std::array<SkullbonezCore::Physics::PointJointIterationSample, 4> samples;
            const std::array<uint8_t, 2> sleep = {};
            REQUIRE( Ragdoll::SolvePointJoints( bodies, joints, sleep, 1.0f / 120.0f, samples ) );
            REQUIRE( samples.back().iteration == 3 );
            CHECK( samples.back().minimumScaledPivot > 0.0f );
            const Vector3 residual = samples.back().relativeAnchorVelocity;
            CHECK( std::abs( residual.x ) < 0.00001f );
            CHECK( std::abs( residual.y ) < 0.00001f );
            CHECK( std::abs( residual.z ) < 0.00001f );
            if ( rotated == 0 ) { referenceImpulse = joints[0].accumulatedImpulse; }
            else
            {
                const Vector3 expected = rotation * referenceImpulse;
                CHECK( joints[0].accumulatedImpulse.x == doctest::Approx( expected.x ).epsilon( 0.00001f ) );
                CHECK( joints[0].accumulatedImpulse.y == doctest::Approx( expected.y ).epsilon( 0.00001f ) );
                CHECK( joints[0].accumulatedImpulse.z == doctest::Approx( expected.z ).epsilon( 0.00001f ) );
            }
            if ( !fixedA )
            {
                const auto resultA = LoadPhysicsBodyHotState( bodies.HotFields(), 0u );
                const auto resultB = LoadPhysicsBodyHotState( bodies.HotFields(), 1u );
                const Vector3 momentum = resultA.linearVelocity * 2.0f + resultB.linearVelocity;
                CHECK( momentum.x == doctest::Approx( b.hot.linearVelocity.x ).epsilon( 0.00001f ) );
                CHECK( momentum.y == doctest::Approx( b.hot.linearVelocity.y ).epsilon( 0.00001f ) );
                CHECK( momentum.z == doctest::Approx( b.hot.linearVelocity.z ).epsilon( 0.00001f ) );
            }
        }
    }
}

TEST_CASE( "Ragdoll point joint: degenerate mass blocks remain finite without warm-start mutation" )
{
    for ( const bool fixed : { false, true } )
    {
        PhysicsBodyStore bodies;
        {
            SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
                SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
            bodies.ReserveCapacity( 2u );
        }
        PhysicsBodyCreateRecord body;
        body.hot.fixed = fixed;
        body.hot.inverseMass = 1.0f;
        body.hot.inverseRotationalInertia = Vector3( 1.0e20f, 0.0f, 0.0f );
        PointJointConstraint joint;
        const auto handleA = bodies.CreateBodyRecord( body );
        const auto handleB = bodies.CreateBodyRecord( body );
        joint.SetBodies( handleA, handleB );
        joint.localAnchorA = joint.localAnchorB = Vector3( 0.0f, 1.0f, 0.0f );
        joint.accumulatedImpulse = Vector3( 1.0f, 2.0f, 3.0f );
        std::array<PointJointConstraint, 1> joints = { joint };
        const std::array<uint8_t, 2> sleep = {};
        REQUIRE( Ragdoll::SolvePointJoints( bodies, joints, sleep, 1.0f / 120.0f ) );
        for ( std::size_t row = 0; row < 2u; ++row )
        {
            const auto result = LoadPhysicsBodyHotState( bodies.HotFields(), row );
            CheckVectorExact( result.linearVelocity, Vector3( 0.0f, 0.0f, 0.0f ) );
            CheckVectorExact( result.angularVelocity, Vector3( 0.0f, 0.0f, 0.0f ) );
        }
    }
}
