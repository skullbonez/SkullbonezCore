//
// File: SkullbonezTests/TestMain.cpp
// Purpose:
//   Own the doctest runner entry point for the unit-test executable.
//
// Mental model:
//   Tests are compiled into SKULLBONEZ_TESTS, separate from the game executable,
//   so unit checks can exercise small contracts without launching DX12 or the
//   full runtime.
//
// Invariants:
//   - Exactly one test translation unit defines DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN.
//   - The test runner must remain console-subsystem friendly for validation
//     scripts and CI-style command output.
//

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../ThirdPtySource/doctest/doctest.h"
