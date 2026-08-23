/*
File: SkullbonezSource/Runtime/App/SceneLoadApplication.h
Purpose:
  Declares App-owned application of detached scene-load results.

Summary:
  SceneLoadTransaction publishes immutable load facts and a phase cursor. App
  applies those facts synchronously to runtime siblings, Replay composition,
  the native host, operator UI, rendering device, and validation owners.

Invariants:
  - Scene retains no pointer or reference to any reaction or presentation owner.
  - Runtime reactions complete before presentation begins.
  - The detached result remains owned by the stack-scoped transaction.

Related:
  - SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h
  - SkullbonezSource/Runtime/App/InputFrameExecution.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
*/
#pragma once

#include "../Input/InputRouter.h"
#include "../Diagnostics/RuntimeDiagnostics.h"
#include "../Diagnostics/RuntimeFrameMetricsOwner.h"
#include "../Scene/SceneLifecycle.h"
#include "../Scene/SceneSessionState.h"

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12RenderDevice;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class AttachedCameraController;
struct CameraControlState;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeOverlayDiagnostics;
class RuntimeTools;
class RuntimeValidationHarness;
class SceneController;
class SceneLoadTransaction;
class Window;
struct RunLaunchOptions;

inline RuntimeSceneDiagnosticFacts ProjectSceneDiagnosticFacts( const SceneSessionState& scene )
{
    RuntimeSceneDiagnosticFacts facts;
    facts.currentSceneIndex = scene.currentSceneIndex;
    facts.loadCount = scene.loadCount;
    facts.manualResetCount = scene.manualResetCount;
    facts.currentFrame = scene.currentFrame;
    facts.targetFrameCount = scene.targetFrameCount;
    facts.modelCount = scene.modelCount;
    facts.rngSeed = scene.rngSeed;
    facts.fixedStep = scene.isFixedStep;
    facts.testComplete = scene.isTestComplete;
    facts.finishLogged = scene.isFinishLogged;
    return facts;
}

struct RuntimeFrameMetricsLifecycleActions
{
    bool resetMeasurements = false;
    bool restartClocks = false;
};

class RuntimeFrameMetricsLifecyclePolicy
{
  public:
    RuntimeFrameMetricsLifecycleActions Observe( const SceneLifecyclePacket& packet )
    {
        return { m_resetObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneCleared ),
                 m_activationObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneActivated ) };
    }
    uint64_t LastResetGeneration() const
    {
        return m_resetObserver.LastAppliedGeneration();
    }
    uint64_t LastActivationGeneration() const
    {
        return m_activationObserver.LastAppliedGeneration();
    }

  private:
    SceneLifecycleGenerationObserver m_resetObserver;
    SceneLifecycleGenerationObserver m_activationObserver;
};

inline void ApplyRuntimeFrameMetricsLifecycle( RuntimeFrameMetricsLifecyclePolicy& policy,
                                               const SceneLifecyclePacket& packet, RuntimeFrameMetricsOwner& metrics )
{
    const RuntimeFrameMetricsLifecycleActions actions = policy.Observe( packet );
    if ( actions.resetMeasurements )
    {
        metrics.ResetMeasurements();
    }
    if ( actions.restartClocks )
    {
        metrics.RestartClocks();
    }
}

inline bool ApplySceneActivationInputReaction( const SceneLifecyclePacket& lifecycle, bool hideCursorAfterActivation,
                                               SceneLifecycleGenerationObserver& lifecycleObserver,
                                               InputRouter& inputRouter )
{
    const bool applyActivation =
        lifecycleObserver.ShouldApply( lifecycle, SceneRuntimeLifecycleEvent::AfterSceneActivated );
    if ( !applyActivation || !hideCursorAfterActivation )
    {
        return false;
    }

    inputRouter.RequestCursorVisible( false );
    return true;
}

constexpr bool SceneRenderActivationCompletesTransition( bool sceneMutationSucceeded, bool activationPending,
                                                         bool renderActivationSucceeded )
{
    return sceneMutationSucceeded && ( !activationPending || renderActivationSucceeded );
}

void ApplySceneLoadRuntimeReactions( SceneLoadTransaction& transaction, const RunLaunchOptions& launchOptions,
                                     RuntimeOverlayDiagnostics& overlays,
                                     SceneLifecycleGenerationObserver& overlayLifecycle, SceneController& sceneController,
                                     SceneLifecycleGenerationObserver& inputLifecycle, InputRouter& inputRouter,
                                     RuntimeInteractionController& interaction,
                                     SceneLifecycleGenerationObserver& cameraLifecycle, CameraControlState& camera,
                                     SceneLifecycleGenerationObserver& attachedCameraLifecycle,
                                     AttachedCameraController& attachedCamera,
                                     RuntimeTools& runtimeTools, ReplayRuntime& replayRuntime );

void ApplySceneLoadPresentation( SceneLoadTransaction& transaction, Window& window, UI::InGameUI& operatorUi,
                                 RuntimeValidationHarness& validationHarness, const RunLaunchOptions& launchOptions,
                                 Rendering::Dx12RenderDevice* renderDevice, bool rendererVsyncEnabled,
                                 SceneController& sceneController );
} // namespace Runtime
} // namespace SkullbonezCore
