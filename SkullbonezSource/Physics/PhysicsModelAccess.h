/*
File: SkullbonezSource/Physics/PhysicsModelAccess.h
Purpose:
  Defines the model-owner access contract between physics stores and GameModel storage.

Mental model:
  Physics hot paths use body, collider, and render stores. GameModelCollection
  still owns model-order authoring and presentation state. This interface names
  the commands and queries physics may ask of the model owner without exposing
  the raw model vector to scene/solver code.

Glossary:
  Model-owner access: Narrow command/query interface over GameModelCollection
    state that physics stores still need for model-order sync.
  Body stream: SoA-backed read-only body data used by hot physics loops.
  SoA (Structure of Arrays): Cache layout that stores each field in a separate
    contiguous array for faster iteration.
  Model-order command: Owner-side operation that applies a body/collider/render
    store update across the current deterministic model order.
  Physics body event sink: Explicit side-effect boundary for solver-triggered
    gameplay reactions such as fixed-tree release.
  Physics diagnostics view: Borrowed read-only retained solver/debug state used
    by SkullScope without giving diagnostics ownership of scene storage.
  Diagnostics model record: Debug-only value record that SkullScope/CSV streams
    serialize without borrowing a GameModel range.

Invariants:
  - Implementations own the underlying model storage and SoA cache.
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

class PhysicsBodyEventSink
{
  public:
    virtual ~PhysicsBodyEventSink() = default;

    virtual void NotifyFixedContact( int modelIndex, float highlightSeconds ) = 0;
    // Ticks presentation timers for contact feedback in model order. Physics
    // supplies the active body count; the model owner clamps to live storage.
    virtual void TickContactHighlights( int modelCount, float deltaSeconds ) = 0;
    virtual void ReleaseAttachedFixedTreeParts( const PhysicsFixedTreeReleaseEvent& event ) = 0;
};

class PhysicsBodyWritebackSink
{
  public:
    virtual ~PhysicsBodyWritebackSink() = default;

    // Owner: the model collection while legacy readers still mirror solved
    // body state. Delete this sink when render, replay, and diagnostics consume
    // physics-owned body rows directly and the boundary checker can forbid
    // single-body GameModel writeback.
    virtual void WriteBackPhysicsBody( const PhysicsBodyStore& bodyStore, int modelIndex ) = 0;
};

class PhysicsModelAccess : public PhysicsBodyWritebackSink
{
  public:
    virtual ~PhysicsModelAccess() = default;

    virtual int ModelCount() const = 0;
    virtual GameObjects::GameModelBodyStream GetPhysicsBodyStream() = 0;
    virtual void InvalidatePhysicsStreams() = 0;
    // Named sync commands for legacy GameModel readers that still sit downstream
    // of store-owned physics mutations. This keeps model-order work with the
    // model owner instead of reopening raw ranges in solver code.
    virtual void WriteBackPhysicsBodies( const PhysicsBodyStore& bodyStore ) = 0;
    // Reloads body records after model-owned event sinks mutate model state,
    // such as fixed-tree release. Callers still own stream invalidation
    // when a later SoA read must observe those model writes.
    virtual void ReloadPhysicsBodies( PhysicsBodyStore& bodyStore, const std::vector<uint8_t>& sleepStates ) = 0;
    // Store refreshes still read model-owned authoring/presentation state. Keep
    // that access with the model owner until those stores have their own
    // construction-time inputs.
    virtual void RefreshPhysicsColliders( ColliderStore& colliderStore, const PhysicsBodyStore& bodyStore ) = 0;
    virtual void RefreshRenderInstances( Rendering::RenderInstanceStore& renderInstanceStore ) = 0;
    virtual PhysicsBodyEventSink& BodyEvents() = 0;
    virtual PhysicsDiagnosticsView GetPhysicsDiagnosticsView() const = 0;
#ifdef _DEBUG
    // Lifetime: string pointers in the returned record are borrowed from the
    // model owner and must not be cached after the current diagnostics write.
    // False means the requested index is outside the dense model order.
    virtual bool TryGetPhysicsDiagnosticsModel( int index, PhysicsDiagnosticsModelRecord& outRecord ) const = 0;
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
};
} // namespace Physics
} // namespace SkullbonezCore
