/*
File: SkullbonezSource/Scene/SceneSnapshotWriter.h
Purpose:
  Serializes the current scene state back into a scene JSON file.

Mental model:
  Callers assemble a non-owning view from the scene (including behavior
  grouping), physics, joint, and world owners. The writer resolves every row by
  stable identity and emits either a direct state object or an asset part state.

Glossary:
  Snapshot: Saved live body/collider/material state, not the original spawn command.
  Save view: Synchronous borrowed references/pointers to authoritative owner data.
  Asset part state: Full live body/collider state nested under its durable asset affiliation.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.
  - Snapshot serialization reads durable metadata from SceneEntityStore, never
    transient GameModel feedback rows.
  - The writer retains no view pointer after Save returns.
  - Owner count or identity disagreement is fatal topology drift; file failure
    is a recoverable Lane R result.

Related:
  - SkullbonezSource/Scene/SceneSnapshotWriter.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/SbResult.h"
#include "../Maths/Vector3.h"
#include "../Physics/PhysicsWorldForces.h"

namespace SkullbonezCore
{
namespace Basics
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
struct SceneSaveView
{
    // Lifetime: every member is borrowed for one synchronous Save call. The
    // caller must keep vector storage stable until Save returns.
    const Basics::SceneEntityStore& entities;
    const Physics::PhysicsBodyStore& bodies;
    const Physics::ColliderStore& colliders;
    const Physics::PointJointConstraint* pointJoints = nullptr;
    int pointJointCount = 0;
    float gravity = 0.0f;
    float fluidSurfaceHeight = 0.0f;
    float fluidDensity = 0.0f;
    Physics::MutualGravitySettings mutualGravity;
};

struct SceneSaveRequest
{
    const char* path = nullptr;
    Math::Vector::Vector3 cameraEye;
    Math::Vector::Vector3 cameraView;
    Math::Vector::Vector3 cameraUp;
    bool physicsOn = false;
    bool textOn = false;
    bool editableScene = false;
    bool fixedStep = false;
    bool waterHidden = false;
    bool terrainHidden = false;
    bool hasFlatSlope = false;
    float flatBaseY = 0.0f;
    float flatSlopeX = 0.0f;
    float flatSlopeZ = 0.0f;
};

class SceneSnapshotWriter
{
  public:
    // Saves one schema-v2 snapshot. External path/write failures return Lane R;
    // mismatched owner topology fails through the engine fatal-invariant lane.
    static Basics::SbResult Save( const SceneSaveView& scene, const SceneSaveRequest& request );
};
} // namespace GameObjects
} // namespace SkullbonezCore
