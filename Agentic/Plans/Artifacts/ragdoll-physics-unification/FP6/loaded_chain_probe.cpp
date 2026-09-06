#define DOCTEST_CONFIG_IMPLEMENT
// A ten-link, unit-mass chain under gravity. Metrics cover 240 fixed steps;
// settled energy and frame-to-frame sag jitter use the final 120 steps.
#include "ThirdPtySource/doctest/doctest.h"

#include "SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "Ragdoll.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <chrono>
#include <cstdio>


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
    double jitterSquared = 0.0;
    double meanKineticEnergy = 0.0;
    double peakKineticEnergy = 0.0;
    double solveMicroseconds = 0.0;
};

void CheckVectorExact( const Vector3& actual, const Vector3& expected )
{
    CHECK( actual.x == expected.x );
    CHECK( actual.y == expected.y );
    CHECK( actual.z == expected.z );
}

LoadedChainMeasurement RunLoadedChain(  )
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

    float previousSag = 0.0f;
    for ( int frame = 0; frame < 240; ++frame )
    {
        for ( int bodyIndex = 1; bodyIndex <= kLoadedChainBodyCount; ++bodyIndex )
        {
            const std::size_t row = static_cast<std::size_t>( bodyIndex );
            auto hot = LoadPhysicsBodyHotState( bodies.HotFields(), row );
            hot.linearVelocity.y += gravity * stepSeconds;
            StorePhysicsBodyHotState( bodies.MutableHotFields(), row, hot );
        }

        const auto started = std::chrono::steady_clock::now();
        REQUIRE( Ragdoll::SolvePointJoints( bodies, constraints, sleepState, stepSeconds ) );
        measurement.solveMicroseconds += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now()-started).count();

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
        double kinetic = 0.0;
        for ( int index = 1; index <= kLoadedChainBodyCount; ++index ) {
            const auto state = LoadPhysicsBodyHotState( bodies.HotFields(), index );
            kinetic += 0.5 * (Dot(state.linearVelocity,state.linearVelocity)+Dot(state.angularVelocity,state.angularVelocity));
        }
        measurement.peakKineticEnergy = (std::max)(measurement.peakKineticEnergy, kinetic);
        if (frame >= 120) {
            const double delta = bottomSag-previousSag;
            measurement.jitterSquared += delta*delta/120.0;
            measurement.meanKineticEnergy += kinetic/120.0;
        }
        previousSag = bottomSag;
    }


    return measurement;
}
} // namespace


TEST_CASE("FP5 measurements") {
 (void)RunLoadedChain();
 for(int run=0;run<10;++run) {
    double elapsed=0.0; LoadedChainMeasurement value;
    for(int repeat=0; repeat<100;++repeat) {value=RunLoadedChain();elapsed+=value.solveMicroseconds;}
    std::printf("FP5_METRICS run=%d sag_m=%.9g peak_sag_m=%.9g jitter_step_rms_m=%.9g settled_kinetic_j=%.9g peak_kinetic_j=%.9g solve_us_per_joint_iteration=%.9g\n",run,value.finalBottomSag,value.maximumBottomSag,std::sqrt(value.jitterSquared),value.meanKineticEnergy,value.peakKineticEnergy,elapsed/(100.0*240.0*10.0*4.0));
 }
}
int main(){doctest::Context c;return c.run();}
