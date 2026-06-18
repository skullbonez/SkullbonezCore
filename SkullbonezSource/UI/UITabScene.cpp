/*
File: SkullbonezSource/UI/UITabScene.cpp
Purpose:
  Implements UI TabScene widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  The UI is immediate-mode-style: each frame reads engine state, computes hit
  boxes, emits draw commands, and returns requests for the run loop to apply.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UITabScene.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UITabScene.h"

#include "SkullbonezUI.h"
#include "UIButton.h"
#include "UIComboBox.h"
#include "UIDrawWidgets.h"
#include "UIInput.h"
#include "UILayout.h"
#include "UIStyle.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::UI::Widgets;

namespace
{
constexpr int UI_SCENE_CONTENT_HEIGHT = 252;
constexpr float UI_SCENE_TIME_SCALE_SLIDER_Y = 196.0f;

char LowerAscii( char value )
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>( value + ( 'a' - 'A' ) ) : value;
}

bool ConsumeFilterKeyPress( SkullbonezCore::UI::SceneTab::UISceneTabState& state, int virtualKey )
{
    return SkullbonezCore::UI::InputControl::ConsumeKeyPress( state.filterKeyWasDown, virtualKey );
}

void AppendFilterChar( SkullbonezCore::UI::SceneTab::UISceneTabState& state, char value )
{
    const size_t len = strlen( state.filter );
    if ( len >= sizeof( state.filter ) - 1 )
    {
        return;
    }

    state.filter[len] = value;
    state.filter[len + 1] = '\0';
    state.comboScroll = 0;
}

void BackspaceFilter( SkullbonezCore::UI::SceneTab::UISceneTabState& state )
{
    const size_t len = strlen( state.filter );
    if ( len == 0 )
    {
        return;
    }

    state.filter[len - 1] = '\0';
    state.comboScroll = 0;
}

void SetSceneHeaderBounds( SkullbonezCore::UI::UIComboBox& combo,
                           SkullbonezCore::UI::UIButton& resetSceneButton,
                           SkullbonezCore::UI::UIButton& resetDefaultsButton,
                           SkullbonezCore::UI::UIButton& saveDefaultsButton,
                           float contentX,
                           float rowBase,
                           float contentW )
{
    const float sceneComboW = SceneTabComboWidth( contentW );
    combo.SetBounds( contentX, rowBase, sceneComboW, 24.0f );
    const float resetX = contentX + sceneComboW + UI_SCENE_HEADER_BUTTON_GAP;
    const float defaultsX = resetX + UI_SCENE_RESET_BUTTON_W + UI_SCENE_HEADER_BUTTON_GAP;
    const float saveDefaultsX = defaultsX + UI_SCENE_RESET_DEFAULTS_BUTTON_W + UI_SCENE_HEADER_BUTTON_GAP;
    resetSceneButton.SetBounds( resetX, rowBase, UI_SCENE_RESET_BUTTON_W, 24.0f );
    resetDefaultsButton.SetBounds( defaultsX, rowBase, UI_SCENE_RESET_DEFAULTS_BUTTON_W, 24.0f );
    saveDefaultsButton.SetBounds( saveDefaultsX, rowBase, UI_SCENE_SAVE_DEFAULTS_BUTTON_W, 24.0f );
    combo.SetDropUp( false );
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace SceneTab
{

int ContentHeight()
{
    return UI_SCENE_CONTENT_HEIGHT;
}


bool FilterMatches( const char* option, const char* filter )
{
    if ( !filter || filter[0] == '\0' )
    {
        return true;
    }
    if ( !option )
    {
        return false;
    }

    for ( int optionStart = 0; option[optionStart] != '\0'; ++optionStart )
    {
        int optionOffset = 0;
        while ( filter[optionOffset] != '\0' &&
                option[optionStart + optionOffset] != '\0' &&
                LowerAscii( option[optionStart + optionOffset] ) == LowerAscii( filter[optionOffset] ) )
        {
            ++optionOffset;
        }
        if ( filter[optionOffset] == '\0' )
        {
            return true;
        }
    }
    return false;
}


int CountFilteredOptions( const char* const* options, int optionCount, const char* filter )
{
    int count = FilterMatches( DEMO_SCENE_OPTION, filter ) ? 1 : 0;
    if ( !options || optionCount <= 0 )
    {
        return count;
    }
    for ( int i = 0; i < optionCount; ++i )
    {
        if ( FilterMatches( options[i], filter ) )
        {
            ++count;
        }
    }
    return count;
}


int FindFilteredOptionIndex( const char* const* options, int optionCount, const char* filter, int filteredIndex )
{
    if ( filteredIndex < 0 )
    {
        return -1;
    }

    int filteredPosition = 0;
    if ( FilterMatches( DEMO_SCENE_OPTION, filter ) )
    {
        if ( filteredPosition == filteredIndex )
        {
            return DEMO_SCENE_BROWSER_INDEX;
        }
        ++filteredPosition;
    }
    if ( !options || optionCount <= 0 )
    {
        return -1;
    }
    for ( int i = 0; i < optionCount; ++i )
    {
        if ( FilterMatches( options[i], filter ) )
        {
            if ( filteredPosition == filteredIndex )
            {
                return i;
            }
            ++filteredPosition;
        }
    }
    return -1;
}


int FilteredPositionForIndex( const char* const* options, int optionCount, const char* filter, int optionIndex )
{
    int filteredPosition = 0;
    if ( FilterMatches( DEMO_SCENE_OPTION, filter ) )
    {
        if ( optionIndex < 0 )
        {
            return filteredPosition;
        }
        ++filteredPosition;
    }
    if ( !options || optionIndex < 0 || optionIndex >= optionCount )
    {
        return -1;
    }

    for ( int i = 0; i < optionCount; ++i )
    {
        if ( !FilterMatches( options[i], filter ) )
        {
            continue;
        }
        if ( i == optionIndex )
        {
            return filteredPosition;
        }
        ++filteredPosition;
    }
    return -1;
}


void ClearFilter( UISceneTabState& state )
{
    state.filter[0] = '\0';
    state.comboScroll = 0;
}


void SetFilter( UISceneTabState& state, const char* filter )
{
    strncpy_s( state.filter, sizeof( state.filter ), filter ? filter : "", _TRUNCATE );
    state.comboScroll = 0;
}


void CloseCombo( UISceneTabState& state, UIComboBox& combo )
{
    combo.Close();
    ClearFilter( state );
}


void CaptureFilterKeyState( UISceneTabState& state )
{
    InputControl::CaptureKeyStates( state.filterKeyWasDown );
}


void ResetPreviewState( UISceneTabState& state )
{
    state.previewTimeScale = -1.0f;
}


void UpdateFilterTyping( UISceneTabState& state,
                         UIComboBox& combo,
                         InGameUIInputResult& result,
                         const char* const* sceneOptions,
                         int sceneOptionCount )
{
    if ( !combo.IsOpen() )
    {
        return;
    }

    for ( int key = 'A'; key <= 'Z'; ++key )
    {
        if ( ConsumeFilterKeyPress( state, key ) )
        {
            AppendFilterChar( state, static_cast<char>( 'a' + key - 'A' ) );
            result.commands.ui.userInteracted = true;
        }
    }
    for ( int key = '0'; key <= '9'; ++key )
    {
        if ( ConsumeFilterKeyPress( state, key ) )
        {
            AppendFilterChar( state, static_cast<char>( key ) );
            result.commands.ui.userInteracted = true;
        }
    }

    const bool isShiftDown = InputControl::IsVirtualKeyDown( VK_SHIFT );
    if ( ConsumeFilterKeyPress( state, VK_SPACE ) )
    {
        AppendFilterChar( state, ' ' );
        result.commands.ui.userInteracted = true;
    }
    if ( ConsumeFilterKeyPress( state, VK_OEM_MINUS ) )
    {
        AppendFilterChar( state, isShiftDown ? '_' : '-' );
        result.commands.ui.userInteracted = true;
    }
    if ( ConsumeFilterKeyPress( state, VK_OEM_PERIOD ) )
    {
        AppendFilterChar( state, '.' );
        result.commands.ui.userInteracted = true;
    }
    if ( ConsumeFilterKeyPress( state, VK_BACK ) )
    {
        BackspaceFilter( state );
        result.commands.ui.userInteracted = true;
    }
    if ( ConsumeFilterKeyPress( state, VK_DELETE ) )
    {
        ClearFilter( state );
        result.commands.ui.userInteracted = true;
    }
    if ( ConsumeFilterKeyPress( state, VK_ESCAPE ) )
    {
        if ( state.filter[0] != '\0' )
        {
            ClearFilter( state );
        }
        else
        {
            CloseCombo( state, combo );
        }
        result.commands.ui.userInteracted = true;
    }
    if ( ConsumeFilterKeyPress( state, VK_RETURN ) && combo.IsOpen() )
    {
        const int sceneIndex = FindFilteredOptionIndex( sceneOptions, sceneOptionCount, state.filter, 0 );
        if ( sceneIndex == DEMO_SCENE_BROWSER_INDEX )
        {
            result.commands.scene.requestDemoScene = true;
            CloseCombo( state, combo );
            result.commands.ui.userInteracted = true;
        }
        else if ( sceneIndex >= 0 )
        {
            result.commands.scene.requestedSceneIndex = sceneIndex;
            CloseCombo( state, combo );
            result.commands.ui.userInteracted = true;
        }
    }
}


bool HandleComboWheel( UISceneTabState& state,
                       UIComboBox& combo,
                       const char* const* sceneOptions,
                       int sceneOptionCount,
                       int mouseX,
                       int mouseY,
                       int wheelDelta,
                       float contentX,
                       float rowBase,
                       float contentW )
{
    if ( wheelDelta == 0 || !combo.IsOpen() )
    {
        return false;
    }

    const float sceneComboW = SceneTabComboWidth( contentW );
    const int filteredSceneCount = CountFilteredOptions( sceneOptions, sceneOptionCount, state.filter );
    const int visibleSceneOptions = SceneComboVisibleCount( filteredSceneCount );
    const int sceneDrawOptions = filteredSceneCount > 0 ? visibleSceneOptions : ( state.filter[0] != '\0' ? 1 : 0 );
    combo.SetBounds( contentX, rowBase, sceneComboW, 24.0f );
    combo.SetDropUp( false );
    if ( combo.HitBox( mouseX, mouseY ) || combo.HitOption( mouseX, mouseY, sceneDrawOptions ) >= 0 )
    {
        const int wheelSteps = wheelDelta / WHEEL_DELTA;
        state.comboScroll = ClampSceneComboScroll( state.comboScroll - wheelSteps, filteredSceneCount );
        return true;
    }
    return false;
}


bool HandleOpenComboClick( UISceneTabState& state,
                           UIComboBox& combo,
                           UIButton& resetSceneButton,
                           UIButton& resetDefaultsButton,
                           UIButton& saveDefaultsButton,
                           InGameUIInputResult& result,
                           const char* const* sceneOptions,
                           int sceneOptionCount,
                           int mouseX,
                           int mouseY,
                           float contentX,
                           float rowBase,
                           float contentW )
{
    if ( !combo.IsOpen() )
    {
        return false;
    }

    const int filteredSceneCount = CountFilteredOptions( sceneOptions, sceneOptionCount, state.filter );
    const int visibleSceneOptions = SceneComboVisibleCount( filteredSceneCount );
    const int sceneDrawOptions = filteredSceneCount > 0 ? visibleSceneOptions : ( state.filter[0] != '\0' ? 1 : 0 );
    state.comboScroll = ClampSceneComboScroll( state.comboScroll, filteredSceneCount );
    SetSceneHeaderBounds( combo, resetSceneButton, resetDefaultsButton, saveDefaultsButton, contentX, rowBase, contentW );

    const int option = combo.HitOption( mouseX, mouseY, sceneDrawOptions );
    if ( resetSceneButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.resetScene = true;
        CloseCombo( state, combo );
    }
    else if ( resetDefaultsButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.resetSceneDefaults = true;
        CloseCombo( state, combo );
    }
    else if ( saveDefaultsButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.saveSceneDefaults = true;
        CloseCombo( state, combo );
    }
    else if ( filteredSceneCount > 0 && option >= 0 && option < visibleSceneOptions )
    {
        const int sceneIndex = FindFilteredOptionIndex( sceneOptions, sceneOptionCount, state.filter, state.comboScroll + option );
        if ( sceneIndex == DEMO_SCENE_BROWSER_INDEX )
        {
            result.commands.scene.requestDemoScene = true;
        }
        else if ( sceneIndex >= 0 )
        {
            result.commands.scene.requestedSceneIndex = sceneIndex;
        }
        CloseCombo( state, combo );
    }
    else
    {
        CloseCombo( state, combo );
    }
    return true;
}


bool HandleContentClick( UISceneTabState& state,
                         UIComboBox& combo,
                         UIButton& resetSceneButton,
                         UIButton& resetDefaultsButton,
                         UIButton& saveDefaultsButton,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         const char* const* sceneOptions,
                         int sceneOptionCount,
                         int selectedSceneOption,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW )
{
    SetSceneHeaderBounds( combo, resetSceneButton, resetDefaultsButton, saveDefaultsButton, contentX, rowBase, contentW );
    if ( resetSceneButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.resetScene = true;
        CloseCombo( state, combo );
        return true;
    }
    if ( resetDefaultsButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.resetSceneDefaults = true;
        CloseCombo( state, combo );
        return true;
    }
    if ( saveDefaultsButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.saveSceneDefaults = true;
        CloseCombo( state, combo );
        return true;
    }
    if ( combo.HitBox( mouseX, mouseY ) )
    {
        ClearFilter( state );
        CaptureFilterKeyState( state );
        const int filteredSceneCount = CountFilteredOptions( sceneOptions, sceneOptionCount, state.filter );
        state.comboScroll = SceneComboScrollForSelection( FilteredPositionForIndex( sceneOptions, sceneOptionCount, state.filter, selectedSceneOption ), filteredSceneCount );
        combo.SetOpen( true );
        return true;
    }
    state.timeScaleSlider.SetBounds( contentX, rowBase + ( UI_SCENE_TIME_SCALE_SLIDER_Y - 42.0f ), contentW, 34.0f );
    if ( state.timeScaleSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TIME_SCALE;
        state.previewTimeScale = state.timeScaleSlider.ValueFromMouse( mouseX, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX, UI_TIME_SCALE_STEP );
        result.commands.sceneOptions.requestedTimeScale = state.previewTimeScale;
        return true;
    }
    return false;
}


bool UpdateActiveSlider( UISceneTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result )
{
    if ( activeSlider != SLIDER_TIME_SCALE )
    {
        return false;
    }

    state.previewTimeScale = state.timeScaleSlider.ValueFromMouse( mouseX, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX, UI_TIME_SCALE_STEP );
    result.commands.sceneOptions.requestedTimeScale = state.previewTimeScale;
    return true;
}


bool CommitActiveSlider( UISceneTabState& state, int activeSlider, InGameUIInputResult& result )
{
    if ( activeSlider == SLIDER_TIME_SCALE && state.previewTimeScale > 0.0f )
    {
        result.commands.sceneOptions.requestedTimeScale = state.previewTimeScale;
        return true;
    }
    return false;
}


void Draw( UISceneTabState& state,
           UIComboBox& combo,
           UIButton& resetSceneButton,
           UIButton& resetDefaultsButton,
           UIButton& saveDefaultsButton,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int mouseX,
           int mouseY )
{
    char buf[160];
    char filterDisplay[80] = {};
    const bool sceneFilterActive = state.filter[0] != '\0';
    const int filteredSceneCount = CountFilteredOptions( data.sceneOptions, data.sceneOptionCount, state.filter );
    const int sceneVisibleCount = SceneComboVisibleCount( filteredSceneCount );
    state.comboScroll = ClampSceneComboScroll( state.comboScroll, filteredSceneCount );
    const int sceneFirstOption = state.comboScroll;
    const int selectedFilteredPosition = FilteredPositionForIndex( data.sceneOptions, data.sceneOptionCount, state.filter, data.selectedSceneOption );
    const int sceneSelectedInSlice = selectedFilteredPosition >= sceneFirstOption && selectedFilteredPosition < sceneFirstOption + sceneVisibleCount ? selectedFilteredPosition - sceneFirstOption : -1;
    const char* visibleSceneOptions[UI_SCENE_COMBO_VISIBLE_OPTIONS] = {};
    for ( int i = 0; i < sceneVisibleCount; ++i )
    {
        const int sceneIndex = FindFilteredOptionIndex( data.sceneOptions, data.sceneOptionCount, state.filter, sceneFirstOption + i );
        visibleSceneOptions[i] = sceneIndex == DEMO_SCENE_BROWSER_INDEX ? DEMO_SCENE_OPTION : ( sceneIndex >= 0 ? data.sceneOptions[sceneIndex] : "" );
    }
    int sceneDrawCount = sceneVisibleCount;
    if ( sceneDrawCount == 0 && sceneFilterActive )
    {
        visibleSceneOptions[0] = "No matches";
        sceneDrawCount = 1;
    }
    const char* selectedSceneName = DEMO_SCENE_OPTION;
    if ( data.sceneOptions && data.selectedSceneOption >= 0 && data.selectedSceneOption < data.sceneOptionCount )
    {
        selectedSceneName = data.sceneOptions[data.selectedSceneOption];
    }
    if ( combo.IsOpen() && sceneFilterActive )
    {
        snprintf( filterDisplay, sizeof( filterDisplay ), "%s", state.filter );
        selectedSceneName = filterDisplay;
    }

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Scene" );
    SetSceneHeaderBounds( combo, resetSceneButton, resetDefaultsButton, saveDefaultsButton, contentX, scrolledY + 42.0f, contentW );

    if ( data.targetFrameCount > 0 )
    {
        const int displayedFrame = ( data.testComplete && data.currentFrame > data.targetFrameCount ) ? data.targetFrameCount : data.currentFrame;
        snprintf( buf, sizeof( buf ), "%d / %d", displayedFrame, data.targetFrameCount );
    }
    else
    {
        snprintf( buf, sizeof( buf ), "%d", data.currentFrame );
    }
    if ( !combo.IsOpen() )
    {
        const float sceneCol2 = contentX + (std::max)( 208.0f, contentW * 0.48f );
        const Style::UIPalette& palette = Style::Palette();
        const float displayTimeScale = state.previewTimeScale > 0.0f ? state.previewTimeScale : data.timeScale;
        char statusBuf[64] = {};
        snprintf( statusBuf, sizeof( statusBuf ), "%s / fixed %s", data.testComplete ? "complete" : "running", data.fixedStep ? "on" : "off" );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 82.0f, "Renderer", data.rendererName, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b );
        DrawLabelValueAt( draw, contentY, contentH, sceneCol2, scrolledY + 82.0f, "Status", statusBuf, palette.accent.r, palette.accent.g, palette.accent.b );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 108.0f, "Frame", buf, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
        snprintf( buf, sizeof( buf ), "%.1f FPS", data.fps );
        DrawLabelValueAt( draw, contentY, contentH, sceneCol2, scrolledY + 108.0f, "Frame rate", buf, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b );
        snprintf( buf, sizeof( buf ), "%d / %d", data.currentSceneIndex + 1, data.sceneCount );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 134.0f, "Scene index", buf, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
        snprintf( buf, sizeof( buf ), "%.6f", data.sceneEnergy );
        DrawLabelValueAt( draw, contentY, contentH, sceneCol2, scrolledY + 134.0f, "Kinetic energy", buf, palette.warningAccent.r, palette.warningAccent.g, palette.warningAccent.b );
        snprintf( buf, sizeof( buf ), "%d", data.modelCount );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 160.0f, "Model count", buf, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
        snprintf( buf, sizeof( buf ), "%.2fx", displayTimeScale );
        state.timeScaleSlider.SetBounds( contentX, scrolledY + UI_SCENE_TIME_SCALE_SLIDER_Y, contentW, 34.0f );
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_SCENE_TIME_SCALE_SLIDER_Y, 34.0f ) )
        {
            state.timeScaleSlider.Draw( draw, "Simulation speed", buf, displayTimeScale, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX );
        }
    }
    if ( IsRowVisible( contentY, contentH, scrolledY + 42.0f, 24.0f ) )
    {
        combo.Draw( draw,
                    "Load scene",
                    selectedSceneName,
                    visibleSceneOptions,
                    sceneDrawCount,
                    sceneSelectedInSlice,
                    mouseX,
                    mouseY );
    }
    if ( IsRowVisible( contentY, contentH, scrolledY + 42.0f, 24.0f ) )
    {
        resetSceneButton.Draw( draw, "Reset", mouseX, mouseY );
        resetDefaultsButton.Draw( draw, "Reset Defaults", mouseX, mouseY );
        saveDefaultsButton.Draw( draw, "Save Defaults", mouseX, mouseY );
    }
}

} // namespace SceneTab
} // namespace UI
} // namespace SkullbonezCore
