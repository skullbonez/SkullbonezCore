//
// File: SkullbonezTests/TestSmoke.cpp
// Purpose:
//   Prove the unit-test harness discovers and runs at least one test case.
//
// Mental model:
//   This smoke case is intentionally behavior-free; later files carry real
//   engine contracts while this one protects the project, runner, and script
//   plumbing.
//
// Invariants:
//   - Keep this test independent of engine source so harness failures are easy
//     to distinguish from engine regressions.
//

#include "../ThirdPtySource/doctest/doctest.h"

TEST_CASE( "smoke: harness runs" )
{
    CHECK( 1 + 1 == 2 );
}
