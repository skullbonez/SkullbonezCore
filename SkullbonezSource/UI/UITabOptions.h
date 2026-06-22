/*
File: SkullbonezSource/UI/UITabOptions.h
Purpose:
  Implements UI TabOptions widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  The UI is immediate-mode-style: each frame reads engine state, computes hit
  boxes, emits draw commands, and returns requests for the run loop to apply.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UITabOptions.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UICheckBox.h"
#include "UICommands.h"
#include "UISlider.h"

namespace SkullbonezCore
{
namespace UI
{

class UIDrawContext;
struct InGameUIFrameData;

namespace OptionsTab
{

constexpr int SLIDER_TIME_SCALE = 1;
constexpr int SLIDER_MODEL_COUNT = 2;

struct UIOptionsTabState
{
    UICheckBox toggles[6];
    UISlider timeScaleSlider;
    UISlider modelCountSlider;
    float previewTimeScale = -1.0f;
    int previewModelCount = -1;
};

int ContentHeight();
void ResetPreviewState( UIOptionsTabState& state );

bool HandleContentClick( UIOptionsTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW,
                         int modelCapacity );

bool UpdateActiveSlider( UIOptionsTabState& state,
                         int activeSlider,
                         int mouseX,
                         int modelCapacity,
                         InGameUIInputResult& result );
bool CommitActiveSlider( UIOptionsTabState& state, int activeSlider, InGameUIInputResult& result );

void Draw( UIOptionsTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int activeSlider );

} // namespace OptionsTab
} // namespace UI
} // namespace SkullbonezCore
