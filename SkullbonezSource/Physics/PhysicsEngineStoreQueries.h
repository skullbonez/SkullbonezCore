/*
File: SkullbonezSource/Physics/PhysicsEngineStoreQueries.h
Purpose:
  Names the bounded internal query path for legacy dense-store readers.

Mental model:
  PhysicsEngine is the public facade and no longer returns solver containers
  directly. Runtime/editor/replay code that still needs dense store scans must
  opt into this physics-owned internal surface until each scan has a narrower
  handle or view query.

Glossary:
  Dense store: Physics-owned compact row storage used by solver and runtime
    presentation readers.
  Internal query: Named physics owner surface for existing non-facade readers.

Invariants:
  - Do not include this header from PhysicsApi.h or PhysicsEngine.h.
  - Store references are borrowed for the immediate frame/tool operation only.
  - New call sites should prefer handle queries or immutable public views when
    they do not require dense row scans.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.h
  - Agentic/Plans/TODO/physics-authority-and-identity.md
*/
#pragma once

#include "PhysicsEngine.h"

namespace SkullbonezCore
{
namespace Physics
{
// Owner: Physics / Runtime API. Reason: existing renderer, editor, and replay
// readers still scan deterministic store rows after the public PhysicsEngine
// facade stops returning containers. Deletion condition: every caller moves to
// handle-specific queries or immutable public views. Checker budget: this header
// is the only non-test place outside PhysicsEngine.cpp that may expose these
// PhysicsEngine-owned store references during Plan 14.
class PhysicsEngineStoreQueries
{
  public:
    static const PhysicsBodyStore& BodyStore( const PhysicsEngine& engine )
    {
        return engine.m_scene.BodyStore();
    }

    static const ColliderStore& Colliders( const PhysicsEngine& engine )
    {
        return engine.m_scene.Colliders();
    }

    static const Math::CollisionDetection::SpatialGrid& SpatialGrid( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetSpatialGrid();
    }

    static const std::vector<int>& FixedContactHighlightBodies( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetFixedContactHighlightBodies();
    }

    static const std::vector<int64_t>& CollisionCellKeys( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetCollisionCellKeys();
    }

    static const std::vector<uint8_t>& CollisionVisualContacts( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetCollisionVisualContacts();
    }

    static const std::vector<uint8_t>& SleepStates( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetSleepStates();
    }

    static const std::vector<int>& SleepIslandVisualIds( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetSleepIslandVisualIds();
    }

    static const std::vector<uint8_t>& SleepSupportedStates( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetSleepSupportedStates();
    }

    static const std::vector<uint8_t>& SleepInhibitedStates( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetSleepInhibitedStates();
    }

    static const std::vector<PhysicsDebugContact>& DebugContacts( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetPhysicsDebugContacts();
    }

    static const std::vector<PhysicsPipelineRecord>& PipelineTrace( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetPhysicsPipelineTrace();
    }

    static const std::vector<PointJointConstraint>& PointJointConstraints( const PhysicsEngine& engine )
    {
        return engine.m_scene.GetPointJointConstraints();
    }
};
} // namespace Physics
} // namespace SkullbonezCore
