/*
File: SkullbonezSource/Runtime/App/ApplicationExitState.cpp
Purpose:
  Implements deterministic application-exit precedence without depending on a
  window system or allocating runtime storage.

Summary:
  The state is a small latch. Normal exit sets the latch, while the first Lane R
  failure retains its compact diagnostic lease. Resolution prefers that exact
  immutable diagnostic over the less informative integer delivered by a
  platform message loop.

Glossary:
  Exit latch: Boolean state recording that the frame loop should stop.
  Failure precedence: Rule that preserves the earliest subsystem-owned failure
    instead of replacing it with later, less specific exit information.
  Synthetic failure: Generic Lane R result created from a nonzero message exit
    code when no subsystem supplied a richer result.

Invariants:
  - RequestOwnedFailure never mutates state for an SkullbonezCore::Core::SbResult success value.
  - Once m_hasOwnedFailure is true, the retained lease and its immutable owner
    and message never change.

Related:
  - SkullbonezSource/Runtime/App/ApplicationExitState.h declares the caller contract.
  - SkullbonezTests/TestApplicationExitState.cpp covers precedence and bounds.
*/
#include "ApplicationExitState.h"

namespace SkullbonezCore
{
namespace Runtime
{
ApplicationExitState::ApplicationExitState( SkullbonezCore::Core::SbDiagnosticStore& diagnostics ) noexcept
    : m_diagnostics( diagnostics )
{
}


void ApplicationExitState::RequestNormalExit() noexcept
{

    // Why: normal shutdown is only a stop request. It must not erase an earlier
    // recoverable failure that the process boundary still needs to report.
    m_exitRequested = true;
}


void ApplicationExitState::RequestOwnedFailure( const SkullbonezCore::Core::SbResult& failure ) noexcept
{

    if ( failure.Ok() || m_hasOwnedFailure )
    {
        return;
    }

    // Invariant: the first retained lease preserves one complete immutable
    // diagnostic without introducing a second owner/message buffer.
    m_failure = failure;
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


SkullbonezCore::Core::SbResult ApplicationExitState::Resolve( int messageExitCode ) const noexcept
{

    if ( m_hasOwnedFailure )
    {
        return m_failure;
    }

    if ( messageExitCode != 0 )
    {

        // Lane R: a platform/environment boundary asked the process to stop with
        // failure but did not provide richer owner diagnostics.
        return m_diagnostics.Failure( "Runtime/ApplicationExit", "application exit message reported nonzero code %d",
                                      messageExitCode );
    }

    return SkullbonezCore::Core::SbResult::Success();
}
} // namespace Runtime
} // namespace SkullbonezCore
