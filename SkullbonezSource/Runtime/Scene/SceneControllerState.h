/*
File: SkullbonezSource/Runtime/Scene/SceneControllerState.h
Purpose:
  Defines the UI-owned scene navigation model and its browser/override values.

Summary:
  InGameUI owns scene discovery and live scene-tab override values as one
  cohesive navigation model. Runtime code borrows individual values only for
  synchronous navigation, load, replay, and reset operations.

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
  - SkullbonezSource/UI/UI.h
  - SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
class SceneRuntime;
struct SceneLoadRequest;

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
} // namespace Runtime

namespace UI
{
struct SceneNavigationModel
{
    // Invariant: browser pointer views and live override sentinels share the
    // UI owner's lifetime; runtime consumers may borrow but never retain them.
    Runtime::RunSceneBrowserState browser;
    Runtime::RunSceneUIOverrideState overrides;

    // Browser policy borrows only the concrete queue owner for one synchronous
    // decision. The returned request owns no callback, pointer, or runtime
    // context and can cross the lifecycle submission boundary by value.
    Runtime::SceneLoadRequest LoadSceneFromBrowserIndex( int index, Runtime::SceneRuntime& scene );
    Runtime::SceneLoadRequest LoadDemoScene( Runtime::SceneRuntime& scene );
    int
    AdjacentCinematicModeBrowserIndex( int direction, int currentSceneBrowserIndex, bool isCinematicTabActive ) const;
    Runtime::SceneLoadRequest
    LoadAdjacentScene( int direction, int currentSceneBrowserIndex, Runtime::SceneRuntime& scene );
};
} // namespace UI
} // namespace SkullbonezCore
