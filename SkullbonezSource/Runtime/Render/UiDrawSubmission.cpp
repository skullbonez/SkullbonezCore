/*
File: SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp
Purpose:
  Implements renderer-owned replay of backend-neutral UI draw commands.

Summary:
  The late UI pass lends one immutable draw list and explicit backend owners.
  This unit translates screen-space shapes and text, resolves preview identities
  against the current renderer snapshot, and owns the preview-only GPU objects.

Mental model:
  UI is the author and Runtime/Render is the printer. The author records what
  appears and in which order; the printer chooses DX12 resources and commands.

Glossary:
  Submission barrier: Flush of queued shapes and text before an image so later
    commands remain visually above it.
  Preview catalog: Renderer-owned frame snapshot that maps the UI's stable
    catalog index to one current texture handle and its presentation metadata.

Invariants:
  - Command order, clip depth, and preview batch barriers are preserved exactly.
  - Pixel coordinates are snapped before conversion to Text2d projection space.
  - A missing or stale preview identity renders the authored fallback panel.
  - Resource handles never travel back into UI-owned retained state.

Related:
  - SkullbonezSource/Runtime/Render/UiDrawSubmission.h
  - SkullbonezSource/UI/UIDrawList.h
  - SkullbonezSource/UI/UIFrameComposition.cpp
*/
#include "UiDrawSubmission.h"
#include "RuntimeRenderPasses.h"

#include "../../Assets/AssetSystem.h"
#include "../../Maths/Matrix4.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Rendering/RenderGpuTimingOwner.h"
#include "../../Rendering/Text.h"
#include "../../UI/UI.h"
#include "../../UI/UIDrawList.h"
#include "../../UI/UIFrameComposition.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr Rendering::PassRasterStateBucket PREVIEW_RASTER_STATE = Rendering::MakePassRasterStateBucket( 0, { false, false,
                                                                                                             false } );

// Concept: UI authors pixels while Text2d submits in projection-space units.
// This translator exists only for one replay call and cannot become retained
// UI state or a renderer capability backdoor.
class ImmediateUiSubmitter
{
  public:
    ImmediateUiSubmitter( int screenW, int screenH, Text::TextBatch& textBatch,
                          Rendering::Dx12GeometryOwner& renderGeometry )
        : m_textBatch( textBatch ), m_renderGeometry( renderGeometry )
    {
        screenW = (std::max)( 1, screenW );

        screenH = (std::max)( 1, screenH );
        m_halfH = Text::Text2d::HalfH( textBatch );
        m_halfW = Text::Text2d::HalfW( textBatch );
        m_scaleX = ( m_halfW * 2.0f ) / static_cast<float>( screenW );
        m_scaleY = ( m_halfH * 2.0f ) / static_cast<float>( screenH );
    }

    void Rect( float x, float y, float w, float h, float r, float g, float b, float a )
    {
        float x0 = Snap( x );
        float y0 = Snap( y );
        float x1 = Snap( x + w );
        float y1 = Snap( y + h );

        if ( x1 <= x0 && w > 0.0f )
        {
            x1 = x0 + 1.0f;
        }

        if ( y1 <= y0 && h > 0.0f )
        {
            y1 = y0 + 1.0f;
        }

        Text::Text2d::BatchQuad( m_textBatch, m_renderGeometry, PixelX( x0 ), PixelY( y1 ), PixelX( x1 ), PixelY( y0 ), r, g,
                                 b, a );
    }

    void Triangle( float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a )
    {
        Text::Text2d::BatchTriangle( m_textBatch, m_renderGeometry, PixelX( x0 ), PixelY( y0 ), PixelX( x1 ), PixelY( y1 ),
                                     PixelX( x2 ), PixelY( y2 ), r, g, b, a );
    }

    void RoundedRect( float x, float y, float w, float h, float radius, float r, float g, float b, float a )
    {

        if ( radius > 1.0f && w > 4.0f && h > 4.0f && a > 0.05f )
        {
            RoundedRectFill( x - 0.5f, y - 0.5f, w + 1.0f, h + 1.0f, radius + 0.5f, r, g, b, a * 0.30f );
        }

        RoundedRectFill( x, y, w, h, radius, r, g, b, a );
    }

    void Text( float x, float y, float pxSize, float r, float g, float b, const char* value )
    {
        Text::Text2d::Render2dTextColor( m_textBatch, PixelX( Snap( x ) ), PixelY( Snap( y ) + pxSize ), pxSize * m_scaleY,
                                         r, g, b, "%s", value );
    }

