/*
File: SkullbonezSource/UI/UISceneNavigationModel.h
Purpose:
  Defines the UI-owned scene browser, live overrides, and navigation model.

Summary:
  InGameUI owns scene discovery and Scene/Run-tab overrides as one cohesive
  value model. Runtime may borrow these values synchronously when it decides
  queue and load policy, but this header contains no Runtime authority.

Invariants:
  - `namePtrs` points into `names`; refresh paths and names before rebuilding
    the pointer views.
  - Override sentinel values preserve the established UI contract: time scale
    `0.0f` means no override, and count values below zero mean unset.
  - Runtime consumers may borrow this model but never retain a pointer or
    publish Runtime authority back into UI.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UI.h
  - SkullbonezSource/Runtime/Scene/SceneControllerState.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace UI
{
struct RunSceneBrowserState
{
    std::vector<std::string> paths;
    std::vector<std::string> names;
    std::vector<const char*> namePtrs;
    int selectedSceneIndex = 0;          // Current runtime scene projected for the Scene-tab combo.
    int selectedCineModeSceneIndex = -1; // -1=Demo/default look, otherwise scene-browser index of live cine/concept
    int CurrentIndexForPath( const std::string* currentScenePath ) const;

    // look
};

struct RunSceneUIOverrideState
{
    float timeScaleOverride = 0.0f;
    int modelCountOverride = -1;
    int solverBallCountOverride = -1;
    int solverBoxCountOverride = -1;
};

struct InteractionRecordingBrowserState
{
    // Invariant: paths and names use the same newest-first index. The UI emits
    // only that index; Runtime resolves the path at the application boundary.
    std::vector<std::string> paths;
    std::vector<std::string> names;
    std::vector<const char*> namePtrs;
    int selectedIndex = 0;
};

struct SceneNavigationModel
{
    // Invariant: browser pointer views and live override sentinels share the
    // UI owner's lifetime; Runtime consumers may borrow but never retain them.
    RunSceneBrowserState browser;
    InteractionRecordingBrowserState recordings;
    RunSceneUIOverrideState overrides;
    void RefreshBrowserList();
    void RefreshInteractionRecordings();
};
} // namespace UI
} // namespace SkullbonezCore
