/*
File: SkullbonezSource/Runtime/UI/GameUI/UITabScene.cpp
Purpose:
  Implements filtered scene navigation, time controls, and detached forecast
  presentation.

Summary:
  Owns filtered scene selection, scene commands, time-control interaction, and
  detached continuous-forecast controls/readout without retaining simulation
  state.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UITabScene.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UITabScene.h"

#include "../../../Core/PlatformWin32.h"
#include "UI.h"
#include "../../../UI/UIButton.h"
#include "../../../UI/UIComboBox.h"
#include "../../../UI/UIDrawWidgets.h"
#include "../../../UI/UIInput.h"
#include "GameUILayout.h"
#include "../../../UI/UIStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>

using namespace SkullbonezCore::UI::GameLayout;
using namespace SkullbonezCore::UI::OperatorControlPolicy;
using namespace SkullbonezCore::UI::Widgets;

namespace
{
constexpr int UI_SCENE_CONTENT_HEIGHT = 470;
constexpr float UI_SCENE_RECORDING_COMBO_Y = 74.0f;
constexpr float UI_SCENE_TIME_SCALE_SLIDER_Y = 196.0f;
constexpr float UI_SCENE_PREDICTION_REVEAL_SLIDER_Y = 236.0f;
constexpr float UI_SCENE_FORECAST_TITLE_Y = 286.0f;
constexpr float UI_SCENE_FORECAST_BUTTON_Y = 312.0f;

// Concept: the reveal slider is authored normalized 0..1 and mapped
// exponentially, because the useful range spans three decades. A linear 1..1000
// control would bury every cinematic pace in the first half percent of travel.
// Full right is the shipped default, where a finished horizon appears within a
// frame or two instead of animating over the whole horizon length.
constexpr float REPLAY_PREDICTION_REVEAL_RATE_MIN = 1.0f;
constexpr float REPLAY_PREDICTION_REVEAL_RATE_MAX = 1000.0f;

float PredictionRevealRateFromNormalized( float normalized )
{
    const float clamped = std::clamp( normalized, 0.0f, 1.0f );
    return REPLAY_PREDICTION_REVEAL_RATE_MIN *
           std::pow( REPLAY_PREDICTION_REVEAL_RATE_MAX / REPLAY_PREDICTION_REVEAL_RATE_MIN, clamped );
}

float NormalizedFromPredictionRevealRate( float rate )
{
    const float clamped = std::clamp( rate, REPLAY_PREDICTION_REVEAL_RATE_MIN, REPLAY_PREDICTION_REVEAL_RATE_MAX );
    return std::log( clamped / REPLAY_PREDICTION_REVEAL_RATE_MIN ) /
           std::log( REPLAY_PREDICTION_REVEAL_RATE_MAX / REPLAY_PREDICTION_REVEAL_RATE_MIN );
}

char LowerAscii( char value )
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>( value + ( 'a' - 'A' ) ) : value;
}

bool ConsumeFilterKeyPress( SkullbonezCore::UI::SceneTab::UISceneTabState& state,
                            const SkullbonezCore::UI::InputControl::UIInputSnapshot& input, int virtualKey )
{
    return SkullbonezCore::UI::InputControl::ConsumeKeyPress( state.filterKeyWasDown, input, virtualKey );
}

void AppendFilterChar( SkullbonezCore::UI::SceneTab::UISceneTabState& state, char value )
{
    // Invariant: The scene filter doubles as the create-scene name buffer, so
    // keep it bounded and reset combo scroll whenever it changes.
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

void RequestNewScene( SkullbonezCore::UI::SceneTab::UISceneTabState& state, SkullbonezCore::UI::InGameUIInputResult& result )
{
    // Concept: The UI requests creation by name; scene runtime owns sanitizing,
    // writing the starter file, refreshing the browser, and loading it.
    if ( state.filter[0] == '\0' )
    {
        return;
    }

    result.commands.scene.createScene = true;
    strncpy_s( result.commands.scene.requestedSceneName, sizeof( result.commands.scene.requestedSceneName ), state.filter,
               _TRUNCATE );

    result.commands.ui.userInteracted = true;
}

void SetSceneHeaderBounds( SkullbonezCore::UI::UIComboBox& combo, SkullbonezCore::UI::UIButton& resetSceneButton,
                           SkullbonezCore::UI::UIButton& resetDefaultsButton,
                           SkullbonezCore::UI::UIButton& saveDefaultsButton, float contentX, float rowBase, float contentW )
{
    const SceneHeaderWidths widths = ResolveSceneHeaderWidths( contentW );
    combo.SetBounds( contentX, rowBase, widths.combo, 24.0f );
    const float resetX = contentX + widths.combo + widths.gap;
    const float defaultsX = resetX + widths.reset + widths.gap;
    const float saveDefaultsX = defaultsX + widths.resetDefaults + widths.gap;
    resetSceneButton.SetBounds( resetX, rowBase, widths.reset, 24.0f );
    resetDefaultsButton.SetBounds( defaultsX, rowBase, widths.resetDefaults, 24.0f );
    saveDefaultsButton.SetBounds( saveDefaultsX, rowBase, widths.saveDefaults, 24.0f );
    combo.SetDropUp( false );
}

void SetRecordingComboBounds( SkullbonezCore::UI::UIComboBox& combo, float contentX, float rowBase, float contentW )
{
    combo.SetBounds( contentX, rowBase + ( UI_SCENE_RECORDING_COMBO_Y - 42.0f ), SceneTabComboWidth( contentW ), 24.0f );
    combo.SetDropUp( false );
}

void SetForecastBounds( SkullbonezCore::UI::SceneTab::UISceneTabState& state, float contentX, float rowBase, float contentW )
{
    constexpr float gap = 6.0f;
    const float toggleWidth = contentW * 0.66f;
    const float resetWidth = contentW - toggleWidth - gap;
    const float y = rowBase + ( UI_SCENE_FORECAST_BUTTON_Y - 42.0f );
    state.continuousForecastToggle.SetBounds( contentX, y, toggleWidth, 24.0f );
    state.resetForecastButton.SetBounds( contentX + toggleWidth + gap, y, resetWidth, 24.0f );
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
    // Why: the scroll extent must reach both time controls and the detached
    // continuous-forecast diagnostics beneath them.
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

        while ( filter[optionOffset] != '\0' && option[optionStart + optionOffset] != '\0' &&
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


bool TextEqualsIgnoreAsciiCase( const char* left, const char* right )
{
    if ( !left || !right )
    {
        return false;
    }

    while ( *left != '\0' && *right != '\0' )
    {
        if ( LowerAscii( *left ) != LowerAscii( *right ) )
        {
            return false;
        }

        ++left;
        ++right;
    }

    return *left == '\0' && *right == '\0';
}


int FindExactOptionIndex( const char* const* options, int optionCount, const char* filter )
{
    if ( !filter || filter[0] == '\0' )
    {
        return -1;
    }

    if ( TextEqualsIgnoreAsciiCase( DEMO_SCENE_OPTION, filter ) )
    {
        return DEMO_SCENE_BROWSER_INDEX;
    }

    if ( !options || optionCount <= 0 )
    {
        return -1;
    }

    for ( int i = 0; i < optionCount; ++i )
    {
        if ( TextEqualsIgnoreAsciiCase( options[i], filter ) )
        {
            return i;
        }
    }

    return -1;
}


int CountFilteredOptions( const char* const* options, int optionCount, const char* filter )
{
    int count = filter && filter[0] != '\0' ? 1 : 0;
    count += FilterMatches( DEMO_SCENE_OPTION, filter ) ? 1 : 0;

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

    if ( filter && filter[0] != '\0' )
    {
        if ( filteredPosition == filteredIndex )
        {
            return NEW_SCENE_BROWSER_INDEX;
        }

        ++filteredPosition;
    }

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

    if ( filter && filter[0] != '\0' )
    {
        if ( optionIndex == NEW_SCENE_BROWSER_INDEX )
        {
            return filteredPosition;
        }

        ++filteredPosition;
    }

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


void CloseCombo( UISceneTabState& state )
{
    state.combo.Close();
    ClearFilter( state );
}

void CloseRecordingCombo( UISceneTabState& state )
{
    state.recordingCombo.Close();
}


void RequestFilterKeySync( UISceneTabState& state )
{
    state.filterKeySyncPending = true;
}


void ResetPreviewState( UISceneTabState& state )
{
    state.previewTimeScale = -1.0f;
    state.previewPredictionReveal = -1.0f;
}


void UpdateFilterTyping( UISceneTabState& state, InGameUIInputResult& result, const InputControl::UIInputSnapshot& input,
                         const char* const* sceneOptions, int sceneOptionCount )
{
    UIComboBox& combo = state.combo;

    if ( !combo.IsOpen() )
    {
        return;
    }

    if ( state.filterKeySyncPending )
    {
        InputControl::CaptureKeyStates( state.filterKeyWasDown, input );
        state.filterKeySyncPending = false;
        return;
    }

    for ( int key = 'A'; key <= 'Z'; ++key )
    {
        if ( ConsumeFilterKeyPress( state, input, key ) )
        {
            AppendFilterChar( state, static_cast<char>( 'a' + key - 'A' ) );
            result.commands.ui.userInteracted = true;
        }
    }

    for ( int key = '0'; key <= '9'; ++key )
    {
        if ( ConsumeFilterKeyPress( state, input, key ) )
        {
            AppendFilterChar( state, static_cast<char>( key ) );
            result.commands.ui.userInteracted = true;
        }
    }

    const bool isShiftDown = InputControl::IsVirtualKeyDown( input, VK_SHIFT );

    if ( ConsumeFilterKeyPress( state, input, VK_SPACE ) )
    {
        AppendFilterChar( state, ' ' );
        result.commands.ui.userInteracted = true;
    }

    if ( ConsumeFilterKeyPress( state, input, VK_OEM_MINUS ) )
    {
        AppendFilterChar( state, isShiftDown ? '_' : '-' );
        result.commands.ui.userInteracted = true;
    }

    if ( ConsumeFilterKeyPress( state, input, VK_OEM_PERIOD ) )
    {
        AppendFilterChar( state, '.' );
        result.commands.ui.userInteracted = true;
    }

    if ( ConsumeFilterKeyPress( state, input, VK_BACK ) )
    {
        BackspaceFilter( state );
        result.commands.ui.userInteracted = true;
    }

    if ( ConsumeFilterKeyPress( state, input, VK_DELETE ) )
    {
        ClearFilter( state );
        result.commands.ui.userInteracted = true;
    }

    if ( ConsumeFilterKeyPress( state, input, VK_ESCAPE ) )
    {
        if ( state.filter[0] != '\0' )
        {
            ClearFilter( state );
        }
        else
        {
            CloseCombo( state );
        }

        result.commands.ui.userInteracted = true;
    }

    if ( ConsumeFilterKeyPress( state, input, VK_RETURN ) && combo.IsOpen() )
    {
        int sceneIndex = FindExactOptionIndex( sceneOptions, sceneOptionCount, state.filter );

        if ( sceneIndex < 0 )
        {
            sceneIndex = FindFilteredOptionIndex( sceneOptions, sceneOptionCount, state.filter, 0 );
        }

        if ( sceneIndex == NEW_SCENE_BROWSER_INDEX )
        {
            RequestNewScene( state, result );
            CloseCombo( state );
        }
        else if ( sceneIndex == DEMO_SCENE_BROWSER_INDEX )
        {
            result.commands.scene.requestDemoScene = true;
            CloseCombo( state );
            result.commands.ui.userInteracted = true;
        }
        else if ( sceneIndex >= 0 )
        {
            result.commands.scene.requestedSceneIndex = sceneIndex;
            CloseCombo( state );
            result.commands.ui.userInteracted = true;
        }
        else if ( state.filter[0] != '\0' )
        {
            RequestNewScene( state, result );
            CloseCombo( state );
        }
    }
}


bool HandleComboWheel( UISceneTabState& state, const char* const* sceneOptions, int sceneOptionCount, int mouseX, int mouseY,
                       int wheelDelta, float contentX, float rowBase, float contentW )
{
    UIComboBox& combo = state.combo;

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


bool HandleOpenComboClick( UISceneTabState& state, InGameUIInputResult& result, const char* const* sceneOptions,
                           int sceneOptionCount, int mouseX, int mouseY, float contentX, float rowBase, float contentW )
{
    if ( !state.combo.IsOpen() )
    {
        return false;
    }

    const int filteredSceneCount = CountFilteredOptions( sceneOptions, sceneOptionCount, state.filter );
    const int visibleSceneOptions = SceneComboVisibleCount( filteredSceneCount );
    const int sceneDrawOptions = filteredSceneCount > 0 ? visibleSceneOptions : ( state.filter[0] != '\0' ? 1 : 0 );
    state.comboScroll = ClampSceneComboScroll( state.comboScroll, filteredSceneCount );
    SetSceneHeaderBounds( state.combo, state.resetSceneButton, state.resetDefaultsButton, state.saveDefaultsButton, contentX,
                          rowBase, contentW );

    const int option = state.combo.HitOption( mouseX, mouseY, sceneDrawOptions );

    if ( state.resetSceneButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.resetScene = true;
        CloseCombo( state );
    }
    else if ( state.resetDefaultsButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.resetSceneDefaults = true;
        CloseCombo( state );
    }
    else if ( state.saveDefaultsButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.saveSceneDefaults = true;
        CloseCombo( state );
    }
    else if ( filteredSceneCount > 0 && option >= 0 && option < visibleSceneOptions )
    {
        const int sceneIndex = FindFilteredOptionIndex( sceneOptions, sceneOptionCount, state.filter,
                                                        state.comboScroll + option );

        if ( sceneIndex == NEW_SCENE_BROWSER_INDEX )
        {
            RequestNewScene( state, result );
        }
        else if ( sceneIndex == DEMO_SCENE_BROWSER_INDEX )
        {
            result.commands.scene.requestDemoScene = true;
        }
        else if ( sceneIndex >= 0 )
        {
            result.commands.scene.requestedSceneIndex = sceneIndex;
        }

        CloseCombo( state );
    }
    else
    {
        CloseCombo( state );
    }

    return true;
}


bool HandleHeaderClick( UISceneTabState& state, InGameUIInputResult& result, int mouseX, int mouseY, float contentX,
                        float rowBase, float contentW )
{
    // Invariant: Scene selection, reset, and save buttons return command
    // intents. Scene load/reset side effects stay outside UI.
    SetSceneHeaderBounds( state.combo, state.resetSceneButton, state.resetDefaultsButton, state.saveDefaultsButton, contentX,
                          rowBase, contentW );

    if ( state.resetSceneButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.resetScene = true;
        CloseCombo( state );
        return true;
    }

    if ( state.resetDefaultsButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.resetSceneDefaults = true;
        CloseCombo( state );
        return true;
    }

    if ( state.saveDefaultsButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.scene.saveSceneDefaults = true;
        CloseCombo( state );
        return true;
    }

    return false;
}


bool HandleClosedComboClick( UISceneTabState& state, const InputControl::UIInputSnapshot& input,
                             const char* const* sceneOptions, int sceneOptionCount, int selectedSceneOption, int mouseX,
                             int mouseY )
{
    UIComboBox& combo = state.combo;

    // Invariant: HandleHeaderClick establishes the shared draw/hit-test bounds
    // before this closed-combo action runs.
    if ( combo.HitBox( mouseX, mouseY ) )
    {
        ClearFilter( state );
        InputControl::CaptureKeyStates( state.filterKeyWasDown, input );
        state.filterKeySyncPending = false;
        const int filteredSceneCount = CountFilteredOptions( sceneOptions, sceneOptionCount, state.filter );
        state.comboScroll = SceneComboScrollForSelection( FilteredPositionForIndex( sceneOptions, sceneOptionCount,
                                                                                    state.filter, selectedSceneOption ),
                                                          filteredSceneCount );

        combo.SetOpen( true );
        return true;
    }

    return false;
}

bool HandleRecordingComboWheel( UISceneTabState& state, int recordingOptionCount, int mouseX, int mouseY, int wheelDelta,
                                float contentX, float rowBase, float contentW )
{
    if ( wheelDelta == 0 || !state.recordingCombo.IsOpen() )
    {
        return false;
    }

    const int visibleOptions = SceneComboVisibleCount( recordingOptionCount );
    SetRecordingComboBounds( state.recordingCombo, contentX, rowBase, contentW );

    if ( state.recordingCombo.HitBox( mouseX, mouseY ) ||
         state.recordingCombo.HitOption( mouseX, mouseY, visibleOptions ) >= 0 )
    {
        state.recordingComboScroll = ClampSceneComboScroll( state.recordingComboScroll - wheelDelta / WHEEL_DELTA,
                                                            recordingOptionCount );
        return true;
    }

    return false;
}

bool HandleOpenRecordingComboClick( UISceneTabState& state, InGameUIInputResult& result, int recordingOptionCount,
                                    int mouseX, int mouseY, float contentX, float rowBase, float contentW )
{
    if ( !state.recordingCombo.IsOpen() )
    {
        return false;
    }

    const int visibleOptions = SceneComboVisibleCount( recordingOptionCount );
    state.recordingComboScroll = ClampSceneComboScroll( state.recordingComboScroll, recordingOptionCount );
    SetRecordingComboBounds( state.recordingCombo, contentX, rowBase, contentW );
    const int option = state.recordingCombo.HitOption( mouseX, mouseY, visibleOptions );

    if ( option >= 0 && option < visibleOptions )
    {
        result.commands.scene.requestedInteractionRecordingIndex = state.recordingComboScroll + option;
        result.commands.ui.userInteracted = true;
    }

    CloseRecordingCombo( state );
    return true;
}

bool HandleClosedRecordingComboClick( UISceneTabState& state, int recordingOptionCount, int selectedRecordingOption,
                                      int mouseX, int mouseY, float contentX, float rowBase, float contentW )
{
    SetRecordingComboBounds( state.recordingCombo, contentX, rowBase, contentW );

    if ( recordingOptionCount <= 0 || !state.recordingCombo.HitBox( mouseX, mouseY ) )
    {
        return false;
    }

    state.recordingComboScroll = SceneComboScrollForSelection( selectedRecordingOption, recordingOptionCount );
    state.recordingCombo.SetOpen( true );
    CloseCombo( state );
    return true;
}


bool HandleTimeScaleClick( UISceneTabState& state, InGameUIInputResult& result, int& activeSlider, int mouseX, int mouseY,
                           float contentX, float rowBase, float contentW )
{
    state.timeScaleSlider.SetBounds( contentX, rowBase + ( UI_SCENE_TIME_SCALE_SLIDER_Y - 42.0f ), contentW, 34.0f );

    if ( state.timeScaleSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_TIME_SCALE;
        state.previewTimeScale = state.timeScaleSlider.ValueFromMouse( mouseX, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX,
                                                                       UI_TIME_SCALE_STEP );

        result.commands.sceneOptions.requestedTimeScale = state.previewTimeScale;
        return true;
    }

    state.predictionRevealSlider.SetBounds( contentX, rowBase + ( UI_SCENE_PREDICTION_REVEAL_SLIDER_Y - 42.0f ), contentW,
                                            34.0f );

    if ( state.predictionRevealSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_PREDICTION_REVEAL;
        state.previewPredictionReveal = state.predictionRevealSlider.ValueFromMouse( mouseX, 0.0f, 1.0f, 0.0f );
        result.commands.physics.requestPredictionRevealRate = true;

        result.commands.physics.requestedPredictionRevealRate = PredictionRevealRateFromNormalized(
            state.previewPredictionReveal );

        return true;
    }

    return false;
}

bool HandleForecastClick( UISceneTabState& state, InGameUIInputResult& result, int mouseX, int mouseY, float contentX,
                          float rowBase, float contentW )
{
    SetForecastBounds( state, contentX, rowBase, contentW );

    if ( state.continuousForecastToggle.HitTest( mouseX, mouseY ) )
    {
        result.commands.forecast.type = UIForecastCommandType::ToggleContinuous;
    }
    else if ( state.resetForecastButton.HitTest( mouseX, mouseY ) )
    {
        result.commands.forecast.type = UIForecastCommandType::Reset;
    }
    else
    {
        return false;
    }

    result.commands.ui.userInteracted = true;
    return true;
}


bool UpdateActiveSlider( UISceneTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result )
{
    if ( activeSlider == SLIDER_PREDICTION_REVEAL )
    {
        state.previewPredictionReveal = state.predictionRevealSlider.ValueFromMouse( mouseX, 0.0f, 1.0f, 0.0f );
        result.commands.physics.requestPredictionRevealRate = true;

        result.commands.physics.requestedPredictionRevealRate = PredictionRevealRateFromNormalized(
            state.previewPredictionReveal );

        return true;
    }

    if ( activeSlider != SLIDER_TIME_SCALE )
    {
        return false;
    }

    state.previewTimeScale = state.timeScaleSlider.ValueFromMouse( mouseX, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX,
                                                                   UI_TIME_SCALE_STEP );

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

    if ( activeSlider == SLIDER_PREDICTION_REVEAL && state.previewPredictionReveal >= 0.0f )
    {
        result.commands.physics.requestPredictionRevealRate = true;

        result.commands.physics.requestedPredictionRevealRate = PredictionRevealRateFromNormalized(
            state.previewPredictionReveal );

        return true;
    }

    return false;
}


void Draw( UISceneTabState& state, const UIDrawContext& draw, const UISceneTabFrameView& data, float contentX,
           float contentY, float contentW, float contentH, float scrolledY, int mouseX, int mouseY )
{
    char buf[160];
    char filterDisplay[80] = {};

    const bool sceneFilterActive = state.filter[0] != '\0';
    const int filteredSceneCount = CountFilteredOptions( data.sceneOptions, data.sceneOptionCount, state.filter );
    const int sceneVisibleCount = SceneComboVisibleCount( filteredSceneCount );
    state.comboScroll = ClampSceneComboScroll( state.comboScroll, filteredSceneCount );
    const int sceneFirstOption = state.comboScroll;
    const int selectedFilteredPosition = FilteredPositionForIndex( data.sceneOptions, data.sceneOptionCount, state.filter,
                                                                   data.selectedSceneOption );

    const int sceneSelectedInSlice = selectedFilteredPosition >= sceneFirstOption &&
                                             selectedFilteredPosition < sceneFirstOption + sceneVisibleCount
                                         ? selectedFilteredPosition - sceneFirstOption
                                         : -1;

    const char* visibleSceneOptions[UI_SCENE_COMBO_VISIBLE_OPTIONS] = {};

    for ( int i = 0; i < sceneVisibleCount; ++i )
    {
        const int sceneIndex = FindFilteredOptionIndex( data.sceneOptions, data.sceneOptionCount, state.filter,
                                                        sceneFirstOption + i );

        if ( sceneIndex == NEW_SCENE_BROWSER_INDEX )
        {
            visibleSceneOptions[i] = NEW_SCENE_OPTION;
        }
        else
        {
            visibleSceneOptions[i] = sceneIndex == DEMO_SCENE_BROWSER_INDEX
                                         ? DEMO_SCENE_OPTION
                                         : ( sceneIndex >= 0 ? data.sceneOptions[sceneIndex] : "" );
        }
    }

    int sceneDrawCount = sceneVisibleCount;
    const char* selectedSceneName = DEMO_SCENE_OPTION;

    if ( data.sceneOptions && data.selectedSceneOption >= 0 && data.selectedSceneOption < data.sceneOptionCount )
    {
        selectedSceneName = data.sceneOptions[data.selectedSceneOption];
    }

    if ( state.combo.IsOpen() && sceneFilterActive )
    {
        snprintf( filterDisplay, sizeof( filterDisplay ), "%s", state.filter );
        selectedSceneName = filterDisplay;
    }

    const int recordingVisibleCount = SceneComboVisibleCount( data.interactionRecordingOptionCount );
    state.recordingComboScroll = ClampSceneComboScroll( state.recordingComboScroll, data.interactionRecordingOptionCount );
    const char* visibleRecordingOptions[UI_SCENE_COMBO_VISIBLE_OPTIONS] = {};

    for ( int i = 0; i < recordingVisibleCount; ++i )
    {
        visibleRecordingOptions[i] = data.interactionRecordingOptions[state.recordingComboScroll + i];
    }

    const char* selectedRecordingName = "No recordings";

    if ( data.interactionRecordingOptions && data.selectedInteractionRecordingOption >= 0 &&
         data.selectedInteractionRecordingOption < data.interactionRecordingOptionCount )
    {
        selectedRecordingName = data.interactionRecordingOptions[data.selectedInteractionRecordingOption];
    }

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Scene" );
    SetSceneHeaderBounds( state.combo, state.resetSceneButton, state.resetDefaultsButton, state.saveDefaultsButton, contentX,
                          scrolledY + 42.0f, contentW );
    SetRecordingComboBounds( state.recordingCombo, contentX, scrolledY + 42.0f, contentW );

    if ( data.targetFrameCount > 0 )
    {
        const int displayedFrame = ( data.testComplete && data.currentFrame > data.targetFrameCount ) ? data.targetFrameCount
                                                                                                      : data.currentFrame;

        snprintf( buf, sizeof( buf ), "%d / %d", displayedFrame, data.targetFrameCount );
    }
    else
    {
        snprintf( buf, sizeof( buf ), "%d", data.currentFrame );
    }

    if ( !state.combo.IsOpen() && !state.recordingCombo.IsOpen() )
    {
        const float sceneCol2 = contentX + (std::max)( 208.0f, contentW * 0.48f );
        const Style::UIPalette& palette = Style::Palette();
        const float displayTimeScale = state.previewTimeScale > 0.0f ? state.previewTimeScale : data.timeScale;
        char statusBuf[64] = {};

        snprintf( statusBuf, sizeof( statusBuf ), "%s / capture lockstep %s", data.testComplete ? "complete" : "running",
                  data.fixedStep ? "on" : "off" );

        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 104.0f, "Renderer", data.rendererName,
                          palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b );

        DrawLabelValueAt( draw, contentY, contentH, sceneCol2, scrolledY + 104.0f, "Status", statusBuf, palette.accent.r,
                          palette.accent.g, palette.accent.b );

        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 128.0f, "Frame", buf, palette.textPrimary.r,
                          palette.textPrimary.g, palette.textPrimary.b );

        snprintf( buf, sizeof( buf ), "%.1f FPS", data.fps );
        DrawLabelValueAt( draw, contentY, contentH, sceneCol2, scrolledY + 128.0f, "Frame rate", buf, palette.accentStrong.r,
                          palette.accentStrong.g, palette.accentStrong.b );

        snprintf( buf, sizeof( buf ), "%d / %d", data.currentSceneIndex + 1, data.sceneCount );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 152.0f, "Scene index", buf, palette.textPrimary.r,
                          palette.textPrimary.g, palette.textPrimary.b );

        snprintf( buf, sizeof( buf ), "%.6f", data.sceneEnergy );
        DrawLabelValueAt( draw, contentY, contentH, sceneCol2, scrolledY + 152.0f, "Kinetic energy", buf,
                          palette.warningAccent.r, palette.warningAccent.g, palette.warningAccent.b );

        snprintf( buf, sizeof( buf ), "%d", data.modelCount );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 176.0f, "Model count", buf, palette.textPrimary.r,
                          palette.textPrimary.g, palette.textPrimary.b );

        snprintf( buf, sizeof( buf ), "%.2fx", displayTimeScale );
        state.timeScaleSlider.SetBounds( contentX, scrolledY + UI_SCENE_TIME_SCALE_SLIDER_Y, contentW, 34.0f );

        if ( IsRowVisible( contentY, contentH, scrolledY + UI_SCENE_TIME_SCALE_SLIDER_Y, 34.0f ) )
        {
            state.timeScaleSlider.Draw( draw, "Simulation speed", buf, displayTimeScale, UI_TIME_SCALE_MIN,
                                        UI_TIME_SCALE_MAX );
        }

        // Why: the row reads in rate units an operator can reason about, while
        // the track position stays normalized so the exponential mapping is what
        // moves under the handle.
        const float displayRevealRate = ( state.previewPredictionReveal >= 0.0f )
                                            ? PredictionRevealRateFromNormalized( state.previewPredictionReveal )
                                            : data.predictionRevealRate;

        if ( displayRevealRate >= REPLAY_PREDICTION_REVEAL_RATE_MAX )
        {
            snprintf( buf, sizeof( buf ), "Instant" );
        }
        else
        {
            snprintf( buf, sizeof( buf ), "%.1fx", static_cast<double>( displayRevealRate ) );
        }

        state.predictionRevealSlider.SetBounds( contentX, scrolledY + UI_SCENE_PREDICTION_REVEAL_SLIDER_Y, contentW, 34.0f );

        if ( IsRowVisible( contentY, contentH, scrolledY + UI_SCENE_PREDICTION_REVEAL_SLIDER_Y, 34.0f ) )
        {
            state.predictionRevealSlider.Draw( draw, "Reveal speed", buf,
                                               NormalizedFromPredictionRevealRate( displayRevealRate ), 0.0f, 1.0f );
        }

        const UISceneForecastFrameView& forecast = data.forecast;
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + UI_SCENE_FORECAST_TITLE_Y, 12.0f,
                          "Continuous orbital forecast" );
        SetForecastBounds( state, contentX, scrolledY + 42.0f, contentW );

        if ( IsRowVisible( contentY, contentH, scrolledY + UI_SCENE_FORECAST_BUTTON_Y, 24.0f ) )
        {
            const Style::UIColor& accent = Style::Accent();
            state.continuousForecastToggle.DrawToggle( draw, "Rolling prediction", forecast.active, accent.r, accent.g,
                                                       accent.b );
            state.resetForecastButton.Draw( draw, "Reset forecast", mouseX, mouseY );
        }

        const float forecastCol2 = contentX + (std::max)( 208.0f, contentW * 0.48f );
        snprintf( buf, sizeof( buf ), "%.2fs", forecast.simulatedSeconds );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 346.0f, "Simulated", buf, palette.accentStrong.r,
                          palette.accentStrong.g, palette.accentStrong.b );
        snprintf( buf, sizeof( buf ), "%.1fx", forecast.simulatedSecondsPerRealSecond );
        DrawLabelValueAt( draw, contentY, contentH, forecastCol2, scrolledY + 346.0f, "Sim / real", buf, palette.accent.r,
                          palette.accent.g, palette.accent.b );

        snprintf( buf, sizeof( buf ), "%.2fs", forecast.rollingWindowAgeSeconds );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 372.0f, "Window age", buf, palette.textPrimary.r,
                          palette.textPrimary.g, palette.textPrimary.b );
        snprintf( buf, sizeof( buf ), "%s / %s", forecast.available ? "available" : "unavailable",
                  forecast.failed ? "failed" : ( forecast.workerInFlight ? "running" : "idle" ) );
        DrawLabelValueAt( draw, contentY, contentH, forecastCol2, scrolledY + 372.0f, "Producer", buf,
                          forecast.failed ? palette.warningAccent.r : palette.textPrimary.r,
                          forecast.failed ? palette.warningAccent.g : palette.textPrimary.g,
                          forecast.failed ? palette.warningAccent.b : palette.textPrimary.b );

        if ( forecast.configured )
        {
            snprintf( buf, sizeof( buf ), "numeric %s / system %s / auxiliary %s", forecast.numericalHealthy ? "ok" : "fail",
                      forecast.systemOrbitalHealthy ? "ok" : "fail", forecast.auxiliaryOrbitalHealthy ? "ok" : "fail" );
        }
        else
        {
            snprintf( buf, sizeof( buf ), "not started" );
        }

        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 398.0f, "Stability", buf, palette.textPrimary.r,
                          palette.textPrimary.g, palette.textPrimary.b );

        if ( forecast.firstFailureCause == OperatorEditorForecastCause::None )
        {
            snprintf( buf, sizeof( buf ), "none" );
        }
        else
        {
            snprintf( buf, sizeof( buf ), "%s @ %.2fs (%u/%u)",
                      OperatorEditorForecastCauseName( forecast.firstFailureCause ), forecast.firstFailureSeconds,
                      forecast.firstFailureSubject, forecast.firstFailureOther );
        }

        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 424.0f, "First cause", buf,
                          palette.warningAccent.r, palette.warningAccent.g, palette.warningAccent.b );
        char energyBuf[48] = "unavailable";
        char angularBuf[48] = "unavailable";

        if ( forecast.energyDriftAvailable )
        {
            snprintf( energyBuf, sizeof( energyBuf ), "%.3e (max %.3e)", forecast.energyDrift,
                      forecast.maximumAbsoluteEnergyDrift );
        }

        if ( forecast.angularMomentumDriftAvailable )
        {
            snprintf( angularBuf, sizeof( angularBuf ), "%.3e (max %.3e)", forecast.angularMomentumDrift,
                      forecast.maximumAngularMomentumDrift );
        }

        snprintf( buf, sizeof( buf ), "E %s / L %s", energyBuf, angularBuf );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 450.0f, "Conservation", buf, palette.textPrimary.r,
                          palette.textPrimary.g, palette.textPrimary.b );
    }

    if ( IsRowVisible( contentY, contentH, scrolledY + 42.0f, 24.0f ) )
    {
        state.combo.Draw( draw, "Load scene",
                          { std::span<const char* const>( visibleSceneOptions, static_cast<std::size_t>( sceneDrawCount ) ),
                            sceneSelectedInSlice, 0u, selectedSceneName },
                          { mouseX, mouseY } );
    }


    if ( !state.combo.IsOpen() && IsRowVisible( contentY, contentH, scrolledY + UI_SCENE_RECORDING_COMBO_Y, 24.0f ) )
    {
        const int selectedInSlice = data.selectedInteractionRecordingOption >= state.recordingComboScroll &&
                                            data.selectedInteractionRecordingOption <
                                                state.recordingComboScroll + recordingVisibleCount
                                        ? data.selectedInteractionRecordingOption - state.recordingComboScroll
                                        : -1;
        state.recordingCombo.Draw( draw, "Replay",
                                   { std::span<const char* const>( visibleRecordingOptions,
                                                                   static_cast<std::size_t>( recordingVisibleCount ) ),
                                     selectedInSlice, 0u, selectedRecordingName },
                                   { mouseX, mouseY } );
    }

    if ( IsRowVisible( contentY, contentH, scrolledY + 42.0f, 24.0f ) )
    {
        state.resetSceneButton.Draw( draw, "Reset", mouseX, mouseY );
        state.resetDefaultsButton.Draw( draw, "Reset Defaults", mouseX, mouseY );
        state.saveDefaultsButton.Draw( draw, "Save Defaults", mouseX, mouseY );
    }
}

} // namespace SceneTab
} // namespace UI
} // namespace SkullbonezCore
