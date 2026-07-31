/*
File: SkullbonezSource/Rendering/Text.h
Purpose:
  Builds and draws bitmap/SDF text for HUD and diagnostics.

Summary:
  Text.h builds and draws bitmap/SDF text for HUD and diagnostics.

Glossary:
  VB (Vertex Buffer): GPU buffer containing text or quad vertex attributes.
  RGBA (Red, Green, Blue, Alpha): Four-channel color payload used by HUD quads.
  FOV (Field of View): Camera/projection angle that defines legacy text space.

Invariants:
  - Text2d owns static backend handles for the active font; RuntimeRenderer owns
    the one fixed-capacity TextBatch passed to every mutating draw operation.
  - Render2dText/BatchQuad enqueue CPU-side vertices into that explicit batch,
    and FlushText/FlushQuads are the draw boundaries for those queues.

Related:
  - SkullbonezSource/Rendering/Text.cpp
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include <array>

#include "../Core/Common.h"
#include "../Core/SbResult.h"
#include "DX12/ShaderDX12.h"
#include "../Maths/Matrix4.h"

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Rendering
{
class Dx12GeometryOwner;
class Dx12ResourceBuilder;
class Dx12TextureOwner;
class Dx12GeometryOwner;
} // namespace Rendering
namespace Text
{

// Concept: TextBatch is frame-local mutable submission state with renderer
// lifetime storage. RuntimeRenderer owns one instance, eliminating hidden BSS
// submission state. The arrays remain fixed-capacity under the runtime
// allocation policy.
class TextBatch
{
  public:
    static constexpr int TEXT_MAX_CHARS = 2048;
    static constexpr int TEXT_FLOATS_PER_VERTEX = 7;
    static constexpr int TEXT_VERTICES_PER_CHAR = 6;
    static constexpr int QUAD_MAX_QUADS = 8192;
    static constexpr int QUAD_FLOATS_PER_VERTEX = 6;
    static constexpr int QUAD_VERTICES_PER_QUAD = 6;
    static constexpr int QUAD_VERTICES_PER_TRIANGLE = 3;

  private:
    friend class Text2d;

    std::array<float, TEXT_MAX_CHARS * TEXT_VERTICES_PER_CHAR * TEXT_FLOATS_PER_VERTEX> m_textVertices {};
    std::array<float, QUAD_MAX_QUADS * QUAD_VERTICES_PER_QUAD * QUAD_FLOATS_PER_VERTEX> m_quadVertices {};
    Math::Transformation::Matrix4 m_projection;
    int m_textVertexCount = 0;
    int m_quadVertexCount = 0;
    int m_projectionWidth = 0;
    int m_projectionHeight = 0;
    float m_halfWidth = 0.0f;
    float m_halfHeight = 0.0f;
};

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
    inline static uint32_t fontTexture = 0;
    inline static uint32_t dynamicVB = 0;                                   // solid-quad VB: [x,y,u,v] — used by Render2dQuad (immediate, one draw per call)
    inline static uint32_t textBatchVB = 0;                                 // batch text VB: [x,y,u,v,r,g,b] — flushed once per frame
    inline static uint32_t quadBatchVB = 0;                                 // batch quad VB: [x,y,r,g,b,a] — flushed once per frame via FlushQuads()
#if !defined( SKULLBONEZ_RENDER_FREE_TESTS )

    // The CPU unit-test lane intentionally omits backend object code. These
    // process-global shader owners exist only in renderer-bearing builds.
    inline static std::unique_ptr<Rendering::ShaderDX12> pTextShader;
    inline static std::unique_ptr<Rendering::ShaderDX12> pSolidShader;
    inline static std::unique_ptr<Rendering::ShaderDX12> pSolidBatchShader; // per-vertex RGBA batch shader
#endif
    inline static float charAdvance[96] = {};

    // Text coordinates are centered on the client rect in legacy frustum units:
    // x/y normally stay within [-0.5, 0.5], fSize is normalized, and the format
    // string accepts printf-style arguments.
    // Queues white SDF text for this frame's text batch.
    static void Render2dTextColor( TextBatch& batch, float xPosition, float yPosition, float fSize, float r, float g,
                                   float b, const char* cRawText,
                                   ... );                                   // Queues colored SDF text for this frame's text batch.
    static void
    FlushText( TextBatch& batch, Rendering::Dx12TextureOwner& renderTextures,
               Rendering::Dx12GeometryOwner& renderCommands );              // Uploads queued text once so HUD strings stay one draw call.
    static void Render2dQuad( TextBatch& batch, Rendering::Dx12GeometryOwner& renderCommands, float x0, float y0, float x1,
                              float y1, float r, float g, float b,
                              float a );                                    // Immediate HUD quad path for legacy call sites.
    static void BatchQuad( TextBatch& batch, Rendering::Dx12GeometryOwner& renderCommands, float x0, float y0, float x1,
                           float y1, float r, float g, float b,
                           float a );                                       // Queues a colored quad for the shared HUD batch.
    static void BatchTriangle( TextBatch& batch, Rendering::Dx12GeometryOwner& renderCommands, float x0, float y0, float x1,
                               float y1, float x2, float y2, float r, float g, float b,
                               float a );                                   // Queues a colored triangle in the shared HUD batch.
    static void
    FlushQuads( TextBatch& batch,
                Rendering::Dx12GeometryOwner& renderCommands );             // Uploads queued quads/triangles once for the frame.
    static SkullbonezCore::Core::SbResult
    BuildFont( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, TextBatch& batch,
               Rendering::Dx12ResourceBuilder& renderResources, Rendering::Dx12TextureOwner& renderTextures,
               Rendering::Dx12GeometryOwner& renderGeometry, const Assets::AssetSystem& assets, int screenW, int screenH,
               const char* cFontName );                                     // Loads or generates SDF atlas resources for the active backend.
    static bool GenerateSdfAtlasToFile( const char* cFontName,
                                        const char* cOutPath );             // Offline SDF atlas writer used by --gen-atlas tooling.
    static void DeleteFont( TextBatch& batch, Rendering::Dx12TextureOwner* renderTextures,
                            Rendering::Dx12GeometryOwner* renderGeometry ); // Releases GPU font resources while a backend is still available.
    static void RebuildProjection( TextBatch& batch, int w, int h );        // Recomputes owned ortho projection after a window resize.
    static float HalfW( const TextBatch& batch )
    {
        return batch.m_halfWidth;
    } // Right edge X in text space; varies with aspect ratio.
    static float HalfH( const TextBatch& batch )
    {
        return batch.m_halfHeight;
    } // Top edge Y in text space; fixed by the text projection FOV.
    static float MeasureText( float fSize, const char* text );              // Width in text-space units for already-formatted strings.

  private:
    static void RenderTextInternal( TextBatch& batch, float xPosition, float yPosition, float fSize, float colR, float colG,
                                    float colB, const char* formatted );
};
} // namespace Text
} // namespace SkullbonezCore
