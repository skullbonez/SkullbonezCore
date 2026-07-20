/*
File: SkullbonezSource/Runtime/Scene/SceneController.h
Purpose:
  Owns scene lifecycle, queue navigation, load transactions, and scene requests.

Summary:
  SceneController owns scene queue, load transactions, frame completion policy,
  and the ordered request batch. It composes one concrete SceneWorld for the
  active scene; callers borrow that owner explicitly instead of reaching world
  domains through lifecycle forwarding methods.

Glossary:
  Scene runtime: Current scene state plus queue navigation data.
  Scene queue: Ordered authored scene list, with an empty path selecting the
    generated demo scene.
  Scene request: Deferred load, reset, create, or defaults-save owner intent.
  Scene world: Concrete owner for active-scene entities, physics, cameras,
    terrain, environment settings, and render presentation.
  Proceed policy: Value packet that freezes the sampled step edge and
    cross-scene pause decision for one frame.

Invariants:
  - SceneController owns queue/index bookkeeping and composes exactly one
    SceneWorld; it does not mirror SceneWorld APIs.
  - All interactive scene submissions enter its fixed request ring.
  - Empty queue path is the generated demo scene sentinel.
  - Queue index lookups must normalize path separators before matching.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntime.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneAutomationGateConfiguration.h"
#include "SceneEntityStore.h"
#include "SceneRequestQueue.h"
#include "SceneRuntime.h"
#include "SceneRuntimeCoordinator.h"
#include "SceneRuntimeUiOptions.h"
#include "SceneWorld.h"
#include "../../Core/SbResult.h"
#include "../../Maths/Vector3.h"

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
struct CinematicRenderConfig;
} // namespace Core
namespace Assets
{
class AssetSystem;
}
namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment
namespace Physics
{
class PhysicsEngine;
class PhysicsDebugVisualizer;
struct PhysicsWorldForces;
} // namespace Physics
namespace Threading
{
class WorkerPool;
}
namespace UI
{
class InGameUI;
struct SceneNavigationModel;
} // namespace UI
namespace Runtime
{
class DiagnosticsRuntime;
class InputRouter;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeOverlayDiagnostics;
class RuntimeValidationHarness;
struct SceneAutomationGateStatus;
class RuntimeRenderer;
class RuntimeTools;
class SimulationSystem;
class Window;
struct AttachedCameraState;
struct RunCameraState;
struct RunDebugState;
struct RunLaunchOptions;
struct RunStartupState;
struct RunTimerState;
struct RuntimeRenderBackendView;
struct SceneFrameAdvanceResult
{
    SceneLoadRequest loadRequest;
    const char* finishReason = nullptr;
    bool restartFrame = false;
    bool requestQuit = false;
    bool holdInteractive = false;
    bool quitIfLoadFails = false;
    bool restartSimulationTimerAfterLoad = false;
    // Value request only; validation retains diagnostic rows and printing.
    bool reportMissingRequirements = false;
};
// Value policy sampled once after input. Every late-frame consumer observes
// the same step edge and cross-scene lock decision for the entire frame turn.
struct SceneFrameProceedPolicy
{
    bool stepRequested = false;
    bool crossScenePauseLocked = false;
    bool proceedAllowed = true;
};
inline SceneFrameProceedPolicy ResolveSceneFrameProceedPolicy( bool crossScenePauseLocked, bool stepRequested )
{
    // Invariant: only the sampled step edge releases a locked scene turn.
    return SceneFrameProceedPolicy{ stepRequested, crossScenePauseLocked, !crossScenePauseLocked || stepRequested };
}
struct SceneDefaultsSaveView
{
    // Lifetime: every owner is borrowed only for one synchronous cold save.
    // The writer retains no pointers across a scene reload.
    const RunDebugState& debug;
    const RuntimeRenderer& renderer;
    const RunCameraState& camera;
    const RunSceneUIOverrideState& uiOverrides;
};

// Concept: scene loading borrows four phase-oriented values instead of
// accepting the process shell's complete owner graph as one flat call. The
// structs carry 18 concrete owners (6 policy, 3 host, 5 interaction, and 4
// presentation); navigation crosses as a detached value snapshot. Window, UI,
// and validation effects return through SceneLoadConsumerOutputs and no
// participant or output is retained by SceneController.
struct SceneLoadPolicyInputs
{
    SkullbonezCore::Core::EngineConfig& config;
    RunLaunchOptions& launchOptions;
    const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender;
    const RunStartupState& startup;
    Assets::AssetSystem& assets;
    Threading::WorkerPool& workerPool;
};

struct SceneLoadHostParticipants
{
    RunTimerState& timers;
    DiagnosticsRuntime& diagnosticsRuntime;
    SimulationSystem& simulation;
};

struct SceneLoadInteractionParticipants
{
    InputRouter& inputRouter;
    RuntimeInteractionController& interaction;
    RunCameraState& camera;
    AttachedCameraState& attachedCamera;
    RuntimeTools& runtimeTools;
    SceneLoadNavigationState navigation;
};

struct SceneLoadPresentationParticipants
{
    ReplayRuntime& replayRuntime;
    RuntimeOverlayDiagnostics& overlays;
    const RuntimeRenderBackendView& renderBackendView;
    RuntimeRenderer& renderer;
};

struct SceneLoadConsumerOutputs
{
    // Value effects are accumulated synchronously and consumed immediately by
    // the four owners intentionally excluded from the load participant graph.
    SceneUiActivation uiActivation;
    SceneAutomationGateConfiguration automationGates;
    SceneLoadNavigationState navigation;
    char windowTitle[256] = {};
    bool hasWindowTitle = false;
    bool applyAutomationGates = false;
    bool applyNavigation = false;
    bool refreshSceneBrowser = false;
    bool resumeGraphicsStress = false;

    void ResetForLoad();
};

// Returns the navigation values visible to a later request in the same owner
// batch. A completed load commits into the output value before the excluded UI
// owner applies it, so follow-up persistence must not fall back to the stale
// submitted snapshot.
inline const SceneLoadNavigationState& SceneNavigationForFollowingRequest( const SceneLoadNavigationState& submitted,
                                                                           const SceneLoadConsumerOutputs& outputs )
{
    return outputs.applyNavigation ? outputs.navigation : submitted;
}

// Applies one completed transaction's value effects at the excluded consumer
// boundaries. Call exactly once after Load/ExecutePending, including failures
// that progressed past scene clearing and therefore emitted reset effects.
void ApplySceneLoadConsumerOutputs( SceneLoadConsumerOutputs& outputs,
                                    Window& window,
                                    UI::InGameUI& operatorUi,
                                    RuntimeValidationHarness& validationHarness,
                                    const RunLaunchOptions& launchOptions );

class SceneController
{
  public:
    SceneController();
    explicit SceneController( std::vector<std::string> queue );

    RunSceneState& State();
    const RunSceneState& State() const;
    // Borrow the concrete active-scene owner. SceneController deliberately has
    // no duplicate entity/physics/camera/terrain/render forwarding surface.
    SceneWorld& Scene();
    const SceneWorld& Scene() const;
    void EnterInteractiveRun();
    bool CanAutomationQuit() const;
    void MarkInteractiveRunComplete();
    void ToggleCrossScenePause();
    bool CrossScenePauseLocked() const;
    SceneFrameProceedPolicy BuildFrameProceedPolicy( bool stepRequested ) const;
    SceneFrameAdvanceResult AdvanceFrame( const SceneAutomationGateStatus& automationGates,
                                          bool proceedAllowed,
                                          bool perfTestActive,
                                          bool screenshotSaved,
                                          bool manualCameraActive,
                                          double elapsedSeconds );

    bool HasEntry( int index ) const;
    bool HasCurrentEntry() const;
    const std::string* CurrentPath() const;
    const std::string& PathAt( int index ) const;
    int QueueSize() const;
    int CurrentIndex() const;
    int NextIndex() const;
    const std::vector<std::string>& Queue() const;

    void BeginLoad( int index );
    void RecordLifecycleEvent( SceneRuntimeLifecycleEvent event, SceneLifecycleConsumerMask consumers );
    void MarkManualReset();
    int FindNormalizedPath( const std::string& normalizedPath ) const;
    int FindGeneratedDemo() const;
    int Append( std::string path );
    bool CurrentQueueIsCinematicDeck() const;
    int AdjacentQueueIndex( int direction ) const;
    SceneLoadRequest ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState );
    SceneLoadRequest AdvanceScene( bool perfTestActive, bool preserveInteractiveUI );
    int PerfPass() const;
    // Lifetime: cold load orchestration borrows each phase value only for this
    // call. No Run backpointer or complete mutable context is retained behind
    // the scene boundary.
    SkullbonezCore::Core::SbResult Load( const SceneLoadRequest& request,
                                         const SceneLoadPolicyInputs& policy,
                                         const SceneLoadHostParticipants& host,
                                         const SceneLoadInteractionParticipants& interaction,
                                         const SceneLoadPresentationParticipants& presentation,
                                         SceneLoadConsumerOutputs& consumerOutputs );
    // Executes the fixed pending batch inside the scene owner. Replay records
    // only requests whose load/create/save operation completes successfully.
    bool ExecutePending( const SceneLoadPolicyInputs& policy,
                         const SceneLoadHostParticipants& host,
                         const SceneLoadInteractionParticipants& interaction,
                         const SceneLoadPresentationParticipants& presentation,
                         SceneLoadConsumerOutputs& consumerOutputs );
    SkullbonezCore::Core::SbResult SaveCurrentDefaults( const SceneDefaultsSaveView& view ) const;

    // Scene request submission stays owner-specific even while Run temporarily
    // executes the returned batch during lifecycle extraction C1.
    void SubmitLoadBrowserIndex( int index );
    void SubmitLoadDemoScene();
    void SubmitResetCurrentScene( bool preserveUIState = true,
                                  bool suppressExitOnComplete = true,
                                  bool preserveRuntimeState = true );
    SkullbonezCore::Core::SbResult SubmitCreateScene( const char* requestedName );
    void SubmitSaveCurrentDefaults();
    SceneRequestBatch TakePendingRequests();
    std::size_t PendingRequestCount() const;
    SceneRuntime& Runtime();
    const SceneRuntime& Runtime() const;

  private:
    SceneRuntime m_runtime;               // Scene queue and active scene-run state
    SceneRequestQueue m_requests;         // Fixed scene-only deferred intent ring.
    int m_perfPass = 0;                   // Scene navigation pass index for two-pass performance captures.
    bool m_crossScenePauseLocked = false; // Operator scene-flow lock preserved across load transactions.
    SceneWorld m_world;                   // Concrete active-scene domain owner; no lifecycle reach-back.
};
} // namespace Runtime
} // namespace SkullbonezCore
