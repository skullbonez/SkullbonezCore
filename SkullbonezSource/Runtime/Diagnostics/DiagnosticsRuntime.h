/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
Purpose:
  Owns runtime diagnostics and capture controllers behind one diagnostics boundary.

Mental model:
  DiagnosticsRuntime owns the process diagnostics lifecycle. Capture, perf,
  memory, and SkullScope controllers keep their artifact-specific state behind
  this boundary while frame code requests synchronous diagnostics operations.

Glossary:
  Capture controller: Screenshot trigger and automation state.
  Diagnostics controller: Perf CSV and queryable physics diagnostic state.
  Artifact path: Validation-facing output path that must stay stable.
  Physics diagnostic command: One-frame key or UI request that changes debug
    presentation state, not simulation state.
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
namespace Basics
{
class SceneController;
}
namespace Rendering
{
class IRenderDiagnostics;
}
namespace UI
{
class InGameUI;
struct UIPhysicsCommands;
} // namespace UI
namespace Basics
{
class Profiler;
class ReplayRuntime;
class SceneController;
class TestScene;
enum class RuntimeInputAction;
struct RunDebugState;

struct DiagnosticsKeyboardShortcutContext
{
    // Lifetime: borrowed for one already-routed action only; InputRouter owns
    // the edge and diagnostics mutates only debug presentation state.
    RunDebugState& debug;
    int& cameraTrackBallIndex;
    const Basics::SceneController& sceneEntities;
    const Rendering::IRenderDiagnostics* renderDiagnostics = nullptr;
    bool sceneMode = false;
    double simulationSeconds = 0.0;
};

struct DiagnosticsUIKeyboardShortcutContext
{
    // Lifetime: borrowed for one keyboard dispatch only; the handler mutates UI
    // overlay visibility, automation flags, and debug presentation state, then
    // reports any cursor/action bookkeeping still owned by the composition root.
    UI::InGameUI& ui;
    RunDebugState& debug;
    RunSceneState& scene;
    CaptureController& capture;
    double nowSeconds = 0.0;
};

struct DiagnosticsUIKeyboardShortcutResult
{
    bool handled = false;                           // True when the action belongs to the diagnostics UI keyboard group.
    bool triggered = false;                         // True when this frame captured the shortcut edge.
    bool releaseMouseToUI = false;                  // True when Run should refresh cursor ownership and release capture.
};

struct DiagnosticsPhysicsOverlayUICommandResult
{
    bool toggledCollisionVisualizer = false;
    bool toggledPhysicsDebugFlags = false;
    bool steppedPipelinePrevious = false;
    bool steppedPipelineNext = false;
    bool toggledPhysicsDebugTransparent = false;
    bool toggledBroadphaseOverlay = false;
};

struct DiagnosticsPhysicsDebugValueUICommandResult
{
    bool setAlpha = false;
    bool setContactLinger = false;
};

void StepDiagnosticsPhysicsPipelineStage( RunDebugState& debug, int direction );
bool HandleDiagnosticsKeyboardShortcut( DiagnosticsKeyboardShortcutContext context,
                                        RuntimeInputAction action,
                                        bool wasPressed );
DiagnosticsUIKeyboardShortcutResult HandleDiagnosticsUIKeyboardShortcut( DiagnosticsUIKeyboardShortcutContext context,
                                                                         RuntimeInputAction action,
                                                                         bool wasPressed );
DiagnosticsPhysicsOverlayUICommandResult
ApplyDiagnosticsPhysicsOverlayUICommands( RunDebugState& debug, const UI::UIPhysicsCommands& commands );
bool ApplyDiagnosticsTerrainContactProbeUICommand( RunDebugState& debug, const UI::UIPhysicsCommands& commands );
DiagnosticsPhysicsDebugValueUICommandResult
ApplyDiagnosticsPhysicsDebugValueUICommands( RunDebugState& debug, const UI::UIPhysicsCommands& commands );

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
    void ResetForSceneLoad( int completedPerfPass );
    void ConfigurePerfLogFlush( bool enabled, int interval );
    void OpenScenePerfLog( const char* path, int pass );
    void ApplySceneAutomationOptions( const TestScene& scene, bool suppressAutomationExit, int perfPass );
    bool PerfTestActive() const;
    void TickPerfLog( const RuntimePerfTickContext& context );
    RuntimeProfilerFrameTimes SampleProfilerFrameTimes() const;
    const MainMemoryStats& RefreshMainMemoryStats( const ReplayRuntime& replay,
                                                   const Basics::SceneController& models,
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
                              const Basics::SceneController& models,
                              const RunSceneState& scene,
                              const char* checkpoint,
                              double nowSeconds );

#ifdef _DEBUG
    RunPhysicsDiagnosticsState& PhysicsDiagnostics();
    const RunPhysicsDiagnosticsState& PhysicsDiagnostics() const;
    bool PhysicsDiagnosticsEnabled() const;

    void SetPhysicsRegressionLogOverride( const char* path );
    void SetPhysicsCollisionTimeLogOverride( const char* path );
    void
    SetPhysicsDiagnosticsPath( Basics::SceneController& models, const char* path, bool fixedStepForcedByDiagnostics );
    void LogSceneFinished( SceneController& scene,
                           const Rendering::IRenderDiagnostics* renderDiagnostics,
                           const char* reason );
    void BeginPhysicsDiagnosticsRun( Basics::SceneController& models,
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
    // Consumes BeforeSceneUnload while the old scene identity is still live.
    // Release builds keep the same typed boundary even though SkullScope end
    // emission is Debug-only.
    void BeforeSceneUnload( const RunSceneState& scene );

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
