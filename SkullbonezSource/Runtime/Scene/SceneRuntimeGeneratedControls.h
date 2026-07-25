/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h
Purpose:
  Declares live generated-scene rebuild helpers outside Run.

Summary:
  Scene UI controls can rebuild the generated model pool while keeping the
  active scene selected. This module owns the deterministic rebuild mutation and
  returns the replay/profiler follow-up work the composition root still runs.

Glossary:
  Generated control: UI action that changes generated scene object counts.
  Generated UI command: One-frame Scene/Run tab request for generated object
    counts.
  Rebuild action: Returned flags for caller-owned replay/profiler cleanup.
  Action status: Lane R result that blocks all rebuild mutations when the GPU
    drain cannot prove old resource use complete.
  Model capacity: Active object capacity limit.

Invariants:
  - Helpers mutate generated scene/model state only through narrow synchronous
    participants and one explicit SceneController topology owner.
  - Generated model/resource mutation starts only after a successful GPU drain.
  - Returned status and follow-up flags must be honored by every caller.

Related:
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneGeneratedSetup.h"
#include "../Camera/CameraControlState.h"
#include "../../Core/SbResult.h"

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12FrameOwner;
}
namespace Runtime
{
class SceneController;
class SimulationSystem;
class RuntimeTools;

// Concept: generated rebuild policy is copied separately from mutable owners so
// no helper receives the complete runtime graph through a catch-all context.
struct SceneGeneratedControlPolicy
{
    const SkullbonezCore::Core::EngineConfig& config;
    GeneratedObjectTypeOverride objectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    int modelCapacity = 0;
};

struct SceneGeneratedControlPresentation
{
    // Lifetime: these UI/camera borrows live only for one rebuild command.
    SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides;
    CameraControlState& camera;
};

struct SceneGeneratedControlResetParticipants
{
    // Hazard: this participant owns the ordered drain/tool/simulation reset
    // transaction that must finish before topology is repopulated.
    SimulationSystem& simulation;
    RuntimeTools& tools;
    Rendering::Dx12FrameOwner* renderFrame = nullptr;
};

struct SceneRuntimeGeneratedControlAction
{
    // Lane R: callers must terminate the current command/frame when a GPU
    // drain failed; no generated model/resource mutation has occurred.
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    bool resetReplayTimeline = false;
    bool scheduleProfileReset = false;
};

struct SceneGeneratedUICommandResult
{
    bool accepted = false;
    SceneRuntimeGeneratedControlAction action;
};

SceneRuntimeGeneratedControlAction ApplyUIModelCountOverride(
    SceneGeneratedControlPolicy policy,
    SceneGeneratedControlPresentation presentation,
    SceneGeneratedControlResetParticipants reset,
    SceneController& scene,
    int count
);
SceneRuntimeGeneratedControlAction ApplyUISolverObjectCounts(
    SceneGeneratedControlPolicy policy,
    SceneGeneratedControlPresentation presentation,
    SceneGeneratedControlResetParticipants reset,
    SceneController& scene,
    int balls,
    int boxes
);
SceneGeneratedUICommandResult ApplySceneGeneratedModelCountUICommand(
    SceneGeneratedControlPolicy policy,
    SceneGeneratedControlPresentation presentation,
    SceneGeneratedControlResetParticipants reset,
    SceneController& scene,
    int requestedModelCount
);
SceneGeneratedUICommandResult ApplySceneGeneratedSolverBallCountUICommand(
    SceneGeneratedControlPolicy policy,
    SceneGeneratedControlPresentation presentation,
    SceneGeneratedControlResetParticipants reset,
    SceneController& scene,
    int requestedSolverBallCount
);
SceneGeneratedUICommandResult ApplySceneGeneratedSolverBoxCountUICommand(
    SceneGeneratedControlPolicy policy,
    SceneGeneratedControlPresentation presentation,
    SceneGeneratedControlResetParticipants reset,
    SceneController& scene,
    int requestedSolverBoxCount
);

} // namespace Runtime
} // namespace SkullbonezCore
