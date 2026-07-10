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
  Model capacity: Active object capacity limit.

Invariants:
  - Helpers mutate generated scene/model state only through the context.
  - Returned actions describe caller-owned follow-up work and must be honored.

Related:
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneGeneratedSetup.h"
#include "../RunCameraState.h"

namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
}
namespace GameObjects
{
class GameModelCollection;
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
    GameObjects::GameModelCollection& models;
    SimulationSystem& simulation;
    RuntimeTools& tools;
    Rendering::IRenderDeviceLifecycle* renderLifecycle = nullptr;
    GeneratedObjectTypeOverride objectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    int modelCapacity = 0;
};

struct SceneRuntimeGeneratedControlAction
{
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
