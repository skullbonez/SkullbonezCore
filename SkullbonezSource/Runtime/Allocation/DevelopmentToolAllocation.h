/*
File: SkullbonezSource/Runtime/Allocation/DevelopmentToolAllocation.h
Purpose:
  Declares bounded allocation-owner scopes for Dear ImGui and Tracy.

Summary:
  Development tools may use dynamic storage without weakening the gameplay
  allocation guard. A thread enters one explicit tool-owner scope around vendor
  work; allocations on every other thread retain their engine phase and owner.
  Fixed registry counters expose ImGui and Tracy totals independently.

Glossary:
  Tool-owner scope: Thread-local attribution applied only while one vendor API
  is running on the calling thread.
  Active-byte cap: Maximum live tracked bytes permitted for one development
  tool before the ordinary allocation guard reports a policy violation.

Invariants:
  - This API exists only in SKULLBONEZ_DEVELOPMENT_TOOLS configurations.
  - ImGui and Tracy never share an owner handle or accounting row.
  - Tool scopes do not change the process-wide gameplay allocation phase.
  - Exceeding a hard cap fails the guard; there is no unbounded fallback.

Related:
  - SkullbonezSource/Runtime/Allocation/DevelopmentToolsCapability.h
  - SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h
  - AGENTS.md (Runtime Static Allocation Policy)
*/
#pragma once

#include "DevelopmentToolsCapability.h"
#include "RuntimeReserveAllocator.h"

#include <cstdint>

namespace SkullbonezCore::Runtime::Allocation
{
enum class DevelopmentToolAllocationOwner
{
    DearImGui = 0,
    Tracy
};

struct DevelopmentToolAllocationStats
{
    const char* ownerName = nullptr;
    uint64_t allocations = 0u;
    uint64_t activeBytes = 0u;
    uint64_t highWaterBytes = 0u;
    int hardCapBytes = 0;
};

class DevelopmentToolAllocationScope
{
  public:
    explicit DevelopmentToolAllocationScope( DevelopmentToolAllocationOwner owner ) noexcept;

    DevelopmentToolAllocationScope( const DevelopmentToolAllocationScope& ) = delete;
    DevelopmentToolAllocationScope& operator=( const DevelopmentToolAllocationScope& ) = delete;

  private:
    RuntimeReserveOwnerScope m_ownerScope;
};

bool CopyDevelopmentToolAllocationStats( DevelopmentToolAllocationOwner owner,
                                         DevelopmentToolAllocationStats& outStats ) noexcept;
} // namespace SkullbonezCore::Runtime::Allocation
