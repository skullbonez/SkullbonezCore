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
struct PhysicsDiagnosticsModelRecord;
struct PhysicsDiagnosticsView;

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
    // Reloads body records after model-owned event commands mutate model state,
    // such as fixed-tree release. Callers still own stream invalidation when a
    // later SoA read must observe those model writes.
    void ReloadPhysicsBodies( PhysicsBodyStore& bodyStore, const std::vector<uint8_t>& sleepStates );
    // Store refreshes still read model-owned authoring/presentation state. Keep
    // that access with the model owner until those stores have their own
    // construction-time inputs.
    void RefreshPhysicsColliders( ColliderStore& colliderStore, const PhysicsBodyStore& bodyStore );
    void RefreshRenderInstances( Rendering::RenderInstanceStore& renderInstanceStore );
    void NotifyFixedContact( int modelIndex, float highlightSeconds );
    // Ticks presentation timers for contact feedback in model order. Physics
    // supplies the active body count; the model owner clamps to live storage.
    void TickContactHighlights( int modelCount, float deltaSeconds );
    void ReleaseAttachedFixedTreeParts( const PhysicsFixedTreeReleaseEvent& event );
    PhysicsDiagnosticsView GetPhysicsDiagnosticsView() const;
#ifdef _DEBUG
    // Lifetime: string pointers in the returned record are borrowed from the
    // model owner and must not be cached after the current diagnostics write.
    // False means the requested index is outside the dense model order.
    bool TryGetPhysicsDiagnosticsModel( int index, PhysicsDiagnosticsModelRecord& outRecord ) const;
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
