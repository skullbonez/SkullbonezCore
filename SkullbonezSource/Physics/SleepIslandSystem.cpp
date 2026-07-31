/*
File: SkullbonezSource/Physics/SleepIslandSystem.cpp
Purpose:
  Groups supported bodies into sleep islands and decides when islands may sleep.

Summary:
  Groups supported bodies into sleep
  islands and decides when islands may sleep.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Support propagation reads fixed-body state from hot arrays, not directly
    from legacy model storage.
  - Support-edge producers fail before the construction-reserved vector can
    grow during steady gameplay.

Related:
  - SkullbonezSource/Physics/SleepIslandSystem.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "SleepIslandSystem.h"

#include "PhysicsBodyStore.h"
#include "PhysicsWorld.h"

using namespace SkullbonezCore::Physics;

void SleepIslandSystem::PropagateSupport( SleepSupportPropagationContext& context,
                                          const PhysicsBodyHotFieldsConstView& hotFields )
{

    // Concept: support propagates upward through a stack.
    //
    // If object B is resting on object A, and A is fixed/asleep/supported, then
    // B can be considered supported too. Repeating that rule lets a whole tower
    // become one stable sleep island instead of requiring every object to touch
    // terrain directly.
    // Lifetime: the three rows below borrow controller-owned spans only for
    // this bounded propagation pass; the system retains no sleep state.
    auto& sleepState = context.sleepState;
    auto& sleepSupportEdges = context.sleepSupportEdges;
    auto& sleepSupportedThisFrame = context.sleepSupportedThisFrame;

    const int modelCount = static_cast<int>( hotFields.fixed.size() );

    if ( modelCount <= 0 || sleepSupportEdges.empty() )
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

        for ( const auto& edge : sleepSupportEdges )
        {
            const int supporter = edge.first;
            const int supported = edge.second;

            if ( supporter < 0 || supporter >= modelCount || supported < 0 || supported >= modelCount )
            {
                continue;
            }

            bool supporterHasSupport = sleepSupportedThisFrame[supporter] != 0;

            if ( !supporterHasSupport && hotFields.fixed[static_cast<std::size_t>( supporter )] != 0u )
            {
                supporterHasSupport = true;
            }

            if ( !supporterHasSupport && supporter < static_cast<int>( sleepState.size() ) && sleepState[supporter] != 0 )
            {
                supporterHasSupport = true;
            }

            if ( supporterHasSupport && sleepSupportedThisFrame[supported] == 0 )
            {
                sleepSupportedThisFrame[supported] = 1;
                changed = true;
            }
        }

        if ( !changed )
        {
            break;
        }
    }
}
