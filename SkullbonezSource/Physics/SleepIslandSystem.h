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
  Body record: Physics-owned snapshot of a body's fixed/sleep/velocity state for
    the current tick.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Support propagation reads fixed-body state from body records, not directly
    from legacy model storage.

Related:
  - SkullbonezSource/Physics/SleepIslandSystem.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "PhysicsBodyStore.h"

namespace SkullbonezCore
{
namespace Physics
{
struct PhysicsBodyRecord;
struct SleepSupportPropagationContext;

class SleepIslandSystem
{
  public:
    void PropagateSupport( SleepSupportPropagationContext& context, std::span<const PhysicsBodyRecord> bodyRecords );
};
} // namespace Physics
} // namespace SkullbonezCore
