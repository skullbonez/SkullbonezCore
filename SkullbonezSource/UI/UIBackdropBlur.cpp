#include "UIBackdropBlur.h"

#include "../SkullbonezIRenderBackend.h"
#include "../SkullbonezMatrix4.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::UI;

namespace
{
constexpr int BLUR_PAD_PIXELS = 10;

float ClampUv( float value )
{
    return std::clamp( value, 0.0f, 1.0f );
}
} // namespace


UIBackdropBlur::~UIBackdropBlur()
{
    ResetResources();
}


void UIBackdropBlur::Invalidate()
{
    m_invalidated = true;
}


void UIBackdropBlur::ResetResources()
{
    if ( IsGfxReady() )
    {
        if ( m_texture != 0 )
        {
            Gfx().DeleteTexture( m_texture );
        }
        if ( m_dynamicVB != 0 )
        {
            Gfx().DestroyDynamicVB( m_dynamicVB );
        }
    }

    m_texture = 0;
    m_dynamicVB = 0;
    m_shader.reset();
    m_textureW = 0;
    m_textureH = 0;
    m_lastX = -1;
    m_lastY = -1;
    m_lastW = 0;
    m_lastH = 0;
    m_invalidated = true;
}


void UIBackdropBlur::EnsureDrawResources()
{
    if ( !m_shader )
    {
        m_shader = Gfx().CreateShader( "shaders/UIBackdropBlur" );
        m_shader->Use();
        m_shader->SetInt( "uTexture", 0 );
    }

    if ( m_dynamicVB == 0 )
    {
        const int attribs[] = { 2, 2 };
        m_dynamicVB = Gfx().CreateDynamicVB( attribs, 2, 6 );
    }
}


void UIBackdropBlur::RefreshSourceTexture( const UIRect& bounds, int screenW, int screenH )
{
    int captureW = 0;
    int captureH = 0;
    std::vector<uint8_t> capture = Gfx().CaptureBackbuffer( captureW, captureH );
    if ( capture.empty() || captureW <= 0 || captureH <= 0 )
    {
        return;
    }

    const int cropX = std::clamp( static_cast<int>( std::floor( bounds.x ) ) - BLUR_PAD_PIXELS, 0, captureW - 1 );
    const int cropY = std::clamp( static_cast<int>( std::floor( bounds.y ) ) - BLUR_PAD_PIXELS, 0, captureH - 1 );
    const int cropRight = std::clamp( static_cast<int>( std::ceil( bounds.x + bounds.w ) ) + BLUR_PAD_PIXELS, cropX + 1, captureW );
    const int cropBottom = std::clamp( static_cast<int>( std::ceil( bounds.y + bounds.h ) ) + BLUR_PAD_PIXELS, cropY + 1, captureH );
    const int cropW = cropRight - cropX;
    const int cropH = cropBottom - cropY;

    const int scaleW = (std::max)( 1, ( cropW + 259 ) / 260 );
    const int scaleH = (std::max)( 1, ( cropH + 189 ) / 190 );
    const int scale = std::clamp( (std::max)( 2, (std::max)( scaleW, scaleH ) ), 2, 5 );
    const int outW = (std::max)( 1, cropW / scale );
    const int outH = (std::max)( 1, cropH / scale );

    m_sourcePixels.assign( static_cast<size_t>( outW ) * outH * 3, 0 );
    const int srcStride = ( captureW * 3 + 3 ) & ~3;

    for ( int oy = 0; oy < outH; ++oy )
    {
        for ( int ox = 0; ox < outW; ++ox )
        {
            int sumR = 0;
            int sumG = 0;
            int sumB = 0;
            int samples = 0;
            for ( int sy = 0; sy < scale; ++sy )
            {
                const int sourceTopY = std::clamp( cropY + cropH - 1 - ( oy * scale + sy ), cropY, cropBottom - 1 );
                const int captureRow = captureH - 1 - sourceTopY;
                for ( int sx = 0; sx < scale; ++sx )
                {
                    const int sourceX = std::clamp( cropX + ox * scale + sx, cropX, cropRight - 1 );
                    const size_t srcIndex = static_cast<size_t>( captureRow ) * srcStride + static_cast<size_t>( sourceX ) * 3;
                    sumB += capture[srcIndex + 0];
                    sumG += capture[srcIndex + 1];
                    sumR += capture[srcIndex + 2];
                    ++samples;
                }
            }

            const float invSamples = samples > 0 ? 1.0f / static_cast<float>( samples ) : 1.0f;
            const size_t dstIndex = ( static_cast<size_t>( oy ) * outW + ox ) * 3;
            m_sourcePixels[dstIndex + 0] = static_cast<uint8_t>( static_cast<float>( sumR ) * invSamples );
            m_sourcePixels[dstIndex + 1] = static_cast<uint8_t>( static_cast<float>( sumG ) * invSamples );
            m_sourcePixels[dstIndex + 2] = static_cast<uint8_t>( static_cast<float>( sumB ) * invSamples );
        }
    }

    if ( m_texture != 0 )
    {
        Gfx().DeleteTexture( m_texture );
        m_texture = 0;
    }

    m_texture = Gfx().CreateTexture2D( m_sourcePixels.data(), outW, outH, 3, false, true );
    m_textureW = outW;
    m_textureH = outH;
    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    m_lastX = cropX;
    m_lastY = cropY;
    m_lastW = cropW;
    m_lastH = cropH;
    m_invalidated = false;
}


