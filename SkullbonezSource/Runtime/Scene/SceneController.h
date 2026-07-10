/*
File: SkullbonezSource/Runtime/Scene/SceneController.h
Purpose:
  Owns scene runtime state, durable entity metadata, physics, and scene requests.

Mental model:
  SceneController is the narrow API around scene queue and scene-run state.
  Run temporarily executes broad load side effects, while this controller owns
  scene state, physics topology, fixed entity records, browser navigation, and
  the ordered request batch those side effects consume.

Glossary:
  Scene runtime: Current scene state plus queue navigation data.
  Scene queue: Ordered authored scene list, with an empty path selecting the
    generated demo scene.
  Scene request: Deferred load, reset, create, or defaults-save owner intent.
  Scene entity store: Fixed scene-lifetime join between identity, live body,
    render material intent, and asset affiliation.

Invariants:
  - SceneController owns queue/index bookkeeping and scene-lifetime physics;
    presentation owners receive only a borrowed PhysicsEngine reference.
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
#include "../../Physics/PhysicsEngine.h"

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}
namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment
namespace Physics
{
class PhysicsEngine;
}
namespace Basics
{
struct RunCameraState;
struct RunDebugState;
struct RunRuntimeSettings;
struct SceneRuntimeControlAction;
struct SceneDefaultsSaveView
{
    // Lifetime: every owner is borrowed only for one synchronous cold save.
    // The writer retains no pointers across a scene reload.
    GameObjects::GameModelCollection& models;
    Environment::WorldEnvironment& world;
    Environment::CameraCollection& cameras;
    const RunDebugState& debug;
    const RunRuntimeSettings& runtimeSettings;
    const RunCameraState& camera;
};

class SceneController
{
  public:
    SceneController() = default;
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
    // actions are immediate cold-boundary intents until Load owns execution.
    SceneRuntimeControlAction LoadSceneFromBrowserIndex( int index );
    SceneRuntimeControlAction LoadDemoSceneFromUI();
    SceneRuntimeControlAction ApplyAdjacentCinematicMode( int direction,
                                                          int selectedCineModeSceneIndex,
                                                          int currentSceneBrowserIndex,
                                                          bool isCinematicTabActive );
    SceneRuntimeControlAction LoadAdjacentSceneFromBrowser( int direction, int currentSceneBrowserIndex );
    SceneRuntimeControlAction
    ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState );
    SceneRuntimeControlAction AdvanceScene( bool perfTestActive, int& perfPass, bool preserveInteractiveUI );
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
    bool TrimForReplayRestore( GameObjects::GameModelCollection& presentations,
                               Physics::PhysicsEngine& physics,
                               int bodyCount );

    std::vector<RunRequiredContactState>& RequiredContacts();
    const std::vector<RunRequiredContactState>& RequiredContacts() const;
    std::vector<RunRequiredBroadphaseXCellsState>& RequiredBroadphaseXCells();
    const std::vector<RunRequiredBroadphaseXCellsState>& RequiredBroadphaseXCells() const;
    void ClearRequiredAutomationGates();
    void UpdateRequiredContacts( GameObjects::GameModelCollection& models, float contactEpsilon );
    bool RequiredContactsComplete() const;
    void UpdateRequiredBroadphaseXCells( const Math::CollisionDetection::SpatialGrid::ActiveCell* activeCells,
                                         int activeCellCount );
    bool RequiredBroadphaseXCellsComplete() const;

    SceneRuntime& Runtime();
    const SceneRuntime& Runtime() const;

  private:
    SceneRuntime m_runtime;                // Scene queue and active scene-run state
    SceneRequestQueue m_requests;          // Fixed scene-only deferred intent ring.
    RunSceneBrowserState m_browser;        // Discovered scene paths and live cine/concept selection.
    RunSceneUIOverrideState m_uiOverrides; // Live Scene-tab overrides preserved across reset when requested.
    SceneEntityStore m_entities;           // Fixed scene-lifetime identity and durable presentation metadata.
    // Lifetime: physics topology is born and cleared with the active scene.
    // Presentation owners borrow this engine; they never own or replace it.
    Physics::PhysicsEngine m_physics;
};
} // namespace Basics
} // namespace SkullbonezCore
