/*
File: SkullbonezSource/UI/UITabOptions.cpp
Purpose:
  Implements UI TabOptions widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UITabOptions.cpp implements UI TabOptions widgets, layout, drawing, or UI
  state for the in-engine controls. As an implementation unit, keep edits
  anchored on UI request, layout, hit-test, and draw-command flow and on the
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
  - SkullbonezSource/UI/UITabOptions.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UITabOptions.h"

#include "UI.h"
#include "UIDrawWidgets.h"
#include "UILayout.h"

#include <algorithm>
#include <cstdio>

using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::UI::Widgets;

namespace
{

void SetToggleBounds( SkullbonezCore::UI::OptionsTab::UIOptionsTabState& state,
                      int index,
                      int row,
                      int column,
                      float col1,
                      float col2,
                      float rowBase,
                      float colW )
{
    const float tx = column == 0 ? col1 : col2;
    state.toggles[index].SetBounds( tx, rowBase + static_cast<float>( row ) * CONTENT_TOGGLE_ROW_H, colW, 24.0f );
}

void SetContentBounds( SkullbonezCore::UI::OptionsTab::UIOptionsTabState& state,
                       float contentX,
                       float rowBase,
                       float contentW )
{
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    const float col1 = contentX;
    const float col2 = contentX + colW + 18.0f;
    SetToggleBounds( state, 0, 0, 0, col1, col2, rowBase, colW );
    SetToggleBounds( state, 1, 0, 1, col1, col2, rowBase, colW );
    SetToggleBounds( state, 2, 1, 0, col1, col2, rowBase, colW );
    SetToggleBounds( state, 3, 1, 1, col1, col2, rowBase, colW );
    SetToggleBounds( state, 4, 2, 0, col1, col2, rowBase, colW );
    SetToggleBounds( state, 5, 2, 1, col1, col2, rowBase, colW );
    state.timeScaleSlider.SetBounds( contentX, rowBase + 126.0f, contentW, 34.0f );
    state.modelCountSlider.SetBounds( contentX, rowBase + 174.0f, contentW, 34.0f );
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace OptionsTab
{

int ContentHeight()
{
    return 286;
}


void ResetPreviewState( UIOptionsTabState& state )
{
    state.previewTimeScale = -1.0f;
    state.previewModelCount = -1;
}


bool HandleContentClick( UIOptionsTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW,
                         int modelCapacity )
{
    const int modelMax = (std::max)( UI_MODEL_COUNT_MIN, modelCapacity );
    SetContentBounds( state, contentX, rowBase, contentW );

    if ( state.toggles[0].HitTest( mouseX, mouseY ) )
    {
        result.commands.sceneOptions.toggleFixedStep = true;
    }
    else if ( state.toggles[1].HitTest( mouseX, mouseY ) )
    {
        result.commands.sceneOptions.toggleTerrainHidden = true;
    }
    else if ( state.toggles[2].HitTest( mouseX, mouseY ) )
    {
        result.commands.sceneOptions.toggleWaterHidden = true;
    }
    else if ( state.toggles[3].HitTest( mouseX, mouseY ) )
    {
        result.commands.sceneOptions.toggleWaterFreeze = true;
    }
    else if ( state.toggles[4].HitTest( mouseX, mouseY ) )
    {
        result.commands.sceneOptions.toggleWaterFlat = true;
    }
    else if ( state.toggles[5].HitTest( mouseX, mouseY ) )
    {
        result.commands.sceneOptions.toggleShadows = true;
    }
    else if ( state.timeScaleSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TIME_SCALE;
        state.previewTimeScale =
            state.timeScaleSlider.ValueFromMouse( mouseX, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX, UI_TIME_SCALE_STEP );
        result.commands.sceneOptions.requestedTimeScale = state.previewTimeScale;
        return true;
    }
    else if ( state.modelCountSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_MODEL_COUNT;
        state.previewModelCount =
            static_cast<int>( state.modelCountSlider.ValueFromMouse( mouseX,
                                                                     static_cast<float>( UI_MODEL_COUNT_MIN ),
                                                                     static_cast<float>( modelMax ),
                                                                     1.0f ) );
        return true;
    }
    return false;
}


bool UpdateActiveSlider( UIOptionsTabState& state,
                         int activeSlider,
                         int mouseX,
                         int modelCapacity,
                         InGameUIInputResult& result )
{
    if ( activeSlider == SLIDER_TIME_SCALE )
    {
        state.previewTimeScale =
            state.timeScaleSlider.ValueFromMouse( mouseX, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX, UI_TIME_SCALE_STEP );
        result.commands.sceneOptions.requestedTimeScale = state.previewTimeScale;
        return true;
    }
    if ( activeSlider == SLIDER_MODEL_COUNT )
    {
        const int modelMax = (std::max)( UI_MODEL_COUNT_MIN, modelCapacity );
        state.previewModelCount =
            static_cast<int>( state.modelCountSlider.ValueFromMouse( mouseX,
                                                                     static_cast<float>( UI_MODEL_COUNT_MIN ),
                                                                     static_cast<float>( modelMax ),
                                                                     1.0f ) );
        return true;
    }
    return false;
}


bool CommitActiveSlider( UIOptionsTabState& state, int activeSlider, InGameUIInputResult& result )
{
    if ( activeSlider == SLIDER_TIME_SCALE && state.previewTimeScale > 0.0f )
    {
        result.commands.sceneOptions.requestedTimeScale = state.previewTimeScale;
        return true;
    }
    if ( activeSlider == SLIDER_MODEL_COUNT && state.previewModelCount >= 0 )
    {
        result.commands.sceneOptions.requestedModelCount = state.previewModelCount;
        return true;
    }
    return false;
}


void Draw( UIOptionsTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int activeSlider )
{
    char buf[128];
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    const float col1 = contentX;
    const float col2 = contentX + colW + 18.0f;
    const float displayTimeScale = ( activeSlider == SLIDER_TIME_SCALE && state.previewTimeScale > 0.0f )
                                       ? state.previewTimeScale
                                       : data.timeScale;
    const int modelMax = (std::max)( UI_MODEL_COUNT_MIN, data.modelCapacity );
    const int rawModelCount = ( activeSlider == SLIDER_MODEL_COUNT && state.previewModelCount >= 0 )
                                  ? state.previewModelCount
                                  : data.modelCount;
    const int displayModelCount = std::clamp( rawModelCount, UI_MODEL_COUNT_MIN, modelMax );
    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Scene Options" );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.toggles[0],
                       col1,
                       scrolledY + 42.0f,
                       colW,
                       "Fixed step",
                       data.fixedStep );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.toggles[1],
                       col2,
                       scrolledY + 42.0f,
                       colW,
                       "Hide terrain",
                       data.terrainHidden );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.toggles[2],
                       col1,
                       scrolledY + 72.0f,
                       colW,
                       "Hide water",
                       data.waterHidden );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.toggles[3],
                       col2,
                       scrolledY + 72.0f,
                       colW,
                       "Freeze water",
                       data.waterFreezeDebug );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.toggles[4],
                       col1,
                       scrolledY + 102.0f,
                       colW,
                       "Flat water",
                       data.waterFlatDebug );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.toggles[5],
                       col2,
                       scrolledY + 102.0f,
                       colW,
                       "Shadows",
                       data.cinematicRendering ? data.cinematic.shadow.enabled : data.ordinaryRender.shadow.enabled );
    snprintf( buf,
              sizeof( buf ),
              "%s alpha %.3f%s",
              data.presentationInterpolation ? "on" : "off",
              data.presentationAlpha,
              data.presentationPinned ? " (capture pin)" : "" );
    DrawLabelValueAt( draw,
                      contentY,
                      contentH,
                      contentX,
                      scrolledY + 138.0f,
                      "Presentation",
                      buf,
                      0.62f,
                      0.86f,
                      0.78f );
    snprintf( buf, sizeof( buf ), "%.2fx", displayTimeScale );
    state.timeScaleSlider.SetBounds( contentX, scrolledY + 168.0f, contentW, 34.0f );
    if ( IsRowVisible( contentY, contentH, scrolledY + 168.0f, 34.0f ) )
    {
        state.timeScaleSlider.Draw( draw, "Time scale", buf, displayTimeScale, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX );
    }
    snprintf( buf, sizeof( buf ), "%d", displayModelCount );
    state.modelCountSlider.SetBounds( contentX, scrolledY + 216.0f, contentW, 34.0f );
    if ( IsRowVisible( contentY, contentH, scrolledY + 216.0f, 34.0f ) )
    {
        state.modelCountSlider.Draw( draw,
                                     "Model count",
                                     buf,
                                     static_cast<float>( displayModelCount ),
                                     static_cast<float>( UI_MODEL_COUNT_MIN ),
                                     static_cast<float>( modelMax ) );
    }
}

} // namespace OptionsTab
} // namespace UI
} // namespace SkullbonezCore
