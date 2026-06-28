/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp
Purpose:
  Provides the runtime diagnostics ownership boundary.

Mental model:
  Run asks DiagnosticsRuntime for capture, performance, memory, and physics
  diagnostic work. This file mostly forwards to the underlying controllers, but
  owns the memory snapshot cache and the shutdown memory-dump artifact.

Glossary:
  Artifact path: Stable validation/debug output path written for tools or
    command-line flags.
  Reconciled memory: Tracked engine bytes plus any process memory not accounted
    for by replay or model collection snapshots.
  SkullScope: Queryable physics diagnostics trace owned by RuntimeDiagnostics.
  JSON (JavaScript Object Notation): Text artifact format used for memory dumps.

Invariants:
  - DiagnosticsRuntime is a boundary; artifact schema and heavy logging formats
    stay in RuntimeDiagnostics or CaptureController unless this file owns them.
  - Memory sampling is cached for UI reads and forced only for explicit dumps.
  - Debug-only physics diagnostics stay behind _DEBUG.

Related:
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
  - SkullbonezSource/Runtime/DiagnosticsController.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
*/
#include "DiagnosticsRuntime.h"

#include "../Replay/ReplayRuntime.h"
#include "../Scene/SceneRuntime.h"
#include "../../Scene/TestScene.h"
#include "../../GameObjects/GameModelCollection.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
constexpr double MAIN_MEMORY_SAMPLE_INTERVAL_SECONDS = 1.0;

void WriteJsonString( FILE* file, const char* value )
{
    // Concept: Memory dumps are written without a JSON library, so this helper
    // is the narrow escaping boundary for user-controlled checkpoint/path text.
    fputc( '"', file );
    if ( value )
    {
        for ( const char* cursor = value; *cursor != '\0'; ++cursor )
        {
            switch ( *cursor )
            {
            case '\\':
                fputs( "\\\\", file );
                break;
            case '"':
                fputs( "\\\"", file );
                break;
            case '\n':
                fputs( "\\n", file );
                break;
            case '\r':
                fputs( "\\r", file );
                break;
            case '\t':
                fputs( "\\t", file );
                break;
            default:
                fputc( *cursor, file );
                break;
            }
        }
    }
    fputc( '"', file );
}
} // namespace

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


void DiagnosticsRuntime::ClosePerfLogWithMemoryCheckpoint( int pass, const char* checkpoint )
{
    m_diagnostics.ClosePerfLogWithMemoryCheckpoint( pass, checkpoint );
}


void DiagnosticsRuntime::LogPerfMemory( int pass, const char* checkpoint )
{
    m_diagnostics.LogPerfMemory( pass, checkpoint );
}


void DiagnosticsRuntime::ResetPerfLogForSceneLoad()
{
    m_diagnostics.ResetPerfLogForSceneLoad();
}


void DiagnosticsRuntime::ConfigurePerfLogFlush( bool enabled, int interval )
{
    m_diagnostics.ConfigurePerfLogFlush( enabled, interval );
}


void DiagnosticsRuntime::OpenScenePerfLog( const char* path, int pass )
{
    m_diagnostics.OpenScenePerfLog( path, pass );
}


void DiagnosticsRuntime::ApplySceneAutomationOptions( const TestScene& scene,
                                                      bool suppressAutomationExit,
                                                      int perfPass )
{
    // Concept: Scene-authored screenshot and perf-log directives are
    // diagnostics automation. Keep the artifact state with DiagnosticsRuntime
    // while scene loading decides when to call it.
    RunScreenshotState& screenshot = m_capture.Screenshot();
    screenshot.screenshotFrame = scene.GetScreenshotFrame();
    screenshot.screenshotMs = scene.GetScreenshotMs();
    screenshot.isScreenshotAndExit = suppressAutomationExit ? false : scene.IsScreenshotAndExit();

    if ( scene.GetScreenshotPath()[0] != '\0' )
    {
        strcpy_s( screenshot.screenshotPath, sizeof( screenshot.screenshotPath ), scene.GetScreenshotPath() );
    }

    screenshot.screenshotInterval = scene.GetScreenshotInterval();
    if ( scene.GetScreenshotDir()[0] != '\0' )
    {
        strcpy_s( screenshot.screenshotDir, sizeof( screenshot.screenshotDir ), scene.GetScreenshotDir() );
        CreateDirectoryA( screenshot.screenshotDir, nullptr );
    }

    const char* perfPath = scene.GetPerfLogPath();
    if ( perfPath[0] != '\0' )
    {
        OpenScenePerfLog( perfPath, perfPass );
    }
}


