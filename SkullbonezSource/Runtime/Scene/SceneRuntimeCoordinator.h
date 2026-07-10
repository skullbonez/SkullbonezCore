/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h
Purpose:
  Declares temporary scene control intents and immediate execution helpers.

Mental model:
  SceneController owns queue/browser navigation. Run provides a temporary
  execution context for returned control intents until C1 moves generated and
  authored scene application behind scene-owned APIs.

Glossary:
  Control action: Explicit request for Run to load, clear automation, or apply
    cinematic mode.
  Execution context: Borrowed Run-owned operations and state needed to perform
    one control action without adding another Run method.
  Scene UI request: One-frame Scene-tab intent submitted to SceneController.
  Scene browser path: Path discovered from the scenes directory and shown in
    the UI browser.
  Interactive scene run: User-owned scene flow where automation should not exit
    the app.

Invariants:
  - SceneController owns scene browser path storage and navigation decisions.
  - The free dispatcher does not own renderer, physics, replay, or UI state; the
    execution context names each remaining borrowed side effect explicitly.
  - Scene queue indices stay owned by SceneController/SceneRuntime.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "SceneRuntimeStyle.h"

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
    // the SceneController request batch preserves their submission order.
    bool resetScene = false;
    bool resetSceneDefaults = false;
    bool loadDemoScene = false;
    bool saveSceneDefaults = false;
    bool createScene = false;
    bool selectScene = false;
    SbResult status = SbResult::Success();
};

using SceneRuntimeEnterInteractiveSceneRunFn = void ( * )( void* context );
using SceneRuntimeLoadSceneFn = bool ( * )( void* context,
                                            int index,
                                            bool preserveUIState,
                                            bool suppressExitOnComplete,
                                            bool preserveRuntimeState );

struct SceneRuntimeControlExecutionContext
{
    // Lifetime: every field is borrowed for one immediate control-action
    // dispatch. Callers must not store this context or reuse it after the frame
    // state that produced its references has changed.
    void* context = nullptr;
    SceneRuntimeEnterInteractiveSceneRunFn enterInteractiveSceneRun = nullptr;
    SceneRuntimeLoadSceneFn loadScene = nullptr;
    RunSceneState& scene;
    bool& screenshotAndExit;
    SceneRuntimeStyleContext style;
};

SceneRuntimeUICommandResult SubmitSceneUIRequests( SceneController& sceneController,
                                                   const UI::UISceneCommands& commands );
bool ExecuteSceneRuntimeControlAction( SceneRuntimeControlExecutionContext context,
                                       const SceneRuntimeControlAction& action );

} // namespace Basics
} // namespace SkullbonezCore
