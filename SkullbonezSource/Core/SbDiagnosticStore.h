/*
File: SkullbonezSource/Core/SbDiagnosticStore.h
Purpose:
  Declares the fixed-capacity owner of immutable Lane R diagnostics.

Summary:
  The App composition root creates one store before every result-producing
  owner. Failure publication copies bounded owner/message bytes into one of 256
  slots and returns a compact lease. The last lease reclaims the slot.

Glossary:
  High-water: Largest number of simultaneously live diagnostic entries.

Invariants:
  - The store has exactly 256 slots, 96 owner bytes, and 512 message bytes.
  - Slot bytes are immutable while one or more leases exist.
  - An eight-bit slot and 56-bit nonzero generation form every failure token.
  - Publication, retain, release, lookup, and counters share one allocation-free
    spin lock; success paths never call the store.
  - The lock records one fixed thread token so same-thread re-entry terminates
    through Lane F instead of spinning indefinitely.
  - App constructs the store before result-producing owners and destroys it
    after every failed result lease; destruction with an active lease is a
    deterministic Lane F lifetime defect before the raw store can dangle.
  - Fatal reporting is selected while locked but emitted only after unlocking.
  - Capacity, owner overflow, generation wrap, stale live access, lease overflow,
    double release, and destruction with active leases are deterministic Lane F
    defects.

Related:
  - SkullbonezSource/Core/SbResult.h
  - SkullbonezSource/Core/SbResult.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "SbResult.h"

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>


namespace SkullbonezCore
{
namespace Core
{
class SbDiagnosticStore
{
  public:
    static constexpr std::size_t CAPACITY = 256;
    static constexpr std::size_t OWNER_CAPACITY = 96;
    static constexpr std::size_t MESSAGE_CAPACITY = 512;

    SbDiagnosticStore() noexcept = default;

    // Lane F terminates instead of unwinding, so the lifetime guard preserves a
    // noexcept destruction contract.
    ~SbDiagnosticStore() noexcept;
    SbDiagnosticStore( const SbDiagnosticStore& ) = delete;
    SbDiagnosticStore& operator=( const SbDiagnosticStore& ) = delete;

    [[nodiscard]] SbResult Failure( const char* owner, const char* format, ... ) noexcept;

    [[nodiscard]] SbDiagnosticCopyStatus CopyDiagnostic( SbDiagnosticIdentity identity, char ( &owner )[OWNER_CAPACITY],
                                                         char ( &message )[MESSAGE_CAPACITY] ) const noexcept;

    [[nodiscard]] std::uint32_t ActiveEntryCount() const noexcept;
    [[nodiscard]] std::uint32_t SessionHighWater() const noexcept;

  private:
    friend class SbResult;
#if defined( SKULLBONEZ_RENDER_FREE_TESTS )
    friend class SbDiagnosticStoreTestAccess;
#endif

    struct Entry
    {
        char owner[OWNER_CAPACITY] = {};
        char message[MESSAGE_CAPACITY] = {};
        std::uint64_t generation = 0;
        std::uint32_t leaseCount = 0;
    };

    [[nodiscard]] SbResult FailureV( const char* owner, const char* format, va_list args ) noexcept;
    void Retain( std::uint64_t token ) noexcept;
    void Release( std::uint64_t token ) noexcept;
    [[nodiscard]] const char* BorrowOwner( std::uint64_t token ) const noexcept;
    [[nodiscard]] const char* BorrowMessage( std::uint64_t token ) const noexcept;

    void Lock() const noexcept;
    void Unlock() const noexcept;
    [[nodiscard]] bool ResolveLiveEntry( std::uint64_t token, std::size_t& slotIndex ) const noexcept;

    mutable std::atomic_flag m_lock = ATOMIC_FLAG_INIT;

    // Zero while unlocked; otherwise the fixed caller token.
    mutable std::atomic<std::uint32_t> m_lockOwnerThread = 0u;
    Entry m_entries[CAPACITY] = {};
    std::uint32_t m_activeEntries = 0;
    std::uint32_t m_sessionHighWater = 0;
};
} // namespace Core
} // namespace SkullbonezCore
