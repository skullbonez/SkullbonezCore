#pragma once

#include "SkullbonezVector3.h"

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
    static bool Save( GameModelCollection& collection, const char* path, bool physicsOn, bool textOn, Environment::WorldEnvironment& worldEnv, const Math::Vector::Vector3& camEye, const Math::Vector::Vector3& camView, const Math::Vector::Vector3& camUp );
};
} // namespace GameObjects
} // namespace SkullbonezCore