void UIBackdropBlur::Draw( const UIDrawContext& draw, const UIRect& bounds, int screenW, int screenH, int currentFrame, double now, bool enabled )
{
    if ( !enabled || bounds.w <= 1.0f || bounds.h <= 1.0f || !IsGfxReady() )
    {
        return;
    }

    const int requestedX = std::clamp( static_cast<int>( std::floor( bounds.x ) ) - BLUR_PAD_PIXELS, 0, (std::max)( 0, screenW - 1 ) );
    const int requestedY = std::clamp( static_cast<int>( std::floor( bounds.y ) ) - BLUR_PAD_PIXELS, 0, (std::max)( 0, screenH - 1 ) );
    const int requestedRight = std::clamp( static_cast<int>( std::ceil( bounds.x + bounds.w ) ) + BLUR_PAD_PIXELS, requestedX + 1, screenW );
    const int requestedBottom = std::clamp( static_cast<int>( std::ceil( bounds.y + bounds.h ) ) + BLUR_PAD_PIXELS, requestedY + 1, screenH );
    const int requestedW = requestedRight - requestedX;
    const int requestedH = requestedBottom - requestedY;
    const bool geometryChanged = requestedX != m_lastX || requestedY != m_lastY || requestedW != m_lastW || requestedH != m_lastH ||
                                 screenW != m_lastScreenW || screenH != m_lastScreenH;
    (void)currentFrame;
    (void)now;

    EnsureDrawResources();

    const bool needsRefresh = m_texture == 0 || m_invalidated || geometryChanged;
    if ( needsRefresh )
    {
        RefreshSourceTexture( bounds, screenW, screenH );
    }

    if ( m_texture == 0 || m_dynamicVB == 0 || !m_shader || m_invalidated || m_lastW <= 0 || m_lastH <= 0 )
    {
        return;
    }

    const float drawX0 = std::clamp( bounds.x, 0.0f, static_cast<float>( screenW ) );
    const float drawY0 = std::clamp( bounds.y, 0.0f, static_cast<float>( screenH ) );
    const float drawX1 = std::clamp( bounds.x + bounds.w, drawX0, static_cast<float>( screenW ) );
    const float drawY1 = std::clamp( bounds.y + bounds.h, drawY0, static_cast<float>( screenH ) );
    if ( drawX1 <= drawX0 || drawY1 <= drawY0 )
    {
        return;
    }

    const float invCropW = 1.0f / static_cast<float>( m_lastW );
    const float invCropH = 1.0f / static_cast<float>( m_lastH );
    const float uvLeft = ClampUv( ( drawX0 - static_cast<float>( m_lastX ) ) * invCropW );
    const float uvRight = ClampUv( ( drawX1 - static_cast<float>( m_lastX ) ) * invCropW );
    const float uvTop = ClampUv( 1.0f - ( drawY0 - static_cast<float>( m_lastY ) ) * invCropH );
    const float uvBottom = ClampUv( 1.0f - ( drawY1 - static_cast<float>( m_lastY ) ) * invCropH );

    const float left = draw.TextX( drawX0 );
    const float right = draw.TextX( drawX1 );
    const float top = draw.TextY( drawY0 );
    const float bottom = draw.TextY( drawY1 );
    const float verts[] = {
        left,  bottom, uvLeft,  uvBottom,
        right, bottom, uvRight, uvBottom,
        right, top,    uvRight, uvTop,
        left,  bottom, uvLeft,  uvBottom,
        right, top,    uvRight, uvTop,
        left,  top,    uvLeft,  uvTop,
    };

    const Matrix4 proj = Matrix4::Ortho( -draw.HalfW(), draw.HalfW(), -draw.HalfH(), draw.HalfH(), -1.0f, 1.0f );
    const bool depthWasEnabled = Gfx().IsDepthTestEnabled();
    const bool blendWasEnabled = Gfx().IsBlendEnabled();

    Gfx().SetDepthTest( false );
    Gfx().SetBlend( true );
    Gfx().SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );
    m_shader->Use();
    m_shader->SetMat4( "uProjection", proj );
    m_shader->SetInt( "uTexture", 0 );
    m_shader->SetVec4( "uTexelSize", 1.0f / static_cast<float>( m_textureW ), 1.0f / static_cast<float>( m_textureH ), 0.0f, 0.0f );
    Gfx().BindTexture( m_texture, 0 );
    Gfx().UploadAndDrawDynamicVB( m_dynamicVB, verts, 6 );
    Gfx().SetDepthTest( depthWasEnabled );
    Gfx().SetBlend( blendWasEnabled );
}
