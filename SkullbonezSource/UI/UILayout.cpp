/*
File: SkullbonezSource/UI/UILayout.cpp
Purpose:
  Implements UI Layout widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UILayout.cpp implements UI Layout widgets, layout, drawing, or UI state for
  the in-engine controls. As an implementation unit, keep edits anchored on UI
  request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UILayout.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UILayout.h"
#include "UIFontMetrics.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace UI
{
namespace Layout
{

int SceneComboVisibleCount( int optionCount )
{
    return std::clamp( optionCount, 0, UI_SCENE_COMBO_VISIBLE_OPTIONS );
}


int ClampSceneComboScroll( int scroll, int optionCount )
{
    const int maxScroll = (std::max)( 0, optionCount - SceneComboVisibleCount( optionCount ) );
    return std::clamp( scroll, 0, maxScroll );
}


int SceneComboScrollForSelection( int selectedIndex, int optionCount )
{
    const int visibleCount = SceneComboVisibleCount( optionCount );
    if ( selectedIndex < 0 || visibleCount <= 0 )
    {
        return 0;
    }
    return ClampSceneComboScroll( selectedIndex - visibleCount / 2, optionCount );
}


float SceneTabComboWidth( float contentW )
{
    const float maxComboW = (std::min)( contentW, 520.0f );
    const float buttonW = UI_SCENE_RESET_BUTTON_W + UI_SCENE_RESET_DEFAULTS_BUTTON_W + UI_SCENE_SAVE_DEFAULTS_BUTTON_W +
                          UI_SCENE_HEADER_BUTTON_GAP * 3.0f;
    const float withButtons = contentW - buttonW;
    return (std::max)( 180.0f, (std::min)( maxComboW, withButtons ) );
}


void SetPipelineStepButtonBounds( UIRect& previous, UIRect& next, float contentX, float contentW, float y )
{
    const float nextX = contentX + contentW - UI_PIPELINE_STEP_BUTTON_W;
    const float previousX = nextX - UI_PIPELINE_STEP_BUTTON_GAP - UI_PIPELINE_STEP_BUTTON_W;
    previous = { previousX, y, UI_PIPELINE_STEP_BUTTON_W, UI_PIPELINE_STEP_BUTTON_H };

    next = { nextX, y, UI_PIPELINE_STEP_BUTTON_W, UI_PIPELINE_STEP_BUTTON_H };
}


UIRect MinimizedRect( int screenW, int screenH, float requestedW )
{
    constexpr float h = 38.0f;
    constexpr float margin = 14.0f;
    const float maxW = (std::max)( 154.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const float w = std::clamp( requestedW, 154.0f, maxW );
    return { margin, (std::max)( margin, static_cast<float>( screenH ) - h - margin ), w, h };
}


float MinimizedWidthForTitle( const char* title, int screenW )
{
    constexpr float margin = 14.0f;
    constexpr float textSize = 12.5f;
    constexpr float chromeW = 76.0f;
    const float maxW = (std::max)( 154.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const float textW = UIFontMetrics::MeasureText( textSize, title ? title : "" );
    return std::clamp( textW + chromeW, 154.0f, maxW );
}


float GravityStrengthFromWorld( float gravity )
{
    return std::clamp( -gravity, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX );
}


float WorldGravityFromStrength( float strength )
{
    return -std::clamp( strength, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX );
}


int RemainingSceneObjectSlots( int modelCapacity, int otherCount )
{
    modelCapacity = (std::max)( UI_SOLVER_COUNT_MIN, modelCapacity );
    otherCount = std::clamp( otherCount, UI_SOLVER_COUNT_MIN, modelCapacity );
    return modelCapacity - otherCount;
}


UIRect FooterRendererComboBounds( float x, float bottomY )
{
    return { x + 32.0f, bottomY + 20.0f, 172.0f, 24.0f };
}


UIRect FooterWaterComboBounds( float x, float bottomY )
{
    return { x + 32.0f, bottomY + 46.0f, 172.0f, 24.0f };
}


UIRect FooterBlurBounds( float x, float bottomY )
{
    return { x + 218.0f, bottomY + 22.0f, 86.0f, 24.0f };
}


UIRect FooterVsyncBounds( float x, float bottomY )
{
    return { x + 306.0f, bottomY + 22.0f, 86.0f, 24.0f };
}


UIRect FooterHitboxBounds( float x, float bottomY )
{
    return { x + 394.0f, bottomY + 22.0f, 86.0f, 24.0f };
}


UIRect FooterTimelineBounds( float x, float bottomY )
{
    return { x + 218.0f, bottomY + 48.0f, 86.0f, 24.0f };
}


UIRect FooterPerfBounds( float x, float bottomY )
{
    return { x + 306.0f, bottomY + 48.0f, 86.0f, 24.0f };
}


uint32_t ReflectionDisabledMask()
{
    return 0u;
}


float SmoothStep( float t )
{
    t = std::clamp( t, 0.0f, 1.0f );
    return t * t * ( 3.0f - 2.0f * t );
}


UIRect LerpRect( const UIRect& from, const UIRect& to, float t )
{
    const float e = SmoothStep( t );
    return { from.x + ( to.x - from.x ) * e,
             from.y + ( to.y - from.y ) * e,
             from.w + ( to.w - from.w ) * e,
             from.h + ( to.h - from.h ) * e };
}

} // namespace Layout
} // namespace UI
} // namespace SkullbonezCore
