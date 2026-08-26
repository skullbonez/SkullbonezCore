/*
File: SkullbonezSource/Runtime/UI/GameUI/GameUILayout.h
Purpose:
  Declares product-specific GameUI geometry and small display policy.

Summary:
  The component foundation supplies generic rectangles and interpolation. This
  Runtime-owned layer arranges the scene combo, Physics pipeline buttons, and
  footer controls, and describes when reflection choices are disabled, without
  teaching those product rules to SKULLBONEZ_UI.

Invariants:
  - Drawing and hit testing call the same bounds helpers.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/GameUILayout.cpp
  - SkullbonezSource/UI/UILayout.h
*/
#pragma once

#include "../../../UI/UIDraw.h"

#include <cstdint>

namespace SkullbonezCore::UI::GameLayout
{
inline constexpr float CONTENT_TOGGLE_ROW_H = 30.0f;

inline constexpr int UI_SCENE_COMBO_VISIBLE_OPTIONS = 12;
inline constexpr float UI_SCENE_HEADER_BUTTON_GAP = 8.0f;
inline constexpr float UI_SCENE_RESET_BUTTON_W = 72.0f;
inline constexpr float UI_SCENE_RESET_DEFAULTS_BUTTON_W = 132.0f;
inline constexpr float UI_SCENE_SAVE_DEFAULTS_BUTTON_W = 132.0f;

inline constexpr float UI_PIPELINE_STEP_BUTTON_W = 26.0f;
inline constexpr float UI_PIPELINE_STEP_BUTTON_H = 22.0f;
inline constexpr float UI_PIPELINE_STEP_BUTTON_GAP = 6.0f;

int SceneComboVisibleCount( int optionCount );
int ClampSceneComboScroll( int scroll, int optionCount );
int SceneComboScrollForSelection( int selectedIndex, int optionCount );

struct SceneHeaderWidths
{
    float combo = 0.0f;
    float reset = 0.0f;
    float resetDefaults = 0.0f;
    float saveDefaults = 0.0f;
    float gap = 0.0f;
};

SceneHeaderWidths ResolveSceneHeaderWidths( float contentW );
float SceneTabComboWidth( float contentW );

void SetPipelineStepButtonBounds( UIRect& previous, UIRect& next, float contentX, float contentW, float y );

UIRect FooterRendererComboBounds( float x, float bottomY );
UIRect FooterWaterComboBounds( float x, float bottomY );
UIRect FooterBlurBounds( float x, float bottomY );
UIRect FooterVsyncBounds( float x, float bottomY );
UIRect FooterHitboxBounds( float x, float bottomY );
UIRect FooterTimelineBounds( float x, float bottomY );
UIRect FooterPerfBounds( float x, float bottomY );
uint32_t ReflectionDisabledMask();
} // namespace SkullbonezCore::UI::GameLayout
