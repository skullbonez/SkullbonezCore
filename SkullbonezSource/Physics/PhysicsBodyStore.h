/*
File: SkullbonezSource/Physics/PhysicsBodyStore.h
Purpose:
  Owns deterministic body-order mutable physics state during simulation.

Mental model:
  Physics-facing body data has an explicit owner. Scene/model authoring code
  submits descriptor values at creation and topology-repair boundaries, while
  standalone creation owns dense body rows directly.

Glossary:
  Body: Simulated object state consumed by the physics step.
  Sleep: Optimization that skips stable bodies until contact or user action
    wakes them.
  Underwater sleep lock: Sleep policy that keeps fully submerged balls dormant
    so buoyancy jitter does not repeatedly wake them.
  Inverse mass: Reciprocal mass value; zero means an immovable body.
  Replay body id: Stable per-scene id used by replay and diagnostics.
  Model row hint: Caller-owned cached dense-row guess; the store may repair or
    invalidate it while resolving stable identity.
  Fixed-tree release: Authored structure rule where one released fixed prop can
    release higher parts in the same tree group.

Invariants:
  - Runtime body records stay in scene/model slot order.
  - Standalone-created records stay dense; handles map allocator slots to the
    current dense row.
  - Public body handles are allocator-owned identities; model-order arrays use
    explicit maps instead of encoding model index inside the handle.
  - Store refreshes load descriptor rows into physics-owned body records before
    a step or explicit editor/replay commit.
  - Steady-frame pose, velocity, and sleep state do not copy back to authoring data;
    readers must use the body, collider, render, or diagnostics stores.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.cpp
  - SkullbonezSource/Physics/PhysicsScene.h
  - Agentic/Plans/TODO/physics-authority-and-identity.md
*/
#pragma once

#include <cstdint>
#include <vector>

#include "PhysicsHandles.h"
#include "../GameObjects/SceneCapacity.h"
#include "PhysicsFixedList.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"
#include "../Core/Common.h"

namespace SkullbonezCore
{
namespace Geometry
{
class Terrain;
}

namespace Physics
{
class ColliderStore;
struct PhysicsBodyCreateDesc;
struct PhysicsWorldForces;

// Describes one fixed-tree release source. Solver and tornado code pass this
// value to the store so released parts inherit deterministic seed velocities.
struct PhysicsFixedTreeReleaseEvent
{
    int sourceIndex = -1;                                                                      // Body row whose release triggers same-tree propagation.
    Math::Vector::Vector3 seedLinearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 seedAngularVelocity = Math::Vector::ZERO_VECTOR;
};

struct PhysicsBodyRecord
{
    PhysicsBodyHandle handle;                                                                  // Stable body handle resolved through the store maps.
    PhysicsSceneObjectId sceneObjectId;                                                        // Scene-local id supplied once by the creation owner.
    uint32_t replayBodyId = 0;                                                                 // Legacy replay id derived from sceneObjectId for traces/replay.
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 invRotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 pendingImpulse = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 pendingImpulseApplicationPoint = Math::Vector::ZERO_VECTOR;
    Geometry::Terrain* terrain = nullptr;                                                      // Borrowed terrain pointer supplied by the authoring descriptor.
    float mass = 0.0f;                                                                         // Authoring mass; fixed bodies still report mass.
    float invMass = 0.0f;                                                                      // Solver inverse mass; fixed bodies use zero.
    float boundingRadius = 0.0f;                                                               // Conservative radius for body-level release/spin policy.
    float volume = 0.0f;                                                                       // Cached body volume used by buoyancy force math.
    float projectedSurfaceArea = 0.0f;                                                         // Cached drag area used by world-force integration.
    float dragCoefficient = 0.0f;                                                              // Cached drag coefficient used by world-force integration.
    float submergedVolumePercent = 0.0f;                                                       // Targeted water snapshot for underwater sleep gates.
    float contactReleaseImpulseThreshold = 1.0f;                                               // Minimum contact impulse before authored fixed props release.
    float angularVelocityLimit = 5.0f;                                                         // Per-body spin cap applied before force integration.
    float contactEpsilon = 0.05f;                                                              // Terrain proximity tolerance used by buoyancy support damping.
    int fixedTreeReleaseRootIndex = -1;                                                        // Authored release group root; -1 means no fixed-tree group.
    bool isFixed = false;                                                                      // True for immovable collision bodies.
    bool isSleeping = false;                                                                   // Physics-owned sleep flag mirrored to diagnostics by model index.
    bool usesWorldInertia = false;                                                             // Non-sphere bodies rotate inertia through orientation.
    bool releasesFromFixedOnContact = false;                                                   // Authored fixed prop can become dynamic after strong contact.
    bool hasPendingImpulse = false;                                                            // One-shot impulse waiting for the next body integration pass.
};

using PhysicsBodyRecordList = PhysicsFixedList<PhysicsBodyRecord, MAX_GAME_MODELS>;
using PhysicsBodyHandleList = PhysicsFixedList<PhysicsBodyHandle, MAX_GAME_MODELS>;
using PhysicsHandleGenerationList = PhysicsFixedList<uint32_t, MAX_GAME_MODELS>;
using PhysicsHandleFlagList = PhysicsFixedList<uint8_t, MAX_GAME_MODELS>;
using PhysicsHandleModelIndexList = PhysicsFixedList<int, MAX_GAME_MODELS>;
using PhysicsHandleReplayIdList = PhysicsFixedList<uint32_t, MAX_GAME_MODELS>;
using PhysicsHandleSlotList = PhysicsFixedList<uint32_t, MAX_GAME_MODELS>;
using PhysicsHandleAssignmentMask = PhysicsFixedList<uint8_t, MAX_GAME_MODELS>;

class PhysicsBodyStore
{
  public:
    PhysicsBodyStore();

