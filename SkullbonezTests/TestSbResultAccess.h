/*
File: SkullbonezTests/TestSbResultAccess.h
Purpose:
  Exposes only private diagnostic-store seams required by fatal child probes.

Summary:
  Production callers cannot manipulate slot counters or the store lock directly.
  The render-free test target uses this friend only for isolated fatal invariant probes
  that prove stale release, overflow, and lock re-entry terminate.

Glossary:
  Fatal child: Isolated test process expected to terminate through fatal invariant.

Invariants:
  - This class is a friend only when SKULLBONEZ_RENDER_FREE_TESTS is defined.
  - No ordinary test uses this seam outside an isolated fatal child.

Related:
  - SkullbonezSource/Core/SbDiagnosticStore.h
  - SkullbonezTests/TestRuntimeContracts.cpp
*/
#pragma once

#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

namespace SkullbonezCore::Core
{
class SbDiagnosticStoreTestAccess
{
  public:
    static void Release( SbDiagnosticStore& store, std::uint64_t token ) noexcept
    {
        store.Release( token );
    }

    static void SaturateLeaseCount( SbDiagnosticStore& store, std::uint64_t token ) noexcept
    {
        store.Lock();
        std::size_t slotIndex = 0u;

        if ( !store.ResolveLiveEntry( token, slotIndex ) )
        {
            store.Unlock();
            return;
        }

        store.m_entries[slotIndex].leaseCount = UINT32_MAX;
        store.Unlock();
    }

    static void ExhaustFirstGeneration( SbDiagnosticStore& store ) noexcept
    {
        store.Lock();
        store.m_entries[0].generation = ( std::uint64_t { 1 } << 56u ) - 1u;
        store.Unlock();
    }

    static void ReenterLock( SbDiagnosticStore& store ) noexcept
    {
        store.Lock();
        store.Lock();
    }
};
} // namespace SkullbonezCore::Core
