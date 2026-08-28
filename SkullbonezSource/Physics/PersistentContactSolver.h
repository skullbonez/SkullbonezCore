/*
File: SkullbonezSource/Physics/PersistentContactSolver.h
Purpose:
  Defines persistent-contact step policy, cache, and body-scratch values.

Summary:
  PhysicsContactSolverStage uses these compact values while it solves
  object/object and object/terrain rows. The values carry no owner authority,
  callback, or retained borrow across a solve.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Raw runtime settings normalize once at the solve boundary; row loops must
    not independently reinterpret authored lower or upper bounds.
  - Every valid scene body index must fit the 15-bit fields in a persistent
    contact key.

Related:
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>

#include "../Core/SceneCapacity.h"
#include "../Maths/RotationMatrix.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{
class PhysicsContactSolverStage;

// Invariant: object/object keys store two 15-bit body indices above the
// 32-bit feature id. Widening this mask would consume bit 62, which
// distinguishes terrain rows from object/object rows.
inline constexpr uint64_t PERSISTENT_CONTACT_BODY_MASK = 0x7fffull;
inline constexpr uint64_t PERSISTENT_CONTACT_TERRAIN_KIND_BIT = 1ull << 62;
static_assert( static_cast<uint64_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS - 1 ) <=
                   PERSISTENT_CONTACT_BODY_MASK,
               "Persistent-contact key body fields must encode every valid scene body index." );

inline int64_t MakePersistentContactCacheKey( int bodyA, int bodyB, uint32_t featureId ) noexcept
{
    if ( bodyB == -1 )
    {
        const uint64_t packed = PERSISTENT_CONTACT_TERRAIN_KIND_BIT |
                                ( ( static_cast<uint64_t>( static_cast<uint32_t>( bodyA ) ) & PERSISTENT_CONTACT_BODY_MASK )
                                  << 32 ) |
                                static_cast<uint64_t>( featureId );
        return static_cast<int64_t>( packed );
    }

    const int lowBody = ( bodyA < bodyB ) ? bodyA : bodyB;
    const int highBody = ( bodyA < bodyB ) ? bodyB : bodyA;
    const uint64_t packed = ( ( static_cast<uint64_t>( static_cast<uint32_t>( lowBody ) ) & PERSISTENT_CONTACT_BODY_MASK )
                              << 47 ) |
                            ( ( static_cast<uint64_t>( static_cast<uint32_t>( highBody ) ) & PERSISTENT_CONTACT_BODY_MASK )
                              << 32 ) |
                            static_cast<uint64_t>( featureId );
    return static_cast<int64_t>( packed );
}

// Concept: the cache key keeps a 32-bit feature suffix beneath either one
// terrain-body field or two canonically ordered object-body fields. Keeping
// decoding beside the layout constants prevents lifecycle owners from
// inventing incompatible masks when they filter or validate retained rows.
inline bool PersistentContactCacheKeyReferencesBody( int64_t signedKey, int bodyIndex ) noexcept
{
    const uint64_t key = static_cast<uint64_t>( signedKey );
    const uint64_t target = static_cast<uint64_t>( static_cast<uint32_t>( bodyIndex ) );

    if ( ( key & PERSISTENT_CONTACT_TERRAIN_KIND_BIT ) != 0u )
    {
        return ( ( key >> 32 ) & PERSISTENT_CONTACT_BODY_MASK ) == target;
    }

    return ( ( key >> 47 ) & PERSISTENT_CONTACT_BODY_MASK ) == target ||
           ( ( key >> 32 ) & PERSISTENT_CONTACT_BODY_MASK ) == target;
}

inline bool PersistentContactCacheKeyBodiesFit( int64_t signedKey, int bodyCount ) noexcept
{
    if ( bodyCount < 0 )
    {
        return false;
    }

    const uint64_t key = static_cast<uint64_t>( signedKey );
    const uint64_t count = static_cast<uint64_t>( bodyCount );
    const uint64_t highBody = ( key >> 32 ) & PERSISTENT_CONTACT_BODY_MASK;

    if ( ( key & ( 1ull << 63 ) ) != 0u )
    {
        return false;
    }

    if ( ( key & PERSISTENT_CONTACT_TERRAIN_KIND_BIT ) != 0u )
    {
        return ( ( key >> 47 ) & PERSISTENT_CONTACT_BODY_MASK ) == 0u && highBody < count;
    }

    const uint64_t lowBody = ( key >> 47 ) & PERSISTENT_CONTACT_BODY_MASK;
    return lowBody < highBody && highBody < count;
}

struct PersistentContactSolverStepPolicy
{
    float objectSlop = 0.0f;
    float objectBaumgarteBeta = 0.0f;
    float objectPositionCorrectionPercent = 0.0f;
    float terrainSlop = 0.0f;
    float terrainBaumgarteBeta = 0.0f;
    float maxBaumgarteBias = 0.0f;
    float contactRestitutionThreshold = 0.0f;    // Effective row threshold; elastic policy may force zero.
    float rawContactRestitutionThreshold = 0.0f; // Authored threshold retained for motion admission.
    float objectFrictionCoefficient = 0.0f;
    float terrainFrictionCoefficient = 0.0f;
    float rollingFrictionCoefficient = 0.0f;
    float spinFrictionCoefficient = 0.0f; // Effective contact-patch length in engine distance units.
    float sleepLinearSpeed = 0.0f;        // Raw authored values used by legacy relative-motion limits.
    float sleepAngularSpeed = 0.0f;
    float nonNegativeSleepLinearSpeed = 0.0f; // Normalized values used by the quiet-body gate.
    float nonNegativeSleepAngularSpeed = 0.0f;
    float gravityMagnitude = 0.0f;

    float contactEpsilon = 0.0f;
    int iterations = 1;
    bool elasticCollisions = false;

    // Why: convergence attribution is observational work for an active
    // diagnostics sink. Private Replay prediction engines have no sink and
    // must not spend their amortized simulation budget collecting live trace.
    bool collectConvergenceDiagnostics = false;
};

struct PersistentContactCacheEntry
{
    // Previous-frame impulse cache. The key encodes the bodies plus feature
    // id so a contact can find last tick's converged impulse even if rows
    // are rebuilt from fresh manifolds this tick.
    int64_t key = 0;
    float accN = 0.0f;
    float accT1 = 0.0f;
    float accT2 = 0.0f;
};

struct SolverBodyState
{
    // Solver scratch copy of dynamic body state. Rows iterate over this
    // compact representation first, then the final velocities are written
    // back to the authoritative hot-field spans after the solve.
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 invInertia = Math::Vector::ZERO_VECTOR;

    // Invariant: per-solve positional cleanup accumulates every manifold
    // contribution here, then publishes one authoritative write per body.
    Math::Vector::Vector3 positionCorrection = Math::Vector::ZERO_VECTOR;
    Math::Transformation::RotationMatrix orientation;
    float invMass = 0.0f;
    bool useWorldInertia = false;
};
} // namespace Physics
} // namespace SkullbonezCore
