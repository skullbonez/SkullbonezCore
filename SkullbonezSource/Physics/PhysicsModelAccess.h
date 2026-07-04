/*
File: SkullbonezSource/Physics/PhysicsModelAccess.h
Purpose:
  Defines the model-owner access facade between physics stores and GameModel storage.

Mental model:
  Physics hot paths use body, collider, and render stores. GameModelCollection
  still owns model-order authoring and presentation state. This concrete facade
  names the commands and queries physics may ask of the model owner without
  making the collection inherit a physics callback interface.

Glossary:
  Model-owner access: Narrow command/query facade over GameModelCollection
    state that physics stores still need for model-order sync.
  Body stream: SoA-backed read-only body data used by hot physics loops.
  SoA (Structure of Arrays): Cache layout that stores each field in a separate
    contiguous array for faster iteration.
  Model-order command: Owner-side operation that applies a body/collider/render
    store update across the current deterministic model order.
  Model-owner event command: Owner-applied side effect requested after physics
    has finished writing compact solver results.
  Physics diagnostics view: Borrowed read-only retained solver/debug state used
    by SkullScope without giving diagnostics ownership of scene storage.
  Diagnostics model record: Debug-only value record that SkullScope/CSV streams
    serialize without borrowing a GameModel range.

Invariants:
  - GameModelCollection owns the underlying model storage and SoA cache.
  - Callers must not cache model-owner references after the operation that
    requested them.
  - Mutations performed through model-owner commands must explicitly call
    InvalidatePhysicsStreams() before a later stream read can observe stale SoA data.
  - Debug diagnostics records may contain borrowed string pointers that are
    valid only for the current emission pass.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - SkullbonezSource/Physics/PhysicsScene.h
  - Agentic/Plans/physics-game-model-authority-plan.md
*/
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "../Maths/Vector3.h"
#include "../GameObjects/GameModelSoACache.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
class GameModelCollection;
} // namespace GameObjects

namespace Rendering
{
class RenderInstanceStore;
} // namespace Rendering

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;

struct PhysicsFixedTreeReleaseEvent
{
    int sourceIndex = -1;
    Math::Vector::Vector3 seedLinearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 seedAngularVelocity = Math::Vector::ZERO_VECTOR;
};

class PhysicsModelAccess
{
  public:
    explicit PhysicsModelAccess( GameObjects::GameModelCollection& collection );

    int ModelCount() const;
    GameObjects::GameModelBodyStream GetPhysicsBodyStream();
    void InvalidatePhysicsStreams();
    // Named sync commands for legacy GameModel readers that still sit downstream
    // of store-owned physics mutations. This keeps model-order work with the
    // model owner instead of reopening raw ranges in solver code.
    void WriteBackPhysicsBodies( const PhysicsBodyStore& bodyStore );
    void WriteBackPhysicsBody( const PhysicsBodyStore& bodyStore, int modelIndex );
    // Reloads body records after legacy model-owned event commands mutate model
    // state. Prefer store-owned commands, such as the fixed-tree release overload
    // below, when the caller already owns PhysicsBodyStore.
    void ReloadPhysicsBodies( PhysicsBodyStore& bodyStore, const std::vector<uint8_t>& sleepStates );
    // Store refreshes still read model-owned authoring/presentation state. Body
    // and collider stores provide physics-owned pose/shape for render records;
    // GameModel supplies material and feedback alpha until rendering owns them.
    // Captures one editor/replay-edited body into PhysicsBodyStore without
    // forcing every render refresh to reload the full model mirror.
    void RefreshPhysicsBodyFromModel( PhysicsBodyStore& bodyStore, int modelIndex );
    void RefreshPhysicsColliders( ColliderStore& colliderStore, const PhysicsBodyStore& bodyStore );
    void RefreshRenderInstances( Rendering::RenderInstanceStore& renderInstanceStore,
                                 const PhysicsBodyStore& bodyStore,
                                 const ColliderStore& colliderStore );
    void NotifyFixedContact( int modelIndex, float highlightSeconds );
    // Ticks presentation timers for contact feedback in model order. Physics
    // supplies the active body count; the model owner clamps to live storage.
    void TickContactHighlights( int modelCount, float deltaSeconds );
    // Legacy model-owned release command retained for non-step callers that do
    // not yet own a current PhysicsBodyStore.
    void ReleaseAttachedFixedTreeParts( const PhysicsFixedTreeReleaseEvent& event );
    // Applies fixed-tree release to the caller-owned body store and returns the
    // dense model indices that need wake propagation. The caller supplies the
    // output vector so repeated release events do not allocate during the step.
    void ReleaseAttachedFixedTreeParts( PhysicsBodyStore& bodyStore,
                                        const PhysicsFixedTreeReleaseEvent& event,
                                        std::vector<int>& outReleasedBodyIndices );
#ifdef _DEBUG
    bool TryGetPhysicsDiagnosticsModelName( int index, const char*& outName ) const;
    // Compatibility owner: GameModelCollection presentation names.
    // Reason: Debug diagnostics still print scene names while pose, motion,
    // mass, and shape facts are sampled from physics-owned stores.
    // Deletion condition: diagnostics rows identify bodies by scene object id
    // or a presentation-name table owned outside GameModel. Checker budget:
    // Phase 8 diagnostics guardrails keep state reads out of this edge.
    void FillPhysicsDiagnosticsNames( int bodyCount, std::vector<const char*>& outNames ) const;
#endif

    std::size_t size() const
    {
        return static_cast<std::size_t>( ModelCount() );
    }

    int Count() const
    {
        return ModelCount();
    }

    GameObjects::GameModelBodyStream GetBodyStream()
    {
        return GetPhysicsBodyStream();
    }

  private:
    GameObjects::GameModelCollection& m_collection;
};
} // namespace Physics
} // namespace SkullbonezCore
