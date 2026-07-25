/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h
Purpose:
  Declares scene navigation load decisions and UI request submission helpers.

Summary:
  SceneController owns queue navigation, borrows the UI-owned browser value for
  one decision, and returns a value-only load request. SceneController consumes
  that request through its cold load transaction; callers only wire explicit
  per-call owner borrows.

Glossary:
  Load request: Accepted navigation result containing an optional scene load
    and whether the runtime should become interactive first.
  Scene UI request: One-frame Scene-tab intent submitted to SceneController.
  Scene browser path: Path discovered from the scenes directory and shown in
    the UI browser.
  Interactive scene run: User-owned scene flow where automation should not exit
    the app.

Invariants:
  - InGameUI owns scene browser path storage; SceneController never retains it.
  - SceneController converts one borrowed browser selection into a value request.
  - Navigation results contain values only; they retain no Run backpointer,
    callback, or borrowed execution context.
  - Scene queue indices stay owned by SceneController/SceneRuntime.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "../../Core/SbResult.h"

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace UI
{
struct UISceneCommands;
}
namespace Runtime
{
class SceneController;

struct SceneLoadRequest
{
    bool accepted = false;
    bool enterInteractiveSceneRun = false;
    int index = -1;
    bool preserveUIState = false;
    bool suppressExitOnComplete = false;
    bool preserveRuntimeState = false;
    bool markManualReset = false;

    static SceneLoadRequest None()
    {
        return {};
    }

    static SceneLoadRequest AcceptedWithoutLoad( bool enterInteractiveSceneRun )
    {
        SceneLoadRequest request;
        request.accepted = true;
        request.enterInteractiveSceneRun = enterInteractiveSceneRun;
        return request;
    }

    static SceneLoadRequest Load(
        int index,
        bool preserveUIState,
        bool suppressExitOnComplete,
        bool preserveRuntimeState,
        bool enterInteractiveSceneRun = false
    )
    {
        SceneLoadRequest request;
        request.accepted = index >= 0;
        request.enterInteractiveSceneRun = enterInteractiveSceneRun;
        request.index = index;
        request.preserveUIState = preserveUIState;
        request.suppressExitOnComplete = suppressExitOnComplete;
        request.preserveRuntimeState = preserveRuntimeState;
        return request;
    }

    bool HasLoad() const
    {
        return accepted && index >= 0;
    }
};

struct SceneRuntimeUICommandResult
{
    // Invariant: flags report accepted UI commands for InputFrame transition
    // recording; SceneController preserves request submission order.
    bool resetScene = false;
    bool resetSceneDefaults = false;
    bool loadDemoScene = false;
    bool saveSceneDefaults = false;
    bool createScene = false;
    bool selectScene = false;
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
};

SceneRuntimeUICommandResult
SubmitSceneUIRequests( SceneController& sceneController, const UI::UISceneCommands& commands );

} // namespace Runtime
} // namespace SkullbonezCore
