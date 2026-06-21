/*
File: SkullbonezSource/RuntimeDiagnostics.h
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
  SkullScope: Queryable physics diagnostics trace workflow used instead of
  loading raw traces into model context.

Related:
  - SkullbonezSource/RuntimeDiagnostics.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdio>
#include <cstdint>

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}

namespace Basics
{
class EngineConfig;
struct ReplayBodyPresentationSample;
struct ReplayPresentationSample;
struct RunSceneState;

struct RunPerfLogState
{
    bool isPerfTest = false;            // Performance logging mode
    bool perfHeaderWritten = false;     // CSV header written for current perf run
    char perfLogPath[256] = {};         // Output path for perf CSV (empty = none)
    FILE* perfLogFile = nullptr;        // Open handle for perf CSV
    bool isPerfLogFlushEnabled = false; // Flush perf CSV on each write (diagnostic mode)
    int perfLogFlushInterval = 0;       // Flush perf CSV every N writes (0 = flush on close only)
    int perfLogWritesSinceFlush = 0;    // Buffered perf-log write count since last flush
#ifdef _DEBUG
    char physicsRegressionLogOverride[256] = {};    // CLI --physics-regression-log path (empty = disabled)
    char physicsCollisionTimeLogOverride[256] = {}; // CLI --physics-collision-time-log path (empty = disabled)
#endif
};

#ifdef _DEBUG
struct RunPhysicsDiagnosticsState
{
    char path[256] = {};                       // CLI --physics-diag path (empty = disabled)
    char currentRunId[32] = {};                // Stable per-load id written into NDJSON rows
    bool isEnabled = false;                    // True when a diagnostics path was provided
    bool isRunActive = false;                  // True after a run row and before the matching end row
    bool fixedStepForcedByDiagnostics = false; // True when --physics-diag forced fixed-step mode
    int runSequence = 0;                       // Incremented on every scene/generated load
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
    static void ClosePerfLog( RunPerfLogState& perfLog );
    static void LogPerfMemory( RunPerfLogState& perfLog, int pass, const char* checkpoint );
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
    static void
    EndPhysicsDiagnosticsRun( RunPhysicsDiagnosticsState& diagnostics, const RunSceneState& scene, const char* status );
#endif
};
} // namespace Basics
} // namespace SkullbonezCore