  private:
    static float Snap( float value )
    {
        return std::floor( value + 0.5f );
    }

    float PixelX( float x ) const
    {
        return -m_halfW + x * m_scaleX;
    }

    float PixelY( float y ) const
    {
        return m_halfH - y * m_scaleY;
    }

    void RoundedSpan( float left, float y, float right, float r, float g, float b, float a )
    {

        if ( right <= left || a <= 0.0f )
        {
            return;
        }

        const float fullLeft = std::ceil( left );
        const float fullRight = std::floor( right );
        const float leftCoverage = std::clamp( fullLeft - left, 0.0f, 1.0f );
        const float rightCoverage = std::clamp( right - fullRight, 0.0f, 1.0f );

        if ( leftCoverage > 0.01f )
        {
            Rect( fullLeft - 1.0f, y, 1.0f, 1.0f, r, g, b, a * leftCoverage );
        }

        if ( fullRight > fullLeft )
        {
            Rect( fullLeft, y, fullRight - fullLeft, 1.0f, r, g, b, a );
        }

        if ( rightCoverage > 0.01f )
        {
            Rect( fullRight, y, 1.0f, 1.0f, r, g, b, a * rightCoverage );
        }
    }

    void RoundedRectFill( float x, float y, float w, float h, float radius, float r, float g, float b, float a )
    {

        if ( w <= 0.0f || h <= 0.0f || a <= 0.0f )
        {
            return;
        }

        const float clampedRadius = std::clamp( radius, 0.0f, (std::min)( w, h ) * 0.5f );

        if ( clampedRadius <= 0.5f )
        {
            Rect( x, y, w, h, r, g, b, a );
            return;
        }

        const int capRows = (std::max)( 1, static_cast<int>( std::ceil( clampedRadius ) ) );
        const float middleY = y + static_cast<float>( capRows );
        const float middleH = h - static_cast<float>( capRows * 2 );

        if ( middleH > 0.0f )
        {
            Rect( x, middleY, w, middleH, r, g, b, a );
        }

        const float radiusSq = clampedRadius * clampedRadius;

        for ( int row = 0; row < capRows; ++row )
        {
            const float sample = (std::min)( static_cast<float>( row ) + 0.5f, clampedRadius );
            const float dy = clampedRadius - sample;
            const float xInset = clampedRadius - std::sqrt( (std::max)( 0.0f, radiusSq - dy * dy ) );
            const float left = x + xInset;
            const float right = x + w - xInset;
            RoundedSpan( left, y + static_cast<float>( row ), right, r, g, b, a );
            RoundedSpan( left, y + h - static_cast<float>( row ) - 1.0f, right, r, g, b, a );
        }
    }

    Text::TextBatch& m_textBatch;
    Rendering::Dx12GeometryOwner& m_renderGeometry;
    float m_halfW = 1.0f;
    float m_halfH = 1.0f;
    float m_scaleX = 1.0f;
    float m_scaleY = 1.0f;
};
} // namespace

UiDrawSubmission::~UiDrawSubmission() = default;

void UiDrawSubmission::Submit( const UI::UIDrawList& drawList, Text::TextBatch& textBatch,
                               Rendering::RenderGpuTimingOwner* gpuTiming, Rendering::Dx12TextureOwner& renderTextures,
                               Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12Diagnostics& renderDiagnostics,
                               int screenW, int screenH )
{
    SubmitCommands( drawList, nullptr, textBatch, gpuTiming, nullptr, nullptr, renderTextures, renderGeometry,
                    renderDiagnostics, screenW, screenH );
}

void UiDrawSubmission::SubmitWithPreviews( const UI::UIDrawList& drawList,
                                           const RuntimeRenderTargetPreviewSnapshot& previewData, Text::TextBatch& textBatch,
                                           Rendering::RenderGpuTimingOwner* gpuTiming, Assets::AssetSystem& assets,
                                           Rendering::Dx12ResourceBuilder& renderResources,
                                           Rendering::Dx12TextureOwner& renderTextures,
                                           Rendering::Dx12GeometryOwner& renderGeometry,
                                           Rendering::Dx12Diagnostics& renderDiagnostics, int screenW, int screenH )
{
    SubmitCommands( drawList, &previewData, textBatch, gpuTiming, &assets, &renderResources, renderTextures, renderGeometry,
                    renderDiagnostics, screenW, screenH );
}

