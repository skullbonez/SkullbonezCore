/*
File: SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h
Purpose:
  Declares authored scene application helpers.

Summary:
  SceneAuthoredSetup owns the deterministic transformation from parsed scene
  JSON into runtime cameras, model bodies, constraints, materials, and
  validation gates. Setup borrows the single SceneWorld owner plus scene-run
  bookkeeping and a value gate configuration for the synchronous load phase.

Glossary:
  Authored scene: Parsed `.scene.json` data that explicitly drives runtime
    setup.
  Required contact: Validation gate requiring two named bodies to touch.
  Required broadphase cells: Validation gate requiring the broadphase grid to
    activate a particular x-cell span.
  Scene entity: Durable scene-owned identity, display, material, and asset row
    committed beside the live physics body.

Invariants:
  - Context structs borrow SceneWorld as one owner and are not retained.
  - Authored scene setup preserves model insertion order and gate resolution.
  - Parsed asset provenance is copied into scene entities at creation.
  - Setup writes a value gate configuration; validation adopts and mutates it
    only after scene loading returns.

Related:
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "../../Core/SbResult.h"

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
}
namespace Physics
{
class PhysicsEngine;
struct RagdollBuildOptions;
} // namespace Physics
namespace Geometry
{
class Terrain;
}
namespace Runtime
{
class AuthoredScene;
struct RunSceneState;
class SceneWorld;
struct SceneAutomationGateConfiguration;

struct SceneAuthoredCameraContext
{
    SceneWorld& sceneWorld;
};

struct SceneAuthoredModelContext
{
    RunSceneState& sceneState;
    SceneWorld& sceneWorld;
    SceneAutomationGateConfiguration& automationGates;
};

struct SceneSimpleRagdollAppendContext
{
    RunSceneState& sceneState;
    SceneWorld& sceneWorld;
};

class SceneAuthoredSetup
{
  public:
    // Returns a recoverable result because scene data and editor placement can
    // fail capacity or identity constraints before the runtime loop owns them.
    static SkullbonezCore::Core::SbResult AppendSimpleRagdoll( SceneSimpleRagdollAppendContext context,
                                                               const Physics::RagdollBuildOptions& options );
    static void SetUpCameras( SceneAuthoredCameraContext context, const AuthoredScene& scene );
    // Returns failure before required gates are resolved when model population
    // cannot append a requested scene object.
    static SkullbonezCore::Core::SbResult SetUpSceneEntities( SceneAuthoredModelContext context,
                                                              const AuthoredScene& scene );
    static void SetUpRequiredContacts( SceneAuthoredModelContext context, const AuthoredScene& scene );
    static void SetUpRequiredBroadphaseXCells( SceneAuthoredModelContext context, const AuthoredScene& scene );
};

} // namespace Runtime
} // namespace SkullbonezCore
