/*
File: SkullbonezSource/UI/UITabSky.h
Purpose:
  Owns the Sky tab widgets, layout, and input handling for in-engine sky tuning.

Mental model:
  The Sky tab is a focused view over the cinematic render config. It emits the
  same cinematic UI commands as the broader Cine tab, but presents only the
  sky, cloud, ray, and palette controls.

Related:
  - SkullbonezSource/UI/UITabSky.cpp
  - SkullbonezSource/UI/UITabCinematic.h
*/
#pragma once

#include "UICheckBox.h"
#include "UICommands.h"
#include "UIDraw.h"
#include "UISlider.h"

namespace SkullbonezCore
{
namespace UI
{

class UIDrawContext;
struct InGameUIFrameData;
struct InGameUIInputResult;

namespace SkyTab
{

constexpr int UI_SKY_SLIDER_COUNT = 26;
constexpr int UI_SKY_FEATURE_COUNT = 4;

struct UISkyTabState
{
    UICheckBox featureToggles[UI_SKY_FEATURE_COUNT];
    UISlider sliders[UI_SKY_SLIDER_COUNT];
};

int ContentHeight();
bool HandleContentClick( UISkyTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float scrolledY,
                         float contentW );
bool UpdateActiveSlider( UISkyTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );
bool CommitActiveSlider( UISkyTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );

void DrawHitboxes( const UISkyTabState& state, const UIDrawContext& draw, float contentR, float contentG, float contentB );
void Draw( UISkyTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY );

} // namespace SkyTab
} // namespace UI
} // namespace SkullbonezCore
