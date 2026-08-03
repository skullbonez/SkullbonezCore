/*
File: SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h
Purpose:
  Owns runtime diagnostic state and logging policy for perf CSVs and SkullScope runs.

Summary:
  Runtime diagnostics are side-channel artifacts. The run loop supplies scene
  and frame context, while this subsystem owns how that context is written.

Glossary:
  NDJSON (Newline-Delimited JSON): One JSON object per line, used by SkullScope
    traces so tools can stream bounded queries.

Invariants:
  - Diagnostic artifacts are side-channel output; enabling them must not change
  physics state except for explicitly forced fixed-step diagnostics.

Related:
  - SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdint>

#include "../../Core/MainMemoryStats.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class Profiler;
} // namespace Core
namespace Physics
{
class PhysicsEngine;
}
namespace Runtime
{
struct SceneSessionState;

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
};

struct ReplayScrubProbeDiagnostic
{

    // Lifetime: bodyName is consumed synchronously while one NDJSON row is
    // written. Every other field is a value snapshot of that probe result.
    uint64_t selectedReplayFrame = 0;
    uint64_t liveReplayFrame = 0;
    int selectedSceneFrame = 0;
    int liveSceneFrame = 0;
    uint64_t selectedStateHash = 0;
    uint64_t liveStateHash = 0;
    uint32_t bodyId = 0;
    int modelIndex = 0;
    const char* bodyName = nullptr;
    float selectedPosition[3] = {};
    float livePosition[3] = {};
    float normalized = 0.0f;
    float distanceSquared = 0.0f;
    std::size_t selectedBodyCount = 0;
    std::size_t liveBodyCount = 0;
    bool applied = false;
    bool restored = false;
    float preLiveDeltaSquared = 0.0f;
    float appliedDeltaSquared = 0.0f;
    float restoredDeltaSquared = 0.0f;
};

struct ReplayRestoreProbeDiagnostic
{

    // Value snapshot used to translate the retained-solver probe into the
    // stable replay_restore NDJSON schema without borrowing replay owners.
    uint64_t targetReplayFrame = 0;
    int targetSceneFrame = 0;
    uint64_t targetSolverHash = 0;
    uint64_t targetPresentationHash = 0;
    std::size_t targetBodyCount = 0;
    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    uint16_t contactCount = 0;
    uint16_t pipelineRecordCount = 0;
    bool checkpointBoundary = false;
    bool hashCaptured = false;
    bool hashMatched = false;
    bool fallbackAttempted = false;
    bool fallbackRestored = false;
};

struct ReplayRestoreResultDiagnostic
{

    // Lifetime: strings are borrowed only for the synchronous log write; this
    // schema carries no replay, scene, or diagnostics ownership.
    const char* restoreSource = nullptr;
    uint64_t targetReplayFrame = 0;
    int targetSceneFrame = 0;
    uint64_t checkpointReplayFrame = 0;
    uint64_t targetSolverHash = 0;
    uint64_t targetPresentationHash = 0;
    std::size_t targetBodyCount = 0;
    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    uint16_t contactCount = 0;
    uint16_t pipelineRecordCount = 0;
    bool checkpointBoundary = false;
    bool hashCaptured = false;
    bool hashMatched = false;
    bool fallbackAttempted = false;
    bool fallbackRestored = false;
    const char* failureReason = nullptr;
};
#endif

struct RuntimeProfilerFrameTimes
{
    float physicsTimeSeconds = 0.0f;
    float renderTimeSeconds = 0.0f;
    float gpuFrameWorkMs = 0.0f;
};

class RuntimeDiagnostics
{
  public:

    // Samples cheap process counters; includePrivateWorkingSet adds a
    // full resident-page walk for diagnostics that need Task Manager parity.
    static SkullbonezCore::Core::MainMemoryProcessStats SampleProcessMemory( bool includePrivateWorkingSet );
    static void ClosePerfLog( RunPerfLogState& perfLog );
    static void ClosePerfLogWithMemoryCheckpoint( RunPerfLogState& perfLog, int pass, const char* checkpoint );
    static void LogPerfMemory( RunPerfLogState& perfLog, int pass, const char* checkpoint );
    static void ResetPerfLogForSceneLoad( RunPerfLogState& perfLog );
    static void ConfigurePerfLogFlush( RunPerfLogState& perfLog, bool enabled, int interval );

    // SkullbonezCore::Core::Profiler is a startup-bound optional dependency so artifact writers do
    // not reopen a process-global profiler locator while ticking frames or
    // scene automation.
    static void OpenScenePerfLog( RunPerfLogState& perfLog, const char* path, int pass,
                                  SkullbonezCore::Core::Profiler* profiler );
    static bool PerfTestActive( const RunPerfLogState& perfLog );
    static void TickPerfLog( RunPerfLogState& perfLog, int pass, int frame, float physicsTimeSeconds,
                             float renderTimeSeconds, SkullbonezCore::Core::Profiler* profiler );
    static RuntimeProfilerFrameTimes SampleProfilerFrameTimes( const SkullbonezCore::Core::Profiler* profiler );

#ifdef _DEBUG
    static void SetPhysicsRegressionLogOverride( RunPerfLogState& perfLog, const char* path );
    static void SetPhysicsCollisionTimeLogOverride( RunPerfLogState& perfLog, const char* path );

    // Diagnostics receives the physics owner directly; artifact setup never
    // needs scene lifecycle, request, or world-presentation authority.
    static void SetPhysicsDiagnosticsPath( RunPhysicsDiagnosticsState& diagnostics, Physics::PhysicsEngine& physics,
                                           const char* path, bool fixedStepForcedByDiagnostics );
    static void LogSceneFinished( SceneSessionState& scene, const char* scenePath, const char* rendererName,
                                  const char* reason );
    static void BeginPhysicsDiagnosticsRun( RunPhysicsDiagnosticsState& diagnostics, Physics::PhysicsEngine& physics,
                                            const SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config,
                                            const char* scenePath, const char* rendererName );
    static void LogReplayScrubProbe( RunPhysicsDiagnosticsState& diagnostics, const SceneSessionState& scene,
                                     const ReplayScrubProbeDiagnostic& probe );
    static void LogReplayRestoreProbe( RunPhysicsDiagnosticsState& diagnostics, const SceneSessionState& scene,
                                       const ReplayRestoreProbeDiagnostic& probe );
    static void LogReplayRestoreResult( RunPhysicsDiagnosticsState& diagnostics, const SceneSessionState& scene,
                                        const ReplayRestoreResultDiagnostic& result );
    static void EndPhysicsDiagnosticsRun( RunPhysicsDiagnosticsState& diagnostics, const SceneSessionState& scene,
                                          const char* status );
#endif
};
} // namespace Runtime
} // namespace SkullbonezCore
