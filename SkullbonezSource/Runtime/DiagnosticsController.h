/*
File: SkullbonezSource/Runtime/DiagnosticsController.h
Purpose:
  Owns runtime diagnostics state for performance logs and SkullScope runs.

Summary:
  DiagnosticsController is the mutable diagnostics owner. RuntimeDiagnostics
  still formats artifacts, but Run no longer directly owns the perf and physics
  diagnostic structs.

Glossary:
  Perf log: CSV-style runtime performance artifact written during runs.
  SkullScope: Queryable physics diagnostics trace workflow.
  Diagnostics artifact: File produced for validation, profiling, or analysis.
  Trace state: Mutable state needed while a diagnostics run is active.

Invariants:
  - RuntimeDiagnostics owns artifact formatting and schema compatibility.
  - Controller state must be closed/flushed through the helper API.

Related:
  - SkullbonezSource/Runtime/RuntimeDiagnostics.h
  - SkullbonezSource/Runtime/RunFrame.cpp
*/
#pragma once

#include "RuntimeDiagnostics.h"

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
} // namespace Core
namespace Runtime
{
class DiagnosticsController
{
  public:
    // Binds the startup-resolved profiler diagnostics source. Null is valid
    // when profiling is compiled out; profile builds should bind before runs.
    void BindProfiler( SkullbonezCore::Core::Profiler* profiler );

    RunPerfLogState& PerfLog();
    const RunPerfLogState& PerfLog() const;

    void ClosePerfLog();
    void ClosePerfLogWithMemoryCheckpoint( int pass, const char* checkpoint );
    void LogPerfMemory( int pass, const char* checkpoint );
    void ResetPerfLogForSceneLoad();
    void ConfigurePerfLogFlush( bool enabled, int interval );
    void OpenScenePerfLog( const char* path, int pass );
    bool PerfTestActive() const;
    void TickPerfLog( const RuntimePerfTickContext& context );
    // Samples the bound profiler without reopening the SkullbonezCore::Core::Profiler::Instance()
    // singleton from frame or diagnostics code.
    RuntimeProfilerFrameTimes SampleProfilerFrameTimes() const;

#ifdef _DEBUG
    RunPhysicsDiagnosticsState& PhysicsDiagnostics();
    const RunPhysicsDiagnosticsState& PhysicsDiagnostics() const;
    bool PhysicsDiagnosticsEnabled() const;
#endif

  private:
    SkullbonezCore::Core::Profiler* m_profiler = nullptr; // Startup-bound diagnostics source; null in non-profile builds.
    RunPerfLogState m_perfLog;                            // Perf CSV paths, handles, and flush policy
#ifdef _DEBUG
    RunPhysicsDiagnosticsState m_physicsDiagnostics;      // Queryable physics trace state
#endif
};
} // namespace Runtime
} // namespace SkullbonezCore
