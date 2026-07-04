/*
File: SkullbonezSource/Physics/PhysicsBodyStore.h
Purpose:
  Owns deterministic body-order mutable physics state during simulation.

Mental model:
  GameModelCollection is still the compatibility adapter, but physics-facing
  body data now has an explicit owner. The store mirrors body index order and
  replay ids so future migrations can keep solver ordering stable while moving
  callers off GameModel.

Glossary:
  Body: Simulated object state consumed by the physics step.
  Sleep: Optimization that skips stable bodies until contact or user action
    wakes them.
  Underwater sleep lock: Sleep policy that keeps fully submerged balls dormant
    so buoyancy jitter does not repeatedly wake them.
  Inverse mass: Reciprocal mass value; zero means an immovable body.
  Replay body id: Stable per-scene id used by replay and diagnostics.

Invariants:
  - Body records stay in GameModelCollection physics model order.
  - Public body handles are allocator-owned identities; model-order arrays use
    explicit maps instead of encoding model index inside the handle.
  - Store refreshes load compatibility GameModel state into the physics-owned
    body records before a step.
  - Store writeback is a named compatibility bridge for legacy render, replay,
    tool, terrain, and shape code that still reads GameModel state.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.cpp
  - SkullbonezSource/Physics/PhysicsScene.h
  - Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md
*/
#pragma once

#include <cstdint>
#include <vector>

#include "PhysicsHandles.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Geometry
{
class Terrain;
}

namespace GameObjects
{
class GameModel;
}

namespace Physics
{
class ColliderStore;
struct PhysicsWorldForces;

struct PhysicsBodyRecord
{
    PhysicsBodyHandle handle;                          // Stable body handle resolved through the store maps.
    PhysicsSceneObjectId sceneObjectId;                // Scene-local id currently mirrored from replay body id.
    uint32_t replayBodyId = 0;                         // Stable replay-facing body id for this scene.
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 invRotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 pendingImpulse = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 pendingImpulseApplicationPoint = Math::Vector::ZERO_VECTOR;
    Geometry::Terrain* terrain = nullptr;              // Borrowed terrain pointer copied from the compatibility model.
    float mass = 0.0f;                                 // Authoring mass; fixed bodies still report mass.
    float invMass = 0.0f;                              // Solver inverse mass; fixed bodies use zero.
    float boundingRadius = 0.0f;                       // Conservative radius for body-level release/spin policy.
    float volume = 0.0f;                               // Cached body volume used by buoyancy force math.
    float projectedSurfaceArea = 0.0f;                 // Cached drag area used by world-force integration.
    float dragCoefficient = 0.0f;                      // Cached drag coefficient used by world-force integration.
    float submergedVolumePercent = 0.0f;               // Targeted water snapshot for underwater sleep gates.
    float contactReleaseImpulseThreshold = 1.0f;       // Minimum contact impulse before authored fixed props release.
    float angularVelocityLimit = 5.0f;                 // Per-body spin cap applied before force integration.
    float contactEpsilon = 0.05f;                      // Terrain proximity tolerance used by buoyancy support damping.
    bool isFixed = false;                              // True for immovable collision bodies.
    bool isSleeping = false;                           // Physics-owned sleep flag mirrored to diagnostics by model index.
    bool usesWorldInertia = false;                     // Non-sphere bodies rotate inertia through orientation.
    bool releasesFromFixedOnContact = false;           // Authored fixed prop can become dynamic after strong contact.
    bool hasPendingImpulse = false;                    // One-shot impulse waiting for the next force integration pass.
};

class PhysicsBodyStore
{
  public:
    PhysicsBodyStore();

    void Clear();
    void Refresh( std::vector<GameObjects::GameModel>& models, const std::vector<uint8_t>& sleepStates );
    void LoadFromModels( std::vector<GameObjects::GameModel>& models, const std::vector<uint8_t>& sleepStates );
    void ClearPendingImpulses();
    // Shrinks the model-order body array for replay restore without reloading
    // from GameModel. Returns false when the requested count is outside the
    // current store range.
    bool TrimToCount( int bodyCount );
    // Restores sampled replay values into the authoritative body record. The
    // replay id must match so stale samples cannot mutate a reused model slot.
    bool RestoreReplayBodyState( int modelIndex,
                                 uint32_t replayBodyId,
                                 bool fixed,
                                 const Math::Vector::Vector3& position,
                                 const Math::Orientation::Quaternion& orientation,
                                 const Math::Vector::Vector3& linearVelocity,
                                 const Math::Vector::Vector3& angularVelocity,
                                 float mass,
                                 float inverseMass,
                                 const Math::Vector::Vector3& rotationalInertia,
                                 const Math::Vector::Vector3& inverseRotationalInertia );
    void WriteBackToModels( std::vector<GameObjects::GameModel>& models ) const;
    void WriteBackToModelAt( std::vector<GameObjects::GameModel>& models, int modelIndex ) const;
    void CaptureMutableStateFromModelAt( std::vector<GameObjects::GameModel>& models, int modelIndex );
    void CopySleepStatesFrom( const std::vector<uint8_t>& sleepStates );
    void CopySleepStatesTo( std::vector<uint8_t>& sleepStates ) const;

    const PhysicsBodyRecord* Data() const;
    int Count() const;
    bool Empty() const;
    PhysicsBodyHandle HandleForModelIndex( int modelIndex ) const;
    int ModelIndexForHandle( PhysicsBodyHandle handle ) const;
    bool Contains( PhysicsBodyHandle handle ) const;
    const std::vector<PhysicsBodyRecord>& Records() const;
    std::vector<PhysicsBodyRecord>& MutableRecords();
    PhysicsBodyRecord* MutableRecordForModelIndex( int modelIndex );
    const PhysicsBodyRecord* RecordForModelIndex( int modelIndex ) const;
    bool WakeBody( int modelIndex );
    bool SeedBodyAsleep( int modelIndex );
    bool SetPendingBodyImpulse( int modelIndex,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );
    bool ApplyBodyImpulse( int modelIndex,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );
    // Advances one mutable body record from its current velocities and shape
    // snapshot. Returns false when the slot is fixed, sleeping, missing, or has
    // no positive time to integrate.
    bool IntegrateBodyPose( const ColliderStore& colliderStore, int modelIndex, float deltaSeconds );
    bool ApplyForces( const PhysicsWorldForces& worldForces,
                      const ColliderStore& colliderStore,
                      int modelIndex,
                      float deltaSeconds );

  private:
    PhysicsBodyHandle
    ResolveHandleForModelIndex( int modelIndex, uint32_t replayBodyId, std::vector<uint8_t>& assignedHandleSlots );
    void RetireUnassignedHandles( const std::vector<uint8_t>& assignedHandleSlots );

    std::vector<PhysicsBodyRecord> m_bodies;           // Body records in GameModelCollection index order.
    std::vector<PhysicsBodyHandle> m_modelBodyHandles; // Model index to store-owned body handle map.
    std::vector<uint32_t> m_handleGenerations;         // Handle-slot generation counters.
    std::vector<uint8_t> m_handleAlive;                // Live handle slot flags.
    std::vector<int> m_handleModelIndices;             // Handle slot to current model index, or -1.
    std::vector<uint32_t> m_handleReplayBodyIds;       // Replay id paired with each live handle slot.
    std::vector<uint32_t> m_freeHandleSlots;           // Retired slots available for deterministic reuse.
};
} // namespace Physics
} // namespace SkullbonezCore
