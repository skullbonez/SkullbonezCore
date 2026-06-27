/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h
Purpose:
  Declares live generated-scene rebuild helpers outside Run.

Mental model:
  Scene UI controls can rebuild the generated model pool while keeping the
  active scene selected. This module owns the deterministic rebuild mutation and
  returns the replay/profiler follow-up work the composition root still runs.

Related:
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/run-composition-root-shrink-plan.md
*/
#pragma once

#include "SceneGeneratedSetup.h"
#include "../RunState.h"

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
class IRenderBackend;
}
namespace Basics
{
class SceneController;
class SimulationController;
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
    SimulationController& simulation;
    RuntimeTools& tools;
    Rendering::IRenderBackend* renderer = nullptr;
    GeneratedObjectTypeOverride objectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    int modelCapacity = 0;
};

struct SceneRuntimeGeneratedControlAction
{
    bool resetReplayTimeline = false;
    bool scheduleProfileReset = false;
};

SceneRuntimeGeneratedControlAction ApplyUIModelCountOverride( SceneRuntimeGeneratedControlContext context, int count );
SceneRuntimeGeneratedControlAction
ApplyUISolverObjectCounts( SceneRuntimeGeneratedControlContext context, int balls, int boxes );

} // namespace Basics
} // namespace SkullbonezCore
