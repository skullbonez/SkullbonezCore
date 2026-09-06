// Handle-keyed point-joint authoring and persistent warm impulse. Prepared
// solver blocks are transient; this record owns the cache across fixed steps.
#pragma once
#include "PhysicsHandles.h"
#include "PointJointSettings.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore::Physics
{
class PhysicsBodyStore;
struct PointJointConstraint
{
    static constexpr uint8_t FLAG_LIMIT_NECK_SWING = 1u << 0;

    PhysicsConstraintHandle handle; // Stable identity; dense solver-row movement never retargets it.
    PhysicsBodyHandle bodyA;
    PhysicsBodyHandle bodyB;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
    float slack = 0.25f;
    float frequencyHz = SkullbonezCore::Physics::POINT_JOINT_DEFAULT_FREQUENCY_HZ;
    float dampingRatio = SkullbonezCore::Physics::POINT_JOINT_DEFAULT_DAMPING_RATIO;
    Math::Vector::Vector3
        accumulatedImpulse = Math::Vector::ZERO_VECTOR; // World-space impulse on body A, retained with this stable handle.
    uint32_t groupId = 0;
    uint8_t flags = 0;

    void SetBodies( PhysicsBodyHandle bodyAHandle, PhysicsBodyHandle bodyBHandle )
    {
        if ( bodyA == bodyAHandle && bodyB == bodyBHandle )
        {
            return;
        }
        bodyA = bodyAHandle;
        bodyB = bodyBHandle;

        // Invariant: an impulse cached for another body pair is not valid for
        // the newly authored constraint, even when the handle is unchanged.
        accumulatedImpulse = Math::Vector::ZERO_VECTOR;
    }

    int BodyAIndex( const PhysicsBodyStore& bodyStore ) const;
    int BodyBIndex( const PhysicsBodyStore& bodyStore ) const;

    bool HasValidBodies() const
    {
        return bodyA.IsValid() && bodyB.IsValid() && bodyA != bodyB;
    }
};

// Detached evidence from one block visit. The shared transaction publishes
// the last visit per joint; numerical fixtures may retain every visit. An
// unvisited or unsolvable block keeps iteration -1. Samples never drive solving.
struct PointJointIterationSample
{
    PhysicsConstraintHandle constraint;
    PhysicsBodyHandle islandRoot;
    int iteration = -1;
    Math::Vector::Vector3 anchorErrorBeforeCorrection = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 relativeAnchorVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 accumulatedImpulse = Math::Vector::ZERO_VECTOR;
    // Compliance-adjusted equation residual in m/s; physical relative velocity
    // can remain non-zero while an implicit soft block has zero residual.
    Math::Vector::Vector3 constraintResidualVelocity = Math::Vector::ZERO_VECTOR;
    float impulseDeltaSq = 0.0f;
    float minimumScaledPivot = 0.0f;
    // Signed kinetic-energy change from this corrective impulse, excluding
    // warm start, gravity/integration, and the separate neck angular limiter.
    float impulseWorkJoules = 0.0f;
    float biasRatePerSecond = 0.0f;
    float complianceScale = 0.0f;
};

} // namespace SkullbonezCore::Physics
