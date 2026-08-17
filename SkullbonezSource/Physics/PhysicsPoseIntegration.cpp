/*
File: PhysicsPoseIntegration.cpp
Purpose:
  Advances one authoritative body pose with deterministic angle construction.

Summary:
  Linear motion remains a direct velocity step. Angular motion constructs one
  world-space delta quaternion from the repository-owned cosine/sine routine,
  composes it on the left, and normalizes the published orientation.

Glossary:
  Exponential-map rotation: Exact axis-angle update for angular velocity held
    constant over a time interval.
  Sinc: sin(x) / x with the removable zero limit defined as exactly one.

Invariants:
  - Source evaluation order is part of the byte-exact Physics contract.
  - The stable sinc form avoids normalizing the angular-velocity axis and has no
    arbitrary near-zero angular-magnitude cutoff.
  - Any output-bit change requires Physics baseline evidence; this owner does
    not grant authority to refresh a golden.

Related:
  - SkullbonezSource/Physics/PhysicsPoseIntegration.h
  - SkullbonezSource/Physics/PhysicsBodyStore.h
  - SkullbonezSource/Maths/DeterministicMath.h
  - SkullbonezTests/TestPhysicsPoseIntegration.cpp
  - Agentic/Reference/physics-overview.md
*/
#include "PhysicsPoseIntegration.h"

#include "PhysicsBodyStore.h"
#include "../Maths/DeterministicMath.h"

#include <cmath>


namespace SkullbonezCore::Physics
{
void IntegrateBodyRecordPose( PhysicsBodyHotState& hot, float deltaSeconds ) noexcept
{
    hot.linearVelocity.Simplify();
    hot.angularVelocity.Simplify();

    hot.position += hot.linearVelocity * deltaSeconds;

    const Math::Vector::Vector3 omega = hot.angularVelocity;
    const float omegaMagnitude = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
    const float halfAngle = 0.5f * omegaMagnitude * deltaSeconds;
    const Math::Deterministic::CosSin halfAngleCosSin = Math::Deterministic::ComputeCosSin( halfAngle );

    // Concept: sinc removes the zero-axis division from axis-angle construction.
    // Its exact zero limit keeps a stationary row finite, while every non-zero
    // magnitude follows the same continuous exponential-map formula.
    const float sinc = halfAngle == 0.0f ? 1.0f : halfAngleCosSin.sine / halfAngle;
    const float vectorScale = 0.5f * deltaSeconds * sinc;
    const Math::Vector::Vector3 vectorPart = omega * vectorScale;
    const Math::Orientation::Quaternion delta( vectorPart.x, vectorPart.y, vectorPart.z, halfAngleCosSin.cosine );

    // Invariant: angular velocity is world-space, so the delta stays on the
    // left. Reversing the operands changes both behavior and baseline bytes.
    hot.orientation = delta * hot.orientation;
    hot.orientation.Normalise();
}
} // namespace SkullbonezCore::Physics
