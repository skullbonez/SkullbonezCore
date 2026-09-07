// Numerical fixture for isolated point blocks, independent of contact stages.
// Production uses the guarded shared transaction; mixed-family fixtures cover
// its phase order and islands. This adapter retains full per-iteration samples.
#pragma once
#include "../SkullbonezSource/Physics/PointJointBlock.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/Ragdoll.h"
#include "../SkullbonezSource/Maths/ScalarMath.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace SkullbonezTests
{
inline bool SolvePointJointsForTest( SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                     std::span<SkullbonezCore::Physics::PointJointConstraint> constraints,
                                     std::span<const uint8_t> sleep, float dt,
                                     std::span<SkullbonezCore::Physics::PointJointIterationSample> samples = {},
                                     int iterations = SkullbonezCore::Physics::POINT_JOINT_SOLVER_ITERATIONS )
{
    using namespace SkullbonezCore::Physics;
    std::fill( samples.begin(), samples.end(), PointJointIterationSample {} );
    if ( constraints.empty() || !std::isfinite( dt ) || dt <= TOLERANCE || iterations < 1 || iterations > 32 )
    {
        return false;
    }
    std::vector<SolverBodyState> bodies( static_cast<std::size_t>( bodyStore.Count() ) );
    const auto hot = bodyStore.HotFields();
    for ( std::size_t i = 0; i < bodies.size(); ++i )
    {
        if ( hot.fixed[i] || ( i < sleep.size() && sleep[i] ) )
        {
            continue;
        }
        auto& body = bodies[i];
        body.linearVelocity = PhysicsBodyLinearVelocity( hot, i );
        body.angularVelocity = PhysicsBodyAngularVelocity( hot, i );
        body.invMass = hot.inverseMass[i];
        body.invInertia = PhysicsBodyInverseInertia( hot, i );
        body.useWorldInertia = bodyStore.Records()[i].usesWorldInertia;
        auto rotation = PhysicsBodyOrientation( hot, i );
        body.orientation = rotation.GetOrientationMatrix();
    }
    std::vector<PointJointBlock> blocks( constraints.size() );
    for ( std::size_t i = 0; i < blocks.size(); ++i )
    {
        blocks[i].Prepare( constraints[i], bodyStore, bodies, dt, i );
    }
    for ( const auto& block : blocks )
    {
        block.WarmStart( bodies );
    }
    for ( int iteration = 0; iteration < iterations; ++iteration )
    {
        for ( auto& block : blocks )
        {
            const std::size_t sampleIndex = block.SourceIndex() * iterations + iteration;
            PointJointIterationSample* sample = sampleIndex < samples.size() ? &samples[sampleIndex] : nullptr;
            if ( sample && block.Active() )
            {
                sample->iteration = iteration;
            }
            (void)block.Solve( bodies, sample );
        }
    }
    for ( const auto& block : blocks )
    {
        block.StoreImpulse( constraints );
    }
    auto output = bodyStore.MutableHotFields();
    for ( std::size_t i = 0; i < bodies.size(); ++i )
    {
        if ( bodies[i].invMass <= 0.0f )
        {
            continue;
        }
        auto state = LoadPhysicsBodyHotState( output, i );
        state.linearVelocity = bodies[i].linearVelocity;
        state.angularVelocity = bodies[i].angularVelocity;
        StorePhysicsBodyHotState( output, i, state );
    }
    (void)Ragdoll::ApplyNeckSwingLimits( bodyStore, constraints, sleep );
    return true;
}
} // namespace SkullbonezTests
