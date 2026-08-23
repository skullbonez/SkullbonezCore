/*
File: SkullbonezSource/UI/UITabControls.h
Purpose:
  Declares the seed, scene-population, and fluid control surface and its
  typed commands.

Summary:
  Previews and commits scene-population,
  seed, and fluid sliders as typed UI commands.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UITabControls.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Interaction/OperatorUiCommands.h"
#include "../../../UI/UISlider.h"

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

bool HandleContentClick( UIControlsTabState& state, InGameUIInputResult& result, int& activeSlider, int mouseX, int mouseY,
                         float contentX, float rowBase, float contentW, int modelCapacity, int lastSolverBallCount,
                         int lastSolverBoxCount );

bool UpdateActiveSlider( UIControlsTabState& state, int activeSlider, int mouseX, int modelCapacity, int lastSolverBallCount,
                         int lastSolverBoxCount, InGameUIInputResult& result );

bool CommitActiveSlider( UIControlsTabState& state, int activeSlider, InGameUIInputResult& result );

void Draw( UIControlsTabState& state, const UIDrawContext& draw, const InGameUIFrameData& data, float contentX,
           float contentY, float contentW, float contentH, float scrolledY );

} // namespace ControlsTab
} // namespace UI
} // namespace SkullbonezCore
