/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                                                                          THE SKULLBONEZ CORE
                                                                                _______
                                                                             .-"       "-.
                                                                            /             \
                                                                           /               \
                                                                           ?   .--. .--.   ?
                                                                           ? )/   ? ?   \( ?
                                                                           ?/ \__/   \__/ \?
                                                                           /      /^\      \
                                                                           \__    '='    __/
                                                                             ?\         /?
                                                                             ?\'"VUUUV"'/?
                                                                             \ `"""""""` /
                                                                              `-._____.-'

                                                                         www.simoneschbach.com
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_TEXT_H
#define SKULLBONEZ_TEXT_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezShader.h"
#include "SkullbonezMatrix4.h"

namespace SkullbonezCore
{
namespace Text
{
/* -- Text 2d ----------------------------------------------------------------------------------------------------------------------------------------------------

    Provides a series of static methods to draw 2D text to the screen using a shader-based
    font atlas. Replaces the legacy wglUseFontOutlines / display list approach.

    Coordinate space matches the legacy system: x/y positions are in the frustum-unit space
    at the near clip plane (FOV=45 degrees, aspect=screen_x/screen_y from engine.cfg).
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Text2d
{

  public:
    static GLuint fontTexture;                             // Font atlas GL texture
    static GLuint textVAO;                                 // VAO for text quad rendering
    static GLuint textVBO;                                 // VBO for text quad rendering
    static std::unique_ptr<Rendering::Shader> pTextShader; // Text m_shader program
    static float charAdvance[96];                          // Normalised advance m_width per char (advance_px / cell_height)

    // NOTES: positioning is relational to centre of client rect
    //		  xPosition and yPosition should be (< 0.5f) and (> - 0.5f)
    //		  fSize should be between 0 and 1
    //		  pass additional arguments to render variables (just like printf)
    static void Render2dText( float xPosition, float yPosition, float fSize, const char* cRawText, ... ); // Renders text to the scene
    static void BuildFont( const HDC hDC, const char* cFontName );                                        // Builds font atlas into GL texture
    static void DeleteFont( void );                                                                       // Releases GL font resources
};
} // namespace Text
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
