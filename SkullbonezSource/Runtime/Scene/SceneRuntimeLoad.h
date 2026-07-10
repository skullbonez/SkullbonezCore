/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h
Purpose:
  Declares scene load orchestration helpers owned by scene runtime code.

Mental model:
  SceneController owns the complete load transaction. This module isolates its
  load-begin decision point: queue validation, runtime-state preservation, GPU
  flush-before-teardown, and controller bookkeeping.

Glossary:
  Load begin: Scene load phase before teardown and object population.
  GPU drain: Checked close, submit, wait, and command-list reopen that proves
    old scene resources are no longer referenced by the GPU.
  Reset snapshot: Preserved operator-owned runtime state for interactive resets.
  Scene browser: UI-facing list of available scene files.

Invariants:
  - BeginSceneRuntimeLoad returns intent/state to SceneController::Load; it does
    not populate the scene itself.
  - A successful GPU drain precedes every scene/controller mutation.
  - Runtime state preservation must happen before SceneController begins load.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneRuntimeReset.h"
#include "../../Core/SbResult.h"

#include <string>

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderDeviceLifecycle;
}
namespace Basics
{
class SceneController;
struct RunCameraState;
struct RunDebugState;
struct RunRuntimeSettings;

struct SceneRuntimeLoadBeginResult
{
    // Lane R: a failed GPU drain leaves shouldLoad false so Run can terminate
    // the load before SceneController or resource owners mutate old state.
    SbResult status = SbResult::Success();
    bool shouldLoad = false;
    bool suppressAutomationExit = false;
    bool shouldPreserveRuntimeState = false;
    SceneRuntimeResetSnapshot resetSnapshot;
    const std::string* scenePath = nullptr;
};

SceneRuntimeLoadBeginResult BeginSceneRuntimeLoad( SceneController& controller,
                                                   RunRuntimeSettings& runtimeSettings,
                                                   RunDebugState& debug,
                                                   RunCameraState& camera,
                                                   Rendering::IRenderDeviceLifecycle* renderLifecycle,
                                                   bool interactiveSceneRunRequested,
                                                   int index,
                                                   bool suppressExitOnComplete,
                                                   bool preserveRuntimeState );
void RefreshSceneBrowserList( RunSceneBrowserState& sceneBrowser );
int CurrentSceneBrowserIndex( const SceneController& controller, const RunSceneBrowserState& sceneBrowser );

} // namespace Basics
} // namespace SkullbonezCore
