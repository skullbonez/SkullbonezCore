/*
File: SkullbonezSource/Runtime/DiagnosticsController.cpp
Purpose:
  Implements runtime diagnostics state ownership.

Mental model:
  The controller stores mutable diagnostics state and delegates artifact writes
  to RuntimeDiagnostics so existing output stays byte-for-byte compatible.

Glossary:
  Perf log: CSV-style runtime performance artifact written during runs.
  SkullScope: Queryable physics diagnostics trace workflow.
  Diagnostics artifact: File produced for validation, profiling, or analysis.

Invariants:
  - Formatting and close behavior stay in RuntimeDiagnostics.
  - Controller moves ownership only; artifact schema must not drift here.

Related:
  - SkullbonezSource/Runtime/DiagnosticsController.h
  - SkullbonezSource/Runtime/RuntimeDiagnostics.h
*/
#include "DiagnosticsController.h"

namespace SkullbonezCore
{
namespace Basics
{
RunPerfLogState& DiagnosticsController::PerfLog()
{
    return m_perfLog;
}


const RunPerfLogState& DiagnosticsController::PerfLog() const
{
    return m_perfLog;
}


void DiagnosticsController::ClosePerfLog()
{
    RuntimeDiagnostics::ClosePerfLog( m_perfLog );
}


void DiagnosticsController::ClosePerfLogWithMemoryCheckpoint( int pass, const char* checkpoint )
{
    RuntimeDiagnostics::ClosePerfLogWithMemoryCheckpoint( m_perfLog, pass, checkpoint );
}


void DiagnosticsController::LogPerfMemory( int pass, const char* checkpoint )
{
    RuntimeDiagnostics::LogPerfMemory( m_perfLog, pass, checkpoint );
}


void DiagnosticsController::ResetPerfLogForSceneLoad()
{
    RuntimeDiagnostics::ResetPerfLogForSceneLoad( m_perfLog );
}


void DiagnosticsController::ConfigurePerfLogFlush( bool enabled, int interval )
{
    RuntimeDiagnostics::ConfigurePerfLogFlush( m_perfLog, enabled, interval );
}


void DiagnosticsController::OpenScenePerfLog( const char* path, int pass )
{
    RuntimeDiagnostics::OpenScenePerfLog( m_perfLog, path, pass );
}


bool DiagnosticsController::PerfTestActive() const
{
    return RuntimeDiagnostics::PerfTestActive( m_perfLog );
}


void DiagnosticsController::TickPerfLog( const RuntimePerfTickContext& context )
{
    RuntimeDiagnostics::TickPerfLog( m_perfLog, context );
}


#ifdef _DEBUG
RunPhysicsDiagnosticsState& DiagnosticsController::PhysicsDiagnostics()
{
    return m_physicsDiagnostics;
}


const RunPhysicsDiagnosticsState& DiagnosticsController::PhysicsDiagnostics() const
{
    return m_physicsDiagnostics;
}


bool DiagnosticsController::PhysicsDiagnosticsEnabled() const
{
    return m_physicsDiagnostics.isEnabled;
}
#endif
} // namespace Basics
} // namespace SkullbonezCore
