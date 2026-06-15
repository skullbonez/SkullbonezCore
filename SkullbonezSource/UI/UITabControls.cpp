/*
File: SkullbonezSource/UI/UITabControls.cpp
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
  - SkullbonezSource/UI/UITabControls.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UITabControls.h"

#include "SkullbonezUI.h"
#include "UIDrawWidgets.h"
#include "UILayout.h"

#include <algorithm>
#include <cstdio>

using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::UI::Widgets;

namespace
{

void SetContentBounds( SkullbonezCore::UI::ControlsTab::UIControlsTabState& state,
                       float contentX,
                       float rowBase,
                       float contentW )
{
    state.seedSlider.SetBounds( contentX, rowBase, contentW, 34.0f );
    state.solverBallSlider.SetBounds( contentX, rowBase + 88.0f, contentW, 34.0f );
    state.solverBoxSlider.SetBounds( contentX, rowBase + 128.0f, contentW, 34.0f );
    state.worldFluidHeightSlider.SetBounds( contentX, rowBase + 210.0f, contentW, 34.0f );
    state.worldFluidDensitySlider.SetBounds( contentX, rowBase + 250.0f, contentW, 34.0f );
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace ControlsTab
{

int ContentHeight()
{
    return 338;
}


void ResetPreviewState( UIControlsTabState& state )
{
    state.previewSolverBallCount = -1;
    state.previewSolverBoxCount = -1;
}


bool HandleContentClick( UIControlsTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW,
                         int lastSolverBallCount,
                         int lastSolverBoxCount )
{
    const int displayBalls = state.previewSolverBallCount >= 0 ? state.previewSolverBallCount : lastSolverBallCount;
    const int displayBoxes = state.previewSolverBoxCount >= 0 ? state.previewSolverBoxCount : lastSolverBoxCount;
    SetContentBounds( state, contentX, rowBase, contentW );

    if ( state.seedSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_SEED;
        result.commands.run.requestedSeed = static_cast<int>( state.seedSlider.ValueFromMouse( mouseX,
                                                                                                static_cast<float>( UI_SEED_MIN ),
                                                                                                static_cast<float>( UI_SEED_MAX ),
                                                                                                1.0f ) );
        return true;
    }
    if ( state.solverBallSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_SOLVER_BALLS;
        const int maxBalls = RemainingGameModelSlots( displayBoxes );
        state.previewSolverBallCount = static_cast<int>( state.solverBallSlider.ValueFromMouse( mouseX,
                                                                                                 static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                                 static_cast<float>( maxBalls ),
                                                                                                 1.0f ) );
        return true;
    }
    if ( state.solverBoxSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_SOLVER_BOXES;
        const int maxBoxes = RemainingGameModelSlots( displayBalls );
        state.previewSolverBoxCount = static_cast<int>( state.solverBoxSlider.ValueFromMouse( mouseX,
                                                                                               static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                               static_cast<float>( maxBoxes ),
                                                                                               1.0f ) );
        return true;
    }
    if ( state.worldFluidHeightSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_WORLD_FLUID_HEIGHT;
        result.commands.water.requestWorldFluidHeight = true;
        result.commands.water.requestedWorldFluidHeight = state.worldFluidHeightSlider.ValueFromMouse( mouseX, UI_WORLD_FLUID_HEIGHT_MIN, UI_WORLD_FLUID_HEIGHT_MAX, UI_WORLD_FLUID_HEIGHT_STEP );
        return true;
    }
    if ( state.worldFluidDensitySlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_WORLD_FLUID_DENSITY;
        result.commands.water.requestWorldFluidDensity = true;
        result.commands.water.requestedWorldFluidDensity = state.worldFluidDensitySlider.ValueFromMouse( mouseX, UI_WORLD_FLUID_DENSITY_MIN, UI_WORLD_FLUID_DENSITY_MAX, UI_WORLD_FLUID_DENSITY_STEP );
        return true;
    }
    return false;
}


bool UpdateActiveSlider( UIControlsTabState& state,
                         int activeSlider,
                         int mouseX,
                         int lastSolverBallCount,
                         int lastSolverBoxCount,
                         InGameUIInputResult& result )
{
    if ( activeSlider == SLIDER_SEED )
    {
        result.commands.run.requestedSeed = static_cast<int>( state.seedSlider.ValueFromMouse( mouseX,
                                                                                                static_cast<float>( UI_SEED_MIN ),
                                                                                                static_cast<float>( UI_SEED_MAX ),
                                                                                                1.0f ) );
        return true;
    }
    if ( activeSlider == SLIDER_SOLVER_BALLS )
    {
        const int boxes = state.previewSolverBoxCount >= 0 ? state.previewSolverBoxCount : lastSolverBoxCount;
        const int maxBalls = RemainingGameModelSlots( boxes );
        state.previewSolverBallCount = static_cast<int>( state.solverBallSlider.ValueFromMouse( mouseX,
                                                                                                 static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                                 static_cast<float>( maxBalls ),
                                                                                                 1.0f ) );
        return true;
    }
    if ( activeSlider == SLIDER_SOLVER_BOXES )
    {
        const int balls = state.previewSolverBallCount >= 0 ? state.previewSolverBallCount : lastSolverBallCount;
        const int maxBoxes = RemainingGameModelSlots( balls );
        state.previewSolverBoxCount = static_cast<int>( state.solverBoxSlider.ValueFromMouse( mouseX,
                                                                                               static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                               static_cast<float>( maxBoxes ),
                                                                                               1.0f ) );
        return true;
    }
    if ( activeSlider == SLIDER_WORLD_FLUID_HEIGHT )
    {
        result.commands.water.requestWorldFluidHeight = true;
        result.commands.water.requestedWorldFluidHeight = state.worldFluidHeightSlider.ValueFromMouse( mouseX, UI_WORLD_FLUID_HEIGHT_MIN, UI_WORLD_FLUID_HEIGHT_MAX, UI_WORLD_FLUID_HEIGHT_STEP );
        return true;
    }
    if ( activeSlider == SLIDER_WORLD_FLUID_DENSITY )
    {
        result.commands.water.requestWorldFluidDensity = true;
        result.commands.water.requestedWorldFluidDensity = state.worldFluidDensitySlider.ValueFromMouse( mouseX, UI_WORLD_FLUID_DENSITY_MIN, UI_WORLD_FLUID_DENSITY_MAX, UI_WORLD_FLUID_DENSITY_STEP );
        return true;
    }
    return false;
}


bool CommitActiveSlider( UIControlsTabState& state, int activeSlider, InGameUIInputResult& result )
{
    if ( activeSlider == SLIDER_SOLVER_BALLS && state.previewSolverBallCount >= 0 )
    {
        result.commands.run.requestedSolverBallCount = state.previewSolverBallCount;
        return true;
    }
    if ( activeSlider == SLIDER_SOLVER_BOXES && state.previewSolverBoxCount >= 0 )
    {
        result.commands.run.requestedSolverBoxCount = state.previewSolverBoxCount;
        return true;
    }
    return false;
}


void Draw( UIControlsTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY )
{
    char buf[128];
    const int displaySeed = static_cast<int>( (std::max)( 1u, data.rngSeed ) );
    const int displaySolverBalls = state.previewSolverBallCount >= 0 ? state.previewSolverBallCount : data.solverBallCount;
    const int displaySolverBoxes = state.previewSolverBoxCount >= 0 ? state.previewSolverBoxCount : data.solverBoxCount;
    const int displayBallMax = RemainingGameModelSlots( displaySolverBoxes );
    const int displayBoxMax = RemainingGameModelSlots( displaySolverBalls );

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Run Controls" );
    snprintf( buf, sizeof( buf ), "%d", displaySeed );
    state.seedSlider.SetBounds( contentX, scrolledY + 42.0f, contentW, 34.0f );
    if ( IsRowVisible( contentY, contentH, scrolledY + 42.0f, 34.0f ) )
    {
        state.seedSlider.Draw( draw, "Seed", buf, static_cast<float>( displaySeed ), static_cast<float>( UI_SEED_MIN ), static_cast<float>( UI_SEED_MAX ) );
    }
    if ( IsRowVisible( contentY, contentH, scrolledY + 104.0f, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 104.0f, 12.0f, "Game Models" );
    }
    snprintf( buf, sizeof( buf ), "%d", displaySolverBalls );
    state.solverBallSlider.SetBounds( contentX, scrolledY + 130.0f, contentW, 34.0f );
    if ( IsRowVisible( contentY, contentH, scrolledY + 130.0f, 34.0f ) )
    {
        state.solverBallSlider.Draw( draw, "Balls", buf, static_cast<float>( displaySolverBalls ), static_cast<float>( UI_SOLVER_COUNT_MIN ), static_cast<float>( displayBallMax ) );
    }
    snprintf( buf, sizeof( buf ), "%d", displaySolverBoxes );
    state.solverBoxSlider.SetBounds( contentX, scrolledY + 170.0f, contentW, 34.0f );
    if ( IsRowVisible( contentY, contentH, scrolledY + 170.0f, 34.0f ) )
    {
        state.solverBoxSlider.Draw( draw, "Boxes", buf, static_cast<float>( displaySolverBoxes ), static_cast<float>( UI_SOLVER_COUNT_MIN ), static_cast<float>( displayBoxMax ) );
    }
    if ( IsRowVisible( contentY, contentH, scrolledY + 226.0f, 18.0f ) )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 226.0f, 12.0f, "Fluid" );
    }
    snprintf( buf, sizeof( buf ), "%.0f", data.worldFluidHeight );
    state.worldFluidHeightSlider.SetBounds( contentX, scrolledY + 252.0f, contentW, 34.0f );
    if ( IsRowVisible( contentY, contentH, scrolledY + 252.0f, 34.0f ) )
    {
        state.worldFluidHeightSlider.Draw( draw, "Fluid height", buf, data.worldFluidHeight, UI_WORLD_FLUID_HEIGHT_MIN, UI_WORLD_FLUID_HEIGHT_MAX );
    }
    snprintf( buf, sizeof( buf ), "%.2f", data.worldFluidDensity );
    state.worldFluidDensitySlider.SetBounds( contentX, scrolledY + 292.0f, contentW, 34.0f );
    if ( IsRowVisible( contentY, contentH, scrolledY + 292.0f, 34.0f ) )
    {
        state.worldFluidDensitySlider.Draw( draw, "Fluid density", buf, data.worldFluidDensity, UI_WORLD_FLUID_DENSITY_MIN, UI_WORLD_FLUID_DENSITY_MAX );
    }
}

} // namespace ControlsTab
} // namespace UI
} // namespace SkullbonezCore
