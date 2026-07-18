/*
File: SkullbonezSource/Core/LockOrderValidator.h
Purpose:
  Declares debug-only lock-order validation helpers.

Summary:
  TrackedMutex behaves like std::mutex, while Debug builds record observed lock
  acquisition order in a directed graph and assert if a new edge creates a
  cycle. Profile/Release keep only the mutex wrapper.

Glossary:
  TrackedMutex: std::mutex wrapper that reports lock acquisition and release to
  the Debug validator.
  Lock graph: Directed graph of observed "held before acquired" relationships.
  Debug build: Configuration where validation asserts are active.

Invariants:
  - TrackedMutex must match std::mutex lock/try_lock/unlock semantics for
    callers; validation is extra instrumentation, not a new locking policy.
  - Lock ids are stable for the lifetime of each TrackedMutex instance.
  - Debug TrackedMutex instances borrow the startup-owned validator once;
    Profile and Release compile the validation members and calls out entirely.

Related:
  - SkullbonezSource/Core/WorkerPool.h
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
#ifdef _DEBUG
    static constexpr uint32_t MAX_LOCK_COUNT = 256;
    bool
    HasCycleFrom( uint32_t node, std::bitset<MAX_LOCK_COUNT>& visiting, std::bitset<MAX_LOCK_COUNT>& visited ) const;
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
    bool try_lock();
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
