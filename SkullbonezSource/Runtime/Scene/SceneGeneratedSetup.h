/*
File: SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
Purpose:
  Declares generated/demo scene population helpers.

Mental model:
  SceneGeneratedSetup owns deterministic generated scene algorithms: default
  demo cameras, mixed object spawning, and exact-count solver objects. Run still
  supplies the live world/model/camera services while later scene phases move
  more load state into scene-owned coordinators.

Invariants:
  - The setup helpers preserve the existing MSVC-compatible RNG sequence.
  - Context structs borrow state; they do not own scene, world, terrain, model,
    or camera storage.
  - Generated setup does not load authored scene files.

Related:
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include "../../Core/Config.h"

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment
namespace GameObjects
{
class GameModelCollection;
}
namespace Physics
{
class PhysicsEngine;
}
namespace Geometry
{
class Terrain;
}
namespace Basics
{
struct RunSceneState;

enum class GeneratedObjectTypeOverride
{
    Mixed,
    AllBalls,
    AllBoxes
};

struct SceneGeneratedCameraContext
{
    Environment::CameraCollection*& cameras;
    Geometry::Terrain& terrain;
};

struct SceneGeneratedModelContext
{
    RunSceneState& scene;
    const EngineConfig& config;
    Environment::WorldEnvironment& world;
    Geometry::Terrain* terrain;
    GameObjects::GameModelCollection& models;
    Physics::PhysicsEngine& physics;
    GeneratedObjectTypeOverride objectTypeOverride = GeneratedObjectTypeOverride::Mixed;
};

class SceneGeneratedSetup
{
  public:
    static void SetUpCameras( SceneGeneratedCameraContext context );
    static void SetUpGameModels( SceneGeneratedModelContext context, int count );
    static void SetUpSolverObjects( SceneGeneratedModelContext context, int balls, int boxes );
};

} // namespace Basics
} // namespace SkullbonezCore
