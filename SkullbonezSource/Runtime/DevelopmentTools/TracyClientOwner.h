/*
File: SkullbonezSource/Runtime/DevelopmentTools/TracyClientOwner.h
Purpose:
  Declares the development-only Tracy process owner and zero-cost marker seams.

Summary:
  One startup-owned object brackets Tracy's manual lifetime. Frame and worker
  call sites use macros that disappear completely when Tracy is not compiled,
  so disabled builds neither evaluate arguments nor retain vendor references.

Glossary:
  Manual lifetime: Tracy mode where the engine explicitly starts and stops the
    client instead of relying on static initialization and destruction.
  Submitted-frame mark: One Tracy frame boundary emitted only after DX12
    Present succeeds.
  Composite main lane: The current engine thread that owns main-loop, render,
    replay/prediction coordination, and cold IO work.

Invariants:
  - TRACY_ENABLE is valid only with SKULLBONEZ_DEVELOPMENT_TOOLS.
  - The client starts before engine workers and stops after every worker joins.
  - Disabled marker macros do not evaluate their arguments.
  - Status snapshots are fixed values; copying them allocates no memory and
    performs no socket or process query.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/TracyClientOwner.cpp
  - SkullbonezSource/Runtime/Allocation/DevelopmentToolAllocation.h
  - ThirdPtySource/tracy/manual/tracy.md
*/
#pragma once

#if defined( TRACY_ENABLE ) && !defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#error "TRACY_ENABLE requires the shared SKULLBONEZ_DEVELOPMENT_TOOLS capability."
#endif

#if defined( TRACY_ENABLE )
namespace SkullbonezCore::Runtime::DevelopmentTools
{
struct TracyClientStatus
{
    bool buildEnabled = true;
    bool initialized = false;
    bool viewerConnected = false;
};

class TracyClientOwner
{
  public:
    TracyClientOwner() noexcept = default;
    ~TracyClientOwner();

    TracyClientOwner( const TracyClientOwner& ) = delete;
    TracyClientOwner& operator=( const TracyClientOwner& ) = delete;

    void Start();
    void Shutdown() noexcept;

    static TracyClientStatus CopyStatus() noexcept;
    static void MarkSubmittedFrame() noexcept;
    static void NameWorkerThread( int workerIndex ) noexcept;

  private:
    bool m_started = false;
};
} // namespace SkullbonezCore::Runtime::DevelopmentTools

#define SKORE_TRACY_MARK_SUBMITTED_FRAME()                                                                             \
    ::SkullbonezCore::Runtime::DevelopmentTools::TracyClientOwner::MarkSubmittedFrame()
#define SKORE_TRACY_NAME_WORKER_THREAD( workerIndex )                                                                  \
    ::SkullbonezCore::Runtime::DevelopmentTools::TracyClientOwner::NameWorkerThread( workerIndex )
#else
// Invariant: the argument tokens are discarded, so disabled builds cannot pay
// formatting, function-call, allocation, or side-effect cost by accident.
#define SKORE_TRACY_MARK_SUBMITTED_FRAME() ( (void)0 )
#define SKORE_TRACY_NAME_WORKER_THREAD( workerIndex ) ( (void)0 )
#endif
