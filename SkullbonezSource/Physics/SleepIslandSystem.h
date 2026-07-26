/*
File: SkullbonezSource/Physics/SleepIslandSystem.h
Purpose:
  Groups supported bodies into sleep islands and decides when islands may sleep.

Summary:
  SleepIslandSystem.h groups supported bodies into sleep islands and decides
  when islands may sleep. As a public header, keep edits anchored on
  deterministic physics, diagnostics, or world-state flow and on the
  glossary/invariants below.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  Hot body fields: Physics-owned arrays holding fixed/sleep/velocity state for
    the current tick.
  Support edge budget: Fixed four-edges-per-body storage ceiling shared by
    contact and point-joint producers.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Support propagation reads fixed-body state from hot arrays, not directly
    from legacy model storage.
  - Support-edge producers fail before the scene-committed list can
    grow during steady gameplay.

Related:
  - SkullbonezSource/Physics/SleepIslandSystem.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "PhysicsBodyStore.h"
#include "PhysicsStageCapacity.h"
#include "../Core/SceneCapacity.h"

#include <cstddef>
#include <utility>

namespace SkullbonezCore
{
namespace Physics
{
struct PhysicsBodyRecord;

constexpr std::size_t MAX_SLEEP_SUPPORT_EDGES = static_cast<std::size_t>( Scene::Capacity::MAX_SCENE_OBJECTS ) * 4u;

// Lane F: support edges are hot solver output. Every producer uses this one
// fail-before-grow boundary so contact density or point joints cannot escape
// the scene-load commit and allocate during steady gameplay.
void AppendSleepSupportEdge( PhysicsCandidatePairList& edges, int supporter, int supported );
void ValidateSleepSupportEdgeCount( std::size_t requested, std::size_t reservedCapacity, std::size_t highWater,
                                    const char* phase );

struct SleepSupportPropagationContext
{
    std::span<uint8_t> sleepState;
    std::span<const std::pair<int, int>> sleepSupportEdges;
    std::span<uint8_t> sleepSupportedThisFrame;
};

class SleepIslandSystem
{
  public:
    void PropagateSupport( SleepSupportPropagationContext& context, const PhysicsBodyHotFieldsConstView& hotFields );
};
} // namespace Physics
} // namespace SkullbonezCore
