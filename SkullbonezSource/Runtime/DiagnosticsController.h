/*
File: SkullbonezSource/Runtime/DiagnosticsController.h
Purpose:
  Owns runtime diagnostics state for performance logs and SkullScope runs.

Mental model:
  DiagnosticsController is the mutable diagnostics owner. RuntimeDiagnostics
  still formats artifacts, but Run no longer directly owns the perf and physics
  diagnostic structs.

Related:
  - SkullbonezSource/Runtime/RuntimeDiagnostics.h
  - SkullbonezSource/Runtime/RunFrame.cpp
*/
#pragma once

#include "RuntimeDiagnostics.h"

namespace SkullbonezCore
{
namespace Basics
{
class DiagnosticsController
{
  public:
    RunPerfLogState& PerfLog();
    const RunPerfLogState& PerfLog() const;

    void ClosePerfLog();
    void LogPerfMemory( int pass, const char* checkpoint );
    void TickPerfLog( const RuntimePerfTickContext& context );

#ifdef _DEBUG
    RunPhysicsDiagnosticsState& PhysicsDiagnostics();
    const RunPhysicsDiagnosticsState& PhysicsDiagnostics() const;
    bool PhysicsDiagnosticsEnabled() const;
#endif

  private:
    RunPerfLogState m_perfLog;                       // Perf CSV paths, handles, and flush policy
#ifdef _DEBUG
    RunPhysicsDiagnosticsState m_physicsDiagnostics; // Queryable physics trace state
#endif
};
} // namespace Basics
} // namespace SkullbonezCore
