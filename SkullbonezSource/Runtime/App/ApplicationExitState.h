/*
File: SkullbonezSource/Runtime/App/ApplicationExitState.h
Purpose:
  Declares the fixed-size application exit request and recoverable-failure
  state used by the process frame loop.

Summary:
  Normal exit is a request to stop successfully. An owned failure is stronger:
  it supplies the compact recoverable error lease that the process boundary must report.
  The first owned failure remains authoritative even if normal or platform
  exit messages arrive later.

Glossary:
  Owned failure: recoverable failure already attributed to the subsystem that
    detected it, including a lease on its immutable owner and diagnostic
    message.
  Message exit code: Integer supplied by the platform message loop when it
    announces process exit; this owner intentionally has no Win32 dependency.

Invariants:
  - The first owned failure wins; later failures and normal exits cannot replace
    its diagnostics.
  - A frame phase reports failure only through RequestPhaseFailure; its public
    phase result contains no status that Run::Execute can drop.
  - A nonzero message exit code becomes a recoverable failure only when no richer
    owned failure exists.
  - All retained state is fixed-size; requesting or resolving exit allocates no
    memory, and the App-owned diagnostic store outlives this state and its
    returned results.

Related:
  - SkullbonezSource/Core/SbResult.h defines the recoverable result carrier.
  - SkullbonezSource/Runtime/App/RunFrame.cpp owns the platform message-loop call site.
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/SbDiagnosticStore.h"

namespace SkullbonezCore
{
namespace Runtime
{
class ApplicationExitState
{
  public:
    explicit ApplicationExitState( SkullbonezCore::Core::SbDiagnosticStore& diagnostics ) noexcept;

    // Requests a successful process exit without changing any previously owned
    // failure.
    void RequestNormalExit() noexcept;

    // Records a subsystem-attributed recoverable failure and requests exit. Success
    // values are ignored, and only the first failure is retained.
    void RequestOwnedFailure( const SkullbonezCore::Core::SbResult& failure ) noexcept;

    // Latches a non-success frame-phase result. Status-free frame signatures
    // make this the only failure signal that Run::Execute can observe.
    void RequestPhaseFailure( const SkullbonezCore::Core::SbResult& failure ) noexcept;

    [[nodiscard]] bool ExitRequested() const noexcept;
    [[nodiscard]] bool HasOwnedFailure() const noexcept;

    // Resolves the result when the platform message loop announces exit. A
    // nonzero integer becomes a generic application failure only when no owner
    // has already supplied a more useful recoverable result.
    //
    // Lifetime: the returned copy keeps the same immutable store entry leased
    // even if this exit state is reset or destroyed.
    [[nodiscard]] SkullbonezCore::Core::SbResult Resolve( int messageExitCode ) const noexcept;

  private:
    SkullbonezCore::Core::SbDiagnosticStore& m_diagnostics;
    SkullbonezCore::Core::SbResult m_failure = SkullbonezCore::Core::SbResult::Success();
    bool m_exitRequested = false;
    bool m_hasOwnedFailure = false;
};
} // namespace Runtime
} // namespace SkullbonezCore
