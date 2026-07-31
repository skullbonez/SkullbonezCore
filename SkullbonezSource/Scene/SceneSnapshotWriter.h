/*
File: SkullbonezSource/Scene/SceneSnapshotWriter.h
Purpose:
  Serializes the current scene state back into a scene JSON file.

Summary:
  The scene, session, and presentation owners each publish their own save value.
  SceneSaveRequest composes those values with the destination path, and the
  writer resolves every row by stable identity before emitting JSON.

Glossary:
  Asset part state: Full live body/collider state nested under its durable asset affiliation.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.
  - Snapshot serialization reads durable metadata from SceneEntityStore, never
    transient legacy object record feedback rows.
  - The writer retains no borrowed store or request value after Save returns.
  - Owner count or identity disagreement is fatal topology drift; file failure
    is a recoverable Lane R result.

Related:
  - SkullbonezSource/Scene/SceneSnapshotWriter.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Core/SbResult.h"
#include "../Maths/Vector3.h"
#include "../Physics/PhysicsWorldForces.h"

namespace SkullbonezCore
{
namespace Runtime
{
class SceneEntityStore;
}
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct PointJointConstraint;
} // namespace Physics

namespace GameObjects
{
struct SceneWorldSaveState
{

    // Lifetime: store and joint members borrow one SceneWorld for one
    // synchronous Save call. The owning world keeps their storage stable.
    const Runtime::SceneEntityStore& entities;
    const Physics::PhysicsBodyStore& bodies;
    const Physics::ColliderStore& colliders;
    const Physics::PointJointConstraint* pointJoints = nullptr;
    int pointJointCount = 0;
    float gravity = 0.0f;
    float fluidSurfaceHeight = 0.0f;
    float fluidDensity = 0.0f;
    Physics::MutualGravitySettings mutualGravity;
    Math::Vector::Vector3 cameraEye;
    Math::Vector::Vector3 cameraView;
    Math::Vector::Vector3 cameraUp;
};

struct SceneSessionSaveState
{
    bool physicsOn = false;
    bool textOn = false;
    bool editableScene = false;
    bool fixedStep = false;
    bool hasFlatSlope = false;
    float flatBaseY = 0.0f;
    float flatSlopeX = 0.0f;
    float flatSlopeZ = 0.0f;
};

struct PresentationSaveState
{
    bool waterHidden = false;
    bool terrainHidden = false;
};

struct SceneSaveRequest
{
    const char* path = nullptr;
    SceneWorldSaveState world;
    SceneSessionSaveState session;
    PresentationSaveState presentation;
};

class SceneSnapshotWriter
{
  public:

    // Saves one schema-v2 snapshot. External path/write failures return Lane R;
    // mismatched owner topology fails through the engine fatal-invariant lane.
    static SkullbonezCore::Core::SbResult Save( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                const SceneSaveRequest& request );
};
} // namespace GameObjects
} // namespace SkullbonezCore
