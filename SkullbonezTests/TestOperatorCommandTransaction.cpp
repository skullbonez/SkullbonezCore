/*
File: TestOperatorCommandTransaction.cpp
Purpose:
  Locks the value-only operator-command owner and unified acceptance ledger.

Summary:
  Compile-time contracts reject a return to copyable transaction couriers or
  pointer-bearing acceptance values. Runtime checks pin detached command
  capture and the default ledger before phase execution.


  One normalized UI packet enters one non-copyable transaction. The only
  durable output is a detached acceptance ledger consumed by InputFrame.

Invariants:
  - The transaction cannot be copied or assigned.
  - The acceptance ledger is a value and contains no runtime owner.
  - Command capture does not mutate the source packet.

Related:
  - SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h
  - SkullbonezSource/Runtime/App/InputFrame.cpp
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h"

#include <type_traits>

namespace
{
using SkullbonezCore::Runtime::OperatorCommandAcceptanceLedger;
using SkullbonezCore::Runtime::OperatorCommandTransaction;

static_assert( !std::is_copy_constructible_v<OperatorCommandTransaction> );
static_assert( !std::is_copy_assignable_v<OperatorCommandTransaction> );
static_assert( std::is_trivially_copyable_v<OperatorCommandAcceptanceLedger> );

TEST_CASE( "Operator command transaction starts with a detached empty ledger" )
{
    SkullbonezCore::UI::InGameUICommands commands;
    commands.renderer.toggleVsync = true;
    OperatorCommandTransaction transaction( commands );
    commands.renderer.toggleVsync = false;

    CHECK( transaction.Phase() == SkullbonezCore::Runtime::OperatorCommandPhaseCursor::Phase::Idle );
    CHECK_FALSE( transaction.Acceptance().toggledVsync );
    CHECK_FALSE( transaction.Acceptance().cameraModeAccepted );
    CHECK_FALSE( transaction.Acceptance().worldOverrideAccepted );
}
} // namespace
