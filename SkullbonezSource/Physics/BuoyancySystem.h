/*
File: SkullbonezSource/Physics/BuoyancySystem.h
Purpose:
  Owns per-body fluid facts and analytic submersion queries.

Summary:
  This fixed-capacity owner stores the five per-body facts used by fluid force,
  terrain support, and underwater sleep policy. Rows stay aligned with the
  body and collider stores, while fixed-step stages borrow only dense spans.

Glossary:
  Sphere cap: Portion of a sphere below the fluid surface; its analytic volume
    gives a deterministic submerged fraction without sampling.
  Submersion snapshot: Per-body fraction cached on the buoyancy row for one
    physics decision, not an authoring value.
  Underwater sleep lock: Sleep policy that keeps fully submerged balls dormant
    so buoyancy jitter does not repeatedly wake them.

Invariants:
  - Facts row i describes body/collider row i; lifecycle commands use the same
    append, swap-last erase, trim, refresh, and clear order as those stores.
  - Only sphere colliders participate in the underwater sleep-lock query.
  - The submerged fraction is deterministic math over body pose, collider
    offset, and the per-tick world-force snapshot.

Related:
  - SkullbonezSource/Physics/BuoyancySystem.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include <cstddef>
#include <span>

#include "../Core/SceneCapacity.h"
#include "PhysicsApi.h"
#include "PhysicsFixedList.h"

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
struct PhysicsWorldForces;

struct BuoyancyBodyFacts
{
    float volume = 0.0f;
    float projectedSurfaceArea = 0.0f;
    float dragCoefficient = 0.0f;
    float submergedVolumePercent = 0.0f;
    float contactEpsilon = 0.05f;
};
static_assert( sizeof( BuoyancyBodyFacts ) == sizeof( float ) * 5u,
               "Buoyancy facts must remain one compact five-float row." );

class BuoyancySystem
{
  private:
    friend class PhysicsEngine;

    // Replay prediction keeps fluid facts aligned with the explicitly cloned
    // body and collider rows; no ordinary BuoyancySystem copy is exposed.
    void CloneReplayPredictionStorageFrom( const BuoyancySystem& source );

    PhysicsFixedList<BuoyancyBodyFacts, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>
        m_bodyFacts { "BuoyancySystem.bodyFacts", PhysicsCapacityReason::SceneBodies };

  public:
    void ReserveCapacity( std::size_t capacity );
    bool AppendBodyFacts( const PhysicsBodyCreateDesc& desc );
    bool RefreshBodyFacts( int index, const PhysicsBodyCreateDesc& desc );
    bool EraseBodyFactsSwapLast( int index );
    bool TrimToCount( int count );
    void Clear();
    int Count() const;
    std::size_t RecordCapacity() const;
    std::span<const BuoyancyBodyFacts> Facts() const;
    std::span<BuoyancyBodyFacts> MutableFacts();

    static bool RefreshUnderwaterSubmersionForBall( const PhysicsWorldForces& worldForces, const PhysicsBodyStore& bodyStore,
                                                    const ColliderStore& colliderStore, BuoyancyBodyFacts& facts,
                                                    int index );
    static bool IsFullySubmergedBall( const BuoyancyBodyFacts& facts, bool fixed, const ColliderStore& colliderStore,
                                      int index );
};
} // namespace Physics
} // namespace SkullbonezCore
