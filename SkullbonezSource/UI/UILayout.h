/*
File: SkullbonezSource/UI/UILayout.h
Purpose:
  Declares deterministic UI geometry, clipping, interpolation, and value-
  conversion helpers.

Summary:
  Centralizes deterministic geometry and value
  conversions shared by composition and hit testing.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UILayout.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{
namespace Layout
{

constexpr float CONTENT_TOGGLE_ROW_H = 30.0f;
constexpr float UI_TIME_SCALE_MIN = 0.10f;
constexpr float UI_TIME_SCALE_MAX = 10.00f;
constexpr float UI_TIME_SCALE_STEP = 0.05f;
constexpr int UI_MODEL_COUNT_MIN = 0;
constexpr float UI_PHYSICS_ALPHA_MIN = 0.05f;
constexpr float UI_PHYSICS_ALPHA_MAX = 1.00f;
constexpr float UI_PHYSICS_ALPHA_STEP = 0.01f;
constexpr float UI_CONTACT_LINGER_MIN = 0.00f;
constexpr float UI_CONTACT_LINGER_MAX = 5.00f;
constexpr float UI_CONTACT_LINGER_STEP = 0.05f;
constexpr float UI_RAY_IMPULSE_MIN = 0.0f;
constexpr float UI_RAY_IMPULSE_MAX = 6000.0f;
constexpr float UI_RAY_IMPULSE_STEP = 100.0f;
constexpr float UI_LAUNCHER_PROJECTILE_SPEED_MIN = 20.0f;
constexpr float UI_LAUNCHER_PROJECTILE_SPEED_MAX = 360.0f;
constexpr float UI_LAUNCHER_PROJECTILE_SPEED_STEP = 5.0f;
constexpr float UI_FRICTION_COEFF_MIN = 0.0f;
constexpr float UI_FRICTION_COEFF_MAX = 3.0f;
constexpr float UI_FRICTION_COEFF_STEP = 0.05f;
constexpr float UI_ROLLING_FRICTION_COEFF_MIN = 0.0f;
constexpr float UI_ROLLING_FRICTION_COEFF_MAX = 0.20f;
constexpr float UI_ROLLING_FRICTION_COEFF_STEP = 0.005f;
constexpr int UI_SEED_MIN = 1;
constexpr int UI_SEED_MAX = 999999;
constexpr int UI_SOLVER_COUNT_MIN = 0;
constexpr float UI_WORLD_GRAVITY_MIN = 0.0f;
constexpr float UI_WORLD_GRAVITY_MAX = 100.0f;
constexpr float UI_WORLD_GRAVITY_STEP = 0.50f;
constexpr float UI_WORLD_FLUID_HEIGHT_MIN = -100.0f;
constexpr float UI_WORLD_FLUID_HEIGHT_MAX = 200.0f;
constexpr float UI_WORLD_FLUID_HEIGHT_STEP = 1.0f;
constexpr float UI_WORLD_FLUID_DENSITY_MIN = 0.0f;
constexpr float UI_WORLD_FLUID_DENSITY_MAX = 5.0f;
constexpr float UI_WORLD_FLUID_DENSITY_STEP = 0.05f;
constexpr float UI_TORNADO_RADIUS_MIN = 40.0f;
constexpr float UI_TORNADO_RADIUS_MAX = 360.0f;
constexpr float UI_TORNADO_RADIUS_STEP = 5.0f;
constexpr float UI_TORNADO_HEIGHT_MIN = 40.0f;
constexpr float UI_TORNADO_HEIGHT_MAX = 300.0f;
constexpr float UI_TORNADO_HEIGHT_STEP = 5.0f;
constexpr float UI_TORNADO_INWARD_MIN = 0.0f;
constexpr float UI_TORNADO_INWARD_MAX = 300.0f;
constexpr float UI_TORNADO_INWARD_STEP = 5.0f;
constexpr float UI_TORNADO_SWIRL_MIN = 0.0f;
constexpr float UI_TORNADO_SWIRL_MAX = 400.0f;
constexpr float UI_TORNADO_SWIRL_STEP = 5.0f;
constexpr float UI_TORNADO_LIFT_MIN = 0.0f;
constexpr float UI_TORNADO_LIFT_MAX = 250.0f;
constexpr float UI_TORNADO_LIFT_STEP = 5.0f;

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
float GravityStrengthFromWorld( float gravity );
float WorldGravityFromStrength( float strength );
int RemainingSceneObjectSlots( int modelCapacity, int otherCount );

UIRect FooterRendererComboBounds( float x, float bottomY );
UIRect FooterWaterComboBounds( float x, float bottomY );
UIRect FooterBlurBounds( float x, float bottomY );
UIRect FooterVsyncBounds( float x, float bottomY );
UIRect FooterHitboxBounds( float x, float bottomY );
UIRect FooterTimelineBounds( float x, float bottomY );
UIRect FooterPerfBounds( float x, float bottomY );
uint32_t ReflectionDisabledMask();

float SmoothStep( float t );
UIRect LerpRect( const UIRect& from, const UIRect& to, float t );

} // namespace Layout
} // namespace UI
} // namespace SkullbonezCore
