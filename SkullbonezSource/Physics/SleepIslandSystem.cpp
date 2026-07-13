/*
File: SkullbonezSource/Physics/SleepIslandSystem.cpp
Purpose:
  Groups supported bodies into sleep islands and decides when islands may sleep.

Summary:
  SleepIslandSystem.cpp groups supported bodies into sleep islands and decides
  when islands may sleep. As an implementation unit, keep edits anchored on
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
  - SkullbonezSource/Physics/SleepIslandSystem.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SleepIslandSystem.h"

#include "PhysicsBodyStore.h"
#include "PhysicsWorld.h"

using namespace SkullbonezCore::Physics;


void SleepIslandSystem::PropagateSupport( SleepSupportPropagationContext& context,
                                          std::span<const PhysicsBodyRecord> bodyRecords )
{
    // Concept: support propagates upward through a stack.
    //
    // If object B is resting on object A, and A is fixed/asleep/supported, then
    // B can be considered supported too. Repeating that rule lets a whole tower
    // become one stable sleep island instead of requiring every object to touch
    // terrain directly.
    auto& m_sleepState = context.sleepState;
    auto& m_sleepSupportEdges = context.sleepSupportEdges;
    auto& m_sleepSupportedThisFrame = context.sleepSupportedThisFrame;

    const int modelCount = static_cast<int>( bodyRecords.size() );
    if ( modelCount <= 0 || m_sleepSupportEdges.empty() )
    {
        return;
    }

    for ( int pass = 0; pass < modelCount; ++pass )
    {
        // This is a bounded fixed-point solve over support edges. At most
        // modelCount passes are needed because each successful pass marks at
        // least one additional body supported; early exit keeps the normal case
        // cheap.
        bool changed = false;
        for ( const auto& edge : m_sleepSupportEdges )
        {
            const int supporter = edge.first;
            const int supported = edge.second;
            if ( supporter < 0 || supporter >= modelCount || supported < 0 || supported >= modelCount )
            {
                continue;
            }

            bool supporterHasSupport = m_sleepSupportedThisFrame[supporter] != 0;
            if ( !supporterHasSupport && bodyRecords[static_cast<std::size_t>( supporter )].isFixed )
            {
                supporterHasSupport = true;
            }
            if ( !supporterHasSupport && supporter < static_cast<int>( m_sleepState.size() ) &&
                 m_sleepState[supporter] != 0 )
            {
                supporterHasSupport = true;
            }

            if ( supporterHasSupport && m_sleepSupportedThisFrame[supported] == 0 )
            {
                m_sleepSupportedThisFrame[supported] = 1;
                changed = true;
            }
        }

        if ( !changed )
        {
            break;
        }
    }
}
