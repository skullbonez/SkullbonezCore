/*
File: SkullbonezSource/Scene/SceneSnapshotWriter.h
Purpose:
  Serializes the current scene state back into a scene JSON file.

Mental model:
  SceneSnapshotWriter.h serializes the current scene state back into a scene
  JSON file. As a public header, keep edits anchored on scene-file parsing or
  snapshot contracts and on the glossary/invariants below.

Glossary:
  Snapshot: Saved live body/collider/material state, not the original spawn command.
  Scene entity store: Durable source for names and render material intent while
    the writer boundary is narrowed in C4.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.
  - Snapshot serialization reads durable metadata from SceneEntityStore, never
    transient GameModel feedback rows.

Related:
  - SkullbonezSource/Scene/SceneSnapshotWriter.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Basics
{
class SceneEntityStore;
}
namespace Environment
{
class WorldEnvironment;
}

namespace GameObjects
{
class GameModelCollection;

class SceneSnapshotWriter
{
  public:
    static bool Save( GameModelCollection& collection,
                      const Basics::SceneEntityStore& entities,
                      const char* path,
                      bool physicsOn,
                      bool textOn,
                      Environment::WorldEnvironment& worldEnv,
                      const Math::Vector::Vector3& camEye,
                      const Math::Vector::Vector3& camView,
                      const Math::Vector::Vector3& camUp,
                      bool editableScene = false,
                      bool fixedStep = false,
                      bool waterHidden = false,
                      bool terrainHidden = false,
                      bool hasFlatSlope = false,
                      float flatBaseY = 0.0f,
                      float flatSlopeX = 0.0f,
                      float flatSlopeZ = 0.0f );
};
} // namespace GameObjects
} // namespace SkullbonezCore
