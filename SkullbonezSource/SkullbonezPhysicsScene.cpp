/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                                                                          THE SKULLBONEZ CORE
                                                                                _______
                                                                             .-"       "-.
                                                                            /             \
                                                                           /               \
                                                                           |   .--. .--.   |
                                                                           | )/   | |   \( |
                                                                           |/ \__/   \__/ \|
                                                                           /      /^\      \
                                                                           \__    '='    __/
                                                                             |\         /|
                                                                             |\'"VUUUV"'/|
                                                                             \ `"""""""` /
                                                                              `-._____.-'

                                                                         www.simoneschbach.com
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

#include "SkullbonezPhysicsScene.h"

namespace SkullbonezCore
{
namespace Scenes
{
PhysicsScene::PhysicsScene(float gravity, int ballCount, int seed)
    : m_gravity(gravity), m_ballCount(ballCount), m_randomSeed(seed)
{
}

PhysicsScene::~PhysicsScene()
{
}

void PhysicsScene::Initialise()
{
  // Scene initialization will be delegated to SkullbonezRun
  // which handles terrain, skybox, cameras, game models, etc.
}

void PhysicsScene::Update(float deltaTime)
{
  // Physics update delegated to SkullbonezRun
}

void PhysicsScene::Render()
{
  // 3D rendering delegated to SkullbonezRun
}

void PhysicsScene::Destroy()
{
  // Cleanup delegated to SkullbonezRun
}

} // namespace Scenes
} // namespace SkullbonezCore
