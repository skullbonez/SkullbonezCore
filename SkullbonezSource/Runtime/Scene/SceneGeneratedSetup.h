/*
File: SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
Purpose:
  Declares generated/demo scene population helpers.

Mental model:
  SceneGeneratedSetup owns deterministic generated scene algorithms: default
  demo cameras, mixed object spawning, and exact-count solver objects. Run still
  supplies the live world/model/camera services while later scene phases move
  more load state into scene-owned coordinators.

Glossary:
  Generated scene: Runtime-created demo scene with deterministic cameras and
    model placement.
  Object type override: Command-line/runtime option that forces generated
    objects to all balls or all boxes.
  Solver object: Exact-count validation object used by deterministic physics
    scenes.
  Population result: Recoverable setup status plus a flag that says whether
    generated setup actually owned the model population for this load.

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

#include "../../Core/SbResult.h"
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
    Environment::CameraCollection& cameras;
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

struct SceneGeneratedPopulationRequest
{
    // Concept: One request captures the generated-scene source of authority:
    // UI exact counts, scene-authored solver counts, or demo defaults.
    int uiModelCountOverride = -1;
    int uiSolverBallCountOverride = -1;
    int uiSolverBoxCountOverride = -1;
    int sceneSolverBallCount = 0;
    int sceneSolverBoxCount = 0;
    int defaultModelCount = 0;
};

struct SceneGeneratedSetupResult
{
    SbResult status;
    bool applied = false; // False means no generated request matched; authored scene setup should continue.
};

class SceneGeneratedSetup
{
  public:
    static void SetUpCameras( SceneGeneratedCameraContext context );
    static SbResult SetUpGameModels( SceneGeneratedModelContext context, int count );
    static SbResult SetUpSolverObjects( SceneGeneratedModelContext context, int balls, int boxes );
    static SceneGeneratedSetupResult TrySetUpRequestedModels( SceneGeneratedModelContext context,
                                                              const SceneGeneratedPopulationRequest& request,
                                                              bool useDefaultWhenNoRequest );
};

} // namespace Basics
} // namespace SkullbonezCore
