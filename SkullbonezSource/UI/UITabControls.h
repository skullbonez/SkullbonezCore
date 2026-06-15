/*
File: SkullbonezSource/UI/UITabControls.h
Purpose:
  Implements UI TabControls widgets, layout, drawing, or UI state for the in-engine controls.

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
  - SkullbonezSource/UI/UITabControls.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UICommands.h"
#include "UISlider.h"

namespace SkullbonezCore
{
namespace UI
{

class UIDrawContext;
struct InGameUIFrameData;

namespace ControlsTab
{

constexpr int SLIDER_SEED = 6;
constexpr int SLIDER_SOLVER_BALLS = 7;
constexpr int SLIDER_SOLVER_BOXES = 8;
constexpr int SLIDER_WORLD_FLUID_HEIGHT = 12;
constexpr int SLIDER_WORLD_FLUID_DENSITY = 13;

struct UIControlsTabState
{
    UISlider seedSlider;
    UISlider solverBallSlider;
    UISlider solverBoxSlider;
    UISlider worldFluidHeightSlider;
    UISlider worldFluidDensitySlider;
    int previewSolverBallCount = -1;
    int previewSolverBoxCount = -1;
};

int ContentHeight();
void ResetPreviewState( UIControlsTabState& state );

bool HandleContentClick( UIControlsTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW,
                         int lastSolverBallCount,
                         int lastSolverBoxCount );

bool UpdateActiveSlider( UIControlsTabState& state,
                         int activeSlider,
                         int mouseX,
                         int lastSolverBallCount,
                         int lastSolverBoxCount,
                         InGameUIInputResult& result );

bool CommitActiveSlider( UIControlsTabState& state, int activeSlider, InGameUIInputResult& result );

void Draw( UIControlsTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY );

} // namespace ControlsTab
} // namespace UI
} // namespace SkullbonezCore