void UiDrawSubmission::SubmitCommands( const UI::UIDrawList& drawList, const RuntimeRenderTargetPreviewSnapshot* previewData,
                                       Text::TextBatch& textBatch, Rendering::RenderGpuTimingOwner* gpuTiming,
                                       Assets::AssetSystem* assets, Rendering::Dx12ResourceBuilder* renderResources,
                                       Rendering::Dx12TextureOwner& renderTextures,
                                       Rendering::Dx12GeometryOwner& renderGeometry,
                                       Rendering::Dx12Diagnostics& renderDiagnostics, int screenW, int screenH )
{
    constexpr float offsetX = 0.0f;
    constexpr float offsetY = 0.0f;
    PROFILE_GPU_BEGIN( gpuTiming, "Frame/UI/Draw" );
    ImmediateUiSubmitter immediateDraw( screenW, screenH, textBatch, renderGeometry );
    UI::UIRect clipStack[UI::UIDrawList::MAX_CLIP_DEPTH];
    int clipDepth = 0;
    auto flushQueued = [&]()
    {
        {
            DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Widgets" );

            Text::Text2d::FlushQuads( textBatch, renderGeometry );
        }
        {
            DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Text" );
            Text::Text2d::FlushText( textBatch, renderTextures, renderGeometry );
        }
    };

    for ( const UI::UIDrawList::Command& command : drawList.Commands() )
    {

        switch ( command.type )
        {
        case UI::UIDrawList::CommandType::Rect:
            immediateDraw.Rect( command.x0 + offsetX, command.y0 + offsetY, command.w, command.h, command.r, command.g,
                                command.b, command.a );

            break;
        case UI::UIDrawList::CommandType::RoundedRect:
            immediateDraw.RoundedRect( command.x0 + offsetX, command.y0 + offsetY, command.w, command.h, command.radius,
                                       command.r, command.g, command.b, command.a );

            break;
        case UI::UIDrawList::CommandType::Triangle:
            immediateDraw.Triangle( command.x0 + offsetX, command.y0 + offsetY, command.x1 + offsetX, command.y1 + offsetY,
                                    command.x2 + offsetX, command.y2 + offsetY, command.r, command.g, command.b, command.a );

            break;
        case UI::UIDrawList::CommandType::Text:
            immediateDraw.Text( command.x0 + offsetX, command.y0 + offsetY, command.pxSize, command.r, command.g, command.b,
                                drawList.TextAt( command.textOffset ) );

            break;
        case UI::UIDrawList::CommandType::PushClip:

            if ( clipDepth < UI::UIDrawList::MAX_CLIP_DEPTH )
            {
                UI::UIRect clip = { command.x0 + offsetX, command.y0 + offsetY, command.w, command.h };

                if ( clipDepth > 0 )
                {
                    clip = UI::FrameComposition::IntersectRect( clipStack[clipDepth - 1], clip );
                }

                clipStack[clipDepth++] = clip;
            }

            break;
        case UI::UIDrawList::CommandType::PopClip:

            if ( clipDepth > 0 )
            {
                --clipDepth;
            }

            break;
        case UI::UIDrawList::CommandType::PreviewImage:
        {

            // Invariant: images split the quad/text batches so commands
            // authored after the image remain above it in the final frame.
            flushQueued();
            const UI::UIRect bounds = { command.x0 + offsetX, command.y0 + offsetY, command.w, command.h };

            const UI::UIRect clip = clipDepth > 0 ? clipStack[clipDepth - 1]
                                                  : UI::UIRect { 0.0f, 0.0f, static_cast<float>( screenW ),
                                                                 static_cast<float>( screenH ) };

            const int targetIndex = static_cast<int>( command.preview.catalogIndex );
            const bool canResolve = command.preview.valid && previewData && assets && renderResources && targetIndex >= 0 &&
                                    targetIndex < previewData->count;

            const RuntimeRenderTargetPreview* resource = canResolve
                                                             ? &previewData->targets[static_cast<size_t>( targetIndex )]
                                                             : nullptr;

            if ( resource && resource->available && resource->textureHandle != 0 )
            {
                EnsurePreviewResources( *assets, *renderResources, renderGeometry );

                if ( m_previewShader && m_previewVertexBuffer != 0 )
                {
                    const UI::UIRect visible = UI::FrameComposition::IntersectRect( bounds, clip );

                    if ( visible.w > 1.0f && visible.h > 1.0f )
                    {
                        const float uvLeft = std::clamp( ( visible.x - bounds.x ) / bounds.w, 0.0f, 1.0f );
                        const float uvRight = std::clamp( ( visible.x + visible.w - bounds.x ) / bounds.w, 0.0f, 1.0f );
                        const float uvTop = std::clamp( ( visible.y - bounds.y ) / bounds.h, 0.0f, 1.0f );
                        const float uvBottom = std::clamp( ( visible.y + visible.h - bounds.y ) / bounds.h, 0.0f, 1.0f );

                        screenW = (std::max)( 1, screenW );
                        screenH = (std::max)( 1, screenH );
                        const float halfH = std::tan( 22.5f * 3.14159265358979323846f / 180.0f );
                        const float halfW = halfH * static_cast<float>( screenW ) / static_cast<float>( screenH );
                        const float scaleX = ( halfW * 2.0f ) / static_cast<float>( screenW );
                        const float scaleY = ( halfH * 2.0f ) / static_cast<float>( screenH );
                        const auto textX = [&]( float x ) { return -halfW + x * scaleX; };

                        const auto textY = [&]( float y ) { return halfH - y * scaleY; };

                        const float left = textX( visible.x );
                        const float right = textX( visible.x + visible.w );
                        const float top = textY( visible.y );
                        const float bottom = textY( visible.y + visible.h );
                        const float vertices[] = {
                            left, bottom, uvLeft, uvBottom, right, bottom, uvRight, uvBottom, right, top, uvRight, uvTop,
                            left, bottom, uvLeft, uvBottom, right, top,    uvRight, uvTop,    left,  top, uvLeft,  uvTop,
                        };

                        const Math::Transformation::Matrix4 projection = Math::Transformation::Matrix4::Ortho( -halfW, halfW,
                                                                                                               -halfH, halfH,
                                                                                                               -1.0f, 1.0f );

                        const int mode = resource->depth ? 2 : ( resource->hdr ? 1 : 0 );
                        m_previewShader->Use();
                        m_previewShader->SetMat4( "uProjection", projection );
                        m_previewShader->SetInt( "uTexture", 0 );
                        m_previewShader->SetVec4( "uPreviewParams", static_cast<float>( mode ), 1.0f, 2.2f, 0.0f );
                        renderTextures.BindTexture( resource->textureHandle, 0 );
                        {
                            DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "RenderTargetPreview" );
                            renderGeometry.UploadAndDrawDynamicVB( m_previewVertexBuffer, vertices, PREVIEW_RASTER_STATE );
                        }
                        renderTextures.BindTexture( 0, 0 );
                    }
                }
            }
            else
            {
                immediateDraw.Rect( bounds.x, bounds.y, bounds.w, bounds.h, command.r, command.g, command.b, command.a );
                immediateDraw.Text( bounds.x + 12.0f, bounds.y + 12.0f, 12.0f, 0.68f, 0.72f, 0.78f,
                                    drawList.TextAt( command.textOffset ) );
            }

            break;
        }
        }
    }

    flushQueued();
    PROFILE_GPU_END( gpuTiming, "Frame/UI/Draw" );
}

void UiDrawSubmission::EnsurePreviewResources( Assets::AssetSystem& assets, Rendering::Dx12ResourceBuilder& renderResources,
                                               Rendering::Dx12GeometryOwner& renderGeometry )
{

    if ( !m_previewShader )
    {
        m_previewShader = assets.CreateShader( renderResources, "shader.ui_render_target_preview" );

        if ( !m_previewShader )
        {
            return;
        }

        m_previewShader->Use();
        m_previewShader->SetInt( "uTexture", 0 );
    }

    if ( m_previewVertexBuffer == 0 )
    {
        const int attributes[] = { 2, 2 };
        m_previewVertexBuffer = renderGeometry.CreateDynamicVB( attributes, 2, 6 );
    }
}

void UiDrawSubmission::ReleaseGpuResources( Rendering::Dx12GeometryOwner* renderGeometry )
{
    m_previewShader.reset();

    if ( m_previewVertexBuffer != 0 )
    {

        if ( renderGeometry )
        {
            renderGeometry->DestroyDynamicVB( m_previewVertexBuffer );
        }

        m_previewVertexBuffer = 0;
    }
}
} // namespace SkullbonezCore::Runtime
