/*
File: SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h
Purpose:
  Declares authored scene application helpers.

Mental model:
  SceneAuthoredSetup owns the deterministic transformation from parsed scene
  JSON into runtime cameras, model bodies, constraints, materials, and
  validation gates. Run still supplies the live storage while scene ownership is
  extracted one reversible slice at a time.

Invariants:
  - Context structs borrow state and are not retained by setup helpers.
  - Authored scene setup preserves model insertion order and gate resolution.
  - Runtime gate state stays mutable after setup because frame updates mark
    contacts and broadphase cells complete.

Related:
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include <vector>

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
namespace Geometry
{
class Terrain;
}
namespace Basics
{
class TestScene;
struct RunSceneState;

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
    Environment::CameraCollection*& cameras;
    Geometry::Terrain& terrain;
};

struct SceneAuthoredModelContext
{
    RunSceneState& sceneState;
    Environment::WorldEnvironment& world;
    Geometry::Terrain* terrain;
    GameObjects::GameModelCollection& models;
    std::vector<RunRequiredContactState>& requiredContacts;
    std::vector<RunRequiredBroadphaseXCellsState>& requiredBroadphaseXCells;
};

class SceneAuthoredSetup
{
  public:
    static void SetUpCameras( SceneAuthoredCameraContext context, const TestScene& scene );
    static void SetUpGameModels( SceneAuthoredModelContext context, const TestScene& scene );
    static void SetUpRequiredContacts( SceneAuthoredModelContext context, const TestScene& scene );
    static void SetUpRequiredBroadphaseXCells( SceneAuthoredModelContext context, const TestScene& scene );
};

} // namespace Basics
} // namespace SkullbonezCore
