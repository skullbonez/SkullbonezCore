/*
File: SkullbonezSource/Runtime/DiagnosticsController.cpp
Purpose:
  Implements runtime diagnostics state ownership.

Mental model:
  The controller stores mutable diagnostics state and delegates artifact writes
  to RuntimeDiagnostics so existing output stays byte-for-byte compatible.
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


void DiagnosticsController::LogPerfMemory( int pass, const char* checkpoint )
{
    RuntimeDiagnostics::LogPerfMemory( m_perfLog, pass, checkpoint );
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
