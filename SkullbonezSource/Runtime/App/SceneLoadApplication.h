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

#include "ApplicationExitState.h"
#include "SceneCaptureApplication.h"
#include "../Input/InputRouter.h"
#include "../Diagnostics/RuntimeDiagnostics.h"
#include "../Diagnostics/RuntimeFrameMetricsOwner.h"
#include "../Scene/SceneLifecycle.h"
#include "../Scene/SceneSessionState.h"
#include "../../Core/SbDiagnosticStore.h"

#include <cstdint>

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
struct OverlayDebugState;
class RuntimeTools;
class EditorToolsOwner;
class RuntimeValidationHarness;
class GraphicsStressController;
class SceneController;
class SceneLoadTransaction;
struct SceneLoadResult;
struct ScenePresentationValues;
class Window;
struct RunLaunchOptions;

inline RuntimeSceneDiagnosticFacts ProjectSceneDiagnosticFacts( const SceneSessionState& scene )
{
    return RuntimeSceneDiagnosticFacts( scene.currentSceneIndex, scene.loadCount, scene.manualResetCount, scene.currentFrame,
                                        scene.targetFrameCount, scene.modelCount, scene.rngSeed, scene.isFixedStep,
                                        scene.isTestComplete, scene.isFinishLogged );
}

inline bool ShouldApplyAuthoredScenePerfLog( const char* commandLinePerfLogPath ) noexcept
{
    return !commandLinePerfLogPath || commandLinePerfLogPath[0] == '\0';
}

inline SkullbonezCore::Core::SbResult ResolvePerfLogArtifactStatus( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                    bool succeeded )
{
    return succeeded ? SkullbonezCore::Core::SbResult::Success()
                     : diagnostics.Failure( "Runtime/Diagnostics",
                                            "Performance CSV could not be completely written, flushed, and closed." );
}

inline SkullbonezCore::Core::SbResult ApplyPerfLogArtifactStatus( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                  ApplicationExitState& exitState, bool succeeded )
{
    SkullbonezCore::Core::SbResult result = ResolvePerfLogArtifactStatus( diagnostics, succeeded );

    if ( !result.Ok() )
    {
        // Invariant: App latches the owned failure before a caller can reduce
        // the scene-load or frame result to a boolean and post WM_QUIT(0).
        exitState.RequestPhaseFailure( result );
    }

    return result;
}

inline SkullbonezCore::Core::SbResult ApplySceneLoadDiagnosticsStatus( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                       ApplicationExitState& exitState,
                                                                       const SkullbonezCore::Core::SbResult& sceneStatus,
                                                                       bool diagnosticsSucceeded )
{
    if ( diagnosticsSucceeded )
    {
        return sceneStatus;
    }

    SkullbonezCore::Core::SbResult perfStatus = ApplyPerfLogArtifactStatus( diagnostics, exitState, false );

    // The scene owner keeps the returned diagnostic when both operations fail;
    // the App exit latch independently retains the required-artifact failure.
    return sceneStatus.Ok() ? perfStatus : sceneStatus;
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
    const bool applyActivation = lifecycleObserver.ShouldApply( lifecycle, SceneRuntimeLifecycleEvent::AfterSceneActivated );

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

enum class SceneAdvanceExitDisposition : uint8_t
{
    None,
    Normal,
    LoadFailure
};

constexpr SceneAdvanceExitDisposition ResolveSceneAdvanceExitDisposition( bool requestQuit, bool loadSucceeded,
                                                                          bool quitIfLoadFails )
{
    // Invariant: a queued-load failure outranks a simultaneous normal quit so
    // App cannot convert incomplete automation into a successful process exit.
    if ( !loadSucceeded && quitIfLoadFails )
    {
        return SceneAdvanceExitDisposition::LoadFailure;
    }

    return requestQuit ? SceneAdvanceExitDisposition::Normal : SceneAdvanceExitDisposition::None;
}

struct SceneAdvanceExitAction
{
    bool postQuit = false;
    int messageExitCode = 0;
};

inline SceneAdvanceExitAction ApplySceneAdvanceExitDisposition( SceneAdvanceExitDisposition disposition,
                                                                const SkullbonezCore::Core::SbResult& loadResult,
                                                                ApplicationExitState& applicationExit )
{
    if ( disposition == SceneAdvanceExitDisposition::LoadFailure )
    {
        // Invariant: retain the scene owner's diagnostic before the platform
        // message can reduce this failure to an integer exit code.
        applicationExit.RequestPhaseFailure( loadResult );
        return { true, 1 };
    }

    return { disposition == SceneAdvanceExitDisposition::Normal, 0 };
}

ScenePresentationValues ProjectScenePresentationValues( const OverlayDebugState& presentation );

const SceneLoadResult& BeginSceneLoadPresentation( SceneLoadTransaction& transaction,
                                                   RuntimeValidationHarness& validationHarness,
                                                   const SceneController& sceneController );
void ApplySceneLoadRenderPresentation( const SceneLifecyclePacket& lifecycle, Rendering::Dx12RenderDevice* renderDevice,
                                       bool rendererVsyncEnabled );
void ApplySceneLoadWindowUiPresentation( const SceneLoadResult& outputs, Window& window, UI::InGameUI& operatorUi );
void ApplySceneLoadGraphicsStressPresentation( const SceneLifecyclePacket& lifecycle,
                                               GraphicsStressController& graphicsStress,
                                               SceneLifecycleGenerationObserver& graphicsStressSceneObserver,
                                               const RunLaunchOptions& launchOptions );
} // namespace Runtime
} // namespace SkullbonezCore
