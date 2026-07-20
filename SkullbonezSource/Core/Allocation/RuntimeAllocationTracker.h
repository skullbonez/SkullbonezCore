/*
File: SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h
Purpose:
  Declares per-thread runtime allocation phase attribution and process-wide
  allocation counters.

Summary:
  The tracker is measurement infrastructure, not a gameplay allocator. Runtime
  code labels broad lifecycle phases on the thread doing the work, and the
  global allocation hook records which phase owned each heap request.

Glossary:
  Allocation guard: CLI-enabled measurement mode that counts heap requests by
    runtime phase and warns when steady gameplay allocates.
  Phase scope: RAII marker that labels allocations on the calling thread until
    the scope restores that thread's previous phase.
  Steady gameplay: Frame work after startup, backend init, and scene load where
    hot paths should reuse preallocated storage.

Invariants:
  - Tracker storage is fixed and must not allocate while recording or reporting.
  - Phase scopes remain active when the optional allocation counter is off;
    renderer and reserve policies consume the same lifecycle label.
  - Each worker must establish its own phase. One thread's nested scope must
    never relabel allocations made concurrently on another thread.

Related:
  - AGENTS.md (Runtime Static Allocation Policy)
  - tools/check_allocation_policy.py
*/
#pragma once

#include <cstdint>
#include <cstdio>

namespace SkullbonezCore
{
namespace Core
{
namespace Allocation
{
enum class RuntimeAllocationPhase
{
    Startup = 0,
    SceneLoad,
    BackendInit,
    SteadyGameplay,
    Physics,
    Render,
    Replay,
    Capture,
    Diagnostics,
    Shutdown,
    Count
};

enum class RuntimeAllocationGuardMode
{
    Off = 0,
    Measure,
    Gameplay
};

class RuntimeAllocationScope
{
  public:
    explicit RuntimeAllocationScope( RuntimeAllocationPhase phase ) noexcept;
    ~RuntimeAllocationScope() noexcept;

    RuntimeAllocationScope( const RuntimeAllocationScope& ) = delete;
    RuntimeAllocationScope& operator=( const RuntimeAllocationScope& ) = delete;

  private:
    RuntimeAllocationPhase m_previous;
};

void SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode mode ) noexcept;
RuntimeAllocationGuardMode GetRuntimeAllocationGuardMode() noexcept;
const char* RuntimeAllocationGuardModeName( RuntimeAllocationGuardMode mode ) noexcept;
const char* RuntimeAllocationPhaseName( RuntimeAllocationPhase phase ) noexcept;

void SetRuntimeAllocationPhase( RuntimeAllocationPhase phase ) noexcept;
RuntimeAllocationPhase GetRuntimeAllocationPhase() noexcept;

bool RuntimeAllocationGuardEnabled() noexcept;
bool RuntimeAllocationGuardHasGameplayViolations() noexcept;
uint64_t RuntimeAllocationGuardViolationCount() noexcept;

void ResetRuntimeAllocationCounters() noexcept;
void PrintRuntimeAllocationSummary( FILE* out ) noexcept;
} // namespace Allocation
} // namespace Core
} // namespace SkullbonezCore
