/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp
Purpose:
  Implements runtime diagnostics state ownership.

Summary:
  The controller stores mutable diagnostics state and delegates artifact writes
  to RuntimeDiagnostics so existing output stays byte-for-byte compatible.

Invariants:
  - Formatting and close behavior stay in RuntimeDiagnostics.
  - Controller moves ownership only; artifact schema must not drift here.

Related:
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.h
  - SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h
  - Agentic/Reference/engine-glossary.md
*/
#include "DiagnosticsController.h"

namespace SkullbonezCore
{
namespace Runtime
{
void DiagnosticsController::BindProfiler( SkullbonezCore::Core::Profiler* profiler )
{
    m_profiler = profiler;
}


RunPerfLogState& DiagnosticsController::PerfLog()
{
    return m_perfLog;
}


bool DiagnosticsController::ClosePerfLog()
{
    return RuntimeDiagnostics::ClosePerfLog( m_perfLog );
}


bool DiagnosticsController::ClosePerfLogWithMemoryCheckpoint( int pass, const char* checkpoint )
{
    return RuntimeDiagnostics::ClosePerfLogWithMemoryCheckpoint( m_perfLog, pass, checkpoint );
}


void DiagnosticsController::ResetPerfLogForSceneLoad()
{
    RuntimeDiagnostics::ResetPerfLogForSceneLoad( m_perfLog );
}


void DiagnosticsController::ConfigurePerfLogFlush( bool enabled, int interval )
{
    RuntimeDiagnostics::ConfigurePerfLogFlush( m_perfLog, enabled, interval );
}


bool DiagnosticsController::OpenScenePerfLog( const char* path, int pass )
{
    return RuntimeDiagnostics::OpenScenePerfLog( m_perfLog, path, pass, m_profiler );
}


bool DiagnosticsController::PerfTestActive() const
{
    return RuntimeDiagnostics::PerfTestActive( m_perfLog );
}


bool DiagnosticsController::TickPerfLog( int pass, int frame, float physicsTimeSeconds, float renderTimeSeconds )
{
    return RuntimeDiagnostics::TickPerfLog( m_perfLog, pass, frame, physicsTimeSeconds, renderTimeSeconds, m_profiler );
}


RuntimeProfilerFrameTimes DiagnosticsController::SampleProfilerFrameTimes() const
{
    return RuntimeDiagnostics::SampleProfilerFrameTimes( m_profiler );
}


#ifdef _DEBUG
RunPhysicsDiagnosticsState& DiagnosticsController::PhysicsDiagnostics()
{
    return m_physicsDiagnostics;
}


#endif
} // namespace Runtime
} // namespace SkullbonezCore
