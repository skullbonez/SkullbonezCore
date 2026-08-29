/*
File: SkullbonezSource/Runtime/App/ApplicationExitState.cpp
Purpose:
  Implements deterministic application-exit precedence without depending on a
  window system or allocating runtime storage.

Summary:
  The state is a small latch. Normal exit sets the latch, while the first
  recoverable failure retains its compact diagnostic lease. Resolution prefers
  that exact immutable diagnostic over the less informative integer delivered by
  a platform message loop.

Glossary:
  Exit latch: Boolean state recording that the frame loop should stop.
  Failure precedence: Rule that preserves the earliest subsystem-owned failure
    instead of replacing it with later, less specific exit information.

Invariants:
  - RequestOwnedFailure never mutates state for an SkullbonezCore::Core::SbResult success value.
  - RequestPhaseFailure rejects success and always leaves a retained failure
    plus an exit request.
  - Once m_hasOwnedFailure is true, the retained lease and its immutable owner
    and message never change.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Runtime/App/ApplicationExitState.h
  - SkullbonezTests/TestApplicationExitState.cpp
*/
#include "ApplicationExitState.h"

#include "InteractionAutomationApplication.h"
#include "../Automation/InteractionAutomationRecorder.h"

#include "../../Core/FatalError.h"

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


void ApplicationExitState::RequestPhaseFailure( const SkullbonezCore::Core::SbResult& failure ) noexcept
{
    if ( failure.Ok() )
    {
        SB_FATAL( "Runtime/ApplicationExit", "RequestPhaseFailure requires a non-success frame-phase result" );
    }

    // Invariant: status-free frame phases have one failure channel. Once this
    // call returns, resolving message exit code 0 must still return failure.
    RequestOwnedFailure( failure );
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
        // Recoverable error: a platform/environment boundary asked the process to stop with
        // failure but did not provide richer owner diagnostics.
        return m_diagnostics.Failure( "Runtime/ApplicationExit", "application exit message reported nonzero code %d",
                                      messageExitCode );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


#if !defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
SkullbonezCore::Core::SbResult
ResolveRunExitAfterInteractionRecording( InteractionAutomationRecorder& recorder,
                                         SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                         ApplicationExitState& applicationExit, int messageExitCode,
                                         InteractionRecordingBoundaryOperation captureArmedBoundary, void* captureContext )
{
    if ( recorder.IsArmed() )
    {
        if ( !captureArmedBoundary )
        {
            applicationExit.RequestOwnedFailure(
                diagnostics.Failure( "InteractionRecorder", "Orderly exit could not capture the armed baseline." ) );
            return applicationExit.Resolve( messageExitCode );
        }

        captureArmedBoundary( captureContext );
    }

    if ( recorder.IsActive() )
    {
        const SkullbonezCore::Core::SbResult save = recorder.StopAndSave( diagnostics, "shutdown", true );

        if ( !save.Ok() )
        {
            // Invariant: the recorder's owned diagnostic outranks a normal
            // WM_QUIT code and remains leased through the returned result.
            applicationExit.RequestOwnedFailure( save );
        }
    }

    return applicationExit.Resolve( messageExitCode );
}
#endif
} // namespace Runtime
} // namespace SkullbonezCore
