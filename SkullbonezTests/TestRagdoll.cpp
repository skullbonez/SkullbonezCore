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
                joint.accumulatedImpulse = 0.0f;
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

    measurement.topJointAccumulatedImpulse = constraints.front().accumulatedImpulse;
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
    // Invariant: clearing each cache before SolvePointJoints reproduces the
    // pre-warm-start solver. Retention must materially reduce both settled and
    // peak numerical stretch, not merely perturb the trajectory.
    CHECK( warm.finalBottomSag <= cold.finalBottomSag * 0.75f );
    CHECK( warm.maximumBottomSag <= cold.maximumBottomSag * 0.90f );
}
