//
// File: SkullbonezTests/TestDiagnosticsLinkStubs.cpp
// Purpose:
//   Provide no-op diagnostics link stubs for the focused unit-test harness.
//
// Mental model:
//   The test project links selected physics translation units directly. Debug
//   builds keep uncalled diagnostics bodies that Profile can discard, so this
//   file supplies cold logging and SkullScope endpoints without importing the
//   full runtime diagnostics stack.
//
// Glossary:
//   Link stub: Test-only definition that satisfies unresolved symbols while
//     keeping the focused test subject small.
//   SkullScope: Runtime trace emitter for queryable physics diagnostics.
//   EngineLog: Debug-only file logger used by diagnostics sinks.
//   Lane F: Fatal invariant path for should-never-happen engine state.
//
// Invariants:
//   - These stubs must not emit files or mutate runtime diagnostics state.
//   - SbFatal must remain non-returning even in tests so code after SB_FATAL is
//     still unreachable to callers and optimizers.
//   - Focused unit tests that need real SkullScope output should link the real
//     diagnostics implementation instead of extending this file.
//
// Related:
//   - SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp
//   - SkullbonezSource/Core/FatalError.h
//   - SkullbonezSource/Core/SkullScope.h
//   - SkullbonezSource/Core/Log.h
//

#include "../SkullbonezSource/Core/FatalError.h"
#include "../SkullbonezSource/Core/Log.h"
#include "../SkullbonezSource/Core/SkullScope.h"

#include <cstdlib>

namespace SkullbonezCore
{
namespace Basics
{
[[noreturn]] void SbFatal( const char*, const char*, ... )
{
    // Why: unit tests link only focused physics/runtime slices. Fatal branches
    // are invariant-failure paths and should fail the process if reached, but
    // the harness does not need the production logger dependency graph.
    std::abort();
}

EngineLog& EngineLog::Get()
{
    static EngineLog log;
    return log;
}

void EngineLog::Writef( const char*, const char*, ... )
{
}

void EngineLog::WriteEventf( const char*, ... )
{
}

void EngineLog::FlushAll()
{
}

const char* EngineLog::EventLogPath()
{
    return "";
}

EngineLog::~EngineLog() = default;
} // namespace Basics

namespace GameObjects
{
#ifdef _DEBUG
void SkullScope::SetPath( const char* )
{
}

void SkullScope::SetRunId( const char* )
{
}

bool SkullScope::IsFrameEnabled() const
{
    return false;
}

void SkullScope::EmitFrame( const Physics::PhysicsDiagnosticsFrameInput& )
{
}
#endif
} // namespace GameObjects
} // namespace SkullbonezCore
