/*
File: SkullbonezSource/Runtime/Scene/SceneController.h
Purpose:
  Owns scene runtime state, cameras, terrain, world settings, durable entity
  metadata, physics, and scene requests.

Mental model:
  SceneController is the narrow API around scene queue and scene-run state.
  Run temporarily executes broad load side effects, while this controller owns
  scene state, camera slots, replaceable terrain, world settings, physics
  topology, fixed entity records, browser navigation, and the ordered request
  batch those side effects consume.

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
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneEntityStore.h"
#include "SceneRequestQueue.h"
#include "SceneRuntime.h"
#include "SceneTerrain.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Physics/PhysicsEngine.h"
#include "../CameraCollection.h"
#include "../../World/WorldEnvironment.h"

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}
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
namespace Basics
{
class DiagnosticsRuntime;
class GraphicsStressController;
class InputRouter;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeRenderer;
class RuntimeTools;
class SimulationSystem;
class Window;
struct AttachedCameraState;
struct CinematicRenderConfig;
class EngineConfig;
struct RunCameraState;
struct RunDebugState;
struct RunLaunchOptions;
struct RunRuntimeSettings;
struct RunStartupState;
struct RunTimerState;
struct RuntimeRenderBackendView;
struct SceneLoadRequest;
struct SceneDefaultsSaveView
{
    // Lifetime: every owner is borrowed only for one synchronous cold save.
    // The writer retains no pointers across a scene reload.
    const RunDebugState& debug;
    const RunRuntimeSettings& runtimeSettings;
    const RunCameraState& camera;
};

class SceneController
{
  public:
    SceneController();
    explicit SceneController( std::vector<std::string> queue );

    RunSceneState& State();
    const RunSceneState& State() const;
    // Concept: Browser and UI override state are scene-owned policy inputs; Run
    // borrows them through this controller instead of storing parallel fields.
    RunSceneBrowserState& Browser();
    const RunSceneBrowserState& Browser() const;
    RunSceneUIOverrideState& UIOverrides();
    const RunSceneUIOverrideState& UIOverrides() const;
    SceneEntityStore& Entities();
    const SceneEntityStore& Entities() const;
    GameObjects::GameModelCollection& Models();
    const GameObjects::GameModelCollection& Models() const;
    Environment::CameraCollection& Cameras();
    const Environment::CameraCollection& Cameras() const;
    Environment::WorldEnvironment& World();
    const Environment::WorldEnvironment& World() const;
    SceneTerrain& Terrain();
    const SceneTerrain& Terrain() const;
    Physics::PhysicsEngine& Physics();
    const Physics::PhysicsEngine& Physics() const;

    bool HasEntry( int index ) const;
    bool HasCurrentEntry() const;
    const std::string* CurrentPath() const;
    const std::string& PathAt( int index ) const;
    int QueueSize() const;
    int CurrentIndex() const;
    int NextIndex() const;
    const std::vector<std::string>& Queue() const;

    void BeginLoad( int index );
    void RecordLifecycleEvent( SceneRuntimeLifecycleEvent event );
    void MarkManualReset();
    int FindNormalizedPath( const std::string& normalizedPath ) const;
    int FindGeneratedDemo() const;
    int Append( std::string path );
    bool CurrentQueueIsCinematicDeck() const;
    int AdjacentQueueIndex( int direction ) const;
    // Scene navigation policy stays with the queue/browser owner. Returned
    // requests are value-only and retain no caller callback or context.
    SceneLoadRequest LoadSceneFromBrowserIndex( int index );
    SceneLoadRequest LoadDemoSceneFromUI();
    int AdjacentCinematicModeBrowserIndex( int direction,
                                           int selectedCineModeSceneIndex,
                                           int currentSceneBrowserIndex,
                                           bool isCinematicTabActive ) const;
    SceneLoadRequest LoadAdjacentSceneFromBrowser( int direction, int currentSceneBrowserIndex );
    SceneLoadRequest ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState );
    SceneLoadRequest AdvanceScene( bool perfTestActive, int& perfPass, bool preserveInteractiveUI );
    // Lifetime: cold load orchestration borrows each concrete owner only for
    // this call. The explicit list is intentional: no Run backpointer or broad
    // mutable context is retained behind the scene boundary.
    SbResult Load( const SceneLoadRequest& request,
                   EngineConfig& m_config,
                   RunLaunchOptions& m_launchOptions,
                   const CinematicRenderConfig& m_defaultCinematicRender,
                   const RunStartupState& m_startup,
                   DiagnosticsRuntime& m_diagnosticsRuntime,
                   RunRuntimeSettings& m_runtimeSettings,
                   RunTimerState& m_timers,
                   Assets::AssetSystem& assets,
                   Threading::WorkerPool& workerPool,
                   Window& window,
                   InputRouter& m_inputRouter,
                   RuntimeInteractionController& m_interaction,
                   RunCameraState& m_camera,
                   AttachedCameraState& m_attachedCamera,
                   SimulationSystem& m_simulation,
                   ReplayRuntime& m_replayRuntime,
                   Runtime::Audio::ContactAudioService& m_contactAudio,
                   UI::InGameUI& m_UI,
                   RunDebugState& m_debug,
                   GraphicsStressController& m_graphicsStress,
                   RuntimeTools& m_runtimeTools,
                   Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer,
                   const RuntimeRenderBackendView& m_renderBackendView,
                   RuntimeRenderer& m_renderer,
                   int& sPerfPass );
    // Executes the fixed pending batch inside the scene owner. Replay records
    // only requests whose load/create/save operation completes successfully.
    bool ExecutePending( EngineConfig& m_config,
                         RunLaunchOptions& m_launchOptions,
                         const CinematicRenderConfig& m_defaultCinematicRender,
                         const RunStartupState& m_startup,
                         DiagnosticsRuntime& m_diagnosticsRuntime,
                         RunRuntimeSettings& m_runtimeSettings,
                         RunTimerState& m_timers,
                         Assets::AssetSystem& assets,
                         Threading::WorkerPool& workerPool,
                         Window& window,
                         InputRouter& m_inputRouter,
                         RuntimeInteractionController& m_interaction,
                         RunCameraState& m_camera,
                         AttachedCameraState& m_attachedCamera,
                         SimulationSystem& m_simulation,
                         ReplayRuntime& m_replayRuntime,
                         Runtime::Audio::ContactAudioService& m_contactAudio,
                         UI::InGameUI& m_UI,
                         RunDebugState& m_debug,
                         GraphicsStressController& m_graphicsStress,
                         RuntimeTools& m_runtimeTools,
                         Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer,
                         const RuntimeRenderBackendView& m_renderBackendView,
                         RuntimeRenderer& m_renderer,
                         int& sPerfPass );
    SbResult SaveCurrentDefaults( const SceneDefaultsSaveView& view ) const;

    // Scene request submission stays owner-specific even while Run temporarily
    // executes the returned batch during lifecycle extraction C1.
    void SubmitLoadBrowserIndex( int index );
    void SubmitLoadDemoScene();
    void SubmitResetCurrentScene( bool preserveUIState = true,
                                  bool suppressExitOnComplete = true,
                                  bool preserveRuntimeState = true );
    SbResult SubmitCreateScene( const char* requestedName );
    void SubmitSaveCurrentDefaults();
    SceneRequestBatch TakePendingRequests();
    std::size_t PendingRequestCount() const;
    // Cold replay restore shrinks every scene-lifetime row owner as one
    // transaction; ReplayRuntime never writes topology through model facades.
    bool TrimForReplayRestore( int bodyCount );

    std::vector<RunRequiredContactState>& RequiredContacts();
    const std::vector<RunRequiredContactState>& RequiredContacts() const;
    std::vector<RunRequiredBroadphaseXCellsState>& RequiredBroadphaseXCells();
    const std::vector<RunRequiredBroadphaseXCellsState>& RequiredBroadphaseXCells() const;
    void ClearRequiredAutomationGates();
    void UpdateRequiredContacts( float contactEpsilon );
    bool RequiredContactsComplete() const;
    void UpdateRequiredBroadphaseXCells( const Math::CollisionDetection::SpatialGrid::ActiveCell* activeCells,
                                         int activeCellCount );
    bool RequiredBroadphaseXCellsComplete() const;

    SceneRuntime& Runtime();
    const SceneRuntime& Runtime() const;

  private:
    SceneRuntime m_runtime;                  // Scene queue and active scene-run state
    SceneRequestQueue m_requests;            // Fixed scene-only deferred intent ring.
    RunSceneBrowserState m_browser;          // Discovered scene paths and live cine/concept selection.
    RunSceneUIOverrideState m_uiOverrides;   // Live Scene-tab overrides preserved across reset when requested.
    SceneEntityStore m_entities;             // Fixed scene-lifetime identity and durable presentation metadata.
    Environment::CameraCollection m_cameras; // Fixed scene camera slots and active camera presentation state.
    Environment::WorldEnvironment m_world;   // Gravity, fluid, and terrain bounds for the active scene.
    SceneTerrain m_terrain;                  // Replaceable terrain and its matching scene-shape classification.
    // Lifetime: physics topology is born and cleared with the active scene.
    // Presentation owners borrow this engine; they never own or replace it.
    Physics::PhysicsEngine m_physics;
    // Lifetime: presentation rows share the scene lifetime and borrow the
    // controller-owned physics engine. Run never owns or replaces this store.
    GameObjects::GameModelCollection m_models;
};
} // namespace Basics
} // namespace SkullbonezCore
