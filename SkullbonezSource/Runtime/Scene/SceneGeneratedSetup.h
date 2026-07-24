/*
File: SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
Purpose:
  Declares generated/demo scene population helpers.

Summary:
  SceneGeneratedSetup owns deterministic generated scene algorithms: default
  demo cameras, mixed object spawning, and exact-count solver objects.
  Each helper borrows the single SceneWorld owner rather than republishing its
  camera, terrain, entity, and physics stores as independent participants.

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
  - Context structs borrow SceneWorld as one owner and retain no store pointer.
  - Generated setup does not load authored scene files.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
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
namespace Runtime
{
class SceneController;
class SceneWorld;
} // namespace Runtime
namespace Physics
{
class PhysicsEngine;
}
namespace Geometry
{
class Terrain;
}
namespace Runtime
{
struct SceneSessionState;

enum class GeneratedObjectTypeOverride
{
    Mixed,
    AllBalls,
    AllBoxes
};

struct SceneGeneratedCameraContext
{
    SceneWorld& sceneWorld;
};

struct SceneGeneratedModelContext
{
    SceneSessionState& scene;
    const SkullbonezCore::Core::EngineConfig& config;
    SceneWorld& sceneWorld;
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
    SkullbonezCore::Core::SbResult status;
    bool applied = false; // False means no generated request matched; authored scene setup should continue.
};

class SceneGeneratedSetup
{
  public:
    static void SetUpCameras( SceneGeneratedCameraContext context );
    static SkullbonezCore::Core::SbResult SetUpSceneEntities( SceneGeneratedModelContext context, int count );
    static SkullbonezCore::Core::SbResult
    SetUpSolverObjects( SceneGeneratedModelContext context, int balls, int boxes );
    static SceneGeneratedSetupResult TrySetUpRequestedModels( SceneGeneratedModelContext context,
                                                              const SceneGeneratedPopulationRequest& request,
                                                              bool useDefaultWhenNoRequest );
};

} // namespace Runtime
} // namespace SkullbonezCore