    void Clear();
    // Cold topology repair imports descriptor rows produced by the collection
    // owner. The store preserves handle-keyed one-shot state while replacing
    // model-order records from those explicit values.
    void LoadFromDescriptors( const std::vector<PhysicsBodyCreateDesc>& bodyDescs,
                              const std::vector<uint8_t>& sleepStates );
    // Creates a physics-owned body row from descriptor data. The store
    // assigns the handle and keeps the row dense; callers supply authored state.
    PhysicsBodyHandle CreateBodyRecord( const PhysicsBodyRecord& record );
    // Imports a body descriptor at a cold creation boundary. The descriptor is
    // the scene/editor-facing contract; body rows remain hot simulation data.
    PhysicsBodyHandle CreateBodyRecord( const PhysicsBodyCreateDesc& desc, bool sleepEnabled );
    // Retires a handle-owned body row and closes the dense record array by
    // moving the last live row into the hole. Existing handles remain stable
    // because handle slots map to current row indices.
    bool DestroyBodyRecord( PhysicsBodyHandle handle );
    void ClearPendingImpulses();
    // Shrinks the model-order body array for replay restore without reloading
    // from authoring records. Returns false when the requested count is outside the
    // current store range.
    bool TrimToCount( int bodyCount );
    // Restores sampled replay values into the authoritative body record. The
    // replay id must match so stale handles cannot mutate a reused body slot.
    bool RestoreReplayBodyState( PhysicsBodyHandle body,
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
    void RefreshRecordFromDescriptorAt( const PhysicsBodyCreateDesc& desc, int modelIndex );
    void CopySleepStatesFrom( const std::vector<uint8_t>& sleepStates );
    void CopySleepStatesTo( std::vector<uint8_t>& sleepStates ) const;
    // Cold descriptor refresh keeps replay identity with the body store. Scene
    // owners supply only the row count; missing rows receive fresh store-scanned ids.
    std::vector<uint32_t> BuildReplayBodyIdsForReload( int sceneEntityCount ) const;
    // Converts an authored fixed body record into a dynamic body without a
    // descriptor reload. Release-on-impact paths call this while they
    // already own the live store row.
    static void ReleaseFixedRecord( PhysicsBodyRecord& record,
                                    const Math::Vector::Vector3& seedLinearVelocity,
                                    const Math::Vector::Vector3& seedAngularVelocity );
    // Releases higher same-tree fixed parts using release-group metadata already
    // copied into body rows. outReleasedBodyIndices is caller-owned scratch.
    void ReleaseAttachedFixedTreeParts( const PhysicsFixedTreeReleaseEvent& event,
                                        std::vector<int>& outReleasedBodyIndices );

    const PhysicsBodyRecord* Data() const;
    int Count() const;
    bool Empty() const;
    PhysicsBodyHandle HandleForModelIndex( int modelIndex ) const;
    // Resolves stable replay identity to the live body handle. modelIndexHint
    // is a fast path only; stale hints fall back to the handle replay-id table.
    PhysicsBodyHandle HandleForReplayBodyId( uint32_t replayBodyId, int modelIndexHint = -1 ) const;
    // Resolves a stable body handle to the current dense row and refreshes the
    // caller-owned cache. Returns -1 and invalidates the hint for stale handles.
    int ResolveModelRow( PhysicsBodyHandle handle, ModelRowHint& hint ) const;
    int ModelIndexForHandle( PhysicsBodyHandle handle ) const;
    bool Contains( PhysicsBodyHandle handle ) const;
    const PhysicsBodyRecordList& Records() const;
    PhysicsBodyRecordList& MutableRecords();
    PhysicsBodyRecord* MutableRecordForHandle( PhysicsBodyHandle handle );
    const PhysicsBodyRecord* RecordForHandle( PhysicsBodyHandle handle ) const;
    PhysicsBodyRecord* MutableRecordForModelIndex( int modelIndex );
    const PhysicsBodyRecord* RecordForModelIndex( int modelIndex ) const;
    // Handle-keyed commands are the store-owned public path. Solver helpers that
    // already walk dense rows mutate PhysicsBodyRecord directly instead of
    // paying a row-index-to-handle round trip.
    bool WakeBody( PhysicsBodyHandle body );
    bool SeedBodyAsleep( PhysicsBodyHandle body );
    // Edits live velocity through the handle-owned body record. The command is
    // intentionally handle-only so replay/editor tools do not regain model-index
    // physics authority while dragging.
    bool SetBodyVelocity( PhysicsBodyHandle body,
                          const Math::Vector::Vector3& linearVelocity,
                          const Math::Vector::Vector3& angularVelocity );
    bool SetPendingBodyImpulse( PhysicsBodyHandle body,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );
    bool ApplyBodyImpulse( PhysicsBodyHandle body,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );
    static bool ConsumePendingBodyImpulse( PhysicsBodyRecord& record );
    // Advances one mutable body record from its current velocities and shape
    // snapshot. Returns false when the slot is fixed, sleeping, missing, or has
    // no positive time to integrate.
    bool IntegrateBodyPose( const ColliderStore& colliderStore, int modelIndex, float deltaSeconds );
    bool ApplyForces( const PhysicsWorldForces& worldForces,
                      const ColliderStore& colliderStore,
                      int modelIndex,
                      float deltaSeconds,
                      const Math::Vector::Vector3* precomputedMutualGravityForce = nullptr );

  private:
    PhysicsBodyHandle ResolveHandleForModelIndex( int modelIndex,
                                                  uint32_t replayBodyId,
                                                  PhysicsHandleAssignmentMask& assignedHandleSlots );
    void RetireUnassignedHandles( const PhysicsHandleAssignmentMask& assignedHandleSlots );

    PhysicsBodyRecordList m_bodies{ "PhysicsBodyStore.bodies" };                               // Body records in scene/model slot order.
    PhysicsBodyHandleList m_modelBodyHandles{ "PhysicsBodyStore.modelBodyHandles" };           // Model index to body handle map.
    PhysicsHandleGenerationList m_handleGenerations{ "PhysicsBodyStore.handleGenerations" };   // Handle-slot generations.
    PhysicsHandleFlagList m_handleAlive{ "PhysicsBodyStore.handleAlive" };                     // Live handle slot flags.
    PhysicsHandleModelIndexList m_handleModelIndices{ "PhysicsBodyStore.handleModelIndices" }; // Slot to model index.
    PhysicsHandleReplayIdList m_handleReplayBodyIds{ "PhysicsBodyStore.handleReplayBodyIds" }; // Slot replay ids.
    PhysicsHandleSlotList m_freeHandleSlots{ "PhysicsBodyStore.freeHandleSlots" };             // Retired reusable slots.
    // Runtime allocation policy: topology repair reuses this handle-slot mask
    // instead of constructing a heap-backed standard-library container.
    PhysicsHandleAssignmentMask m_assignedHandleScratch{ "PhysicsBodyStore.assignedHandleScratch" };
};
} // namespace Physics
} // namespace SkullbonezCore
