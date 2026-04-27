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

#ifndef SKULLBONEZ_PENDULUM_SCENE_H
#define SKULLBONEZ_PENDULUM_SCENE_H

#include "SkullbonezScene.h"
#include "SkullbonezDoublePendulum.h"
#include "SkullbonezRenderer2D.h"

namespace SkullbonezCore
{
namespace Scenes
{
/* -- Pendulum Scene -------------------------------------------------------------------

    2D double pendulum physics visualization scene.
    Uses orthographic rendering with line and circle primitives.

-------------------------------------------------------------------------------------------------*/
class PendulumScene : public Scene
{
  private:
    float m_gravity;
    std::unique_ptr<Physics::DoublePendulum> m_pPendulum;
    std::unique_ptr<Geometry::Renderer2D> m_p2DRenderer;
    int m_screenWidth;
    int m_screenHeight;

  public:
    PendulumScene( float gravity = 30.0f, int screenWidth = 1200, int screenHeight = 900 );
    ~PendulumScene();

    RendererType GetRendererType() const override
    {
        return RendererType::RENDERER_2D;
    }
    float GetGravity() const override
    {
        return m_gravity;
    }
    const char* GetName() const override
    {
        return "Pendulum Scene";
    }

    void Initialise() override;
    void Update( float deltaTime ) override;
    void Render() override;
    void Destroy() override;

    void SetGravity( float gravity )
    {
        m_gravity = gravity;
    }
    void SetScreenDimensions( int width, int height )
    {
        m_screenWidth = width;
        m_screenHeight = height;
    }
};

} // namespace Scenes
} // namespace SkullbonezCore

#endif // SKULLBONEZ_PENDULUM_SCENE_H
