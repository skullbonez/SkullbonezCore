/*
File: SkullbonezSource/Runtime/Scene/SceneController.h
Purpose:
  Owns scene lifecycle, queue navigation, cold scene mutation, and scene requests.

Summary:
  SceneController owns the scene queue, cold mutation, frame completion policy,
  and ordered request batch. SceneLoadTransaction separately enforces consumer
  phase order. The controller composes one concrete SceneWorld for the active
  scene; callers borrow it instead of reaching domains through forwarding.

Glossary:
  Scene world: Concrete owner for active-scene entities, physics, cameras,
    terrain, environment settings, and render presentation.

Invariants:
  - SceneController owns queue/index bookkeeping and composes exactly one
    SceneWorld; it does not mirror SceneWorld APIs.
  - All interactive scene submissions enter its fixed request ring.
  - Empty queue path is the generated demo scene sentinel.
  - Queue index lookups must normalize path separators before matching.

Related:
  - SkullbonezSource/Runtime/Scene/SceneSessionState.h
  - SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneAutomationGateConfiguration.h"
#include "SceneEntityStore.h"
#include "SceneRequestQueue.h"
#include "SceneSessionState.h"
#include "SceneLoadRequest.h"
#include "SceneLoadPresentation.h"
#include "SceneWorld.h"
#include "../Camera/CameraControlState.h"
#include "../Diagnostics/OverlayDebugState.h"
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
namespace Rendering
{
class Dx12FrameOwner;
class Dx12RenderDevice;
class Dx12ResourceBuilder;
} // namespace Rendering
namespace UI
{
class InGameUI;
struct SceneNavigationModel;
} // namespace UI
namespace Runtime
{
class AuthoredScene;
class AttachedCameraController;
class DiagnosticsRuntime;
class InputRouter;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeOverlayDiagnostics;
class RuntimeValidationHarness;
struct SceneAutomationGateStatus;
class RuntimeRenderer;
class RuntimeTools;
class SceneLoadTransaction;
class SimulationSystem;
class Window;
struct AttachedCameraState;
struct CameraControlState;
struct OverlayDebugState;
struct RunLaunchOptions;
struct RunStartupState;
struct RunTimerState;
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
    return SceneFrameProceedPolicy { stepRequested, crossScenePauseLocked, !crossScenePauseLocked || stepRequested };
}
struct SceneDefaultsSaveView
{

    // Lifetime: every owner is borrowed only for one synchronous cold save.
    // The writer retains no pointers across a scene reload.
    const OverlayDebugState& debug;
    const RuntimeRenderer& renderer;
    const CameraControlState& camera;
    const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides;
};

struct SceneLoadCompletedWorldChange
{
    float previousGravity = 0.0f;
    float previousFluidHeight = 0.0f;
    float previousFluidDensity = 0.0f;
    float gravity = 0.0f;
    float fluidHeight = 0.0f;
    float fluidDensity = 0.0f;
};

class SceneController : public SceneSession
{
  public:
    explicit SceneController( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics );
    SceneController( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, std::vector<std::string> queue );

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
    SceneFrameAdvanceResult AdvanceFrame( const SceneAutomationGateStatus& automationGates, bool proceedAllowed,
                                          bool perfTestActive, bool screenshotSaved, bool manualCameraActive,
                                          double elapsedSeconds );

    void RecordLifecycleEvent( SceneRuntimeLifecycleEvent event, SceneLifecycleConsumerMask consumers );
    SceneLoadRequest ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState );
    SceneLoadRequest AdvanceScene( bool perfTestActive, bool preserveInteractiveUI );
    int PerfPass() const;
    bool ApplyCinematicBrowserStyle( RunLaunchOptions& launchOptions, UI::RunSceneBrowserState& sceneBrowser,
                                     const Assets::AssetSystem& assets,
                                     SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                                     const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic, int index );
    void ApplyLiveStyle( RunLaunchOptions& launchOptions, UI::RunSceneBrowserState& sceneBrowser,
                         SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                         const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic,
                         const AuthoredScene& styleScene );
    bool ApplyDemoHeroStyle( RunLaunchOptions& launchOptions, UI::RunSceneBrowserState& sceneBrowser,
                             const Assets::AssetSystem& assets, SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                             const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic );

    // Executes the fixed pending batch inside the scene owner. Replay records
    // only requests whose operation completes successfully. The transaction
    // owns outputs and enforces the later reaction/presentation phases.
    bool ExecutePending( SceneLoadTransaction& transaction, SkullbonezCore::Core::EngineConfig& config,
                         RunLaunchOptions& launchOptions,
                         const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender,
                         const RunStartupState& startup, Assets::AssetSystem& assets, Threading::WorkerPool& workerPool,
                         DiagnosticsRuntime& diagnosticsRuntime, Rendering::Dx12FrameOwner* renderFrame,
                         Rendering::Dx12ResourceBuilder* renderResources, RuntimeRenderer& renderer );
    SkullbonezCore::Core::SbResult SaveCurrentDefaults( const SceneDefaultsSaveView& view ) const;

    // Scene request submission and ordered batch execution stay owner-specific;
    // SceneRequestExecution.cpp consumes the fixed pending batch.
    void SubmitLoadBrowserIndex( int index );
    void SubmitLoadDemoScene();
    void SubmitResetCurrentScene( bool preserveUIState = true, bool suppressExitOnComplete = true,
                                  bool preserveRuntimeState = true );
    SkullbonezCore::Core::SbResult SubmitCreateScene( const char* requestedName );
    SceneLoadRequest CreateScene( const char* requestedName );
    void SubmitSaveCurrentDefaults();
    SceneUICommandSubmissionResult SubmitUIRequests( const UI::UISceneCommands& commands );
    SceneRequestBatch TakePendingRequests();

  private:
    friend class SceneLoadTransaction;

    // Lifetime: cold load orchestration borrows each phase value only for this
    // call. The transaction owns detached outputs; neither owner stores a Run
    // backpointer or complete mutable context.
    // Hazard: renderFrame proves old GPU use complete before scene mutation;
    // renderResources is borrowed only afterward for cold terrain construction.
    SkullbonezCore::Core::SbResult
    Load( const SceneLoadRequest& request, SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions,
          const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender, const RunStartupState& startup,
          Assets::AssetSystem& assets, Threading::WorkerPool& workerPool, DiagnosticsRuntime& diagnosticsRuntime,
          Rendering::Dx12FrameOwner* renderFrame, Rendering::Dx12ResourceBuilder* renderResources, RuntimeRenderer& renderer,
          SceneLoadTransaction& transaction );

    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    SceneRequestQueue m_requests;         // Fixed scene-only deferred intent ring.
    int m_perfPass = 0;                   // Scene navigation pass index for two-pass performance captures.
    bool m_crossScenePauseLocked = false; // Operator scene-flow lock preserved across load transactions.
    SceneWorld m_world;                   // Concrete active-scene domain owner; no lifecycle reach-back.
};
} // namespace Runtime
} // namespace SkullbonezCore
