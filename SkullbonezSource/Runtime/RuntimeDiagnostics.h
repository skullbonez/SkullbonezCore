/*
File: SkullbonezSource/Runtime/RuntimeDiagnostics.h
Purpose:
  Owns runtime diagnostic state and logging policy for perf CSVs and SkullScope runs.

Mental model:
  Runtime diagnostics are side-channel artifacts. The run loop supplies scene
  and frame context, while this subsystem owns how that context is written.

Glossary:
  CLI (Command-Line Interface): Flags passed to SKULLBONEZ_CORE.exe at launch.
  CSV (Comma-Separated Values): Text table format used for perf and physics
  regression output.
  NDJSON (Newline-Delimited JSON): One JSON object per line, used by SkullScope
    traces so tools can stream bounded queries.
  Private working set: Resident process pages not shared with other processes;
    matching it requires a page-level OS query.
  SkullScope: Queryable physics diagnostics trace workflow used instead of
    loading raw traces into model context.
  Contact-audio frame aggregate: One diagnostic row summarizing how many raw
    contact facts became reduced patch candidates and submitted voices.

Invariants:
  - Diagnostic artifacts are side-channel output; enabling them must not change
  physics state except for explicitly forced fixed-step diagnostics.

Related:
  - SkullbonezSource/Runtime/RuntimeDiagnostics.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdint>

#include "../Core/MainMemoryStats.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}

namespace Runtime
{
namespace Audio
{
struct ContactAudioDecision;
struct ContactAudioStats;
} // namespace Audio
} // namespace Runtime

namespace Basics
{
class EngineConfig;
struct ReplayBodyPresentationSample;
struct ReplayPresentationSample;
struct ReplaySolverFrameSample;
struct RunSceneState;

struct RunPerfLogState
{
    bool isPerfTest = false;                        // Perf-suite mode; runtime writes pass/frame rows while scene advances.
    bool perfHeaderWritten = false;                 // Prevents duplicate CSV headers across passes in one run.
    char perfLogPath[256] = {};                     // Output path for perf CSV; empty disables file logging.
    FILE* perfLogFile = nullptr;                    // Open perf CSV handle owned by RuntimeDiagnostics until ClosePerfLog.
    bool isPerfLogFlushEnabled = false;             // Diagnostic mode: force flush after every perf write.
    int perfLogFlushInterval = 0;                   // Flush every N writes; 0 means flush only at close.
    int perfLogWritesSinceFlush = 0;                // Buffered perf-log rows since last explicit flush.
#ifdef _DEBUG
    char physicsRegressionLogOverride[256] = {};    // CLI --physics-regression-log path (empty = disabled)
    char physicsCollisionTimeLogOverride[256] = {}; // CLI --physics-collision-time-log path (empty = disabled)
#endif
};

#ifdef _DEBUG
struct RunPhysicsDiagnosticsState
{
    char path[256] = {};                            // CLI --physics-diag path (empty = disabled)
    char currentRunId[32] = {};                     // Stable per-load id written into NDJSON rows
    bool isEnabled = false;                         // True when a diagnostics path was provided
    bool isRunActive = false;                       // True after a run row and before the matching end row
    bool fixedStepForcedByDiagnostics = false;      // True when --physics-diag forced fixed-step mode
    int runSequence = 0;                            // Incremented on every scene/generated load
    uint32_t contactAudioEventSequence = 0;         // Unique event ids for runtime-side contact audio verdicts.
};
#endif

struct RuntimePerfTickContext
{
    int pass = 0;
    int frame = 0;
    float physicsTimeSeconds = 0.0f;
    float renderTimeSeconds = 0.0f;
};

class RuntimeDiagnostics
{
  public:
    // Samples cheap process counters; includePrivateWorkingSet adds a
    // full resident-page walk for diagnostics that need Task Manager parity.
    static MainMemoryProcessStats SampleProcessMemory( bool includePrivateWorkingSet );
    static void ClosePerfLog( RunPerfLogState& perfLog );
    static void ClosePerfLogWithMemoryCheckpoint( RunPerfLogState& perfLog, int pass, const char* checkpoint );
    static void LogPerfMemory( RunPerfLogState& perfLog, int pass, const char* checkpoint );
    static void ResetPerfLogForSceneLoad( RunPerfLogState& perfLog );
    static void ConfigurePerfLogFlush( RunPerfLogState& perfLog, bool enabled, int interval );
    static void OpenScenePerfLog( RunPerfLogState& perfLog, const char* path, int pass );
    static bool PerfTestActive( const RunPerfLogState& perfLog );
    static void TickPerfLog( RunPerfLogState& perfLog, const RuntimePerfTickContext& context );

#ifdef _DEBUG
    static void SetPhysicsRegressionLogOverride( RunPerfLogState& perfLog, const char* path );
    static void SetPhysicsCollisionTimeLogOverride( RunPerfLogState& perfLog, const char* path );
    static void SetPhysicsDiagnosticsPath( RunPhysicsDiagnosticsState& diagnostics,
                                           GameObjects::GameModelCollection& models,
                                           const char* path,
                                           bool fixedStepForcedByDiagnostics );
    static void
    LogSceneFinished( RunSceneState& scene, const char* scenePath, const char* rendererName, const char* reason );
    static void BeginPhysicsDiagnosticsRun( RunPhysicsDiagnosticsState& diagnostics,
                                            GameObjects::GameModelCollection& models,
                                            const RunSceneState& scene,
                                            const EngineConfig& config,
                                            const char* scenePath,
                                            const char* rendererName );
    static void LogReplayScrubProbe( RunPhysicsDiagnosticsState& diagnostics,
                                     const RunSceneState& scene,
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
                                     float restoredDeltaSquared );
    static void LogReplayRestoreProbe( RunPhysicsDiagnosticsState& diagnostics,
                                       const RunSceneState& scene,
                                       const ReplaySolverFrameSample& selected,
                                       uint64_t restoredSolverHash,
                                       uint64_t restoredPresentationHash,
                                       std::size_t restoredBodyCount,
                                       bool hashCaptured,
                                       bool hashMatched,
                                       bool fallbackAttempted,
                                       bool fallbackRestored );
    static void LogReplayRestoreResult( RunPhysicsDiagnosticsState& diagnostics,
                                        const RunSceneState& scene,
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
                                        const char* failureReason );
    static void LogContactAudioDecision( RunPhysicsDiagnosticsState& diagnostics,
                                         const RunSceneState& scene,
                                         const Runtime::Audio::ContactAudioDecision& decision );
    static void LogContactAudioStepStats( RunPhysicsDiagnosticsState& diagnostics,
                                          const RunSceneState& scene,
                                          const Runtime::Audio::ContactAudioStats& stats );
    static void
    EndPhysicsDiagnosticsRun( RunPhysicsDiagnosticsState& diagnostics, const RunSceneState& scene, const char* status );
#endif
};
} // namespace Basics
} // namespace SkullbonezCore
