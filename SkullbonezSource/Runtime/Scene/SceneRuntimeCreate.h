/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.h
Purpose:
  Declares scene-runtime helpers for creating starter scene files from UI input.

Summary:
  Runtime input owns when the Create Scene command fires. Scene runtime owns the
  filename cleanup, starter file authoring, browser refresh, and queue action
  needed to load the new scene.

Glossary:
  Starter scene: Minimal editable scene file generated from UI input.
  Scene browser: UI-facing list of available scene files.
  Load request: Value-only accepted navigation result returned to the caller.

Invariants:
  - The create helper writes scene files but does not perform the actual load.
  - Context borrows controller/browser state for the duration of the call only.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.cpp
  - SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
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

SceneLoadRequest CreateSceneFromUI( SceneRuntimeCreateContext context, const char* requestedName );

} // namespace Basics
} // namespace SkullbonezCore
