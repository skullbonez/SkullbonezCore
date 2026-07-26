/*
File: SkullbonezSource/Physics/PhysicsBodyStore.h
Purpose:
  Owns deterministic body-order mutable physics state during simulation.

Summary:
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
  Scene object id: Stable per-scene id used by replay and diagnostics.
  Model row hint: Caller-owned cached dense-row guess; the store may repair or
    invalidate it while resolving stable identity.
  Fixed-tree release: Authored structure rule where one released fixed prop can
    release higher parts in the same tree group.
  Hot SoA fields: Parallel component arrays used by per-body stage kernels so
    eight adjacent bodies can be loaded without gathering from records.

Invariants:
  - Runtime cold records and hot arrays stay in scene/model slot order.
  - Standalone-created rows stay dense; handles map allocator slots to the
    current dense row.
  - Public body handles are allocator-owned identities; model-order arrays use
    explicit maps instead of encoding model index inside the handle.
  - Store refreshes load descriptor rows into physics-owned cold records and
    hot arrays before a step or explicit editor/replay commit.
  - Every hot component array starts on a 32-byte boundary and has exactly the
    same live dense prefix as the cold metadata rows.
  - Steady-frame pose, velocity, and sleep state do not copy back to authoring data;
    readers must use the body, collider, render, or diagnostics stores.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.cpp
  - SkullbonezSource/Physics/PhysicsEngine.h
  - Agentic/Reports/2026-07-11/physics-authority-and-identity-closure-review.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "PhysicsHandles.h"
#include "PhysicsTerrainView.h"
#include "../Core/SceneCapacity.h"
#include "PhysicsFixedList.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"
#include "../Core/Common.h"

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
}
namespace Physics
{
class ColliderStore;
struct PhysicsBodyCreateDesc;
struct PhysicsWorldForces;

// Describes one fixed-tree release source. Solver and external-force code pass this
// value to the store so released parts inherit deterministic seed velocities.
struct PhysicsFixedTreeReleaseEvent
{
    int sourceIndex = -1;                                                                       // Body row whose release triggers same-tree propagation.
    Math::Vector::Vector3 seedLinearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 seedAngularVelocity = Math::Vector::ZERO_VECTOR;
};

struct PhysicsBodyRecord
{
    PhysicsBodyHandle handle;                                                                   // Stable body handle resolved through the store maps.
    PhysicsSceneObjectId sceneObjectId;                                                         // Scene-local id supplied once by the creation owner.
    // Invariant: the retired duplicate replay-id scalar used to occupy these
    // four bytes. Keep the following vector block on its proven 16-byte
    // boundary without restoring a second identity authority.
    uint32_t vectorAlignmentPadding = 0;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 pendingImpulse = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 pendingImpulseApplicationPoint = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;                                                                          // Authoring mass; fixed bodies still report mass.
    float volume = 0.0f;                                                                        // Cached body volume used by buoyancy force math.
    float projectedSurfaceArea = 0.0f;                                                          // Cached drag area used by world-force integration.
    float dragCoefficient = 0.0f;                                                               // Cached drag coefficient used by world-force integration.
    float submergedVolumePercent = 0.0f;                                                        // Targeted water snapshot for underwater sleep gates.
    float contactReleaseImpulseThreshold = 1.0f;                                                // Minimum contact impulse before authored fixed props release.
    float angularVelocityLimit = 5.0f;                                                          // Per-body spin cap applied before force integration.
    float contactEpsilon = 0.05f;                                                               // Terrain proximity tolerance used by buoyancy support damping.
    int fixedTreeReleaseRootIndex = -1;                                                         // Authored release group root; -1 means no fixed-tree group.
    bool usesWorldInertia = false;                                                              // Non-sphere bodies rotate inertia through orientation.
    bool releasesFromFixedOnContact = false;                                                    // Authored fixed prop can become dynamic after strong contact.
    bool hasPendingImpulse = false;                                                             // One-shot impulse waiting for the next body integration pass.
};

static_assert( offsetof( PhysicsBodyRecord, rotationalInertia ) == 16u,
               "PhysicsBodyRecord vector metadata must retain its 16-byte boundary" );

// Plain one-row value used only at cold creation/restore boundaries and inside
// scalar kernels. Live storage remains the component arrays below.
struct PhysicsBodyHotState
{
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 inverseRotationalInertia = Math::Vector::ZERO_VECTOR;
    float inverseMass = 0.0f;
    float boundingRadius = 0.0f;
    bool fixed = false;
    bool awake = true;
};

struct PhysicsBodyCreateRecord
{
    PhysicsBodyRecord cold;
    PhysicsBodyHotState hot;
};

struct PhysicsBodyRestoreState
{
    // Concept: one Physics-owned body state crosses cold replay/validation
    // restore boundaries. It carries identity and values only—never a Replay
    // type, mutable store, or callback—and is consumed synchronously.
    PhysicsBodyHandle body;
    PhysicsSceneObjectId sceneObjectId;
    bool fixed = false;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;
    float inverseMass = 0.0f;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 inverseRotationalInertia = Math::Vector::ZERO_VECTOR;
};

using PhysicsBodyRecordList = PhysicsFixedList<PhysicsBodyRecord, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using PhysicsBodyHandleList = PhysicsFixedList<PhysicsBodyHandle, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using PhysicsHandleGenerationList = PhysicsFixedList<uint32_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using PhysicsHandleFlagList = PhysicsFixedList<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using PhysicsHandleModelIndexList = PhysicsFixedList<int, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using PhysicsHandleSceneObjectIdList = PhysicsFixedList<PhysicsSceneObjectId,
                                                        SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using PhysicsHandleSlotList = PhysicsFixedList<uint32_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using PhysicsHandleAssignmentMask = PhysicsFixedList<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using PhysicsBodyIndexList = PhysicsFixedList<int, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;

// Borrowed hot-field spans keep stage inputs explicit and prevent kernels from
// reaching unrelated cold authoring state. They are the only live hot-state
// authority; PhysicsBodyRecord contains cold metadata only.
struct PhysicsBodyHotFieldsConstView
{
    std::span<const float> positionX;
    std::span<const float> positionY;
    std::span<const float> positionZ;
    std::span<const float> orientationX;
    std::span<const float> orientationY;
    std::span<const float> orientationZ;
    std::span<const float> orientationW;
    std::span<const float> linearVelocityX;
    std::span<const float> linearVelocityY;
    std::span<const float> linearVelocityZ;
    std::span<const float> angularVelocityX;
    std::span<const float> angularVelocityY;
    std::span<const float> angularVelocityZ;
    std::span<const float> inverseMass;
    std::span<const float> inverseInertiaX;
    std::span<const float> inverseInertiaY;
    std::span<const float> inverseInertiaZ;
    std::span<const float> boundingRadius;
    std::span<const uint8_t> fixed;
    std::span<const uint8_t> awake;
};

struct PhysicsBodyHotFieldsView
{
    std::span<float> positionX;
    std::span<float> positionY;
    std::span<float> positionZ;
    std::span<float> orientationX;
    std::span<float> orientationY;
    std::span<float> orientationZ;
    std::span<float> orientationW;
    std::span<float> linearVelocityX;
    std::span<float> linearVelocityY;
    std::span<float> linearVelocityZ;
    std::span<float> angularVelocityX;
    std::span<float> angularVelocityY;
    std::span<float> angularVelocityZ;
    std::span<float> inverseMass;
    std::span<float> inverseInertiaX;
    std::span<float> inverseInertiaY;
    std::span<float> inverseInertiaZ;
    std::span<float> boundingRadius;
    std::span<uint8_t> fixed;
    std::span<uint8_t> awake;
};

// Why: each view aggregates 20 spans. Hot helpers borrow the aggregate by
// reference so nested scalar calls do not copy hundreds of bytes per body or
// candidate pair; the spans still retain the store-owned lifetime.
inline PhysicsBodyHotFieldsConstView ConstPhysicsBodyHotFields( const PhysicsBodyHotFieldsView& fields )
{
    return { fields.positionX,
             fields.positionY,
             fields.positionZ,
             fields.orientationX,
             fields.orientationY,
             fields.orientationZ,
             fields.orientationW,
             fields.linearVelocityX,
             fields.linearVelocityY,
             fields.linearVelocityZ,
             fields.angularVelocityX,
             fields.angularVelocityY,
             fields.angularVelocityZ,
             fields.inverseMass,
             fields.inverseInertiaX,
             fields.inverseInertiaY,
             fields.inverseInertiaZ,
             fields.boundingRadius,
             fields.fixed,
             fields.awake };
}

inline Math::Vector::Vector3 PhysicsBodyPosition( const PhysicsBodyHotFieldsConstView& fields, std::size_t index )
{
    return { fields.positionX[index], fields.positionY[index], fields.positionZ[index] };
}

inline Math::Vector::Vector3 PhysicsBodyLinearVelocity( const PhysicsBodyHotFieldsConstView& fields, std::size_t index )
{
    return { fields.linearVelocityX[index], fields.linearVelocityY[index], fields.linearVelocityZ[index] };
}

inline Math::Vector::Vector3 PhysicsBodyAngularVelocity( const PhysicsBodyHotFieldsConstView& fields,
                                                         std::size_t index )
{
    return { fields.angularVelocityX[index], fields.angularVelocityY[index], fields.angularVelocityZ[index] };
}

inline Math::Vector::Vector3 PhysicsBodyInverseInertia( const PhysicsBodyHotFieldsConstView& fields, std::size_t index )
{
    return { fields.inverseInertiaX[index], fields.inverseInertiaY[index], fields.inverseInertiaZ[index] };
}

inline Math::Orientation::Quaternion PhysicsBodyOrientation( const PhysicsBodyHotFieldsConstView& fields,
                                                             std::size_t index )
{
    return { fields.orientationX[index],
             fields.orientationY[index],
             fields.orientationZ[index],
             fields.orientationW[index] };
}

inline PhysicsBodyHotState LoadPhysicsBodyHotState( const PhysicsBodyHotFieldsConstView& fields, std::size_t index )
{
    PhysicsBodyHotState state;
    state.position = Math::Vector::Vector3( fields.positionX[index], fields.positionY[index], fields.positionZ[index] );
    state.orientation = Math::Orientation::Quaternion( fields.orientationX[index],
                                                       fields.orientationY[index],
                                                       fields.orientationZ[index],
                                                       fields.orientationW[index] );
    state.linearVelocity = Math::Vector::Vector3( fields.linearVelocityX[index],
                                                  fields.linearVelocityY[index],
                                                  fields.linearVelocityZ[index] );
    state.angularVelocity = Math::Vector::Vector3( fields.angularVelocityX[index],
                                                   fields.angularVelocityY[index],
                                                   fields.angularVelocityZ[index] );
    state.inverseRotationalInertia = Math::Vector::Vector3( fields.inverseInertiaX[index],
                                                            fields.inverseInertiaY[index],
                                                            fields.inverseInertiaZ[index] );
    state.inverseMass = fields.inverseMass[index];
    state.boundingRadius = fields.boundingRadius[index];
    state.fixed = fields.fixed[index] != 0u;
    state.awake = fields.awake[index] != 0u;
    return state;
}

inline PhysicsBodyHotState LoadPhysicsBodyHotState( const PhysicsBodyHotFieldsView& fields, std::size_t index )
{
    return LoadPhysicsBodyHotState( ConstPhysicsBodyHotFields( fields ), index );
}

inline void
StorePhysicsBodyHotState( const PhysicsBodyHotFieldsView& fields, std::size_t index, const PhysicsBodyHotState& state )
{
    fields.positionX[index] = state.position.x;
    fields.positionY[index] = state.position.y;
    fields.positionZ[index] = state.position.z;
    state.orientation.GetComponents( fields.orientationX[index],
                                     fields.orientationY[index],
                                     fields.orientationZ[index],
                                     fields.orientationW[index] );
    fields.linearVelocityX[index] = state.linearVelocity.x;
    fields.linearVelocityY[index] = state.linearVelocity.y;
    fields.linearVelocityZ[index] = state.linearVelocity.z;
    fields.angularVelocityX[index] = state.angularVelocity.x;
    fields.angularVelocityY[index] = state.angularVelocity.y;
    fields.angularVelocityZ[index] = state.angularVelocity.z;
    fields.inverseMass[index] = state.inverseMass;
    fields.inverseInertiaX[index] = state.inverseRotationalInertia.x;
    fields.inverseInertiaY[index] = state.inverseRotationalInertia.y;
    fields.inverseInertiaZ[index] = state.inverseRotationalInertia.z;
    fields.boundingRadius[index] = state.boundingRadius;
    fields.fixed[index] = state.fixed ? 1u : 0u;
    fields.awake[index] = state.awake ? 1u : 0u;
}

#ifdef _MSC_VER
// Why: the deliberate 32-byte array starts add harmless intra-object padding.
// MSVC's C4324 is promoted by the repository warning policy even though this
// layout is the alignment contract being tested, so suppress only this class.
#pragma warning( push )
#pragma warning( disable : 4324 )
#endif
class PhysicsBodyStore
{
  public:
    PhysicsBodyStore();

    void Clear();
    // Cold topology repair imports descriptor rows produced by the collection
    // owner. The store preserves handle-keyed one-shot state while replacing
    // model-order records from those explicit values.
    void LoadFromDescriptors( std::span<const PhysicsBodyCreateDesc> bodyDescs, std::span<const uint8_t> sleepStates );
    // Creates a physics-owned body row from descriptor data. The store
    // assigns the handle and keeps the row dense; callers supply authored state.
    PhysicsBodyHandle CreateBodyRecord( const PhysicsBodyCreateRecord& record );
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
    // Restores sampled replay values into the authoritative hot-field arrays.
    // The scene id must match so stale handles cannot mutate a reused body slot.
    bool RestoreReplayBodyState( const PhysicsBodyRestoreState& restore );
    void RefreshRecordFromDescriptorAt( const PhysicsBodyCreateDesc& desc, int modelIndex );
    void CopySleepStatesFrom( std::span<const uint8_t> sleepStates );
    void CopySleepStatesTo( std::vector<uint8_t>& sleepStates ) const;
    // Cold descriptor refresh keeps scene identity with the body store. Scene
    // owners supply only the row count; missing rows receive fresh store-scanned ids.
    std::vector<PhysicsSceneObjectId> BuildSceneObjectIdsForReload( int sceneEntityCount ) const;
    // Converts an authored fixed body row into a dynamic body without a
    // descriptor reload. Release-on-impact paths call the store by dense row.
    bool ReleaseFixedBody( int modelIndex,
                           const Math::Vector::Vector3& seedLinearVelocity,
                           const Math::Vector::Vector3& seedAngularVelocity );
    // Releases higher same-tree fixed parts using release-group metadata already
    // copied into body rows. outReleasedBodyIndices is caller-owned scratch.
    void ReleaseAttachedFixedTreeParts( const PhysicsFixedTreeReleaseEvent& event,
                                        PhysicsBodyIndexList& outReleasedBodyIndices );

    const PhysicsBodyRecord* Data() const;
    int Count() const;
    bool Empty() const;
    PhysicsBodyHandle HandleForModelIndex( int modelIndex ) const;
    // Resolves stable scene identity to the live body handle. modelIndexHint
    // is a fast path only; stale hints fall back to the handle identity table.
    PhysicsBodyHandle HandleForSceneObjectId( PhysicsSceneObjectId sceneObjectId, int modelIndexHint = -1 ) const;
    // Resolves a stable body handle to the current dense row and refreshes the
    // caller-owned cache. Returns -1 and invalidates the hint for stale handles.
    int ResolveModelRow( PhysicsBodyHandle handle, ModelRowHint& hint ) const;
    int ModelIndexForHandle( PhysicsBodyHandle handle ) const;
    bool Contains( PhysicsBodyHandle handle ) const;
    // Lifetime: these spans borrow the store's live dense prefix and must not be
    // retained across scene mutation, compaction, or store destruction.
    std::span<const PhysicsBodyRecord> Records() const;
    std::span<PhysicsBodyRecord> MutableRecords();
    // Lifetime: hot spans borrow fixed store storage and remain valid until the
    // store is destroyed. Only the dense prefix up to Count() is exposed.
    PhysicsBodyHotFieldsConstView HotFields() const;
    PhysicsBodyHotFieldsView MutableHotFields();
    std::size_t RecordCapacity() const;
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
    bool ConsumePendingBodyImpulse( int modelIndex );
    // Advances one mutable body record from its current velocities and shape
    // snapshot. Returns false when the slot is fixed, sleeping, missing, or has
    // no positive time to integrate.
    bool IntegrateBodyPose( Core::Profiler* profiler,
                            const ColliderStore& colliderStore,
                            const PhysicsTerrainView& terrain,
                            int modelIndex,
                            float deltaSeconds );
    bool ApplyForces( const PhysicsWorldForces& worldForces,
                      const ColliderStore& colliderStore,
                      const PhysicsTerrainView& terrain,
                      int modelIndex,
                      float deltaSeconds,
                      const Math::Vector::Vector3* precomputedMutualGravityForce = nullptr );

  private:
    PhysicsBodyHandle ResolveHandleForModelIndex( int modelIndex,
                                                  PhysicsSceneObjectId sceneObjectId,
                                                  PhysicsHandleAssignmentMask& assignedHandleSlots );
    void RetireUnassignedHandles( const PhysicsHandleAssignmentMask& assignedHandleSlots );
    void ClearHotFields();
    void ResizeHotFields( std::size_t count );
    PhysicsBodyHotState HotStateForModelIndex( int modelIndex ) const;
    void StoreHotStateAt( int modelIndex, const PhysicsBodyHotState& state );

    PhysicsBodyRecordList m_bodies { "PhysicsBodyStore.bodies" };                               // Cold records in dense scene/model order.
    alignas( 32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_positionX {
        "PhysicsBodyStore.positionX" };
    alignas( 32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_positionY {
        "PhysicsBodyStore.positionY" };
    alignas( 32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_positionZ {
        "PhysicsBodyStore.positionZ" };
    alignas( 32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_orientationX {
        "PhysicsBodyStore.orientationX" };
    alignas( 32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_orientationY {
        "PhysicsBodyStore.orientationY" };
    alignas( 32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_orientationZ {
        "PhysicsBodyStore.orientationZ" };
    alignas( 32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_orientationW {
        "PhysicsBodyStore.orientationW" };
    alignas(
        32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_linearVelocityX {
        "PhysicsBodyStore.linearVelocityX" };
    alignas(
        32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_linearVelocityY {
        "PhysicsBodyStore.linearVelocityY" };
    alignas(
        32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_linearVelocityZ {
        "PhysicsBodyStore.linearVelocityZ" };
    alignas(
        32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_angularVelocityX {
        "PhysicsBodyStore.angularVelocityX" };
    alignas(
        32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_angularVelocityY {
        "PhysicsBodyStore.angularVelocityY" };
    alignas(
        32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_angularVelocityZ {
        "PhysicsBodyStore.angularVelocityZ" };
    alignas( 32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_inverseMass {
        "PhysicsBodyStore.inverseMass" };
    alignas(
        32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_inverseInertiaX {
        "PhysicsBodyStore.inverseInertiaX" };
    alignas(
        32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_inverseInertiaY {
        "PhysicsBodyStore.inverseInertiaY" };
    alignas(
        32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_inverseInertiaZ {
        "PhysicsBodyStore.inverseInertiaZ" };
    alignas( 32 ) mutable PhysicsFixedList<float, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_boundingRadius {
        "PhysicsBodyStore.boundingRadius" };
    alignas( 32 ) mutable PhysicsHandleFlagList m_fixed { "PhysicsBodyStore.fixed" };
    alignas( 32 ) mutable PhysicsHandleFlagList m_awake { "PhysicsBodyStore.awake" };
    PhysicsBodyHandleList m_modelBodyHandles { "PhysicsBodyStore.modelBodyHandles" };           // Model index to body handle map.
    PhysicsHandleGenerationList m_handleGenerations {
        "PhysicsBodyStore.handleGenerations" };                                                 // Handle-slot generations.
    PhysicsHandleFlagList m_handleAlive { "PhysicsBodyStore.handleAlive" };                     // Live handle slot flags.
    PhysicsHandleModelIndexList m_handleModelIndices { "PhysicsBodyStore.handleModelIndices" }; // Slot to model index.
    PhysicsHandleSceneObjectIdList m_handleSceneObjectIds {
        "PhysicsBodyStore.handleSceneObjectIds" };                                              // Slot scene ids.
    PhysicsHandleSlotList m_freeHandleSlots { "PhysicsBodyStore.freeHandleSlots" };             // Retired reusable slots.
    // Runtime allocation policy: topology repair reuses this handle-slot mask
    // instead of constructing a heap-backed standard-library container.
    PhysicsHandleAssignmentMask m_assignedHandleScratch { "PhysicsBodyStore.assignedHandleScratch" };
};
#ifdef _MSC_VER
#pragma warning( pop )
#endif
} // namespace Physics
} // namespace SkullbonezCore
