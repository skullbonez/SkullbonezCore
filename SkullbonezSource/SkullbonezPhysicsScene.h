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

#ifndef SKULLBONEZ_PHYSICS_SCENE_H
#define SKULLBONEZ_PHYSICS_SCENE_H

#include "SkullbonezScene.h"

namespace SkullbonezCore
{
namespace Scenes
{
/* -- Physics Scene ---------------------------------------------------------------

    3D physics simulation scene with terrain, skybox, water, and balls.
    Uses perspective rendering with full game object physics.

-------------------------------------------------------------------------------------------------*/
class PhysicsScene : public Scene
{
  private:
    float m_gravity;
    int m_ballCount;
    int m_randomSeed;

  public:
    PhysicsScene( float gravity = -30.0f, int ballCount = 300, int seed = 42 );
    ~PhysicsScene();

    RendererType GetRendererType() const override
    {
        return RendererType::RENDERER_3D;
    }
    float GetGravity() const override
    {
        return m_gravity;
    }
    const char* GetName() const override
    {
        return "Physics Scene";
    }

    void Initialise() override;
    void Update( float deltaTime ) override;
    void Render() override;
    void Destroy() override;

    // Configuration
    void SetGravity( float gravity )
    {
        m_gravity = gravity;
    }
    void SetBallCount( int count )
    {
        m_ballCount = count;
    }
    void SetRandomSeed( int seed )
    {
        m_randomSeed = seed;
    }
};

} // namespace Scenes
} // namespace SkullbonezCore

#endif // SKULLBONEZ_PHYSICS_SCENE_H
