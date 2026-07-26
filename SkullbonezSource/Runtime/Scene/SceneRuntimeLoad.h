/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h
Purpose:
  Declares scene load orchestration helpers owned by scene runtime code.

Summary:
  SceneController owns the complete load transaction. This module separates
  failure-safe preparation from the first bookkeeping mutation: queue
  validation, runtime-state preservation, and GPU drain happen before commit.

Glossary:
  Load preparation: Failure-safe phase before teardown and object population.
  GPU drain: Checked close, submit, wait, and command-list reopen that proves
    old scene resources are no longer referenced by the GPU.
  Reset snapshot: Preserved operator-owned runtime state for interactive resets.
  Scene browser: UI-facing list of available scene files.

Invariants:
  - PrepareSceneRuntimeLoad returns intent/state without mutating any owner.
  - CommitSceneRuntimeLoad is called only after a successful GPU drain, lifecycle
    generation start, and BeforeSceneUnload consumers have completed.
  - Runtime state preservation is captured before SceneController begins load.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
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
class Dx12FrameOwner;
}
namespace Runtime
{
class SceneController;
struct CameraControlState;
struct OverlayDebugState;
class RuntimeRenderer;

struct SceneRuntimeLoadBeginResult
{

    // Lane R: a failed GPU drain leaves shouldLoad false so SceneController can
    // report failure before it or any frame/resource consumer mutates.
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    bool shouldLoad = false;
    bool makeInteractive = false;
    bool suppressAutomationExit = false;
    bool shouldPreserveRuntimeState = false;
    int index = -1;
    SceneRuntimeResetSnapshot resetSnapshot;
    const std::string* scenePath = nullptr;
};

SceneRuntimeLoadBeginResult
PrepareSceneRuntimeLoad( const SceneController& controller, const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                         const RuntimeRenderer& renderer, const OverlayDebugState& debug, const CameraControlState& camera,
                         Rendering::Dx12FrameOwner* renderFrame, bool interactiveSceneRunRequested, int index,
                         bool suppressExitOnComplete, bool preserveRuntimeState );
void CommitSceneRuntimeLoad( SceneController& controller, SceneLoadNavigationState& navigation,
                             const SceneRuntimeLoadBeginResult& prepared );
void RefreshSceneBrowserList( SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser );
int CurrentSceneBrowserIndex( const SceneController& controller,
                              const SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser );

} // namespace Runtime
} // namespace SkullbonezCore
