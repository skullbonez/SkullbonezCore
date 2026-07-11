/*
File: SkullbonezSource/Runtime/ApplicationExitState.h
Purpose:
  Declares the fixed-size application exit request and recoverable-failure
  state used by the process frame loop.

Mental model:
  Normal exit is a request to stop successfully. An owned failure is stronger:
  it supplies the Lane R result that the process boundary must report. The
  first owned failure remains authoritative even if normal or platform exit
  messages arrive later.

Glossary:
  Lane R: Recoverable result lane for an external-input or environment failure.
  Owned failure: Lane R failure already attributed to the subsystem that
    detected it, including its owner and diagnostic message.
  Message exit code: Integer supplied by the platform message loop when it
    announces process exit; this owner intentionally has no Win32 dependency.

Invariants:
  - The first owned failure wins; later failures and normal exits cannot replace
    its diagnostics.
  - A nonzero message exit code becomes a Lane R failure only when no richer
    owned failure exists.
  - All state is inline and fixed-size; requesting or resolving exit allocates
    no memory.

Related:
  - SkullbonezSource/Core/SbResult.h defines the Lane R result carrier.
  - SkullbonezSource/Runtime/RunFrame.cpp owns the platform message-loop call site.
  - Agentic/Plans/TODO/runtime-shell-decomposition.md tracks application command ownership.
*/
#pragma once

#include "../Core/SbResult.h"

#include <array>
#include <cstddef>

namespace SkullbonezCore
{
namespace Basics
{
class ApplicationExitState
{
  public:
    static constexpr std::size_t FAILURE_OWNER_CAPACITY = 96;
    static constexpr std::size_t FAILURE_MESSAGE_CAPACITY = sizeof( SbError::message );

    // Requests a successful process exit without changing any previously owned
    // failure.
    void RequestNormalExit() noexcept;

    // Records a subsystem-attributed Lane R failure and requests exit. Success
    // values are ignored, and only the first failure is retained.
    void RequestOwnedFailure( const SbResult& failure ) noexcept;

    [[nodiscard]] bool ExitRequested() const noexcept;
    [[nodiscard]] bool HasOwnedFailure() const noexcept;

    // Resolves the result when the platform message loop announces exit. A
    // nonzero integer becomes a generic application failure only when no owner
    // has already supplied a more useful Lane R result.
    //
    // Lifetime: a returned owned-failure result borrows its owner string from
    // this state. The state must outlive the caller's use of that SbResult.
    [[nodiscard]] SbResult Resolve( int messageExitCode ) const noexcept;

  private:
    std::array<char, FAILURE_OWNER_CAPACITY> m_failureOwner = {};
    std::array<char, FAILURE_MESSAGE_CAPACITY> m_failureMessage = {};
    bool m_exitRequested = false;
    bool m_hasOwnedFailure = false;
};
} // namespace Basics
} // namespace SkullbonezCore
