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
  Object type override: Command-line/runtime option that forces generated
    objects to all balls or all boxes.
  Population mode: Caller-resolved choice between model, exact-solver, and no
    generated population after UI/authored/default precedence is applied.
  Population result: Recoverable setup status plus a flag that says whether
    generated setup actually owned the model population for this load.

Invariants:
  - The setup helpers preserve the existing MSVC-compatible RNG sequence.
  - Helpers borrow SceneWorld as one owner and retain no store pointer.
  - Generated setup does not load authored scene files.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - SkullbonezSource/Runtime/Scene/SceneLoadRequest.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/SbResult.h"
#include "../../Core/Config.h"
#include "../Startup/RunLaunchOptions.h"

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

enum class GeneratedPopulationMode
{
    None,
    Models,
    Solver
};

struct SceneGeneratedSetupResult
{
    SkullbonezCore::Core::SbResult status;
    bool applied = false; // False means no generated request matched; authored scene setup should continue.
};

class SceneGeneratedSetup
{
  public:
    static void SetUpCameras( SceneWorld& sceneWorld );
    static SkullbonezCore::Core::SbResult SetUpSceneEntities( SceneSessionState& scene,
                                                              const SkullbonezCore::Core::EngineConfig& config,
                                                              SceneWorld& sceneWorld,
                                                              GeneratedObjectTypeOverride objectTypeOverride, int count );
    static SkullbonezCore::Core::SbResult
    SetUpSolverObjects( SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config, SceneWorld& sceneWorld,
                        GeneratedObjectTypeOverride objectTypeOverride, int balls, int boxes );
    static SceneGeneratedSetupResult
    TrySetUpRequestedModels( SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config,
                             SceneWorld& sceneWorld, GeneratedObjectTypeOverride objectTypeOverride,
                             GeneratedPopulationMode mode, int modelCount, int balls, int boxes );
};

} // namespace Runtime
} // namespace SkullbonezCore
