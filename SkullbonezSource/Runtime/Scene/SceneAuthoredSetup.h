/*
File: SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h
Purpose:
  Declares authored scene application helpers.

Summary:
  SceneAuthoredSetup owns the deterministic transformation from parsed scene
  JSON into runtime cameras, model bodies, constraints, materials, and
  validation gates. Run still supplies the live storage while scene ownership is
  extracted one reversible slice at a time.

Glossary:
  Authored scene: Parsed `.scene.json` data that explicitly drives runtime
    setup.
  Required contact: Validation gate requiring two named bodies to touch.
  Required broadphase cells: Validation gate requiring the broadphase grid to
    activate a particular x-cell span.
  Scene entity: Durable scene-owned identity, display, material, and asset row
    committed beside the live physics body.

Invariants:
  - Context structs borrow state and are not retained by setup helpers.
  - Authored scene setup preserves model insertion order and gate resolution.
  - Parsed asset provenance is copied into scene entities at creation.
  - Runtime gate state stays mutable after setup because frame updates mark
    contacts and broadphase cells complete.

Related:
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "../../Core/SbResult.h"

#include <vector>

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment
namespace Basics
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
namespace Basics
{
class TestScene;
struct RunSceneState;
class SceneEntityStore;

struct RunRequiredContactState
{
    char nameA[64] = {};
    char nameB[64] = {};
    int bodyA = -1;
    int bodyB = -1;
    bool touched = false;
};

struct RunRequiredBroadphaseXCellsState
{
    int minCellX = 0;
    int maxCellX = 0;
    int cellY = 0;
    int cellZ = 0;
    int lastActiveCellCount = 0;
    int lastObservedMinX = 0;
    int lastObservedMaxX = 0;
    int lastMissingCellX = -1;
    bool hasObservedXRange = false;
    bool activated = false;
};

struct SceneAuthoredCameraContext
{
    Environment::CameraCollection& cameras;
    Geometry::Terrain& terrain;
};

struct SceneAuthoredModelContext
{
    RunSceneState& sceneState;
    Environment::WorldEnvironment& world;
    Geometry::Terrain* terrain;
    Basics::SceneController& models;
    SceneEntityStore& entities;
    Physics::PhysicsEngine& physics;
    std::vector<RunRequiredContactState>& requiredContacts;
    std::vector<RunRequiredBroadphaseXCellsState>& requiredBroadphaseXCells;
};

struct SceneSimpleRagdollAppendContext
{
    RunSceneState& sceneState;
    Environment::WorldEnvironment& world;
    Geometry::Terrain* terrain;
    Basics::SceneController& models;
    Physics::PhysicsEngine& physics;
};

class SceneAuthoredSetup
{
  public:
    // Returns a recoverable result because scene data and editor placement can
    // fail capacity or identity constraints before the runtime loop owns them.
    static SbResult AppendSimpleRagdoll( SceneSimpleRagdollAppendContext context,
                                         const Physics::RagdollBuildOptions& options );
    static void SetUpCameras( SceneAuthoredCameraContext context, const TestScene& scene );
    // Returns failure before required gates are resolved when model population
    // cannot append a requested scene object.
    static SbResult SetUpSceneEntities( SceneAuthoredModelContext context, const TestScene& scene );
    static void SetUpRequiredContacts( SceneAuthoredModelContext context, const TestScene& scene );
    static void SetUpRequiredBroadphaseXCells( SceneAuthoredModelContext context, const TestScene& scene );
};

} // namespace Basics
} // namespace SkullbonezCore
