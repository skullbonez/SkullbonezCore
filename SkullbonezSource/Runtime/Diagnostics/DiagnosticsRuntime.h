/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
Purpose:
  Owns runtime diagnostics and publishes typed shortcut results.

Summary:
  DiagnosticsRuntime owns the perf, memory, and SkullScope lifecycle. Capture
  remains a sibling owner composed by App; diagnostics shortcuts return bounded
  actions instead of borrowing Input or Capture owners.
  Scene-end capacity tables are emitted while old scene identity remains live.

Glossary:
  Diagnostics controller: Perf CSV and queryable physics diagnostic state.
  Capacity table: Resident-descending fixed-store rows emitted at scene unload
    and final process shutdown.

Invariants:
  - Artifact formatting stays in RuntimeDiagnostics and CaptureSystem.
  - Existing output paths and command-line behavior must not drift here.
  - Capacity reporting is synchronous and retains no scene path or row span.

Related:
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIStressPolicy.h"
#include "DiagnosticsController.h"
#include "DiagnosticsKeyboardShortcuts.h"

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
} // namespace Core
namespace Physics
{
class PhysicsEngine;
}
namespace Rendering
{
class Dx12Diagnostics;
}
namespace Runtime
{
struct OverlayDebugState;

enum class DiagnosticsUiKeyboardCommand : uint8_t
{
    ToggleVisibility,
    TogglePerformanceHistogram,
    ToggleMemoryOverlay
};

struct DiagnosticsUIKeyboardShortcutResult
{
    bool handled = false;                                    // True when the action belongs to the diagnostics UI keyboard group.
    bool triggered = false;                                  // True when this frame captured the shortcut edge.
    bool releaseMouseToUI = false;                           // True when Run should refresh cursor ownership and release capture.
    bool markInteractiveRun = false;
    bool disableExitOnComplete = false;
    bool disableCaptureAutomationExit = false;
};

DiagnosticsUIKeyboardShortcutResult
HandleDiagnosticsUIKeyboardShortcut( OverlayDebugState& debug, DiagnosticsUiKeyboardCommand command, bool wasPressed );

class DiagnosticsRuntime
{
  public:

    // Startup binding that keeps perf CSV and frame-time diagnostics off the
    // global profiler accessor after initialization.
    void BindProfiler( SkullbonezCore::Core::Profiler* profiler );

    RunPerfLogState& PerfLog();

    void ClosePerfLog();
    void ClosePerfLogWithMemoryCheckpoint( int pass, const char* checkpoint );
    void ResetPerfLogForSceneLoad();
    void ResetForSceneLoad( int completedPerfPass );
    void ConfigurePerfLogFlush( bool enabled, int interval );
    void OpenScenePerfLog( const char* path, int pass );
    void ApplyScenePerfLogOptions( const char* path, int perfPass );
    bool PerfTestActive() const;
    void TickPerfLog( int pass, int frame, float physicsTimeSeconds, float renderTimeSeconds );
    RuntimeProfilerFrameTimes SampleProfilerFrameTimes() const;

    // Accepts replay accounting already published by the composition root so
    // the UI pass cannot reopen replay ownership while reconciling totals.
    const SkullbonezCore::Core::MainMemoryStats&
    RefreshMainMemoryStats( const SkullbonezCore::Core::MainMemoryReplayStats& replay,
                            const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects, double nowSeconds,
                            bool force, bool includePrivateWorkingSet = true );
    const SkullbonezCore::Core::MainMemoryStats& MainMemoryStatsSnapshot() const;
    void SetMainMemoryDumpPath( const char* path );
    bool MainMemoryDumpRequested() const;
    bool WriteMainMemoryDump( const SkullbonezCore::Core::MainMemoryReplayStats& replay,
                              const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects,
                              const RuntimeSceneDiagnosticFacts& scene, const char* checkpoint, double nowSeconds );

#ifdef _DEBUG
    RunPhysicsDiagnosticsState& PhysicsDiagnostics();

    void SetPhysicsRegressionLogOverride( const char* path );
    void SetPhysicsCollisionTimeLogOverride( const char* path );
    void SetPhysicsDiagnosticsPath( Physics::PhysicsEngine& physics, const char* path,
                                    bool renderFrameLockstepForcedByDiagnostics );
    bool LogSceneFinished( const RuntimeSceneDiagnosticFacts& scene, const char* scenePath,
                           const Rendering::Dx12Diagnostics* renderDiagnostics, const char* reason );
    void BeginPhysicsDiagnosticsRun( Physics::PhysicsEngine& physics, const RuntimeSceneDiagnosticFacts& scene,
                                     const SkullbonezCore::Core::EngineConfig& config, const char* scenePath,
                                     const char* rendererName, bool explicitRenderFrameLockstep,
                                     bool effectiveRenderFrameLockstep );
    void LogReplayScrubProbe( const RuntimeSceneDiagnosticFacts& scene, const ReplayScrubProbeDiagnostic& probe );
    void LogReplayRestoreProbe( const RuntimeSceneDiagnosticFacts& scene, const ReplayRestoreProbeDiagnostic& probe );
    void LogReplayRestoreResult( const RuntimeSceneDiagnosticFacts& scene, const ReplayRestoreResultDiagnostic& result );
    void EndPhysicsDiagnosticsRun( const RuntimeSceneDiagnosticFacts& scene, const char* status );
#endif

    // Consumes BeforeSceneUnload while the old scene identity is still live.
    // Capacity rows are emitted in every build; SkullScope end emission remains
    // Debug-only.
    void BeforeSceneUnload( int loadCount, int currentFrame, const char* scenePath );
    void ReportStoreCapacityRows( int loadCount, const char* scenePath, const char* status );

    UIStressPolicyOwner& UIStress();

  private:
    DiagnosticsController m_diagnostics;                     // Perf/test logs and queryable physics diagnostic trace
    SkullbonezCore::Core::MainMemoryStats m_mainMemoryStats; // Cached process/replay/model memory snapshot for UI and dumps.
    double m_lastMainMemorySampleSeconds = -1000.0;          // Coarse sampling guard so UI draw does not rescan every frame.

    // Cache-mode guard: a deep diagnostics caller cannot reuse a recent fast UI sample.
    bool m_lastMainMemorySampleUsedPrivateWorkingSetQuery = false;
    char m_mainMemoryDumpPath[260] = {};                     // CLI --memory-dump output path; empty disables shutdown dump.
    UIStressPolicyOwner m_uiStress;                          // Deterministic UI stress policy and random cursor
};
} // namespace Runtime
} // namespace SkullbonezCore
