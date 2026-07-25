/*
File: SkullbonezSource/UI/UITabCinematic.h
Purpose:
  Owns the Cinematic tab widgets, layout, and input handling for the in-engine controls.

Summary:
  UITabCinematic.h owns the Cinematic tab widgets, layout, and input handling
  for the in-engine controls. As a public header, keep edits anchored on UI
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
  - SkullbonezSource/UI/UITabCinematic.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UICheckBox.h"
#include "UIComboBox.h"
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

namespace CinematicTab
{

struct UICinematicTabState
{
    UIComboBox modeCombo;
    UICheckBox featureToggles[static_cast<int>( UICinematicFeature::Count )];
    UISlider sliders[static_cast<int>( UICinematicParam::Count )];
};

int ContentHeight();
bool IsComboOpen( const UICinematicTabState& state );
void CloseCombo( UICinematicTabState& state );
bool HandleOpenComboClick(
    UICinematicTabState& state,
    InGameUIInputResult& result,
    const char* const* sceneOptions,
    int sceneOptionCount,
    int mouseX,
    int mouseY
);
bool HandleContentClick(
    UICinematicTabState& state,
    InGameUIInputResult& result,
    int& activeSlider,
    int mouseX,
    int mouseY,
    float contentX,
    float scrolledY,
    float contentW
);
bool UpdateActiveSlider( UICinematicTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );
bool CommitActiveSlider( UICinematicTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );

void DrawHitboxes(
    const UICinematicTabState& state,
    const UIDrawContext& draw,
    const InGameUIFrameData& data,
    float contentR,
    float contentG,
    float contentB
);
void Draw(
    UICinematicTabState& state,
    const UIDrawContext& draw,
    const InGameUIFrameData& data,
    float contentX,
    float contentY,
    float contentW,
    float contentH,
    float scrolledY,
    int mouseX,
    int mouseY
);

} // namespace CinematicTab
} // namespace UI
} // namespace SkullbonezCore
