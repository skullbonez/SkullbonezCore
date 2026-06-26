/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h
Purpose:
  Declares scene load orchestration helpers owned by scene runtime code.

Mental model:
  Scene loading is still being peeled out of Run one phase at a time. This
  module owns the load-begin decision point: queue validation, runtime-state
  preservation, GPU flush-before-teardown, and SceneController load bookkeeping.

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
class IRenderBackend;
}
namespace Basics
{
class SceneController;

struct SceneRuntimeLoadBeginContext
{
    SceneController& controller;
    SceneRuntimeResetContext reset;
    RunSceneBrowserState& sceneBrowser;
    Rendering::IRenderBackend* renderer = nullptr;
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

} // namespace Basics
} // namespace SkullbonezCore
