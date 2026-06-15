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

class PersistentContactSolver
{
  public:
    void Solve( PhysicsWorld& world, GameObjects::GameModelCollection& collection, float dt );
};
} // namespace Physics
} // namespace SkullbonezCore
