/*
File: RuntimeValidationHarness.h
Purpose:
  Owns opt-in live-style and deterministic graphics-stress harness state.

Summary:
  RuntimeValidationHarness groups the two external validation controls that
  mutate presentation state while the application runs. Run sequences the
  owner's startup, frame, capture, scene-reload, and exit operations without
  retaining either concrete controller.

Glossary:
  Live style: Control-folder protocol that applies style JSON and requests a
    screenshot without restarting the process.
  Graphics stress: Seeded DX12/runtime churn used to reproduce resource and
    lifetime faults deterministically.
  Resume: Scene-load transition that preserves the stress random stream and
    counters while restoring launch cadence.

Invariants:
  - The owner is allocated only during Startup and lives for the process.
  - Live-style polling stays in the input phase; capture consumption stays
    after render and UI submission.
  - Graphics-stress random state advances only through the owned controller.
  - Scene reload resumes stress without resetting its persistent counters.

Related:
  - SkullbonezSource/Runtime/Run.cpp
  - SkullbonezSource/Runtime/RunFrame.cpp
  - SkullbonezSource/Runtime/RuntimeStressController.cpp
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
*/
#pragma once

#include <memory>

#include "GraphicsStressController.h"
#include "LiveStyleController.h"

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCaptureBackend;
class IRenderDiagnostics;
} // namespace Rendering
namespace Runtime
{
class CaptureController;
class ReplayRuntime;
struct RuntimeFrameHostView;
struct RuntimeFrameInteractionView;
struct RuntimeFramePresentationView;
struct RuntimeFrameSceneView;
struct RunLaunchOptions;
struct RunStartupOverrides;
struct SceneRuntimeStyleContext;

class RuntimeValidationHarness
{
  public:
    static std::unique_ptr<RuntimeValidationHarness> CreateForStartup();

    bool ConfigureStartup( const RunStartupOverrides& overrides, RunLaunchOptions& launchOptions );
    void MarkLiveStyleReady();
    void TickLiveStyle( SceneRuntimeStyleContext context );
    bool HasPendingLiveStyleCapture() const;
    void SavePendingLiveStyleCapture( CaptureController& capture, Rendering::IRenderCaptureBackend& backend );

    void ResumeGraphicsStressAfterSceneLoad( const RunLaunchOptions& launchOptions );
    void PrintGraphicsStressExitSummary( int currentSceneFrame ) const;
    void ExecuteGraphicsStressFrame( RuntimeFrameHostView& host,
                                     RuntimeFrameInteractionView& interactionOwners,
                                     RuntimeFrameSceneView& sceneOwners,
                                     RuntimeFramePresentationView& presentationOwners,
                                     ReplayRuntime& replayRuntime,
                                     const Rendering::IRenderDiagnostics& renderDiagnostics );

  private:
    LiveStyleController m_liveStyle;
    GraphicsStressController m_graphicsStress;
};
} // namespace Runtime
} // namespace SkullbonezCore
