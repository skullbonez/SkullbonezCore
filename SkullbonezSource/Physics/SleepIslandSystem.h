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

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Support propagation reads fixed-body state from hot arrays, not directly
    from legacy model storage.

Related:
  - SkullbonezSource/Physics/SleepIslandSystem.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "PhysicsBodyStore.h"

#include <utility>
#include <vector>

namespace SkullbonezCore
{
namespace Physics
{
struct PhysicsBodyRecord;
struct SleepSupportPropagationContext
{
    std::span<uint8_t> sleepState;
    std::span<const std::pair<int, int>> sleepSupportEdges;
    std::span<uint8_t> sleepSupportedThisFrame;
};

class SleepIslandSystem
{
  public:
    void PropagateSupport( SleepSupportPropagationContext& context, PhysicsBodyHotFieldsConstView hotFields );
};
} // namespace Physics
} // namespace SkullbonezCore
