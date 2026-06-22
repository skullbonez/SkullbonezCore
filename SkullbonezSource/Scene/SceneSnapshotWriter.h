/*
File: SkullbonezSource/Scene/SceneSnapshotWriter.h
Purpose:
  Serializes the current scene state back into a scene JSON file.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.

Related:
  - SkullbonezSource/Scene/SceneSnapshotWriter.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
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
