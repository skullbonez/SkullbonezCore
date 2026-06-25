/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h
Purpose:
  Coordinates scene load/reset/advance decisions above SceneController.

Mental model:
  SceneRuntime owns queue state. SceneRuntimeCoordinator owns lifecycle
  decisions that choose which scene entry to load next. Run still performs the
  heavy load side effects through callbacks until later Phase 3 slices move
  generated and authored scene application behind scene-owned APIs.

Invariants:
  - The coordinator does not own scene browser path storage.
  - The coordinator does not mutate renderer, physics, replay, or UI state
    directly; remaining side effects are named callbacks.
  - Scene queue indices stay owned by SceneController/SceneRuntime.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
class SceneController;

struct SceneRuntimeCoordinatorCallbacks
{
    using VoidFn = void ( * )( void* user );
    using LoadSceneFn = void ( * )( void* user,
                                    int index,
                                    bool preserveUIState,
                                    bool suppressExitOnComplete,
                                    bool preserveRuntimeState );
    using CurrentSceneBrowserIndexFn = int ( * )( void* user );
    using IsCinematicTabActiveFn = bool ( * )( void* user );
    using ApplyCinematicModeFromBrowserIndexFn = bool ( * )( void* user, int index );

    void* user = nullptr;
    VoidFn enterInteractiveSceneRun = nullptr;
    VoidFn clearCurrentSceneAutomation = nullptr;
    LoadSceneFn loadScene = nullptr;
    CurrentSceneBrowserIndexFn currentSceneBrowserIndex = nullptr;
    IsCinematicTabActiveFn isCinematicTabActive = nullptr;
    ApplyCinematicModeFromBrowserIndexFn applyCinematicModeFromBrowserIndex = nullptr;
};

class SceneRuntimeCoordinator
{
  public:
    SceneRuntimeCoordinator( SceneController& sceneController, SceneRuntimeCoordinatorCallbacks callbacks );

    void LoadSceneFromBrowserIndex( int index, const std::vector<std::string>& sceneBrowserPaths );
    void LoadDemoSceneFromUI();
    bool ApplyAdjacentCinematicMode( int direction,
                                     const std::vector<std::string>& sceneBrowserPaths,
                                     int selectedCineModeSceneIndex );
    void LoadAdjacentSceneFromBrowser( int direction, const std::vector<std::string>& sceneBrowserPaths );
    void ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState );
    bool AdvanceScene( bool perfTestActive, int& perfPass, bool preserveInteractiveUI );

  private:
    void EnterInteractiveSceneRun() const;
    void ClearCurrentSceneAutomation() const;
    void LoadScene( int index, bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState ) const;
    int CurrentSceneBrowserIndex() const;
    bool IsCinematicTabActive() const;
    bool ApplyCinematicModeFromBrowserIndex( int index ) const;

    SceneController& m_sceneController;
    SceneRuntimeCoordinatorCallbacks m_callbacks;
};

} // namespace Basics
} // namespace SkullbonezCore
