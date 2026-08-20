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
  Required broadphase cells: Validation gate requiring the broadphase grid to
    activate a particular x-cell span.

Invariants:
  - Helpers borrow SceneWorld as one owner and retain no store pointer.
  - Authored scene setup preserves model insertion order and gate resolution.
  - Parsed asset provenance is copied into scene entities at creation.
  - Setup writes a value gate configuration; validation adopts and mutates it
    only after scene loading returns.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - Agentic/Reference/engine-glossary.md
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
struct SceneSessionState;
class SceneWorld;
struct SceneAutomationGateConfiguration;

class SceneAuthoredSetup
{
  public:

    // Returns a recoverable result because scene data and editor placement can
    // fail capacity or identity constraints before the runtime loop owns them.
    static SkullbonezCore::Core::SbResult AppendSimpleRagdoll( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                               SceneWorld& sceneWorld,
                                                               const Physics::RagdollBuildOptions& options );
    static void SetUpCameras( SceneWorld& sceneWorld, const AuthoredScene& scene );

    // Returns failure before required gates are resolved when model population
    // cannot append a requested scene object.
    static SkullbonezCore::Core::SbResult SetUpSceneEntities( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                                              SceneSessionState& sceneState, SceneWorld& sceneWorld,
                                                              SceneAutomationGateConfiguration& automationGates,
                                                              const AuthoredScene& scene );
    static void SetUpRequiredContacts( SceneWorld& sceneWorld, SceneAutomationGateConfiguration& automationGates,
                                       const AuthoredScene& scene );
    static SkullbonezCore::Core::SbResult
    SetUpRequiredSleepingDynamicBodies( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, SceneWorld& sceneWorld,
                                        SceneAutomationGateConfiguration& automationGates, const AuthoredScene& scene );
    static void SetUpRequiredBroadphaseXCells( SceneAutomationGateConfiguration& automationGates,
                                               const AuthoredScene& scene );
};

} // namespace Runtime
} // namespace SkullbonezCore
