/*
File: SkullbonezTests/TestSbResultAccess.h
Purpose:
  Exposes only the private lease release seam required by Lane F child probes.

Summary:
  Production callers cannot release a diagnostic token directly. The render-free
  test target uses this friend to prove stale/double release terminates without
  weakening the shipping API.

Glossary:
  Fatal child: Isolated test process expected to terminate through Lane F.

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
};
} // namespace SkullbonezCore::Core
