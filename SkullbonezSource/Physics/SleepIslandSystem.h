/*
File: SkullbonezSource/Physics/SleepIslandSystem.h
Purpose:
  Propagates fixed or sleeping support through the bounded sleep-support graph.

Summary:
  SleepIslandSystem performs a deterministic bounded propagation over
  frame-owned support edges. It marks supported bodies but retains no sleep
  state and does not choose island transitions.

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
  - Agentic/Reference/engine-glossary.md
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
