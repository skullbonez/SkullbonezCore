/*
File: SkullbonezSource/Physics/ContactSolverCommon.h
Purpose:
  Shares contact-row math helpers and data structures across physics solver code.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/Common.h"
#include "../Maths/Vector3.h"

#include <cmath>

namespace SkullbonezCore
{
namespace Physics
{
namespace ContactSolver
{

// Shared contact-solver math used by object/object and object/terrain rows.
// A "row" is one tiny rule such as "do not move into the wall along this normal"
// or "slow sideways sliding along this tangent." Keeping the math here helps the
// terrain and object paths agree about directions, effective mass, and friction.
inline void BuildContactTangents( const Math::Vector::Vector3& normal,
                                  Math::Vector::Vector3& tangent1,
                                  Math::Vector::Vector3& tangent2 )
{
    // Catto-style 3D contact solving treats friction as two scalar tangent rows
    // attached to the same contact point as the normal row. The tangent frame
    // must be deterministic: if two identical runs pick different tangent axes,
    // accumulated friction impulses and warm-start cache values can diverge even
    // when the normal contact is unchanged.
    //
    // The seed-vector branch avoids a near-zero Gram-Schmidt result when the
    // normal is already close to world X. After subtracting the normal component,
    // tangent1 is normalized and tangent2 is the cross product that completes the
    // orthonormal basis. This is intentionally simple and branch-stable because
    // it runs in hot contact setup paths.
    if ( fabsf( normal.x ) > 0.9f )
    {
        tangent1 = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    }
    else
    {
        tangent1 = Math::Vector::Vector3( 1.0f, 0.0f, 0.0f );
    }

    tangent1 -= normal * ( tangent1 * normal );
    float tangentMag = Math::Vector::VectorMag( tangent1 );
    if ( tangentMag > TOLERANCE )
    {
        tangent1 /= tangentMag;
    }
    tangent2 = Math::Vector::CrossProduct( normal, tangent1 );
}

inline void ClampFrictionVector( float& accT1, float& accT2, float limit )
{
    // Catto 2005 presents two independent tangent bounds for the 2D examples.
    // Skullbonez solves 3D contacts, so two tangent rows form one friction vector
    // in the contact plane. Clamping the vector length, rather than each axis
    // independently, keeps diagonal friction from exceeding the requested budget
    // by sqrt(2). This helper is shared so terrain and object contacts converge
    // toward the same friction policy as the terrain path is migrated further
    // into the Catto row kernel.
    float limitSq = limit * limit;
    float magSq = accT1 * accT1 + accT2 * accT2;
    if ( magSq > limitSq && magSq > TOLERANCE * TOLERANCE )
    {
        float scale = limit / sqrtf( magSq );
        accT1 *= scale;
        accT2 *= scale;
    }
}

template <typename ApplyInvInertia>
inline float ComputeStaticBodyEffectiveMass( float invMass,
                                             const Math::Vector::Vector3& axis,
                                             const Math::Vector::Vector3& r,
                                             ApplyInvInertia applyInvInertia )
{
    // Effective mass is the scalar denominator in Catto's row solve:
    //
    //     lambda = velocity_error / (J * M^-1 * J^T)
    //
    // For a point contact, the linear part contributes inverse mass and the
    // angular part contributes axis dot ((I^-1 * (r cross axis)) cross r).
    // Terrain is a static body, so only the dynamic body's inverse mass and
    // inverse inertia appear here.
    const Math::Vector::Vector3 rCrossAxis = Math::Vector::CrossProduct( r, axis );
    const Math::Vector::Vector3 invInertiaTerm = applyInvInertia( rCrossAxis );
    const float k = invMass + axis * Math::Vector::CrossProduct( invInertiaTerm, r );
    return ( k > TOLERANCE ) ? ( 1.0f / k ) : 0.0f;
}

template <typename ApplyInvInertiaA, typename ApplyInvInertiaB>
inline float ComputeTwoBodyEffectiveMass( float invMassA,
                                          float invMassB,
                                          const Math::Vector::Vector3& axis,
                                          const Math::Vector::Vector3& rA,
                                          const Math::Vector::Vector3& rB,
                                          ApplyInvInertiaA applyInvInertiaA,
                                          ApplyInvInertiaB applyInvInertiaB )
{
    // Same row denominator as ComputeStaticBodyEffectiveMass, but with both
    // dynamic bodies contributing linear and angular terms. Keeping the formula
    // here avoids subtle drift between terrain/object contact paths: any later
    // fix to sign convention, inertia transform, or tolerance should happen once
    // and then be picked up by both solver families.
    const Math::Vector::Vector3 rAxAxis = Math::Vector::CrossProduct( rA, axis );
    const Math::Vector::Vector3 rBxAxis = Math::Vector::CrossProduct( rB, axis );
    const float k = invMassA + invMassB + axis * Math::Vector::CrossProduct( applyInvInertiaA( rAxAxis ), rA ) +
                    axis * Math::Vector::CrossProduct( applyInvInertiaB( rBxAxis ), rB );
    return ( k > TOLERANCE ) ? ( 1.0f / k ) : 0.0f;
}

} // namespace ContactSolver
} // namespace Physics
} // namespace SkullbonezCore
