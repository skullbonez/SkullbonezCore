/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.h
Purpose:
  Declares authored scene UI-option application outside Run.

Summary:
  Scene JSON may author the initial UI window, tab, stress, and test-pattern
  state for automation scenes. Scene runtime owns the decision tree, while Run
  still passes borrowed UI and diagnostics owners until those stores move.

Glossary:
  UI options: Optional `ui` block parsed from a `.scene.json` file.
  Automation scene: Scene with screenshot/perf/exit behavior that should keep
    the UI hidden unless explicitly authored otherwise.
  UI stress: Deterministic diagnostics input churn driven by scene data.

Invariants:
  - `preserveUIState` prevents authored scene UI from overriding live operator
    window/tab state during resets.
  - Stress options are diagnostics state and remain applied even when visible UI
    window state is preserved.

Related:
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - SkullbonezSource/Scene/TestScene.h
  - SkullbonezSource/UI/UI.h
*/
#pragma once

namespace SkullbonezCore
{
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class DiagnosticsRuntime;
struct RunDebugState;
struct SceneUIOptions;

struct SceneRuntimeUiOptionsContext
{
    // Lifetime: These are borrowed only for one scene-load application; the
    // helper never stores UI, diagnostics, or debug references.
    UI::InGameUI& ui;
    DiagnosticsRuntime& diagnostics;
    RunDebugState& debug;
    double nowSeconds = 0.0;
    bool preserveUIState = false;
    bool automationScene = false;
};

void ApplySceneRuntimeUiOptions( SceneRuntimeUiOptionsContext context, const SceneUIOptions& options );

} // namespace Runtime
} // namespace SkullbonezCore
