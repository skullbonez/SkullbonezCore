/*
File: SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.cpp
Purpose:
  Enforces the operator-command transaction's adjacent phase walk.

Summary:
  The cursor is the fatal boundary around copied command values and synchronous
  concrete-owner phase calls. Completion is an ordinary final edge in the same
  adjacent-only contract.

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

void OperatorCommandTransaction::Complete()
{
    AdvanceOrFatal( OperatorCommandPhaseCursor::Phase::Complete, "Complete" );
}
} // namespace Runtime
} // namespace SkullbonezCore
