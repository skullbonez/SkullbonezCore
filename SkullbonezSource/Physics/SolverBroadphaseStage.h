/*
File: SkullbonezSource/Physics/SolverBroadphaseStage.h
Purpose:
  Exposes the pure broadphase candidate filter used by the solver driver.

Summary:
  SpatialGrid provides locality candidates; this stage rejects pairs whose
  swept bounding spheres, including per-body angular reach, cannot touch during
  the current fixed tick and rejects dormant/dormant pairs before they enter
  solver-visible work. The geometry-only filter operation remains explicit so Debug
  can preserve sleep-pruned diagnostics at the same admission boundary without
  restoring dormant solver work.

Glossary:
  Contact skin: Extra radius added to a broadphase sphere so near misses still
    reach narrowphase when solver tolerances may need them.
  Swept segment: Relative start-to-end motion of one body against another over
    a fixed tick.
  Sleep-only pair: Two dormant dynamic bodies that cannot create work until an
    awake mover reaches either body.

Invariants:
  - This filter is conservative: invalid radius data stays accepted so a later,
    exact stage can decide, while out-of-range indices are rejected.
  - False positives are allowed; false negatives can drop real collisions and
    break deterministic physics baselines.
  - The geometry-only operation remains available to Debug diagnostics so
    SleepPrunedPair observes the admission boundary before sleep pruning.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Physics/SpatialGrid.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "ColliderStore.h"
#include "PhysicsBroadphaseStepValues.h"
#include "PhysicsBodyStore.h"
#include "../Maths/MathsCommon.h"
#include "../Maths/Vector3.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace SkullbonezCore
{
namespace Physics
{
// Invariant: fixed-step candidate owners may append only inside construction-
// reserved storage. Equality is already exhaustion because emplace_back would
// otherwise trigger runtime growth.
inline bool BroadphaseCandidateAppendHasCapacity( std::size_t size, std::size_t capacity )
{
    return size < capacity;
}

inline Math::Vector::Vector3 BroadphaseCandidateBodyPosition( const PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
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
// tick; the relative-motion segment covers CCD and wakeup cases. The filter is
// a synchronous capability and never outlives the stores referenced below.
class BroadphasePairFilter
{
  private:
    const PhysicsBodyStore& m_bodyStore;
    const ColliderStore& m_colliderStore;
    BroadphaseBodyActivityView m_activity;
    BroadphaseSweepContactEnvelope m_envelope;

  public:
    BroadphasePairFilter( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                          BroadphaseBodyActivityView activity, BroadphaseSweepContactEnvelope envelope )
        : m_bodyStore( bodyStore ), m_colliderStore( colliderStore ), m_activity( activity ), m_envelope( envelope )
    {
        const int modelCount = (std::min)( bodyStore.Count(), colliderStore.Count() );

        if ( activity.BodyCount() != modelCount )
        {
            SB_FATAL( "Physics/BroadphasePairFilter",
                      "Broadphase filter body domain mismatch: activity=%d bodies=%d colliders=%d.", activity.BodyCount(),
                      bodyStore.Count(), colliderStore.Count() );
        }
    }

    bool BothSleeping( int a, int b ) const noexcept
    {
        return a >= 0 && b >= 0 && a < m_activity.BodyCount() && b < m_activity.BodyCount() && m_activity.IsSleeping( a ) &&
               m_activity.IsSleeping( b );
    }

    int BodyCount() const noexcept
    {
        return m_activity.BodyCount();
    }

    bool GeometryCanTouch( int a, int b ) const
    {
        const int modelCount = m_activity.BodyCount();

        if ( a < 0 || b < 0 || a >= modelCount || b >= modelCount )
        {
            return false;
        }

        const PhysicsBodyHotFieldsConstView hotFields = m_bodyStore.HotFields();
        const std::span<const ColliderRecord> colliderRecords = m_colliderStore.Records();
        const float expansionA = m_activity.AngularExpansion( a );
        const float expansionB = m_activity.AngularExpansion( b );
        const float radiusA = BroadphaseCandidateBodyRadius( colliderRecords, a ) + (std::max)( 0.0f, expansionA );
        const float radiusB = BroadphaseCandidateBodyRadius( colliderRecords, b ) + (std::max)( 0.0f, expansionB );

        if ( !std::isfinite( radiusA ) || !std::isfinite( radiusB ) || !std::isfinite( expansionA ) ||
             !std::isfinite( expansionB ) || radiusA < 0.0f || radiusB < 0.0f )
        {
            return true;
        }

        const Math::Vector::Vector3 relativeStart = BroadphaseCandidateBodyPosition( hotFields, a ) -
                                                    BroadphaseCandidateBodyPosition( hotFields, b );
        const Math::Vector::Vector3 relativeDisplacement = ( PhysicsBodyLinearVelocity( hotFields,
                                                                                        static_cast<std::size_t>( a ) ) -
                                                             PhysicsBodyLinearVelocity( hotFields,
                                                                                        static_cast<std::size_t>( b ) ) ) *
                                                           m_envelope.DeltaTime();
        const float contactRadius = radiusA + radiusB + m_envelope.ContactSkin();
        const float contactRadiusSq = contactRadius * contactRadius;
        const float relativeLengthSq = Math::Vector::VectorMagSquared( relativeDisplacement );

        if ( relativeLengthSq <= TOLERANCE * TOLERANCE )
        {
            return Math::Vector::VectorMagSquared( relativeStart ) <= contactRadiusSq;
        }

        float t = -( Dot( relativeStart, relativeDisplacement ) ) / relativeLengthSq;
        t = (std::max)( 0.0f, (std::min)( 1.0f, t ) );
        const Math::Vector::Vector3 closestRelative = relativeStart + relativeDisplacement * t;
        return Math::Vector::VectorMagSquared( closestRelative ) <= contactRadiusSq;
    }

    bool CanTouch( int a, int b ) const
    {
        return !BothSleeping( a, b ) && GeometryCanTouch( a, b );
    }
};
} // namespace Physics
} // namespace SkullbonezCore
