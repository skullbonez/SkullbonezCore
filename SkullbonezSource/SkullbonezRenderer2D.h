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

/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_RENDERER_2D_H
#define SKULLBONEZ_RENDERER_2D_H

/* -- INCLUDES ------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezShader.h"
#include "SkullbonezVector3.h"

/* -- USING CLAUSES -------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math;

namespace SkullbonezCore
{
namespace Geometry
{
/* -- Skullbonez Renderer 2D ---------------------------------------------------------------------------

    Simple 2D rendering system for primitives (lines, circles).
    Uses orthographic projection and line/triangle rendering.

-------------------------------------------------------------------------------------------------*/
class Renderer2D
{
private:
  Shader* m_pLineShader;
  unsigned int m_lineVAO;
  unsigned int m_lineVBO;
  unsigned int m_maxLineVertices;
  unsigned int m_currentLineVertices;

  struct LineVertex
  {
    float x, y;
    float r, g, b, a;
  };

public:
  Renderer2D();
  ~Renderer2D();

  void Initialise(int screenWidth, int screenHeight);
  void Destroy();

  void BeginFrame(int screenWidth, int screenHeight);
  void DrawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a = 1.0f);
  void DrawCircle(float cx, float cy, float radius, int segments, float r, float g, float b, float a = 1.0f);
  void EndFrame();
};
} // namespace Geometry
} // namespace SkullbonezCore

#endif // SKULLBONEZ_RENDERER_2D_H
