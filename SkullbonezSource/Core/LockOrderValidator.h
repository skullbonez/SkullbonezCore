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
  - Debug TrackedMutex instances cache the frozen validator once; Profile and
    Release compile the validation member and calls out entirely.

Related:
  - SkullbonezSource/Core/WorkerPool.h
*/

#pragma once

#include <cstdint>
#include <mutex>

namespace SkullbonezCore
{
namespace Threading
{

class LockOrderValidator
{
  public:
    // Frozen diagnostics singleton: this is intentionally not a runtime service
    // locator. It is only the Debug lock-order instrumentation endpoint used by
    // TrackedMutex and must stay independent of config and renderer lifetime.
    static LockOrderValidator& Instance();

    void RegisterLock( uint32_t lockId, const char* name );
    void RecordAcquisition( uint32_t lockId, uint32_t threadId );
    void RecordRelease( uint32_t lockId, uint32_t threadId );

  private:
    bool DetectCycleUnlocked() const;
};

class TrackedMutex
{
  public:
    explicit TrackedMutex( const char* name );

    void lock();
    bool try_lock();
    void unlock();

  private:
    std::mutex m_inner;
    const char* m_name;
    uint32_t m_id;
#ifdef _DEBUG
    LockOrderValidator* m_validator;
#endif
};

} // namespace Threading
} // namespace SkullbonezCore