bool DiagnosticsRuntime::PerfTestActive() const
{
    return m_diagnostics.PerfTestActive();
}


void DiagnosticsRuntime::TickPerfLog( const RuntimePerfTickContext& context )
{
    m_diagnostics.TickPerfLog( context );
}

const MainMemoryStats& DiagnosticsRuntime::RefreshMainMemoryStats( const ReplayRuntime& replay,
                                                                   const GameObjects::GameModelCollection& models,
                                                                   double nowSeconds,
                                                                   bool force )
{
    const bool sampleDue = m_lastMainMemorySampleSeconds < 0.0 ||
                           nowSeconds - m_lastMainMemorySampleSeconds >= MAIN_MEMORY_SAMPLE_INTERVAL_SECONDS;
    if ( !force && !sampleDue )
    {
        return m_mainMemoryStats;
    }

    // Concept: The UI wants cheap repeated reads, while shutdown dumps need a
    // fresh sample. Build one reconciled snapshot, then cache it with the sample
    // timestamp.
    MainMemoryStats stats;
    stats.sampleTimeSeconds = nowSeconds;
    stats.process = RuntimeDiagnostics::SampleProcessMemory();
    stats.replay = replay.CollectMemoryStats();
    stats.gameObjects = models.CollectMemoryStats();
    stats.trackedEngineBytes = stats.replay.totalBytes + stats.gameObjects.totalBytes + stats.otherTrackedBytes;
    if ( stats.process.available )
    {
        // Why: Task Manager numbers include memory not tracked by replay or
        // GameModelCollection. The reconciliation fields make that gap explicit
        // instead of hiding it in the engine bucket.
        if ( stats.process.taskManagerBytes >= stats.trackedEngineBytes )
        {
            stats.unattributedProcessBytes = stats.process.taskManagerBytes - stats.trackedEngineBytes;
            stats.reconciledTotalBytes = stats.process.taskManagerBytes;
        }
        else
        {
            stats.trackedOvershootBytes = stats.trackedEngineBytes - stats.process.taskManagerBytes;
        }
        stats.reconciledTotalBytes =
            stats.trackedEngineBytes + stats.unattributedProcessBytes - stats.trackedOvershootBytes;
        stats.reconciliationDeltaBytes = stats.reconciledTotalBytes >= stats.process.taskManagerBytes
                                             ? stats.reconciledTotalBytes - stats.process.taskManagerBytes
                                             : stats.process.taskManagerBytes - stats.reconciledTotalBytes;
    }
    else
    {
        stats.reconciledTotalBytes = stats.trackedEngineBytes;
    }

    m_mainMemoryStats = stats;
    m_lastMainMemorySampleSeconds = nowSeconds;
    return m_mainMemoryStats;
}


const MainMemoryStats& DiagnosticsRuntime::MainMemoryStatsSnapshot() const
{
    return m_mainMemoryStats;
}


void DiagnosticsRuntime::SetMainMemoryDumpPath( const char* path )
{
    if ( !path )
    {
        m_mainMemoryDumpPath[0] = '\0';
        return;
    }
    strncpy_s( m_mainMemoryDumpPath, sizeof( m_mainMemoryDumpPath ), path, _TRUNCATE );
}


const char* DiagnosticsRuntime::MainMemoryDumpPath() const
{
    return m_mainMemoryDumpPath;
}


bool DiagnosticsRuntime::MainMemoryDumpRequested() const
{
    return m_mainMemoryDumpPath[0] != '\0';
}


