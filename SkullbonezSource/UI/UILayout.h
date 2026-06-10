#pragma once

#include "UIDraw.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{
namespace Layout
{

// The diagnostics UI uses fixed pixel constants. These values define hit boxes
// and draw boxes together, so layout helpers keep input and rendering aligned.
constexpr int RENDERER_GL = 0;
constexpr int RENDERER_DX11 = 1;
constexpr int RENDERER_DX12 = 2;

constexpr float CONTENT_TOGGLE_ROW_H = 30.0f;
constexpr float UI_TIME_SCALE_MIN = 0.10f;
constexpr float UI_TIME_SCALE_MAX = 10.00f;
constexpr float UI_TIME_SCALE_STEP = 0.05f;
constexpr int UI_MODEL_COUNT_MIN = 0;
constexpr int UI_MODEL_COUNT_MAX = 1000;
constexpr int UI_GAME_MODEL_TOTAL_MAX = 1000;
constexpr float UI_PHYSICS_ALPHA_MIN = 0.05f;
constexpr float UI_PHYSICS_ALPHA_MAX = 1.00f;
constexpr float UI_PHYSICS_ALPHA_STEP = 0.01f;
constexpr float UI_CONTACT_LINGER_MIN = 0.00f;
constexpr float UI_CONTACT_LINGER_MAX = 5.00f;
constexpr float UI_CONTACT_LINGER_STEP = 0.05f;
constexpr int UI_SEED_MIN = 1;
constexpr int UI_SEED_MAX = 999999;
constexpr int UI_SOLVER_COUNT_MIN = 0;
constexpr int UI_SOLVER_COUNT_MAX = UI_GAME_MODEL_TOTAL_MAX;
constexpr float UI_WORLD_GRAVITY_MIN = 0.0f;
constexpr float UI_WORLD_GRAVITY_MAX = 100.0f;
constexpr float UI_WORLD_GRAVITY_STEP = 0.50f;
constexpr float UI_WORLD_FLUID_HEIGHT_MIN = -100.0f;
constexpr float UI_WORLD_FLUID_HEIGHT_MAX = 200.0f;
constexpr float UI_WORLD_FLUID_HEIGHT_STEP = 1.0f;
constexpr float UI_WORLD_FLUID_DENSITY_MIN = 0.0f;
constexpr float UI_WORLD_FLUID_DENSITY_MAX = 5.0f;
constexpr float UI_WORLD_FLUID_DENSITY_STEP = 0.05f;

constexpr int UI_SCENE_COMBO_VISIBLE_OPTIONS = 12;
constexpr float UI_SCENE_HEADER_BUTTON_GAP = 8.0f;
constexpr float UI_SCENE_RESET_BUTTON_W = 72.0f;
constexpr float UI_SCENE_RESET_DEFAULTS_BUTTON_W = 132.0f;
constexpr float UI_SCENE_SAVE_DEFAULTS_BUTTON_W = 132.0f;

constexpr float UI_PIPELINE_STEP_BUTTON_W = 26.0f;
constexpr float UI_PIPELINE_STEP_BUTTON_H = 22.0f;
constexpr float UI_PIPELINE_STEP_BUTTON_GAP = 6.0f;

constexpr double UI_WINDOW_ANIMATION_SECONDS = 0.18;

int SceneComboVisibleCount( int optionCount );
int ClampSceneComboScroll( int scroll, int optionCount );
int SceneComboScrollForSelection( int selectedIndex, int optionCount );
float SceneTabComboWidth( float contentW );

void SetPipelineStepButtonBounds( UIRect& previous, UIRect& next, float contentX, float contentW, float y );

UIRect MinimizedRect( int screenW, int screenH, float requestedW );
float MinimizedWidthForTitle( const char* title, int screenW );
float GravityStrengthFromWorld( float gravity );
float WorldGravityFromStrength( float strength );
int RemainingGameModelSlots( int otherCount );

UIRect FooterRendererComboBounds( float x, float bottomY );
UIRect FooterWaterComboBounds( float x, float bottomY );
UIRect FooterBlurBounds( float x, float bottomY );
UIRect FooterVsyncBounds( float x, float bottomY );
UIRect FooterTimelineBounds( float x, float bottomY );
UIRect FooterPerfBounds( float x, float bottomY );
uint32_t ReflectionDisabledMask( int rendererIndex );

float SmoothStep( float t );
UIRect LerpRect( const UIRect& from, const UIRect& to, float t );

} // namespace Layout
} // namespace UI
} // namespace SkullbonezCore
