/*
File: SkullbonezSource/LockOrderValidator.h
Purpose:
  Declares debug-only lock-order validation helpers.

Mental model:
  TrackedMutex behaves like std::mutex, while Debug builds record observed lock
  acquisition order in a directed graph and assert if a new edge creates a
  cycle. Profile/Release keep only the mutex wrapper.

Glossary:
  TrackedMutex: std::mutex wrapper that reports lock acquisition and release to
  the Debug validator.
  Lock graph: Directed graph of observed "held before acquired" relationships.
  Debug build: Configuration where validation asserts are active.

Related:
  - Agentic/Plans/worker-system-plan.md
  - SkullbonezSource/WorkerPool.h
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
};

} // namespace Threading
} // namespace SkullbonezCore
