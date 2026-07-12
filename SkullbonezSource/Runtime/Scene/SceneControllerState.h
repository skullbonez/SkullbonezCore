/*
File: SkullbonezSource/Runtime/Scene/SceneControllerState.h
Purpose:
  Defines scene-controller-owned browser and UI override state.

Summary:
  SceneController owns scene discovery and live scene-tab override values. Run
  borrows these shelves through the controller while broader scene loading still
  coordinates through the composition root.

Glossary:
  Scene browser: UI-facing list of authored scene paths plus stable name
    pointers for combo-box presentation.
  UI override: Live Scene/Run tab value that should survive interactive reset
    and feed the next generated-scene rebuild.

Invariants:
  - `namePtrs` points into `names`; refresh paths/names before rebuilding
    pointer views.
  - Override sentinel values mirror the previous RunState defaults: time scale
    `0.0f` means no override, and count values below zero mean unset.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
struct RunSceneBrowserState
{
    std::vector<std::string> paths;
    std::vector<std::string> names;
    std::vector<const char*> namePtrs;
    int selectedCineModeSceneIndex = -1; // -1=Demo/default look, otherwise scene-browser index of live cine/concept look
};

struct RunSceneUIOverrideState
{
    float timeScaleOverride = 0.0f;
    int modelCountOverride = -1;
    int solverBallCountOverride = -1;
    int solverBoxCountOverride = -1;
};
} // namespace Basics
} // namespace SkullbonezCore
