/*
File: SkullbonezSource/UI/UIBackdropBlur.cpp
Purpose:
  Draws the optional non-readback backdrop panel behind the in-engine controls.

Summary:
  The backdrop is ordinary UI geometry. Earlier versions captured the active
  back buffer and uploaded a downsampled texture; the Carmack backend-capability
  pass moves capture/readback back to screenshots and validation paths only.

Glossary:
  Capture backend: Narrow renderer capability that can read pixels from the
  active back buffer.

Invariants:
  - UIBackdropBlur must not call renderer-global capture/accessor helpers or
    create GPU resources.
  - Invalidation still tracks why the panel should refresh its cached bounds,
    but the refresh is now just geometry/state bookkeeping.

Related:
  - SkullbonezSource/UI/UIBackdropBlur.h
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "UIBackdropBlur.h"

#include "UIStyle.h"

#include <algorithm>

using namespace SkullbonezCore::UI;

namespace
{
constexpr float BACKDROP_PAD_PIXELS = 10.0f;
}


UIBackdropBlur::~UIBackdropBlur()
{
    ResetResources();
}


void UIBackdropBlur::Invalidate( UIBackdropBlurInvalidationReason reason )
{
    m_invalidated = true;
    m_lastInvalidationReason = reason;
}


void UIBackdropBlur::ResetResources()
{
    m_lastScreenW = 0;
    m_lastScreenH = 0;
    m_lastX = -1;
    m_lastY = -1;
    m_lastW = 0;
    m_lastH = 0;
    m_invalidated = true;
    m_lastInvalidationReason = UIBackdropBlurInvalidationReason::ResourceReset;
}


void UIBackdropBlur::Draw( const UIDrawContext& draw, const UIRect& bounds, int screenW, int screenH, int currentFrame,
                           double now, bool enabled )
{
    (void)currentFrame;
    (void)now;

    if ( !enabled || bounds.w <= 1.0f || bounds.h <= 1.0f )
    {
        return;
    }

    const int requestedX = std::clamp( static_cast<int>( bounds.x - BACKDROP_PAD_PIXELS ), 0, (std::max)( 0, screenW - 1 ) );

    const int requestedY = std::clamp( static_cast<int>( bounds.y - BACKDROP_PAD_PIXELS ), 0, (std::max)( 0, screenH - 1 ) );

    const int requestedRight = std::clamp( static_cast<int>( bounds.x + bounds.w + BACKDROP_PAD_PIXELS ), requestedX + 1,
                                           screenW );

    const int requestedBottom = std::clamp( static_cast<int>( bounds.y + bounds.h + BACKDROP_PAD_PIXELS ), requestedY + 1,
                                            screenH );

    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    m_lastX = requestedX;
    m_lastY = requestedY;
    m_lastW = requestedRight - requestedX;
    m_lastH = requestedBottom - requestedY;
    m_invalidated = false;

    const Style::UIPalette& palette = Style::Palette();
    Style::UIColor fill = palette.window;
    fill.a = (std::min)( fill.a, 0.34f );
    Style::UIColor border = palette.innerBorder;
    border.a = (std::min)( border.a, 0.24f );

    // Why: this deliberately remains a UI draw command instead of a captured
    // texture so readback latency cannot enter the ordinary overlay path.
    const UIRect paddedBounds = { static_cast<float>( m_lastX ), static_cast<float>( m_lastY ),
                                  static_cast<float>( m_lastW ), static_cast<float>( m_lastH ) };

    draw.RoundedPanel( paddedBounds, Style::Radii().window, fill, border );
}
