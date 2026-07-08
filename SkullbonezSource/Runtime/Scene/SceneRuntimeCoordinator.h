/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h
Purpose:
  Coordinates scene load/reset/advance decisions above SceneController.

Mental model:
  SceneRuntime owns queue state. SceneRuntimeCoordinator owns lifecycle
  decisions that choose which scene entry to load next. Run still performs
  returned control intents until later Phase 3 slices move generated and
  authored scene application behind scene-owned APIs.

Glossary:
  Control action: Explicit request for Run to load, clear automation, or apply
    cinematic mode.
  Scene UI command: One-frame Scene-tab request translated into deferred runtime
    commands.
  Scene browser path: Path discovered from the scenes directory and shown in
    the UI browser.
  Interactive scene run: User-owned scene flow where automation should not exit
    the app.

Invariants:
  - The coordinator does not own scene browser path storage.
  - The coordinator does not mutate renderer, physics, replay, or UI state
    directly; remaining side effects are returned as explicit control intents.
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
namespace UI
{
struct UISceneCommands;
}
namespace Basics
{
class SceneController;
class RuntimeCommandQueue;

enum class SceneRuntimeControlActionType
{
    None,
    ClearCurrentSceneAutomation,
    LoadScene,
    ApplyCinematicModeFromBrowserIndex,
};

struct SceneRuntimeControlAction
{
    SceneRuntimeControlActionType type = SceneRuntimeControlActionType::None;
    bool enterInteractiveSceneRun = false;
    int index = -1;
    bool preserveUIState = false;
    bool suppressExitOnComplete = false;
    bool preserveRuntimeState = false;

    static SceneRuntimeControlAction None()
    {
        return {};
    }

    static SceneRuntimeControlAction ClearCurrentSceneAutomation( bool enterInteractiveSceneRun )
    {
        SceneRuntimeControlAction action;
        action.type = SceneRuntimeControlActionType::ClearCurrentSceneAutomation;
        action.enterInteractiveSceneRun = enterInteractiveSceneRun;
        return action;
    }

    static SceneRuntimeControlAction LoadScene( int index,
                                                bool preserveUIState,
                                                bool suppressExitOnComplete,
                                                bool preserveRuntimeState,
                                                bool enterInteractiveSceneRun = false )
    {
        SceneRuntimeControlAction action;
        action.type = SceneRuntimeControlActionType::LoadScene;
        action.enterInteractiveSceneRun = enterInteractiveSceneRun;
        action.index = index;
        action.preserveUIState = preserveUIState;
        action.suppressExitOnComplete = suppressExitOnComplete;
        action.preserveRuntimeState = preserveRuntimeState;
        return action;
    }

    static SceneRuntimeControlAction ApplyCinematicModeFromBrowserIndex( int index )
    {
        SceneRuntimeControlAction action;
        action.type = SceneRuntimeControlActionType::ApplyCinematicModeFromBrowserIndex;
        action.index = index;
        return action;
    }
};

struct SceneRuntimeUICommandResult
{
    // Invariant: flags report accepted UI commands for RunInput action logging;
    // queued RuntimeCommand order remains the behavior contract.
    bool resetScene = false;
    bool resetSceneDefaults = false;
    bool loadDemoScene = false;
    bool saveSceneDefaults = false;
    bool createScene = false;
    bool selectScene = false;
};

SceneRuntimeUICommandResult QueueSceneUIRuntimeCommands( RuntimeCommandQueue& runtimeCommands,
                                                         const UI::UISceneCommands& commands );

class SceneRuntimeCoordinator
{
  public:
    explicit SceneRuntimeCoordinator( SceneController& sceneController );

    SceneRuntimeControlAction LoadSceneFromBrowserIndex( int index, const std::vector<std::string>& sceneBrowserPaths );
    SceneRuntimeControlAction LoadDemoSceneFromUI();
    SceneRuntimeControlAction ApplyAdjacentCinematicMode( int direction,
                                                          const std::vector<std::string>& sceneBrowserPaths,
                                                          int selectedCineModeSceneIndex,
                                                          int currentSceneBrowserIndex,
                                                          bool isCinematicTabActive );
    SceneRuntimeControlAction LoadAdjacentSceneFromBrowser( int direction,
                                                            const std::vector<std::string>& sceneBrowserPaths,
                                                            int currentSceneBrowserIndex );
    SceneRuntimeControlAction
    ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState );
    SceneRuntimeControlAction AdvanceScene( bool perfTestActive, int& perfPass, bool preserveInteractiveUI );

  private:
    SceneController& m_sceneController;
};

} // namespace Basics
} // namespace SkullbonezCore
