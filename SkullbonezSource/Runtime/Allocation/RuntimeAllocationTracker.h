/*
File: SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.h
Purpose:
  Declares the process-wide runtime allocation phase tracker.

Mental model:
  The tracker is measurement infrastructure, not a gameplay allocator. Runtime
  code labels broad lifecycle phases, and the global allocation hook records
  which phase owned each heap request.

Glossary:
  Allocation guard: CLI-enabled measurement mode that counts heap requests by
    runtime phase and warns when steady gameplay allocates.
  Phase scope: RAII marker that labels allocations until the scope restores the
    previous process phase.
  Steady gameplay: Frame work after startup, backend init, and scene load where
    hot paths should reuse preallocated storage.

Invariants:
  - Tracker storage is fixed and must not allocate while recording or reporting.
  - Worker threads read the same process phase as the main thread so parallel
    physics allocations cannot hide behind a default thread-local phase.

Related:
  - Agentic/Plans/In_Progress/runtime-static-allocation-policy-plan.md
  - tools/check_allocation_policy.py
*/
#pragma once

#include <cstdint>
#include <cstdio>

namespace SkullbonezCore
{
namespace Runtime
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
    bool m_active;
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
} // namespace Runtime
} // namespace SkullbonezCore
