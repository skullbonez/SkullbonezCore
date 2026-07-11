/*
File: SkullbonezSource/Runtime/ApplicationExitState.cpp
Purpose:
  Implements deterministic application-exit precedence without depending on a
  window system or allocating runtime storage.

Mental model:
  The state is a small latch. Normal exit sets the latch, while the first Lane R
  failure also stores bounded diagnostics. Resolution prefers those diagnostics
  over the less informative integer delivered by a platform message loop.

Glossary:
  Exit latch: Boolean state recording that the frame loop should stop.
  Failure precedence: Rule that preserves the earliest subsystem-owned failure
    instead of replacing it with later, less specific exit information.
  Synthetic failure: Generic Lane R result created from a nonzero message exit
    code when no subsystem supplied a richer result.

Invariants:
  - RequestOwnedFailure never mutates state for an SbResult success value.
  - Once m_hasOwnedFailure is true, its owner and message never change.
  - Bounded copies always leave their destination null-terminated.

Related:
  - SkullbonezSource/Runtime/ApplicationExitState.h declares the caller contract.
  - SkullbonezTests/TestApplicationExitState.cpp covers precedence and bounds.
*/
#include "ApplicationExitState.h"

#include <cstdio>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
template <std::size_t Capacity>
void CopyBoundedText( std::array<char, Capacity>& destination, const char* source ) noexcept
{
    static_assert( Capacity > 0, "bounded text storage must include a null terminator" );
    std::snprintf( destination.data(), destination.size(), "%s", source ? source : "" );
    destination.back() = '\0';
}
} // namespace


void ApplicationExitState::RequestNormalExit() noexcept
{
    // Why: normal shutdown is only a stop request. It must not erase an earlier
    // recoverable failure that the process boundary still needs to report.
    m_exitRequested = true;
}


void ApplicationExitState::RequestOwnedFailure( const SbResult& failure ) noexcept
{
    if ( failure.ok || m_hasOwnedFailure )
    {
        return;
    }

    // Invariant: capture both bounded strings before publishing the failure bit
    // so every observable owned failure has complete diagnostics.
    CopyBoundedText( m_failureOwner, failure.error.owner );
    CopyBoundedText( m_failureMessage, failure.error.message );
    m_hasOwnedFailure = true;
    m_exitRequested = true;
}


bool ApplicationExitState::ExitRequested() const noexcept
{
    return m_exitRequested;
}


bool ApplicationExitState::HasOwnedFailure() const noexcept
{
    return m_hasOwnedFailure;
}


SbResult ApplicationExitState::Resolve( int messageExitCode ) const noexcept
{
    if ( m_hasOwnedFailure )
    {
        return SbResult::Failure( m_failureOwner.data(), "%s", m_failureMessage.data() );
    }

    if ( messageExitCode != 0 )
    {
        // Lane R: a platform/environment boundary asked the process to stop with
        // failure but did not provide richer owner diagnostics.
        return SbResult::Failure( "Runtime/ApplicationExit",
                                  "application exit message reported nonzero code %d",
                                  messageExitCode );
    }

    return SbResult::Success();
}
} // namespace Basics
} // namespace SkullbonezCore
