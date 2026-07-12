//
// File: SkullbonezTests/TestFatalCases.h
// Purpose:
//   Shares child-process fatal probes between the doctest entry point and tests.
//
// Mental model:
//   The parent test runner owns its executable path; the fatal-case dispatcher
//   uses that path to re-enter the binary in one isolated negative scenario.
//
// Glossary:
//   Fatal probe: Child invocation expected to end through an engine Lane F
//   invariant rather than return normally.
//
// Invariants:
//   - A known fatal case must terminate before RunRuntimeFatalCase returns.
//   - Fatal probes run in a child so the parent can continue the CPU suite.
//
// Related:
//   - SkullbonezTests/TestMain.cpp
//   - SkullbonezTests/TestRuntimeContracts.cpp
//

#pragma once

const char* RuntimeTestExecutablePath();
bool RunRuntimeFatalCase( const char* caseName );
