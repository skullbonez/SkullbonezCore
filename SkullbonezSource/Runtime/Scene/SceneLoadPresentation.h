/*
File: SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h
Purpose:
  Declares authored scene UI-option application outside Run.

Summary:
  Scene JSON may author the initial UI window, tab, stress, and test-pattern
  state for automation scenes. Scene runtime produces an activation value while
  applying diagnostics-owned fields; the UI owner consumes that value after the
  scene transaction returns.

Invariants:
  - `preserveUIState` prevents authored scene UI from overriding live operator
    window/tab state during resets.
  - Stress options are diagnostics state and remain applied even when visible UI
    window state is preserved.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - SkullbonezSource/Scene/AuthoredScene.h
  - SkullbonezSource/UI/UI.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Scene/AuthoredScene.h"

namespace SkullbonezCore
{
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class DiagnosticsRuntime;
struct OverlayDebugState;
struct SceneUiActivation
{

    // Value-only copy of authored UI intent. The scene owner retains neither
    // the parsed AuthoredScene nor the complete UI owner across the load boundary.
    SceneUIOptions authoredOptions;
    double nowSeconds = 0.0;
    bool hasAuthoredOptions = false;
    bool preserveUIState = false;
    bool automationScene = false;
    bool forceVisible = false;
    bool forceUnminimized = false;
};

} // namespace Runtime
} // namespace SkullbonezCore
