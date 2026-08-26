/*
File: SkullbonezSource/Runtime/UI/GameUI/GameUILayout.cpp
Purpose:
  Implements product-specific GameUI geometry and small display policy.

Summary:
  Scene selectors, Physics pipeline buttons, and footer controls use one
  Runtime-owned geometry vocabulary; reflection availability remains a small
  product policy while the reusable UI library stays domain-neutral.

Invariants:
  - Bounds returned here are consumed unchanged by drawing and hit testing.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/GameUILayout.h
*/
#include "GameUILayout.h"

#include <algorithm>

namespace SkullbonezCore::UI::GameLayout
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

SceneHeaderWidths ResolveSceneHeaderWidths( float contentW )
{
    SceneHeaderWidths widths;
    contentW = (std::max)( 1.0f, contentW );
    constexpr float naturalButtons = UI_SCENE_RESET_BUTTON_W + UI_SCENE_RESET_DEFAULTS_BUTTON_W +
                                     UI_SCENE_SAVE_DEFAULTS_BUTTON_W;

    widths.gap = (std::min)( UI_SCENE_HEADER_BUTTON_GAP, contentW / 16.0f );
    const float availableWithoutGaps = (std::max)( 1.0f, contentW - widths.gap * 3.0f );

    if ( availableWithoutGaps >= naturalButtons + 1.0f )
    {
        widths.reset = UI_SCENE_RESET_BUTTON_W;
        widths.resetDefaults = UI_SCENE_RESET_DEFAULTS_BUTTON_W;
        widths.saveDefaults = UI_SCENE_SAVE_DEFAULTS_BUTTON_W;
        widths.combo = (std::min)( 520.0f, availableWithoutGaps - naturalButtons );
        return widths;
    }

    // Invariant: every control remains inside the one authored header row. At
    // compact widths the combo receives one quarter and buttons scale together.
    widths.combo = availableWithoutGaps * 0.25f;
    const float scaledButtonSpace = availableWithoutGaps - widths.combo;
    const float scale = scaledButtonSpace / naturalButtons;
    widths.reset = UI_SCENE_RESET_BUTTON_W * scale;
    widths.resetDefaults = UI_SCENE_RESET_DEFAULTS_BUTTON_W * scale;
    widths.saveDefaults = scaledButtonSpace - widths.reset - widths.resetDefaults;
    return widths;
}

float SceneTabComboWidth( float contentW )
{
    return ResolveSceneHeaderWidths( contentW ).combo;
}

void SetPipelineStepButtonBounds( UIRect& previous, UIRect& next, float contentX, float contentW, float y )
{
    const float nextX = contentX + contentW - UI_PIPELINE_STEP_BUTTON_W;
    const float previousX = nextX - UI_PIPELINE_STEP_BUTTON_GAP - UI_PIPELINE_STEP_BUTTON_W;
    previous = { previousX, y, UI_PIPELINE_STEP_BUTTON_W, UI_PIPELINE_STEP_BUTTON_H };
    next = { nextX, y, UI_PIPELINE_STEP_BUTTON_W, UI_PIPELINE_STEP_BUTTON_H };
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
} // namespace SkullbonezCore::UI::GameLayout
