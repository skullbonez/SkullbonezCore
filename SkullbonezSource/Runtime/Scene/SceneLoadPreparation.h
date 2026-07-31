/*
File: SkullbonezSource/Runtime/Scene/SceneLoadPreparation.h
Purpose:
  Declares scene load orchestration helpers owned by scene runtime code.

Summary:
  SceneController owns the complete load transaction. This module separates
  failure-safe preparation from the first bookkeeping mutation: queue
  validation, runtime-state preservation, and GPU drain happen before commit.

Invariants:
  - SceneLoadTransaction preparation returns intent/state without mutating any owner.
  - Commit runs only after a successful GPU drain, lifecycle
    generation start, and BeforeSceneUnload consumers have completed.
  - Runtime state preservation is captured before SceneController begins load.

Related:
  - SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Preparation.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneResetPreservation.h"
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

struct SceneLoadBeginResult
{

    // Lane R: a failed GPU drain leaves shouldLoad false so SceneController can
    // report failure before it or any frame/resource consumer mutates.
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    bool shouldLoad = false;
    bool makeInteractive = false;
    bool suppressAutomationExit = false;
    bool shouldPreserveRuntimeState = false;
    int index = -1;
    SceneResetPreservationSnapshot resetSnapshot;
    const std::string* scenePath = nullptr;
};

} // namespace Runtime
} // namespace SkullbonezCore
