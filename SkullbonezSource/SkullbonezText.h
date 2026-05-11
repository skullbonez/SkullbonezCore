#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezIShader.h"
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
    inline static uint32_t fontTexture = 0;
    inline static uint32_t dynamicVB = 0;   // solid-quad VB: [x,y,u,v] — used by Render2dQuad
    inline static uint32_t textBatchVB = 0; // batch text VB: [x,y,u,v,r,g,b] — flushed once per frame
    inline static std::unique_ptr<Rendering::IShader> pTextShader;
    inline static std::unique_ptr<Rendering::IShader> pSolidShader;
    inline static float charAdvance[96] = {};

    // NOTES: positioning is relational to centre of client rect
    //		  xPosition and yPosition should be (< 0.5f) and (> - 0.5f)
    //		  fSize should be between 0 and 1
    //		  pass additional arguments to render variables (just like printf)
    static void Render2dText( float xPosition, float yPosition, float fSize, const char* cRawText, ... );                                 // Accumulates white text into the batch
    static void Render2dTextColor( float xPosition, float yPosition, float fSize, float r, float g, float b, const char* cRawText, ... ); // Accumulates colored text into the batch
    static void FlushText();                                                                                                              // Uploads and draws all accumulated text in one call
    static void Render2dQuad( float x0, float y0, float x1, float y1, float r, float g, float b, float a );                               // Renders a flat-coloured 2D HUD quad (immediate, separate draw)
    static void BuildFont( const char* cFontName );                                                                                       // Loads (or generates) SDF atlas, builds GPU resources
    static bool GenerateSdfAtlasToFile( const char* cFontName, const char* cOutPath );                                                    // Generates SDF atlas to binary file (also usable via --gen-atlas)
    static void DeleteFont();                                                                                                             // Releases GPU font resources
    static void RebuildProjection( int w, int h );                                                                                        // Recomputes ortho projection after a window resize
};
} // namespace Text
} // namespace SkullbonezCore
