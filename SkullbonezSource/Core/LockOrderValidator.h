/*
File: SkullbonezSource/Core/LockOrderValidator.h
Purpose:
  Declares debug-only lock-order validation helpers.

Summary:
  TrackedMutex behaves like std::mutex, while Debug builds record observed lock
  acquisition order in a directed graph and assert if a new edge creates a
  cycle. A pure probe classifier mirrors those Debug tripwires for tests, while
  Profile/Release keep only the mutex wrapper.

Glossary:
  TrackedMutex: std::mutex wrapper that reports lock acquisition and release to
  the Debug validator.
  Lock graph: Directed graph of observed "held before acquired" relationships.

Invariants:
  - TrackedMutex must match std::mutex lock/try_lock/unlock semantics for
    callers; validation is extra instrumentation, not a new locking policy.
  - Lock ids are stable for the lifetime of each TrackedMutex instance.
  - Debug TrackedMutex instances borrow the startup-owned validator once;
    Profile and Release compile the validation members and calls out entirely.
  - The test seam classifies Debug failure conditions but owns no graph, stack,
    or synchronization state.

Related:
  - SkullbonezSource/Core/WorkerPool.h
  - Agentic/Reference/engine-glossary.md
*/

#pragma once

#include <cstdint>
#include <mutex>
#ifdef _DEBUG
#include <array>
#include <bitset>
#endif

namespace SkullbonezCore
{
namespace Threading
{

class LockOrderValidator
{
  public:
    uint32_t RegisterLock( const char* name );
    void RecordAcquisition( uint32_t lockId );
    void RecordRelease( uint32_t lockId );

  private:
    enum class AcquisitionProbeFinding : uint8_t
    {
        None,
        InvalidId,
        Cycle,
        HeldStackExhausted,
    };

    // Lane P: RecordAcquisition owns the Debug-only graph and stack mutation,
    // while this pure classifier lets focused tests prove every tripwire
    // condition without deliberately invoking the CRT assertion dialog.
    static constexpr AcquisitionProbeFinding ClassifyAcquisitionProbe( uint32_t lockId, bool cycleDetected,
                                                                       bool heldStackFull ) noexcept
    {
        if ( lockId == 0u || lockId > MAX_LOCK_COUNT )
        {
            return AcquisitionProbeFinding::InvalidId;
        }

        if ( cycleDetected )
        {
            return AcquisitionProbeFinding::Cycle;
        }

        return heldStackFull ? AcquisitionProbeFinding::HeldStackExhausted : AcquisitionProbeFinding::None;
    }

    friend struct LockOrderValidatorTestAccess;
    static constexpr uint32_t MAX_LOCK_COUNT = 256;

#ifdef _DEBUG
    bool HasCycleFrom( uint32_t node, std::bitset<MAX_LOCK_COUNT>& visiting, std::bitset<MAX_LOCK_COUNT>& visited ) const;
    bool DetectCycleUnlocked() const;

    // Lifetime: Init owns this graph for longer than its WorkerPool borrow.
    // Keeping the state in the concrete owner removes teardown-order and
    // service-locator ambiguity from the lock path.
    std::mutex m_mutex;

    // Invariant: Debug diagnostics are bounded too. One bit row per registered
    // lock records observed ordering without runtime growth or heap fallback.
    std::array<std::bitset<MAX_LOCK_COUNT>, MAX_LOCK_COUNT> m_edges = {};
    std::array<const char*, MAX_LOCK_COUNT> m_names = {};
    uint32_t m_nextLockId = 1;
#endif
};

class TrackedMutex
{
  public:
    TrackedMutex( const char* name, LockOrderValidator& validator );

    void lock();
    void unlock();

  private:
    std::mutex m_inner;
#ifdef _DEBUG
    uint32_t m_id;
    LockOrderValidator* m_validator;
#endif
};

} // namespace Threading
} // namespace SkullbonezCore
