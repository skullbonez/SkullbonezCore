/*
File: SkullbonezSource/Physics/SleepIslandSystem.h
Purpose:
  Owns persistent simulation-island contact topology and support diagnostics.

Summary:
  SimulationIslandSystem retains canonical active contact edges across fixed
  steps and publishes the exact bodies touched by topology changes. Diagnostic
  support propagation remains a separate stateless operation in this owner.

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
#include <tuple>
#include <utility>

namespace SkullbonezCore
{
namespace Physics
{
struct PhysicsBodyRecord;
struct PersistentContact;
struct PointJointConstraint;

constexpr std::size_t MAX_SLEEP_SUPPORT_EDGES = static_cast<std::size_t>( Scene::Capacity::MAX_SCENE_OBJECTS ) * 4u;

// Fatal invariant: support edges are hot solver output. Every producer uses this one
// fail-before-grow boundary so contact density or point joints cannot escape
// the scene-load commit and allocate during steady gameplay.
void AppendSleepSupportEdge( PhysicsCandidatePairList& edges, int supporter, int supported );
void ValidateSleepSupportEdgeCount( std::size_t requested, std::size_t reservedCapacity, std::size_t highWater,
                                    const char* phase );

struct SimulationIslandJointEdge
{
    uint32_t handleIndex = 0u;
    uint32_t handleGeneration = 0u;
    int bodyA = -1;
    int bodyB = -1;
    uint64_t descriptorHash = 0u;
};

inline bool operator<( const SimulationIslandJointEdge& left, const SimulationIslandJointEdge& right )
{
    return std::tie( left.handleIndex, left.handleGeneration, left.bodyA, left.bodyB, left.descriptorHash ) <
           std::tie( right.handleIndex, right.handleGeneration, right.bodyA, right.bodyB, right.descriptorHash );
}

inline bool operator==( const SimulationIslandJointEdge& left, const SimulationIslandJointEdge& right )
{
    return left.handleIndex == right.handleIndex && left.handleGeneration == right.handleGeneration &&
           left.bodyA == right.bodyA && left.bodyB == right.bodyB && left.descriptorHash == right.descriptorHash;
}

class SimulationIslandSystem
{
  private:
    PhysicsCandidatePairList m_previousContactEdges { "SimulationIslandSystem.previousContactEdges",
                                                      PhysicsCapacityReason::CandidatePairs };
    PhysicsCandidatePairList m_activeContactEdges { "SimulationIslandSystem.activeContactEdges",
                                                    PhysicsCapacityReason::CandidatePairs };
    PhysicsBodyRowList<SimulationIslandJointEdge> m_previousJointEdges { "SimulationIslandSystem.previousJointEdges",
                                                                         PhysicsCapacityReason::PointJoints };
    PhysicsBodyRowList<SimulationIslandJointEdge> m_activeJointEdges { "SimulationIslandSystem.activeJointEdges",
                                                                       PhysicsCapacityReason::PointJoints };
    PhysicsBodyRowList<uint8_t> m_previousStaticContacts { "SimulationIslandSystem.previousStaticContacts",
                                                           PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_activeStaticContacts { "SimulationIslandSystem.activeStaticContacts",
                                                         PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_topologyChangedBodies { "SimulationIslandSystem.topologyChangedBodies",
                                                          PhysicsCapacityReason::SceneBodies };
    bool m_needsSeed = true;

  public:
    void Reserve( std::size_t bodyCapacity, std::size_t contactEdgeCapacity, std::size_t pointJointCapacity );
    void Clear();
    void Invalidate();
    void Rebuild( const PhysicsBodyStore& bodyStore, std::span<const PersistentContact> persistentContacts,
                  std::span<const PointJointConstraint> pointJoints, std::span<const uint8_t> sleepState );
    std::span<const std::pair<int, int>> ActiveContactEdges() const;
    std::span<const SimulationIslandJointEdge> ActiveJointEdges() const;
    std::span<const uint8_t> TopologyChangedBodies() const;
    uint64_t CollectDynamicMemoryBytes() const;
};

struct SleepSupportPropagationContext
{
    std::span<uint8_t> sleepState;
    std::span<const std::pair<int, int>> sleepSupportEdges;
    std::span<uint8_t> sleepSupportedThisFrame;
};

class SleepSupportPropagationSystem
{
  public:
    void PropagateSupport( SleepSupportPropagationContext& context, const PhysicsBodyHotFieldsConstView& hotFields );
};
} // namespace Physics
} // namespace SkullbonezCore
