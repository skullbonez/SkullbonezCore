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

#include <array>
#include <cstdlib>
#include <cstring>

#if defined( _MSC_VER ) && defined( _DEBUG )
#include <crtdbg.h>
#endif

#if defined( SKULLBONEZ_RUNTIME_FATAL_TESTS )
#include "TestFatalCases.h"
#endif

namespace
{
const char* g_executablePath = nullptr;
}

const char* RuntimeTestExecutablePath()
{
    // Why: native static coverage rewrites the parent executable in place.
    // Fatal child probes use an uninstrumented sibling so their deliberate
    // abort cannot wait on collector shutdown and become a timeout failure.
#if defined( _MSC_VER )
    static const std::array<char, 4096> childOverride = []
    {
        std::array<char, 4096> value = {};
        std::size_t requiredBytes = 0u;
        getenv_s( &requiredBytes, value.data(), value.size(), "SKULLBONEZ_TEST_CHILD_EXE" );
        return value;
    }();

    if ( childOverride[0] != '\0' )
    {
        return childOverride.data();
    }
#else
    const char* childOverride = std::getenv( "SKULLBONEZ_TEST_CHILD_EXE" );

    if ( childOverride && childOverride[0] != '\0' )
    {
        return childOverride;
    }
#endif

    return g_executablePath;
}

int main( int argc, char** argv )
{
    g_executablePath = argc > 0 ? argv[0] : nullptr;
#if defined( SKULLBONEZ_RUNTIME_FATAL_TESTS )
    if ( argc == 3 && std::strcmp( argv[1], "--fatal-case" ) == 0 )
    {
#if defined( _MSC_VER ) && defined( _DEBUG )
        // Why: named fatal children are noninteractive process probes. Route
        // Debug CRT assertions to stderr and suppress Windows fault reporting
        // so the parent observes termination instead of an assertion dialog.
        _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE );
        _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDERR );
        _set_abort_behavior( 0u, _WRITE_ABORT_MSG | _CALL_REPORTFAULT );
#endif
        // A normal return means the named invariant failed to terminate.
        return RunRuntimeFatalCase( argv[2] ) ? 0 : 2;
    }
#endif

    doctest::Context context( argc, argv );
    return context.run();
}
