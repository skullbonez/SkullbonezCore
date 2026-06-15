#pragma once

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}

namespace Physics
{
class PhysicsWorld;

class SleepIslandSystem
{
  public:
    void PropagateSupport( PhysicsWorld& world, GameObjects::GameModelCollection& collection );
};
} // namespace Physics
} // namespace SkullbonezCore
