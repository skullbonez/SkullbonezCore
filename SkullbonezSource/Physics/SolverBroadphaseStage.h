/*
File: SkullbonezSource/Physics/SolverBroadphaseStage.h
Purpose:
  Exposes the pure broadphase candidate filter used by the solver driver.

Summary:
  SpatialGrid provides locality candidates; this stage rejects pairs whose
  swept bounding spheres cannot touch during the current fixed tick.

Glossary:
  Candidate pair: Two model-order body indices emitted by broadphase before
    object narrowphase builds exact manifolds.
  Contact skin: Extra radius added to a broadphase sphere so near misses still
    reach narrowphase when solver tolerances may need them.
  Swept segment: Relative start-to-end motion of one body against another over
    a fixed tick.

Invariants:
  - This filter is conservative: invalid radius data stays accepted so a later,
    exact stage can decide, while out-of-range indices are rejected.
  - False positives are allowed; false negatives can drop real collisions and
    break deterministic physics baselines.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Physics/SpatialGrid.h
*/
#pragma once

#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "../Maths/MathsCommon.h"
#include "../Maths/Vector3.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace SkullbonezCore
{
namespace Physics
{
struct BroadphaseCandidateFilterContext
{
    std::span<const PhysicsBodyRecord> bodyRecords;
    PhysicsBodyHotFieldsConstView hotFields;
    std::span<const ColliderRecord> colliderRecords;
    int modelCount = 0;
    float dt = 0.0f;
    float contactSkin = 0.0f;
};

inline Math::Vector::Vector3 BroadphaseCandidateBodyPosition( const PhysicsBodyHotFieldsConstView& hotFields,
                                                              int bodyIndex )
{
    return PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyIndex ) );
}

inline float BroadphaseCandidateBodyRadius( std::span<const ColliderRecord> colliderRecords, int bodyIndex )
{
    return colliderRecords[static_cast<std::size_t>( bodyIndex )].boundingRadius;
}

// Why: sharing one spatial-grid cell is only a locality hint. Dense wall scenes
// can put many small boxes in one cell, so reject pairs whose swept bounding
// spheres never approach before appending them to the solver candidate vector.
//
// Invariant: this remains a broadphase test. It may keep false positives, but
// it must not reject a pair whose exact shapes could touch during this fixed
// tick; the relative-motion segment covers CCD and wakeup cases.
inline bool BroadphaseCandidateCanTouch( const BroadphaseCandidateFilterContext* contextValue, int a, int b )
{
    if ( contextValue == nullptr )
    {
        return true;
    }

    const BroadphaseCandidateFilterContext& context = *contextValue;
    if ( a < 0 || b < 0 || a >= context.modelCount || b >= context.modelCount )
    {
        return false;
    }

    const float radiusA = BroadphaseCandidateBodyRadius( context.colliderRecords, a );
    const float radiusB = BroadphaseCandidateBodyRadius( context.colliderRecords, b );
    if ( !std::isfinite( radiusA ) || !std::isfinite( radiusB ) || radiusA < 0.0f || radiusB < 0.0f )
    {
        return true;
    }

    const Math::Vector::Vector3 relativeStart = BroadphaseCandidateBodyPosition( context.hotFields, a ) -
                                                BroadphaseCandidateBodyPosition( context.hotFields, b );
    const Math::Vector::Vector3 relativeDisplacement =
        ( PhysicsBodyLinearVelocity( context.hotFields, static_cast<std::size_t>( a ) ) -
          PhysicsBodyLinearVelocity( context.hotFields, static_cast<std::size_t>( b ) ) ) *
        context.dt;
    const float contactRadius = radiusA + radiusB + context.contactSkin;
    const float contactRadiusSq = contactRadius * contactRadius;
    const float relativeLengthSq = Math::Vector::VectorMagSquared( relativeDisplacement );
    if ( relativeLengthSq <= TOLERANCE * TOLERANCE )
    {
        return Math::Vector::VectorMagSquared( relativeStart ) <= contactRadiusSq;
    }

    float t = -( relativeStart * relativeDisplacement ) / relativeLengthSq;
    t = (std::max)( 0.0f, (std::min)( 1.0f, t ) );
    const Math::Vector::Vector3 closestRelative = relativeStart + relativeDisplacement * t;
    return Math::Vector::VectorMagSquared( closestRelative ) <= contactRadiusSq;
}
} // namespace Physics
} // namespace SkullbonezCore
