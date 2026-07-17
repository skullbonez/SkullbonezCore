/*
File: SkullbonezSource/Runtime/Scene/SceneController.h
Purpose:
  Owns scene runtime state, cameras, terrain, world settings, durable entity
  metadata, physics, and scene requests.

Summary:
  SceneController owns scene queue, load transactions, frame completion policy,
  camera slots, replaceable terrain, world settings, physics topology, fixed
  entity records, value-only browser navigation decisions, and the ordered
  request batch. InGameUI owns browser/override state; the process shell supplies
  explicit cold-operation owners and sequences returned results.

Glossary:
  Scene runtime: Current scene state plus queue navigation data.
  Scene queue: Ordered authored scene list, with an empty path selecting the
    generated demo scene.
  Scene request: Deferred load, reset, create, or defaults-save owner intent.
  Scene entity store: Fixed scene-lifetime join between identity, live body,
    render material intent, and asset affiliation.
  World environment: Scene-owned gravity, fluid, and terrain-bound settings
    borrowed by physics, replay, and rendering.
  Scene cameras: Fixed camera slots, tween state, and active render pose reset
    and populated with each scene load.
  Scene terrain: Replaceable height-map or flat-slope owner published only after
    construction and any required GPU drain succeed.

Invariants:
  - SceneController owns queue/index bookkeeping, camera/terrain state, world
    settings, and scene-lifetime physics; consumers receive only borrowed owner
    references.
  - All interactive scene submissions enter its fixed request ring.
  - Durable display/material/asset metadata lives in its fixed entity store.
  - Empty queue path is the generated demo scene sentinel.
  - Queue index lookups must normalize path separators before matching.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntime.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneEntityStore.h"
#include "SceneRequestQueue.h"
#include "SceneRuntime.h"
#include "SceneRuntimeCoordinator.h"
#include "SceneTerrain.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Core/SbResult.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../CameraCollection.h"
#include "../../World/WorldEnvironment.h"

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
namespace Runtime
{
namespace Audio
{
class ContactAudioService;
}
} // namespace Runtime
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class DiagnosticsRuntime;
class InputRouter;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeOverlayDiagnostics;
class RuntimeValidationHarness;
class SceneAutomationGateTracker;
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
};
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
// accepting the process shell's complete owner graph as one flat call. Each
// value is synchronous-only, contains at most six concrete owners, and is
// never retained by SceneController.
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
    Window& window;
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
    UI::InGameUI& operatorUi;
};

struct SceneLoadPresentationParticipants
{
    Audio::ContactAudioService& contactAudio;
    ReplayRuntime& replayRuntime;
    RuntimeOverlayDiagnostics& overlays;
    RuntimeValidationHarness& validationHarness;
    const RuntimeRenderBackendView& renderBackendView;
    RuntimeRenderer& renderer;
};

// Concept: scene creation returns the recoverable authoring result together
// with the physics handle published by the successful cross-store commit.
struct SceneEntityCreateResult
{
    SkullbonezCore::Core::SbResult status;
    Physics::PhysicsBodyHandle body;
};

class SceneController
{
#include "SceneController.Objects.inl"

  public:
    SceneController();
    explicit SceneController( std::vector<std::string> queue );

    RunSceneState& State();
    const RunSceneState& State() const;
    SceneEntityStore& Entities();
    const SceneEntityStore& Entities() const;
    Environment::CameraCollection& Cameras();
    const Environment::CameraCollection& Cameras() const;
    Environment::WorldEnvironment& World();
    const Environment::WorldEnvironment& World() const;
    SceneTerrain& Terrain();
    const SceneTerrain& Terrain() const;
    Physics::PhysicsEngine& Physics();
    const Physics::PhysicsEngine& Physics() const;
    // Executes one deterministic live-scene physics step against the
    // controller-owned model and physics stores. Replay restore may call this
    // same boundary so its hash proof cannot drift from ordinary frame steps.
    void StepPhysics( float fixedDt,
                      const SkullbonezCore::Core::EngineConfig& config,
                      const Physics::PhysicsWorldForces& worldForces,
                      Threading::WorkerPool& workerPool );
    void ApplyWaterHeightControl( bool pageDown, bool pageUp, float dt );
    void EnterInteractiveRun();
    bool CanAutomationQuit() const;
    void MarkInteractiveRunComplete();
    void ToggleCrossScenePause();
    bool CrossScenePauseLocked() const;
    SceneFrameAdvanceResult AdvanceFrame( const SceneAutomationGateTracker& automationGates,
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
    // Scene navigation policy stays with the queue/browser owner. Returned
    // requests are value-only and retain no caller callback or context.
    SceneLoadRequest LoadSceneFromBrowserIndex( int index, const RunSceneBrowserState& browser );
    SceneLoadRequest LoadDemoSceneFromUI();
    int AdjacentCinematicModeBrowserIndex( int direction,
                                           int selectedCineModeSceneIndex,
                                           int currentSceneBrowserIndex,
                                           bool isCinematicTabActive,
                                           const RunSceneBrowserState& browser ) const;
    SceneLoadRequest
    LoadAdjacentSceneFromBrowser( int direction, int currentSceneBrowserIndex, const RunSceneBrowserState& browser );
    SceneLoadRequest ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState );
    SceneLoadRequest AdvanceScene( bool perfTestActive, bool preserveInteractiveUI );
    int PerfPass() const;
    // Lifetime: cold load orchestration borrows each phase value only for this
    // call. No Run backpointer or complete mutable context is retained behind
    // the scene boundary.
    SkullbonezCore::Core::SbResult Load( const SceneLoadRequest& request,
                                         SceneLoadPolicyInputs policy,
                                         SceneLoadHostParticipants host,
                                         SceneLoadInteractionParticipants interaction,
                                         SceneLoadPresentationParticipants presentation );
    // Executes the fixed pending batch inside the scene owner. Replay records
    // only requests whose load/create/save operation completes successfully.
    bool ExecutePending( SceneLoadPolicyInputs policy,
                         SceneLoadHostParticipants host,
                         SceneLoadInteractionParticipants interaction,
                         SceneLoadPresentationParticipants presentation );
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
    // Cold replay restore shrinks every scene-lifetime row owner as one
    // transaction; ReplayRuntime never writes topology through model facades.
    bool TrimForReplayRestore( int bodyCount );

    SceneRuntime& Runtime();
    const SceneRuntime& Runtime() const;

  private:
    SceneRuntime m_runtime;                  // Scene queue and active scene-run state
    SceneRequestQueue m_requests;            // Fixed scene-only deferred intent ring.
    int m_perfPass = 0;                      // Scene navigation pass index for two-pass performance captures.
    bool m_crossScenePauseLocked = false;    // Operator scene-flow lock preserved across load transactions.
    SceneEntityStore m_entities;             // Fixed scene-lifetime identity and durable presentation metadata.
    Environment::CameraCollection m_cameras; // Fixed scene camera slots and active camera presentation state.
    Environment::WorldEnvironment m_world;   // Gravity, fluid, and terrain bounds for the active scene.
    SceneTerrain m_terrain;                  // Replaceable terrain and its matching scene-shape classification.
    // Lifetime: physics topology is born and cleared with the active scene.
    // Presentation owners borrow this engine; they never own or replace it.
    Physics::PhysicsEngine m_physics;
};
} // namespace Runtime
} // namespace SkullbonezCore
