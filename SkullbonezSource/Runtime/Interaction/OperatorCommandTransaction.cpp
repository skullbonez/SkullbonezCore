/*
File: SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.cpp
Purpose:
  Enforces the operator-command transaction's adjacent phase walk.

Summary:
  OC1 establishes the fatal phase boundary around copied command values. OC2
  moves the existing concrete-owner command kernels into these phase methods
  without changing this cursor contract.

Mental model:
  Each method passes one turnstile. Calling a phase twice, skipping ahead, or
  moving backward terminates at the first broken invariant.

Invariants:
  - Every phase method advances exactly one adjacent edge.
  - Failed transitions leave no partially accepted phase.
  - This implementation retains no runtime owner.

Related:
  - SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h
  - SkullbonezTests/TestRuntimeContracts.cpp
*/
#include "OperatorCommandTransaction.h"

#include "../../Core/FatalError.h"

namespace SkullbonezCore
{
namespace Runtime
{
OperatorCommandTransaction::OperatorCommandTransaction( const UI::InGameUICommands& commands ) : m_commands( commands )
{
}

void OperatorCommandTransaction::AdvanceOrFatal( OperatorCommandPhaseCursor::Phase next, const char* operation )
{
    const OperatorCommandPhaseCursor::Phase current = m_phase.Current();

    if ( !m_phase.TryAdvance( next ) )
    {
        SB_FATAL( "Runtime/OperatorCommandTransaction", "Illegal phase transition. operation=%s current=%u next=%u",
                  operation, static_cast<unsigned int>( current ), static_cast<unsigned int>( next ) );
    }
}

void OperatorCommandTransaction::ApplyDeviceAndMode()
{
    AdvanceOrFatal( OperatorCommandPhaseCursor::Phase::DeviceAndMode, "ApplyDeviceAndMode" );
}

void OperatorCommandTransaction::ApplyPhysicsControl()
{
    AdvanceOrFatal( OperatorCommandPhaseCursor::Phase::PhysicsControl, "ApplyPhysicsControl" );
}

void OperatorCommandTransaction::ApplyRuntimePresentation()
{
    AdvanceOrFatal( OperatorCommandPhaseCursor::Phase::RuntimePresentation, "ApplyRuntimePresentation" );
}

void OperatorCommandTransaction::ApplySimulationPolicy()
{
    AdvanceOrFatal( OperatorCommandPhaseCursor::Phase::SimulationPolicy, "ApplySimulationPolicy" );
}

void OperatorCommandTransaction::ApplyPhysicsMaterial()
{
    AdvanceOrFatal( OperatorCommandPhaseCursor::Phase::PhysicsMaterial, "ApplyPhysicsMaterial" );
}

void OperatorCommandTransaction::ApplyWorldPolicy()
{
    AdvanceOrFatal( OperatorCommandPhaseCursor::Phase::WorldPolicy, "ApplyWorldPolicy" );
}

void OperatorCommandTransaction::ApplyCinematicPolicy()
{
    AdvanceOrFatal( OperatorCommandPhaseCursor::Phase::CinematicPolicy, "ApplyCinematicPolicy" );
}

void OperatorCommandTransaction::Complete()
{
    AdvanceOrFatal( OperatorCommandPhaseCursor::Phase::Complete, "Complete" );
}
} // namespace Runtime
} // namespace SkullbonezCore
