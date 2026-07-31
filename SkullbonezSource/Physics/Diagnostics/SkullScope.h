/*
File: SkullbonezSource/Physics/Diagnostics/SkullScope.h
Purpose:
  Defines compact physics diagnostics records emitted for SkullScope queries.

Summary:
  Physics owns the stores, contacts, pipeline records, and sleep-island facts
  sampled by each trace. Keeping the bounded writer beside those inputs makes
  the diagnostic dependency point inward to Physics rather than upward from
  Core.

Invariants:
  - SkullScope state is debug-only and records run-local counters, not global
    engine state.
  - SetPath and SetRunId reset derived state so a new trace cannot inherit
    penetration-window counters from a previous run.

Related:
  - SkullbonezSource/Physics/Diagnostics/SkullScope.cpp
  - SkullbonezSource/Physics/PhysicsDiagnosticsSink.h
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


namespace SkullbonezCore::Physics
{
struct PhysicsDiagnosticsFrameInput;

namespace Diagnostics
{
class SkullScope final
{
  public:
#ifdef _DEBUG
    void SetPath( const char* path );
    void SetRunId( const char* runId );

    // Returns whether a frame trace has both an output path and run id. Callers
    // can use this to avoid gathering cold presentation data when tracing is off.
    bool IsFrameEnabled() const;

    // Emits one bounded trace frame from physics-owned stores and diagnostics
    // views. The frame input must outlive the call but is never retained.
    void EmitFrame( const PhysicsDiagnosticsFrameInput& frame );
#endif

  private:
#ifdef _DEBUG
    void ResetRunState();
    void ResetPenetrationState();

    char m_physicsDiagnosticsPath[256] = {};
    char m_physicsDiagnosticsRunId[32] = {};
    int m_physicsDiagnosticsFrame = 0;
    int m_physicsDiagnosticsEventCounter = 0;
    double m_physicsDiagnosticsTimeSeconds = 0.0;
    double m_physicsDiagnosticsPrevEnergy = 0.0;
    bool m_physicsDiagnosticsHasPrevEnergy = false;
    char m_physicsDiagnosticsPenetrationContact[64] = {};
    int m_physicsDiagnosticsPenetrationFrames = 0;
    int m_physicsDiagnosticsPenetrationGrowthFrames = 0;
    double m_physicsDiagnosticsPenetrationWindowStart = 0.0;
    double m_physicsDiagnosticsPrevPenetration = 0.0;
    bool m_physicsDiagnosticsPenetrationSustainedReported = false;
    bool m_physicsDiagnosticsPenetrationGrowingReported = false;
#endif
};
} // namespace Diagnostics
} // namespace SkullbonezCore::Physics
