//
// File: SkullbonezTests/TestMain.cpp
// Purpose:
//   Own the doctest runner entry point for the unit-test executable.
//
// Summary:
//   Tests are compiled into executables separate from the game. The Windows
//   suite enables fatal-child routing, while the portable CPU target reuses
//   this runner without linking Runtime-owned fatal dispatchers.
//
// Glossary:
//   Fatal child case: Named subprocess probe expected to terminate nonzero at
//   an engine invariant boundary.
//
// Invariants:
//   - Exactly one test translation unit defines DOCTEST_CONFIG_IMPLEMENT.
//   - The test runner must remain console-subsystem friendly for validation
//     scripts and CI-style command output.
//   - Fatal child cases bypass doctest and must terminate nonzero when the
//     Windows suite enables SKULLBONEZ_RUNTIME_FATAL_TESTS.
//
// Related:
//   - SkullbonezTests/TestFatalCases.h
//   - SkullbonezTests/TestRuntimeContracts.cpp
//

#define DOCTEST_CONFIG_IMPLEMENT
#include "../ThirdPtySource/doctest/doctest.h"

#include <cstring>

#if defined( SKULLBONEZ_RUNTIME_FATAL_TESTS )
#include "TestFatalCases.h"
#endif

namespace
{
const char* g_executablePath = nullptr;
}

const char* RuntimeTestExecutablePath()
{
    return g_executablePath;
}

int main( int argc, char** argv )
{
    g_executablePath = argc > 0 ? argv[0] : nullptr;
#if defined( SKULLBONEZ_RUNTIME_FATAL_TESTS )
    if ( argc == 3 && std::strcmp( argv[1], "--fatal-case" ) == 0 )
    {
        // A normal return means the named invariant failed to terminate.
        return RunRuntimeFatalCase( argv[2] ) ? 0 : 2;
    }
#endif

    doctest::Context context( argc, argv );
    return context.run();
}
