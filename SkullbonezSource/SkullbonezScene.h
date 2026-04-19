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

#ifndef SKULLBONEZ_SCENE_H
#define SKULLBONEZ_SCENE_H

#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Scenes
{
/* -- Renderer Type -----------------------------------------------------------------------

    2D: Orthographic rendering (pendulum, 2D visualizations)
    3D: Perspective rendering (physics simulation, terrain, etc)

-------------------------------------------------------------------------------------------------*/
enum class RendererType
{
  RENDERER_2D,
  RENDERER_3D
};

/* -- Scene Interface -----------------------------------------------------------------

    Base class for all scenes. Each scene encapsulates:
    - Initialization (geometry, cameras, physics setup)
    - Update logic (physics, animations)
    - Rendering (3D or 2D)
    - Configuration (gravity, scene parameters)

-------------------------------------------------------------------------------------------------*/
class Scene
{
public:
  virtual ~Scene() = default;

  // Configuration
  virtual RendererType GetRendererType() const = 0;
  virtual float GetGravity() const = 0;
  virtual const char* GetName() const = 0;

  // Lifecycle
  virtual void Initialise() = 0;
  virtual void Update(float deltaTime) = 0;
  virtual void Render() = 0;
  virtual void Destroy() = 0;
};

} // namespace Scenes
} // namespace SkullbonezCore

#endif // SKULLBONEZ_SCENE_H
