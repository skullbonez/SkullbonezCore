//
// File: SkullbonezTests/TestFatalCases.h
// Purpose:
//   Shares child-process fatal probes between the doctest entry point and tests.
//
// Summary:
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

#include <initializer_list>

const char* RuntimeTestExecutablePath();

// Launches the named fatal probe in an isolated copy of this test executable,
// then verifies termination and every required diagnostic fragment.
void ExpectRuntimeFatalCase( const char* caseName, std::initializer_list<const char*> expectedDiagnostics );

// Returns false for an unknown name. A recognized RenderGraph case must reach
// its Lane F invariant before this dispatcher can return true.
bool RunRenderGraphFatalCase( const char* caseName );

// Routes a named fatal child to its subsystem-owned dispatcher. A known case
// returns only when the invariant failed to terminate the child as required.
bool RunRuntimeFatalCase( const char* caseName );
