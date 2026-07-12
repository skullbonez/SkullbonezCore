//
// File: SkullbonezTests/TestMain.cpp
// Purpose:
//   Own the doctest runner entry point for the unit-test executable.
//
// Summary:
//   Tests are compiled into SKULLBONEZ_TESTS, separate from the game executable,
//   so unit checks can exercise small contracts without launching DX12 or the
//   full runtime.
//
// Glossary:
//   Fatal child case: Named subprocess probe expected to terminate nonzero at
//   an engine invariant boundary.
//
// Invariants:
//   - Exactly one test translation unit defines DOCTEST_CONFIG_IMPLEMENT.
//   - The test runner must remain console-subsystem friendly for validation
//     scripts and CI-style command output.
//   - Fatal child cases bypass doctest and must terminate nonzero.
//
// Related:
//   - SkullbonezTests/TestFatalCases.h
//   - SkullbonezTests/TestRuntimeContracts.cpp
//

#define DOCTEST_CONFIG_IMPLEMENT
#include "../ThirdPtySource/doctest/doctest.h"

#include "TestFatalCases.h"

#include <cstring>

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
    if ( argc == 3 && std::strcmp( argv[1], "--fatal-case" ) == 0 )
    {
        // A normal return means the named invariant failed to terminate.
        return RunRuntimeFatalCase( argv[2] ) ? 0 : 2;
    }

    doctest::Context context( argc, argv );
    return context.run();
}