bool DiagnosticsRuntime::WriteMainMemoryDump( const ReplayRuntime& replay,
                                              const GameObjects::GameModelCollection& models,
                                              const RunSceneState& scene,
                                              const char* checkpoint,
                                              double nowSeconds )
{
    if ( !MainMemoryDumpRequested() )
    {
        return false;
    }

    const MainMemoryStats& stats = RefreshMainMemoryStats( replay, models, nowSeconds, true );
    FILE* file = nullptr;
    if ( fopen_s( &file, m_mainMemoryDumpPath, "wb" ) != 0 || !file )
    {
        fprintf( stderr, "[memory] Failed to open memory dump: %s\n", m_mainMemoryDumpPath );
        return false;
    }

    // Invariant: skullbonez.main_memory.v1 is a script-facing artifact schema.
    // Add fields compatibly or bump the schema if a consumer must change.
    fputs( "{\n  \"schema\": \"skullbonez.main_memory.v1\",\n  \"checkpoint\": ", file );
    WriteJsonString( file, checkpoint && checkpoint[0] != '\0' ? checkpoint : "shutdown" );
    fprintf( file,
             ",\n  \"frame\": %d,\n"
             "  \"sample_time_seconds\": %.6f,\n"
             "  \"process\": {\n"
             "    \"available\": %s,\n"
             "    \"task_manager_metric_name\": \"%s\",\n"
             "    \"task_manager_memory_bytes\": %llu,\n"
             "    \"working_set_bytes\": %llu,\n"
             "    \"private_working_set_bytes\": %llu,\n"
             "    \"private_commit_bytes\": %llu,\n"
             "    \"pagefile_usage_bytes\": %llu\n"
             "  },\n"
             "  \"replay\": {\n"
             "    \"total_bytes\": %llu,\n"
             "    \"presentation_bytes\": %llu,\n"
             "    \"solver_bytes\": %llu,\n"
             "    \"events_bytes\": %llu,\n"
             "    \"loaded_replay_bytes\": %llu,\n"
             "    \"prediction_bytes\": %llu,\n"
             "    \"path_and_cause_bytes\": %llu,\n"
             "    \"render_scratch_bytes\": %llu,\n"
             "    \"presentation_samples\": %llu,\n"
             "    \"solver_samples\": %llu,\n"
             "    \"event_samples\": %llu,\n"
             "    \"loaded_replay_samples\": %llu,\n"
             "    \"prediction_frames\": %llu,\n"
             "    \"path_nodes\": %llu,\n"
             "    \"cause_rows\": %llu,\n"
             "    \"ghost_requests\": %llu\n"
             "  },\n"
             "  \"game_objects\": {\n"
             "    \"total_bytes\": %llu,\n"
             "    \"model_bytes\": %llu,\n"
             "    \"model_vector_bytes\": %llu,\n"
             "    \"soa_cache_bytes\": %llu,\n"
             "    \"physics_store_bytes\": %llu,\n"
             "    \"collider_store_bytes\": %llu,\n"
             "    \"render_store_bytes\": %llu,\n"
             "    \"physics_world_bytes\": %llu,\n"
             "    \"debug_and_broadphase_bytes\": %llu,\n"
             "    \"model_count\": %llu,\n"
             "    \"model_capacity\": %llu,\n"
             "    \"body_store_capacity\": %llu,\n"
             "    \"collider_store_capacity\": %llu,\n"
             "    \"render_store_capacity\": %llu\n"
             "  },\n"
             "  \"tracked_engine_bytes\": %llu,\n"
             "  \"other_tracked_bytes\": %llu,\n"
             "  \"unattributed_process_bytes\": %llu,\n"
             "  \"tracked_overshoot_bytes\": %llu,\n"
             "  \"reconciled_total_bytes\": %llu,\n"
             "  \"reconciliation_delta_bytes\": %llu,\n"
             "  \"scene\": {\n"
             "    \"current_frame\": %d,\n"
             "    \"target_frames\": %d,\n"
             "    \"model_count\": %d,\n"
             "    \"test_complete\": %s\n"
             "  }\n"
             "}\n",
             scene.currentFrame,
             stats.sampleTimeSeconds,
             stats.process.available ? "true" : "false",
             stats.process.taskManagerMetricName,
             static_cast<unsigned long long>( stats.process.taskManagerBytes ),
             static_cast<unsigned long long>( stats.process.workingSetBytes ),
             static_cast<unsigned long long>( stats.process.privateWorkingSetBytes ),
             static_cast<unsigned long long>( stats.process.privateCommitBytes ),
             static_cast<unsigned long long>( stats.process.pagefileUsageBytes ),
             static_cast<unsigned long long>( stats.replay.totalBytes ),
             static_cast<unsigned long long>( stats.replay.presentationBytes ),
             static_cast<unsigned long long>( stats.replay.solverBytes ),
             static_cast<unsigned long long>( stats.replay.eventsBytes ),
             static_cast<unsigned long long>( stats.replay.loadedReplayBytes ),
             static_cast<unsigned long long>( stats.replay.predictionBytes ),
             static_cast<unsigned long long>( stats.replay.pathAndCauseBytes ),
             static_cast<unsigned long long>( stats.replay.renderScratchBytes ),
             static_cast<unsigned long long>( stats.replay.presentationSamples ),
             static_cast<unsigned long long>( stats.replay.solverSamples ),
             static_cast<unsigned long long>( stats.replay.eventSamples ),
             static_cast<unsigned long long>( stats.replay.loadedReplaySamples ),
             static_cast<unsigned long long>( stats.replay.predictionFrames ),
             static_cast<unsigned long long>( stats.replay.pathNodes ),
             static_cast<unsigned long long>( stats.replay.causeRows ),
             static_cast<unsigned long long>( stats.replay.ghostRequests ),
             static_cast<unsigned long long>( stats.gameObjects.totalBytes ),
             static_cast<unsigned long long>( stats.gameObjects.modelVectorBytes ),
             static_cast<unsigned long long>( stats.gameObjects.modelVectorBytes ),
             static_cast<unsigned long long>( stats.gameObjects.soaCacheBytes ),
             static_cast<unsigned long long>( stats.gameObjects.physicsStoreBytes ),
             static_cast<unsigned long long>( stats.gameObjects.colliderStoreBytes ),
             static_cast<unsigned long long>( stats.gameObjects.renderStoreBytes ),
             static_cast<unsigned long long>( stats.gameObjects.physicsWorldBytes ),
             static_cast<unsigned long long>( stats.gameObjects.debugAndBroadphaseBytes ),
             static_cast<unsigned long long>( stats.gameObjects.modelCount ),
             static_cast<unsigned long long>( stats.gameObjects.modelCapacity ),
             static_cast<unsigned long long>( stats.gameObjects.bodyStoreCapacity ),
             static_cast<unsigned long long>( stats.gameObjects.colliderStoreCapacity ),
             static_cast<unsigned long long>( stats.gameObjects.renderStoreCapacity ),
             static_cast<unsigned long long>( stats.trackedEngineBytes ),
             static_cast<unsigned long long>( stats.otherTrackedBytes ),
             static_cast<unsigned long long>( stats.unattributedProcessBytes ),
             static_cast<unsigned long long>( stats.trackedOvershootBytes ),
             static_cast<unsigned long long>( stats.reconciledTotalBytes ),
             static_cast<unsigned long long>( stats.reconciliationDeltaBytes ),
             scene.currentFrame,
             scene.targetFrameCount,
             scene.modelCount,
             scene.isTestComplete ? "true" : "false" );

    fclose( file );
    fprintf( stdout, "[memory] Wrote main memory dump: %s\n", m_mainMemoryDumpPath );
    return true;
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
    // Lifetime: RuntimeDiagnostics owns the trace file/session. This boundary
    // only supplies current runtime state and never caches trace handles.
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
    // Invariant: Replay restore diagnostics are forwarded with their exact
    // hashes, counts, and flags so SkullScope queries can distinguish checkpoint
    // restores from fallback restores.
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
