/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
Purpose:
  Owns runtime diagnostics and capture controllers behind one diagnostics boundary.

Mental model:
  DiagnosticsRuntime is the Phase 7 compatibility owner. Capture, perf logs,
  and SkullScope state keep their existing controllers and artifact formats,
  while Run reaches them through one diagnostics runtime member.

Glossary:
  Capture controller: Screenshot trigger and automation state.
  Diagnostics controller: Perf CSV and queryable physics diagnostic state.
  Artifact path: Validation-facing output path that must stay stable.
  Private working set: Resident process pages not shared with other processes;
    matching it requires a page-level OS query.

Invariants:
  - Artifact formatting stays in RuntimeDiagnostics and CaptureSystem.
  - Existing output paths and command-line behavior must not drift here.

Related:
  - SkullbonezSource/Runtime/CaptureController.h
  - SkullbonezSource/Runtime/DiagnosticsController.h
*/
#pragma once

#include "../CaptureController.h"
#include "../DiagnosticsController.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}
namespace Rendering
{
class IRenderDiagnostics;
}
namespace Basics
{
class Profiler;
class ReplayRuntime;
class TestScene;
enum class RuntimeInputAction;
class RuntimeInputContext;
struct RunDebugState;

struct DiagnosticsKeyboardShortcutContext
{
    // Lifetime: borrowed for one keyboard dispatch only; diagnostics mutates
    // only debug presentation state and input-edge memory.
    RuntimeInputContext& input;
    RunDebugState& debug;
    int& cameraTrackBallIndex;
    const GameObjects::GameModelCollection& sceneEntities;
    const Rendering::IRenderDiagnostics* renderDiagnostics = nullptr;
    bool sceneMode = false;
    double simulationSeconds = 0.0;
};

void StepDiagnosticsPhysicsPipelineStage( RunDebugState& debug, int direction );
bool HandleDiagnosticsKeyboardShortcut( DiagnosticsKeyboardShortcutContext context,
                                        RuntimeInputAction action,
                                        int virtualKey );

class DiagnosticsRuntime
{
  public:
    CaptureController& Capture();
    const CaptureController& Capture() const;

    DiagnosticsController& Diagnostics();
    const DiagnosticsController& Diagnostics() const;
    // Startup binding that keeps perf CSV and frame-time diagnostics off the
    // global profiler accessor after initialization.
    void BindProfiler( Profiler* profiler );

    RunPerfLogState& PerfLog();
    const RunPerfLogState& PerfLog() const;

    void ClosePerfLog();
    void ClosePerfLogWithMemoryCheckpoint( int pass, const char* checkpoint );
    void LogPerfMemory( int pass, const char* checkpoint );
    void ResetPerfLogForSceneLoad();
    void ConfigurePerfLogFlush( bool enabled, int interval );
    void OpenScenePerfLog( const char* path, int pass );
    void ApplySceneAutomationOptions( const TestScene& scene, bool suppressAutomationExit, int perfPass );
    bool PerfTestActive() const;
    void TickPerfLog( const RuntimePerfTickContext& context );
    RuntimeProfilerFrameTimes SampleProfilerFrameTimes() const;
    const MainMemoryStats& RefreshMainMemoryStats( const ReplayRuntime& replay,
                                                   const GameObjects::GameModelCollection& models,
                                                   double nowSeconds,
                                                   bool force,
                                                   bool includePrivateWorkingSet = true );
    const MainMemoryStats& RefreshMainMemoryStats( const ReplayRuntime& replay,
                                                   const MainMemoryGameObjectStats& gameObjects,
                                                   double nowSeconds,
                                                   bool force,
                                                   bool includePrivateWorkingSet = true );
    const MainMemoryStats& MainMemoryStatsSnapshot() const;
    void SetMainMemoryDumpPath( const char* path );
    const char* MainMemoryDumpPath() const;
    bool MainMemoryDumpRequested() const;
    bool WriteMainMemoryDump( const ReplayRuntime& replay,
                              const GameObjects::GameModelCollection& models,
                              const RunSceneState& scene,
                              const char* checkpoint,
                              double nowSeconds );

#ifdef _DEBUG
    RunPhysicsDiagnosticsState& PhysicsDiagnostics();
    const RunPhysicsDiagnosticsState& PhysicsDiagnostics() const;
    bool PhysicsDiagnosticsEnabled() const;

    void SetPhysicsRegressionLogOverride( const char* path );
    void SetPhysicsCollisionTimeLogOverride( const char* path );
    void SetPhysicsDiagnosticsPath( GameObjects::GameModelCollection& models,
                                    const char* path,
                                    bool fixedStepForcedByDiagnostics );
    void LogSceneFinished( RunSceneState& scene, const char* scenePath, const char* rendererName, const char* reason );
    void BeginPhysicsDiagnosticsRun( GameObjects::GameModelCollection& models,
                                     const RunSceneState& scene,
                                     const EngineConfig& config,
                                     const char* scenePath,
                                     const char* rendererName );
    void LogReplayScrubProbe( const RunSceneState& scene,
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
    void LogReplayRestoreProbe( const RunSceneState& scene,
                                const ReplaySolverFrameSample& selected,
                                uint64_t restoredSolverHash,
                                uint64_t restoredPresentationHash,
                                std::size_t restoredBodyCount,
                                bool hashCaptured,
                                bool hashMatched,
                                bool fallbackAttempted,
                                bool fallbackRestored );
    void LogReplayRestoreResult( const RunSceneState& scene,
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
    void EndPhysicsDiagnosticsRun( const RunSceneState& scene, const char* status );
#endif

    // Invariant: UI stress state is deterministic scene-driven input churn.
    // Keep it cheap and seed-based so validation can reproduce failures.
    struct UIStressState
    {
        bool enabled = false;                       // Deterministic scene-driven UI stress runner
        unsigned int randomState = 0x7F4A7C15u;     // LCG state, seeded from scene UI options
        int actionsPerFrame = 4;                    // Cheap UI state mutations per rendered frame
        int framesRun = 0;                          // Stress-run frame counter independent of scene resets
    };

    UIStressState& UIStress();
    const UIStressState& UIStress() const;

  private:
    CaptureController m_capture;                    // Screenshot trigger and capture automation
    DiagnosticsController m_diagnostics;            // Perf/test logs and queryable physics diagnostic trace
    MainMemoryStats m_mainMemoryStats;              // Cached process/replay/model memory snapshot for UI and dumps.
    double m_lastMainMemorySampleSeconds = -1000.0; // Coarse sampling guard so UI draw does not rescan every frame.
    // Cache-mode guard: a deep diagnostics caller cannot reuse a recent fast UI sample.
    bool m_lastMainMemorySampleUsedPrivateWorkingSetQuery = false;
    char m_mainMemoryDumpPath[260] = {};            // CLI --memory-dump output path; empty disables shutdown dump.
    UIStressState m_uiStress;                       // Deterministic UI stress run state
};
} // namespace Basics
} // namespace SkullbonezCore
