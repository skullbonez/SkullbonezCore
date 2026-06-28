/*
File: SkullbonezSource/Rendering/Text.h
Purpose:
  Builds and draws bitmap/SDF text for HUD and diagnostics.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Asset system: Runtime-owned registry borrowed during font setup to resolve
  text and HUD quad shaders.
  SDF (Signed Distance Field): Texture representation used for crisp scalable
  text rendering.
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
  VB (Vertex Buffer): GPU buffer containing text or quad vertex attributes.
  RGBA (Red, Green, Blue, Alpha): Four-channel color payload used by HUD quads.
  UV (Texture Coordinates): Font-atlas coordinates used to sample glyphs.
  FOV (Field of View): Camera/projection angle that defines legacy text space.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Command/resource facets: Borrowed renderer capabilities supplied by the UI
  text pass for the duration of HUD drawing and font resource setup.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Text2d owns static backend handles for the active font and batch buffers;
    DeleteFont must run before backend teardown.
  - Render2dText/BatchQuad enqueue CPU-side vertices, and FlushText/FlushQuads
    are the draw boundaries for those queues.
  - Draw calls require a live ScopedRenderContext; Text2d may cache opaque
    handles, but not renderer ownership.

Related:
  - SkullbonezSource/Rendering/Text.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"
#include "IShader.h"
#include "../Maths/Matrix4.h"

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
} // namespace Assets
namespace Rendering
{
class IRenderCommandContext;
class IRenderResourceFactory;
} // namespace Rendering

namespace Text
{
/* -- Text 2d
----------------------------------------------------------------------------------------------------------------------------------------------------

    Provides a series of static methods to draw 2D text to the screen using a shader-based
    font atlas. Replaces the old display-list font approach.

    Coordinate space matches the legacy system: x/y positions are in the frustum-unit space
    at the near clip plane (FOV=45 degrees, aspect=screen_x/screen_y from engine.cfg).
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Text2d
{

  public:
    // Lifetime: binds the per-pass render facets for legacy static Text2d draw
    // helpers. It restores the previous binding on destruction so nested UI
    // helpers cannot leak a frame context beyond the active text pass.
    class ScopedRenderContext
    {
      public:
        ScopedRenderContext( Rendering::IRenderCommandContext& renderCommands,
                             Rendering::IRenderResourceFactory& renderResources );
        ~ScopedRenderContext();
        ScopedRenderContext( const ScopedRenderContext& ) = delete;
        ScopedRenderContext& operator=( const ScopedRenderContext& ) = delete;

      private:
        Rendering::IRenderCommandContext* m_previousCommands = nullptr;
        Rendering::IRenderResourceFactory* m_previousResources = nullptr;
    };

    inline static uint32_t fontTexture = 0;
    inline static uint32_t dynamicVB = 0;                                // solid-quad VB: [x,y,u,v] — used by Render2dQuad (immediate, one draw per call)
    inline static uint32_t textBatchVB = 0;                              // batch text VB: [x,y,u,v,r,g,b] — flushed once per frame
    inline static uint32_t quadBatchVB = 0;                              // batch quad VB: [x,y,r,g,b,a] — flushed once per frame via FlushQuads()
    inline static std::unique_ptr<Rendering::IShader> pTextShader;
    inline static std::unique_ptr<Rendering::IShader> pSolidShader;
    inline static std::unique_ptr<Rendering::IShader> pSolidBatchShader; // per-vertex RGBA batch shader
    inline static float charAdvance[96] = {};

    inline static float s_halfW = 0.0f;                                  // current ortho half-width  (right edge X)
    inline static float s_halfH = 0.0f;                                  // current ortho half-height (top edge Y)

    // Text coordinates are centered on the client rect in legacy frustum units:
    // x/y normally stay within [-0.5, 0.5], fSize is normalized, and the format
    // string accepts printf-style arguments.
    static void Render2dText( float xPosition,
                              float yPosition,
                              float fSize,
                              const char* cRawText,
                              ... );                                     // Queues white SDF text for this frame's text batch.
    static void Render2dTextColor( float xPosition,
                                   float yPosition,
                                   float fSize,
                                   float r,
                                   float g,
                                   float b,
                                   const char* cRawText,
                                   ... );                                // Queues colored SDF text for this frame's text batch.
    static void FlushText();                                             // Uploads queued text once so HUD strings stay one draw call.
    static void Render2dQuad( float x0,
                              float y0,
                              float x1,
                              float y1,
                              float r,
                              float g,
                              float b,
                              float a );                                 // Immediate HUD quad path for legacy call sites.
    static void BatchQuad( float x0,
                           float y0,
                           float x1,
                           float y1,
                           float r,
                           float g,
                           float b,
                           float a );                                    // Queues a colored quad for the shared HUD batch.
    static void BatchTriangle( float x0,
                               float y0,
                               float x1,
                               float y1,
                               float x2,
                               float y2,
                               float r,
                               float g,
                               float b,
                               float a );                                // Queues a colored triangle in the shared HUD batch.
    static void FlushQuads();                                            // Uploads queued quads/triangles once for the frame.
    static void BuildFont( const Assets::AssetSystem& assets,
                           Rendering::IRenderResourceFactory& renderResources,
                           const char* cFontName,
                           int initialScreenW,
                           int initialScreenH );                         // Loads or generates SDF atlas resources for the active backend.
    static bool GenerateSdfAtlasToFile( const char* cFontName,
                                        const char* cOutPath );          // Offline SDF atlas writer used by --gen-atlas tooling.
    static void DeleteFont( Rendering::IRenderResourceFactory* renderResources = nullptr ); // Releases GPU font resources before backend teardown.
    static void RebuildProjection( int w, int h );                       // Recomputes ortho projection after a window resize
    static float HalfW()
    {
        return s_halfW;
    } // Right edge X in text space; varies with aspect ratio.
    static float HalfH()
    {
        return s_halfH;
    } // Top edge Y in text space; fixed by the text projection FOV.
    static float MeasureText( float fSize, const char* text );           // Width in text-space units for already-formatted strings.
};
} // namespace Text
} // namespace SkullbonezCore
