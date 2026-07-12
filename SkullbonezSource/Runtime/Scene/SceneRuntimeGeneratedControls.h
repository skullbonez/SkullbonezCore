/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h
Purpose:
  Declares live generated-scene rebuild helpers outside Run.

Mental model:
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
  - Helpers mutate generated scene/model state only through the context.
  - Generated model/resource mutation starts only after a successful GPU drain.
  - Returned status and follow-up flags must be honored by every caller.

Related:
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneGeneratedSetup.h"
#include "../RunCameraState.h"
#include "../../Core/SbResult.h"

namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
}
namespace Basics
{
class SceneController;
}
namespace Geometry
{
class Terrain;
}
namespace Rendering
{
class IRenderDeviceLifecycle;
}
namespace Basics
{
class SceneController;
class SimulationSystem;
class RuntimeTools;

struct SceneRuntimeGeneratedControlContext
{
    RunSceneState& scene;
    RunSceneUIOverrideState& uiOverrides;
    RunCameraState& camera;
    SceneController& controller;
    const EngineConfig& config;
    Environment::WorldEnvironment& world;
    Geometry::Terrain* terrain = nullptr;
    Basics::SceneController& models;
    SimulationSystem& simulation;
    RuntimeTools& tools;
    Rendering::IRenderDeviceLifecycle* renderLifecycle = nullptr;
    GeneratedObjectTypeOverride objectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    int modelCapacity = 0;
};

struct SceneRuntimeGeneratedControlAction
{
    // Lane R: callers must terminate the current command/frame when a GPU
    // drain failed; no generated model/resource mutation has occurred.
    SbResult status = SbResult::Success();
    bool resetReplayTimeline = false;
    bool scheduleProfileReset = false;
};

struct SceneGeneratedUICommandResult
{
    bool accepted = false;
    SceneRuntimeGeneratedControlAction action;
};

SceneRuntimeGeneratedControlAction ApplyUIModelCountOverride( SceneRuntimeGeneratedControlContext context, int count );
SceneRuntimeGeneratedControlAction
ApplyUISolverObjectCounts( SceneRuntimeGeneratedControlContext context, int balls, int boxes );
SceneGeneratedUICommandResult ApplySceneGeneratedModelCountUICommand( SceneRuntimeGeneratedControlContext context,
                                                                      int requestedModelCount );
SceneGeneratedUICommandResult ApplySceneGeneratedSolverBallCountUICommand( SceneRuntimeGeneratedControlContext context,
                                                                           int requestedSolverBallCount );
SceneGeneratedUICommandResult ApplySceneGeneratedSolverBoxCountUICommand( SceneRuntimeGeneratedControlContext context,
                                                                          int requestedSolverBoxCount );

} // namespace Basics
} // namespace SkullbonezCore
