/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h
Purpose:
  Declares scene load orchestration helpers owned by scene runtime code.

Mental model:
  Scene loading is still being peeled out of Run one phase at a time. This
  module owns the load-begin decision point: queue validation, runtime-state
  preservation, GPU flush-before-teardown, and SceneController load bookkeeping.

Glossary:
  Load begin: Scene load phase before teardown and object population.
  Reset snapshot: Preserved operator-owned runtime state for interactive resets.
  Scene browser: UI-facing list of available scene files.

Invariants:
  - BeginSceneRuntimeLoad returns intent/state; it does not populate the scene.
  - Runtime state preservation must happen before SceneController begins load.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/run-composition-root-shrink-plan.md
*/
#pragma once

#include "SceneRuntimeReset.h"

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

struct SceneRuntimeLoadBeginContext
{
    SceneController& controller;
    SceneRuntimeResetContext reset;
    RunSceneBrowserState& sceneBrowser;
    Rendering::IRenderDeviceLifecycle* renderLifecycle = nullptr;
    bool interactiveSceneRunRequested = false;
};

struct SceneRuntimeLoadBeginResult
{
    bool shouldLoad = false;
    bool suppressAutomationExit = false;
    bool shouldPreserveRuntimeState = false;
    SceneRuntimeResetSnapshot resetSnapshot;
    const std::string* scenePath = nullptr;
};

SceneRuntimeLoadBeginResult BeginSceneRuntimeLoad( SceneRuntimeLoadBeginContext& context,
                                                   int index,
                                                   bool suppressExitOnComplete,
                                                   bool preserveRuntimeState );
void RefreshSceneBrowserList( RunSceneBrowserState& sceneBrowser );
int CurrentSceneBrowserIndex( const SceneController& controller, const RunSceneBrowserState& sceneBrowser );

} // namespace Basics
} // namespace SkullbonezCore
