/*
File: SkullbonezSource/UI/UITabSky.h
Purpose:
  Owns the Sky tab widgets, layout, and input handling for in-engine sky tuning.

Summary:
  The Sky tab is a focused view over the cinematic render config. It emits the
  same cinematic UI commands as the broader Cine tab, but presents only the
  sky, cloud, ray, and palette controls.

Glossary:
  Active slider: Global UI drag id captured while the user drags a slider.

Invariants:
  - Slider and feature counts must match the specs in UITabSky.cpp.
  - UISkyTabState owns widget state only; render config changes are command
    intents handled by runtime code.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UITabSky.cpp
  - SkullbonezSource/Runtime/UI/GameUI/UITabCinematic.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../../UI/UIButton.h"
#include "../../../UI/UICheckBox.h"
#include "../../Interaction/OperatorUiCommands.h"
#include "../../../UI/UIDraw.h"
#include "../../../UI/UISlider.h"

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
    UIButton saveButton;
    UICheckBox featureToggles[UI_SKY_FEATURE_COUNT];
    UISlider sliders[UI_SKY_SLIDER_COUNT];
};

int ContentHeight();
bool HandleContentClick( UISkyTabState& state, InGameUIInputResult& result, int& activeSlider, int mouseX, int mouseY,
                         float contentX, float scrolledY, float contentW );
bool UpdateActiveSlider( UISkyTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );
bool CommitActiveSlider( UISkyTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );

void DrawHitboxes( const UISkyTabState& state, const UIDrawContext& draw, float contentR, float contentG, float contentB );
void Draw( UISkyTabState& state, const UIDrawContext& draw, const InGameUIFrameData& data, float contentX, float contentY,
           float contentW, float contentH, float scrolledY, int mouseX, int mouseY );

} // namespace SkyTab
} // namespace UI
} // namespace SkullbonezCore
