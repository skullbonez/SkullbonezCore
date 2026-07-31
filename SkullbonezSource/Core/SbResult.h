/*
File: SkullbonezSource/Core/SbResult.h
Purpose:
  Declares the compact Lane R recoverable-result lease.

Summary:
  A successful result is a zero identity. A failed result leases immutable
  owner/message bytes from an App-composed SbDiagnosticStore. Copies retain the
  same lease, moves transfer it, and the last release reclaims the fixed slot.

Glossary:
  Diagnostic token: Packed eight-bit slot and 56-bit generation identity.
  Lease: One live SbResult reference that prevents a diagnostic slot from reuse.

Invariants:
  - On Win64 SbResult is exactly a store pointer plus a 64-bit token.
  - Success construction, copying, moving, inspection, and destruction never
    touch a diagnostic store.
  - A failed copy retains before replacing any destination lease; moves transfer
    without changing the lease count and leave the source successful.
  - ErrorOwner and ErrorMessage return borrows valid only while that exact
    result remains alive, failed, unmoved, and unassigned.
  - The App-owned diagnostic store outlives every failed result lease; a failed
    result destructor, copy, or accessor requires that store to remain alive.
  - Callers that need diagnostic bytes beyond that borrow use the store's
    bounded CopyDiagnostic operation.

Related:
  - SkullbonezSource/Core/SbDiagnosticStore.h
  - SkullbonezSource/Core/FatalError.h
  - Agentic/Reports/2026-07-28/sbresult-compact-success-path-sr1-decision.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>


namespace SkullbonezCore
{
namespace Core
{
class SbDiagnosticStore;


struct SbDiagnosticIdentity
{
    SbDiagnosticStore* store = nullptr;
    std::uint64_t token = 0;
};


enum class SbDiagnosticCopyStatus
{
    Copied,
    SuccessIdentity,
    ForeignStore,
    Stale
};


class [[nodiscard]] SbResult
{
  public:
    SbResult() noexcept = default;
    ~SbResult() noexcept;

    SbResult( const SbResult& source ) noexcept;
    SbResult& operator=( const SbResult& source ) noexcept;
    SbResult( SbResult&& source ) noexcept;
    SbResult& operator=( SbResult&& source ) noexcept;

    [[nodiscard]] static SbResult Success() noexcept;
    [[nodiscard]] bool Ok() const noexcept;
    [[nodiscard]] const char* ErrorOwner() const noexcept;
    [[nodiscard]] const char* ErrorMessage() const noexcept;
    [[nodiscard]] SbDiagnosticIdentity DiagnosticIdentity() const noexcept;

  private:
    friend class SbDiagnosticStore;
    SbResult( SbDiagnosticStore& store, std::uint64_t token ) noexcept;
    void Release() noexcept;

    SbDiagnosticStore* m_store = nullptr;
    std::uint64_t m_token = 0;
};


static_assert( sizeof( SbResult ) == 16, "SbResult must remain the compact two-word Lane R carrier" );
} // namespace Core
} // namespace SkullbonezCore
