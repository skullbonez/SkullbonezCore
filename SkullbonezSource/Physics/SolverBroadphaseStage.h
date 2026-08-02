/*
File: SkullbonezSource/Physics/SolverBroadphaseStage.h
Purpose:
  Exposes the pure broadphase candidate filter used by the solver driver.

Summary:
  SpatialGrid provides locality candidates; this stage rejects pairs whose
  swept bounding spheres cannot touch during the current fixed tick and rejects
  dormant/dormant pairs before they enter solver-visible work. Debug can count
  exact predicate invocations on the owning step thread without changing the
  Profile/Release predicate.

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
  - The geometry-only predicate remains available to Debug diagnostics so
    SleepPrunedPair retains its pre-P3 admission boundary.
  - One synchronous broadphase call owns the Debug thread-local counter between
    reset and sample; concurrently stepped worlds on other threads remain
    isolated.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Physics/SpatialGrid.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "ColliderStore.h"
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
#if defined( _DEBUG )

// Pair-stream oracle counter. The broadphase owner resets this immediately
// before grid collection, then samples it at the raw and post-augmentation
// boundaries. Keeping the increment inside the predicate counts every concrete
// invocation, including calls that reject before any pair reaches an output.
// Invariant: one synchronous broadphase stage owns this thread between reset
// and sample. Thread-local storage isolates concurrently stepped worlds while
// keeping predicate call sites independent of the stage object.
inline thread_local uint64_t g_broadphaseCandidateGeometryInvocationCount = 0;

inline void ResetBroadphaseCandidateGeometryInvocationCount()
{
    g_broadphaseCandidateGeometryInvocationCount = 0;
}

inline uint64_t BroadphaseCandidateGeometryInvocationCount()
{
    return g_broadphaseCandidateGeometryInvocationCount;
}
#endif

inline bool BroadphaseCandidateBothSleeping( std::span<const uint8_t> sleepState, int a, int b )
{
    return a >= 0 && b >= 0 && a < static_cast<int>( sleepState.size() ) && b < static_cast<int>( sleepState.size() ) &&
           sleepState[a] != 0u && sleepState[b] != 0u;
}

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
// tick; the relative-motion segment covers CCD and wakeup cases.
inline bool BroadphaseCandidateGeometryCanTouch( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                                 float dt, float contactSkin, int a, int b )
{
#if defined( _DEBUG )
    ++g_broadphaseCandidateGeometryInvocationCount;
#endif
    const int modelCount = (std::min)( bodyStore.Count(), colliderStore.Count() );

    if ( a < 0 || b < 0 || a >= modelCount || b >= modelCount )
    {
        return false;
    }

    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    const float radiusA = BroadphaseCandidateBodyRadius( colliderRecords, a );
    const float radiusB = BroadphaseCandidateBodyRadius( colliderRecords, b );

    if ( !std::isfinite( radiusA ) || !std::isfinite( radiusB ) || radiusA < 0.0f || radiusB < 0.0f )
    {
        return true;
    }

    const Math::Vector::Vector3 relativeStart = BroadphaseCandidateBodyPosition( hotFields, a ) -
                                                BroadphaseCandidateBodyPosition( hotFields, b );
    const Math::Vector::Vector3 relativeDisplacement = ( PhysicsBodyLinearVelocity( hotFields,
                                                                                    static_cast<std::size_t>( a ) ) -
                                                         PhysicsBodyLinearVelocity( hotFields,
                                                                                    static_cast<std::size_t>( b ) ) ) *
                                                       dt;
    const float contactRadius = radiusA + radiusB + contactSkin;
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

inline bool BroadphaseCandidateCanTouch( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                         std::span<const uint8_t> sleepState, float dt, float contactSkin, int a, int b )
{
    return !BroadphaseCandidateBothSleeping( sleepState, a, b ) &&
           BroadphaseCandidateGeometryCanTouch( bodyStore, colliderStore, dt, contactSkin, a, b );
}
} // namespace Physics
} // namespace SkullbonezCore
