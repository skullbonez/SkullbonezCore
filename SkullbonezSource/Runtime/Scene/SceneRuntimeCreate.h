/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.h
Purpose:
  Declares scene-runtime helpers for creating starter scene files from UI input.

Mental model:
  Runtime input owns when the Create Scene command fires. Scene runtime owns the
  filename cleanup, starter file authoring, browser refresh, and queue action
  needed to load the new scene.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.cpp
  - SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h
  - Agentic/Plans/run-composition-root-shrink-plan.md
*/
#pragma once

#include "SceneRuntimeCoordinator.h"
#include "SceneRuntimeLoad.h"

namespace SkullbonezCore
{
namespace Basics
{

struct SceneRuntimeCreateContext
{
    SceneController& controller;
    RunSceneBrowserState& sceneBrowser;
};

SceneRuntimeControlAction CreateSceneFromUI( SceneRuntimeCreateContext context, const char* requestedName );

} // namespace Basics
} // namespace SkullbonezCore
