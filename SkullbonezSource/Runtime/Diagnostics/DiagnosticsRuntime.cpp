/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp
Purpose:
  Provides the runtime diagnostics ownership boundary.
*/
#include "DiagnosticsRuntime.h"

namespace SkullbonezCore
{
namespace Basics
{
CaptureController& DiagnosticsRuntime::Capture()
{
    return m_capture;
}


const CaptureController& DiagnosticsRuntime::Capture() const
{
    return m_capture;
}


DiagnosticsController& DiagnosticsRuntime::Diagnostics()
{
    return m_diagnostics;
}


const DiagnosticsController& DiagnosticsRuntime::Diagnostics() const
{
    return m_diagnostics;
}


RunPerfLogState& DiagnosticsRuntime::PerfLog()
{
    return m_diagnostics.PerfLog();
}


const RunPerfLogState& DiagnosticsRuntime::PerfLog() const
{
    return m_diagnostics.PerfLog();
}


void DiagnosticsRuntime::ClosePerfLog()
{
    m_diagnostics.ClosePerfLog();
}


void DiagnosticsRuntime::LogPerfMemory( int pass, const char* checkpoint )
{
    m_diagnostics.LogPerfMemory( pass, checkpoint );
}


void DiagnosticsRuntime::TickPerfLog( const RuntimePerfTickContext& context )
{
    m_diagnostics.TickPerfLog( context );
}


#ifdef _DEBUG
RunPhysicsDiagnosticsState& DiagnosticsRuntime::PhysicsDiagnostics()
{
    return m_diagnostics.PhysicsDiagnostics();
}


const RunPhysicsDiagnosticsState& DiagnosticsRuntime::PhysicsDiagnostics() const
{
    return m_diagnostics.PhysicsDiagnostics();
}


bool DiagnosticsRuntime::PhysicsDiagnosticsEnabled() const
{
    return m_diagnostics.PhysicsDiagnosticsEnabled();
}


void DiagnosticsRuntime::SetPhysicsRegressionLogOverride( const char* path )
{
    RuntimeDiagnostics::SetPhysicsRegressionLogOverride( m_diagnostics.PerfLog(), path );
}


void DiagnosticsRuntime::SetPhysicsCollisionTimeLogOverride( const char* path )
{
    RuntimeDiagnostics::SetPhysicsCollisionTimeLogOverride( m_diagnostics.PerfLog(), path );
}


void DiagnosticsRuntime::SetPhysicsDiagnosticsPath( GameObjects::GameModelCollection& models,
                                                    const char* path,
                                                    bool fixedStepForcedByDiagnostics )
{
    RuntimeDiagnostics::SetPhysicsDiagnosticsPath( m_diagnostics.PhysicsDiagnostics(),
                                                   models,
                                                   path,
                                                   fixedStepForcedByDiagnostics );
}


void DiagnosticsRuntime::LogSceneFinished( RunSceneState& scene,
                                           const char* scenePath,
                                           const char* rendererName,
                                           const char* reason )
{
    RuntimeDiagnostics::LogSceneFinished( scene, scenePath, rendererName, reason );
}


void DiagnosticsRuntime::BeginPhysicsDiagnosticsRun( GameObjects::GameModelCollection& models,
                                                     const RunSceneState& scene,
                                                     const EngineConfig& config,
                                                     const char* scenePath,
                                                     const char* rendererName )
{
    RuntimeDiagnostics::BeginPhysicsDiagnosticsRun( m_diagnostics.PhysicsDiagnostics(),
                                                    models,
                                                    scene,
                                                    config,
                                                    scenePath,
                                                    rendererName );
}


void DiagnosticsRuntime::LogReplayScrubProbe( const RunSceneState& scene,
                                              const ReplayPresentationSample& selected,
                                              const ReplayPresentationSample& live,
                                              const ReplayBodyPresentationSample& selectedBody,
                                              const ReplayBodyPresentationSample& liveBody,
                                              float normalized,
                                              float distanceSquared,
                                              bool applied,
                                              bool restored,
                                              float preLiveDeltaSquared,
                                              float appliedDeltaSquared,
                                              float restoredDeltaSquared )
{
    RuntimeDiagnostics::LogReplayScrubProbe( m_diagnostics.PhysicsDiagnostics(),
                                             scene,
                                             selected,
                                             live,
                                             selectedBody,
                                             liveBody,
                                             normalized,
                                             distanceSquared,
                                             applied,
                                             restored,
                                             preLiveDeltaSquared,
                                             appliedDeltaSquared,
                                             restoredDeltaSquared );
}


void DiagnosticsRuntime::LogReplayRestoreProbe( const RunSceneState& scene,
                                                const ReplaySolverFrameSample& selected,
                                                uint64_t restoredSolverHash,
                                                uint64_t restoredPresentationHash,
                                                std::size_t restoredBodyCount,
                                                bool hashCaptured,
                                                bool hashMatched,
                                                bool fallbackAttempted,
                                                bool fallbackRestored )
{
    RuntimeDiagnostics::LogReplayRestoreProbe( m_diagnostics.PhysicsDiagnostics(),
                                               scene,
                                               selected,
                                               restoredSolverHash,
                                               restoredPresentationHash,
                                               restoredBodyCount,
                                               hashCaptured,
                                               hashMatched,
                                               fallbackAttempted,
                                               fallbackRestored );
}


void DiagnosticsRuntime::LogReplayRestoreResult( const RunSceneState& scene,
                                                 const char* restoreSource,
                                                 uint64_t targetReplayFrame,
                                                 int targetSceneFrame,
                                                 uint64_t checkpointReplayFrame,
                                                 uint64_t targetSolverHash,
                                                 uint64_t targetPresentationHash,
                                                 std::size_t targetBodyCount,
                                                 uint64_t restoredSolverHash,
                                                 uint64_t restoredPresentationHash,
                                                 std::size_t restoredBodyCount,
                                                 uint16_t contactCount,
                                                 uint16_t pipelineRecordCount,
                                                 bool checkpointBoundary,
                                                 bool hashCaptured,
                                                 bool hashMatched,
                                                 bool fallbackAttempted,
                                                 bool fallbackRestored,
                                                 const char* failureReason )
{
    RuntimeDiagnostics::LogReplayRestoreResult( m_diagnostics.PhysicsDiagnostics(),
                                                scene,
                                                restoreSource,
                                                targetReplayFrame,
                                                targetSceneFrame,
                                                checkpointReplayFrame,
                                                targetSolverHash,
                                                targetPresentationHash,
                                                targetBodyCount,
                                                restoredSolverHash,
                                                restoredPresentationHash,
                                                restoredBodyCount,
                                                contactCount,
                                                pipelineRecordCount,
                                                checkpointBoundary,
                                                hashCaptured,
                                                hashMatched,
                                                fallbackAttempted,
                                                fallbackRestored,
                                                failureReason );
}


void DiagnosticsRuntime::EndPhysicsDiagnosticsRun( const RunSceneState& scene, const char* status )
{
    RuntimeDiagnostics::EndPhysicsDiagnosticsRun( m_diagnostics.PhysicsDiagnostics(), scene, status );
}
#endif


DiagnosticsRuntime::UIStressState& DiagnosticsRuntime::UIStress()
{
    return m_uiStress;
}


const DiagnosticsRuntime::UIStressState& DiagnosticsRuntime::UIStress() const
{
    return m_uiStress;
}
} // namespace Basics
} // namespace SkullbonezCore
