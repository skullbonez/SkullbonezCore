/*
File: SkullbonezSource/UI/UITabSound.cpp
Purpose:
  Implements the minimal in-engine Sound tab.

Summary:
  Drawing and hit testing share one layout. Sliders write previews locally
  while dragging and emit a one-frame command for the run loop to apply to
  the audio service. Nothing here persists config files or mutates audio
  objects directly.

Concept — what the two sliders mean:
  Volume is a plain multiplier on every thud. Thud threshold is the minimum
  impact energy (joules — think "how hard the hit was") a collision needs
  before any sound plays; raising it silences light taps, lowering it lets
  soft touches through. Distance falloff, pitch spread, cooldowns, and voice
  limits are fixed policy inside ContactAudioService and are deliberately
  not exposed here.

Glossary:
  Sample library: Decoded candidate thud sounds that can be previewed with
    the Play button or assigned to the selected material set with Use.
  Sound set: Material-pair recipe (e.g. stone vs anything) that maps a
    contact to a sample list; the picker exists so Use knows its target.
  Fitted picker text: One-line selector label clipped before the
    right-aligned previous/next buttons.

Invariants:
  - Selected set/sample indices are clamped against the current frame
    snapshot before hit testing or drawing.
  - The threshold slider edits joules directly; no unit conversion happens
    in the UI layer.

Related:
  - SkullbonezSource/UI/UITabSound.h
  - SkullbonezSource/Runtime/Audio/ContactAudioService.h
*/
#include "UITabSound.h"

#include "UI.h"
#include "UIDrawWidgets.h"
#include "UILayout.h"
#include "../Rendering/Text.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::UI::Widgets;
using UISoundParam = SkullbonezCore::UI::UISoundParam;

namespace
{

constexpr int UI_SOUND_GLOBAL_SLIDER_BASE = 8000;
constexpr float SOUND_HEADER_Y = 16.0f;
constexpr float SOUND_TOGGLE_Y = 42.0f;
constexpr float SOUND_STATS_Y = 78.0f;
constexpr float SOUND_GLOBAL_TITLE_Y = 118.0f;
constexpr float SOUND_GLOBAL_SLIDER_Y = 144.0f;
constexpr float SOUND_SLIDER_H = 34.0f;
constexpr float SOUND_SLIDER_STEP_Y = 40.0f;
constexpr float SOUND_SAMPLE_TITLE_Y = 240.0f;
constexpr float SOUND_SAMPLE_PICKER_Y = 266.0f;
constexpr float SOUND_SAMPLE_ACTION_Y = 300.0f;
constexpr float SOUND_SAMPLE_BUTTON_W = 72.0f;
constexpr float SOUND_SAMPLE_BUTTON_H = 26.0f;
constexpr float SOUND_SET_TITLE_Y = 350.0f;
constexpr float SOUND_SET_PICKER_Y = 376.0f;
constexpr float SOUND_SET_META_Y = 410.0f;
constexpr float SOUND_CONTENT_BOTTOM_Y = 440.0f;

struct SoundSliderSpec
{
    UISoundParam param;
    const char* label;
    const char* format;
    float minValue;
    float maxValue;
    float step;
};

constexpr SoundSliderSpec kGlobalSliders[] = {
    { UISoundParam::MasterGain, "Volume", "%.2f", 0.0f, 2.0f, 0.05f },
    { UISoundParam::MinImpactEnergy, "Thud threshold", "%.0f J", 10.0f, 2000.0f, 5.0f },
};

static_assert( sizeof( kGlobalSliders ) / sizeof( kGlobalSliders[0] ) ==
                   SkullbonezCore::UI::SoundTab::SOUND_GLOBAL_SLIDER_COUNT,
               "Global Sound tab slider specs must match Sound tab state." );

void DrawHitboxRect( const SkullbonezCore::UI::UIDrawContext& draw,
                     const SkullbonezCore::UI::UIRect& bounds,
                     float r,
                     float g,
                     float b )
{
    if ( bounds.w <= 0.0f || bounds.h <= 0.0f )
    {
        return;
    }
    draw.Rect( bounds.x, bounds.y, bounds.w, bounds.h, r, g, b, 0.060f );
    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, r, g, b, 0.94f );
}

int GlobalSliderIndexFromActiveSlider( int activeSlider )
{
    const int index = activeSlider - UI_SOUND_GLOBAL_SLIDER_BASE;
    return index >= 0 && index < SkullbonezCore::UI::SoundTab::SOUND_GLOBAL_SLIDER_COUNT ? index : -1;
}

const SkullbonezCore::UI::UISoundSetFrameData* SelectedSet( const SkullbonezCore::UI::SoundTab::UISoundTabState& state,
                                                            const SkullbonezCore::UI::InGameUIFrameData& data )
{
    if ( data.soundSetCount <= 0 )
    {
        return nullptr;
    }
    const int setIndex = std::clamp( state.selectedSetIndex, 0, data.soundSetCount - 1 );
    return &data.soundSets[setIndex];
}

const char* SampleLeafName( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        return "No samples loaded";
    }
    const char* slash = strrchr( path, '/' );
    const char* backslash = strrchr( path, '\\' );
    const char* leaf = slash && backslash ? ( slash > backslash ? slash : backslash ) : ( slash ? slash : backslash );
    return leaf ? leaf + 1 : path;
}

void EllipsizeToWidth( char* text, size_t textSize, float pxSize, float maxWidth )
{
    if ( !text || textSize == 0 )
    {
        return;
    }
    if ( maxWidth <= 0.0f )
    {
        text[0] = '\0';
        return;
    }
    if ( SkullbonezCore::Text::Text2d::MeasureText( pxSize, text ) <= maxWidth )
    {
        return;
    }

    size_t len = strlen( text );
    while ( len > 3 && SkullbonezCore::Text::Text2d::MeasureText( pxSize, text ) > maxWidth )
    {
        text[len - 3] = '.';
        text[len - 2] = '.';
        text[len - 1] = '.';
        text[len] = '\0';
        --len;
    }
    if ( SkullbonezCore::Text::Text2d::MeasureText( pxSize, text ) > maxWidth )
    {
        text[0] = '\0';
    }
}


void DrawFittedPickerText( const SkullbonezCore::UI::UIDrawContext& draw,
                           float x,
                           float y,
                           float pxSize,
                           float r,
                           float g,
                           float b,
                           const char* value,
                           float maxWidth )
{
    // Why: Sound picker buttons are right-aligned in narrow tabs, so labels must
    // fit the left-side row space instead of drawing past the window edge.
    if ( maxWidth <= 6.0f )
    {
        return;
    }

    char text[160] = {};
    snprintf( text, sizeof( text ), "%s", value ? value : "" );
    EllipsizeToWidth( text, sizeof( text ), pxSize, maxWidth );
    if ( text[0] != '\0' )
    {
        draw.Text( x, y, pxSize, r, g, b, text );
    }
}


float GlobalDisplayValue( const SkullbonezCore::UI::SoundTab::UISoundTabState& state,
                          const SkullbonezCore::UI::InGameUIFrameData& data,
                          int activeSlider,
                          int index )
{
    if ( activeSlider == UI_SOUND_GLOBAL_SLIDER_BASE + index && state.previewGlobalValues[index] >= 0.0f )
    {
        return state.previewGlobalValues[index];
    }
    switch ( kGlobalSliders[index].param )
    {
    case UISoundParam::MasterGain:
        return data.contactAudioMasterGain;
    case UISoundParam::MinImpactEnergy:
        return data.contactAudioMinImpactEnergy;
    default:
        return 0.0f;
    }
}

void SetGlobalSliderResult( SkullbonezCore::UI::SoundTab::UISoundTabState& state,
                            SkullbonezCore::UI::InGameUIInputResult& result,
                            int sliderIndex,
                            int mouseX )
{
    const SoundSliderSpec& spec = kGlobalSliders[sliderIndex];
    const float value =
        state.globalSliders[sliderIndex].ValueFromMouse( mouseX, spec.minValue, spec.maxValue, spec.step );
    state.previewGlobalValues[sliderIndex] = value;
    result.commands.sound.requestedParam = spec.param;
    result.commands.sound.requestedValue = value;
}

void SetContentBounds( SkullbonezCore::UI::SoundTab::UISoundTabState& state,
                       float contentX,
                       float scrolledY,
                       float contentW )
{
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    state.enabledToggle.SetBounds( contentX, scrolledY + SOUND_TOGGLE_Y, colW, 24.0f );
    for ( int i = 0; i < SkullbonezCore::UI::SoundTab::SOUND_GLOBAL_SLIDER_COUNT; ++i )
    {
        state.globalSliders[i].SetBounds( contentX,
                                          scrolledY + SOUND_GLOBAL_SLIDER_Y +
                                              static_cast<float>( i ) * SOUND_SLIDER_STEP_Y,
                                          contentW,
                                          SOUND_SLIDER_H );
    }
    SetPipelineStepButtonBounds( state.previousSampleButton,
                                 state.nextSampleButton,
                                 contentX,
                                 contentW,
                                 scrolledY + SOUND_SAMPLE_PICKER_Y );
    state.previewSampleButton.SetBounds( contentX,
                                         scrolledY + SOUND_SAMPLE_ACTION_Y,
                                         SOUND_SAMPLE_BUTTON_W,
                                         SOUND_SAMPLE_BUTTON_H );
    state.selectSampleButton.SetBounds( contentX + SOUND_SAMPLE_BUTTON_W + 10.0f,
                                        scrolledY + SOUND_SAMPLE_ACTION_Y,
                                        SOUND_SAMPLE_BUTTON_W,
                                        SOUND_SAMPLE_BUTTON_H );
    SetPipelineStepButtonBounds( state.previousSetButton,
                                 state.nextSetButton,
                                 contentX,
                                 contentW,
                                 scrolledY + SOUND_SET_PICKER_Y );
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace SoundTab
{

int ContentHeight()
{
    return static_cast<int>( SOUND_CONTENT_BOTTOM_Y );
}


void ResetPreviewState( UISoundTabState& state )
{
    for ( float& value : state.previewGlobalValues )
    {
        value = -1.0f;
    }
}


void ClampSelection( UISoundTabState& state, const InGameUIFrameData& data )
{
    if ( data.soundSetCount <= 0 )
    {
        state.selectedSetIndex = 0;
        state.lastSetCount = 0;
    }
    else
    {
        state.selectedSetIndex = std::clamp( state.selectedSetIndex, 0, data.soundSetCount - 1 );
        state.lastSetCount = std::clamp( data.soundSetCount, 0, UI_SOUND_SET_MAX );
    }
    if ( data.soundSampleCount <= 0 )
    {
        state.selectedSampleIndex = 0;
        state.lastSampleCount = 0;
    }
    else
    {
        state.selectedSampleIndex = std::clamp( state.selectedSampleIndex, 0, data.soundSampleCount - 1 );
        state.lastSampleCount = std::clamp( data.soundSampleCount, 0, UI_SOUND_SAMPLE_MAX );
    }
}


void DrawHitboxes( const UISoundTabState& state,
                   const UIDrawContext& draw,
                   const InGameUIFrameData& data,
                   float r,
                   float g,
                   float b )
{
    (void)data;
    DrawHitboxRect( draw, state.enabledToggle.Bounds(), r, g, b );
    for ( const UISlider& slider : state.globalSliders )
    {
        DrawHitboxRect( draw, slider.Bounds(), r, g, b );
    }
    DrawHitboxRect( draw, state.previousSampleButton, 1.0f, 0.62f, 0.18f );
    DrawHitboxRect( draw, state.nextSampleButton, 1.0f, 0.62f, 0.18f );
    DrawHitboxRect( draw, state.previewSampleButton.Bounds(), r, g, b );
    DrawHitboxRect( draw, state.selectSampleButton.Bounds(), r, g, b );
    DrawHitboxRect( draw, state.previousSetButton, 1.0f, 0.62f, 0.18f );
    DrawHitboxRect( draw, state.nextSetButton, 1.0f, 0.62f, 0.18f );
}


bool HandleContentClick( UISoundTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float scrolledY,
                         float contentW )
{
    if ( state.lastSetCount <= 0 )
    {
        state.selectedSetIndex = 0;
    }
    else
    {
        state.selectedSetIndex = std::clamp( state.selectedSetIndex, 0, state.lastSetCount - 1 );
    }
    if ( state.lastSampleCount <= 0 )
    {
        state.selectedSampleIndex = 0;
    }
    else
    {
        state.selectedSampleIndex = std::clamp( state.selectedSampleIndex, 0, state.lastSampleCount - 1 );
    }
    SetContentBounds( state, contentX, scrolledY, contentW );

    if ( state.enabledToggle.HitTest( mouseX, mouseY ) )
    {
        result.commands.sound.toggleEnabled = true;
        return false;
    }
    for ( int i = 0; i < SOUND_GLOBAL_SLIDER_COUNT; ++i )
    {
        if ( state.globalSliders[i].HitTest( mouseX, mouseY ) )
        {
            activeSlider = UI_SOUND_GLOBAL_SLIDER_BASE + i;
            SetGlobalSliderResult( state, result, i, mouseX );
            return true;
        }
    }
    if ( state.previousSampleButton.Contains( mouseX, mouseY ) && state.lastSampleCount > 0 )
    {
        state.selectedSampleIndex = ( state.selectedSampleIndex + state.lastSampleCount - 1 ) % state.lastSampleCount;
        return false;
    }
    if ( state.nextSampleButton.Contains( mouseX, mouseY ) && state.lastSampleCount > 0 )
    {
        state.selectedSampleIndex = ( state.selectedSampleIndex + 1 ) % state.lastSampleCount;
        return false;
    }
    if ( state.previewSampleButton.HitTest( mouseX, mouseY ) && state.lastSampleCount > 0 )
    {
        result.commands.sound.previewSampleIndex = state.selectedSampleIndex;
        return false;
    }
    if ( state.selectSampleButton.HitTest( mouseX, mouseY ) && state.lastSampleCount > 0 && state.lastSetCount > 0 )
    {
        result.commands.sound.requestedSetIndex = state.selectedSetIndex;
        result.commands.sound.selectSampleIndex = state.selectedSampleIndex;
        return false;
    }
    if ( state.previousSetButton.Contains( mouseX, mouseY ) && state.lastSetCount > 0 )
    {
        state.selectedSetIndex = ( state.selectedSetIndex + state.lastSetCount - 1 ) % state.lastSetCount;
        return false;
    }
    if ( state.nextSetButton.Contains( mouseX, mouseY ) && state.lastSetCount > 0 )
    {
        state.selectedSetIndex = ( state.selectedSetIndex + 1 ) % state.lastSetCount;
        return false;
    }
    return false;
}


bool UpdateActiveSlider( UISoundTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result )
{
    const int globalIndex = GlobalSliderIndexFromActiveSlider( activeSlider );
    if ( globalIndex >= 0 )
    {
        SetGlobalSliderResult( state, result, globalIndex, mouseX );
        return true;
    }
    return false;
}


bool CommitActiveSlider( UISoundTabState& state, int activeSlider, InGameUIInputResult& result )
{
    const int globalIndex = GlobalSliderIndexFromActiveSlider( activeSlider );
    if ( globalIndex >= 0 && state.previewGlobalValues[globalIndex] >= 0.0f )
    {
        result.commands.sound.requestedParam = kGlobalSliders[globalIndex].param;
        result.commands.sound.requestedValue = state.previewGlobalValues[globalIndex];
        return true;
    }
    return false;
}


void Draw( UISoundTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int activeSlider,
           int mouseX,
           int mouseY )
{
    ClampSelection( state, data );
    const UISoundSetFrameData* set = SelectedSet( state, data );
    SetContentBounds( state, contentX, scrolledY, contentW );

    char buf[160];
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + SOUND_HEADER_Y, 16.0f, "Sound" );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.enabledToggle,
                       contentX,
                       scrolledY + SOUND_TOGGLE_Y,
                       colW,
                       "Contact audio",
                       data.contactAudioEnabled );

    // One read-only line replaces the old counter toggles: how many contact
    // facts arrived, how many thuds played, and why the rest stayed silent.
    const uint32_t quietContacts = data.contactAudioRejectedByMotion + data.contactAudioRejectedByEnergy +
                                   data.contactAudioRejectedByCooldown + data.contactAudioRejectedByDistance;
    snprintf( buf,
              sizeof( buf ),
              "%u facts  %u pairs  %u played  %u quiet  %u dropped",
              data.contactAudioEventsSeen,
              data.contactAudioPairCandidates,
              data.contactAudioSubmittedVoices,
              quietContacts,
              data.contactAudioDroppedVoices );
    DrawLabelValueAt( draw,
                      contentY,
                      contentH,
                      contentX,
                      scrolledY + SOUND_STATS_Y,
                      "Stats",
                      data.contactAudioAvailable ? buf : "Not available",
                      data.contactAudioAvailable ? 0.78f : 0.95f,
                      data.contactAudioAvailable ? 0.88f : 0.55f,
                      data.contactAudioAvailable ? 0.91f : 0.32f );

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + SOUND_GLOBAL_TITLE_Y, 12.0f, "Thud" );
    for ( int i = 0; i < SOUND_GLOBAL_SLIDER_COUNT; ++i )
    {
        const float sliderY = scrolledY + SOUND_GLOBAL_SLIDER_Y + static_cast<float>( i ) * SOUND_SLIDER_STEP_Y;
        const float value = GlobalDisplayValue( state, data, activeSlider, i );
        snprintf( buf, sizeof( buf ), kGlobalSliders[i].format, value );
        if ( IsRowVisible( contentY, contentH, sliderY, SOUND_SLIDER_H ) )
        {
            state.globalSliders[i].Draw( draw,
                                         kGlobalSliders[i].label,
                                         buf,
                                         value,
                                         kGlobalSliders[i].minValue,
                                         kGlobalSliders[i].maxValue );
        }
    }

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + SOUND_SAMPLE_TITLE_Y, 12.0f, "Sample Library" );
    if ( IsRowVisible( contentY, contentH, scrolledY + SOUND_SAMPLE_PICKER_Y, UI_PIPELINE_STEP_BUTTON_H ) )
    {
        DrawPipelineStepButton( draw,
                                state.previousSampleButton,
                                true,
                                data.soundSampleCount > 0 && state.previousSampleButton.Contains( mouseX, mouseY ) );
        DrawPipelineStepButton( draw,
                                state.nextSampleButton,
                                false,
                                data.soundSampleCount > 0 && state.nextSampleButton.Contains( mouseX, mouseY ) );
        const char* sampleName = "No samples loaded";
        if ( data.soundSampleCount > 0 )
        {
            sampleName = SampleLeafName( data.soundSamplePaths[state.selectedSampleIndex] );
        }
        snprintf( buf,
                  sizeof( buf ),
                  "%d / %d  %s",
                  data.soundSampleCount > 0 ? state.selectedSampleIndex + 1 : 0,
                  data.soundSampleCount,
                  sampleName );
        const float textMaxW = (std::max)( 0.0f, state.previousSampleButton.x - contentX - 12.0f );
        DrawFittedPickerText( draw,
                              contentX,
                              scrolledY + SOUND_SAMPLE_PICKER_Y + 5.0f,
                              11.0f,
                              0.84f,
                              0.92f,
                              0.94f,
                              buf,
                              textMaxW );
    }
    if ( data.soundSampleCount > 0 &&
         IsRowVisible( contentY, contentH, scrolledY + SOUND_SAMPLE_ACTION_Y, SOUND_SAMPLE_BUTTON_H ) )
    {
        state.previewSampleButton.Draw( draw, "Play", mouseX, mouseY );
        state.selectSampleButton.Draw( draw, "Use", mouseX, mouseY );
    }

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + SOUND_SET_TITLE_Y, 12.0f, "Material Recipe" );
    if ( IsRowVisible( contentY, contentH, scrolledY + SOUND_SET_PICKER_Y, UI_PIPELINE_STEP_BUTTON_H ) )
    {
        DrawPipelineStepButton( draw,
                                state.previousSetButton,
                                true,
                                data.soundSetCount > 0 && state.previousSetButton.Contains( mouseX, mouseY ) );
        DrawPipelineStepButton( draw,
                                state.nextSetButton,
                                false,
                                data.soundSetCount > 0 && state.nextSetButton.Contains( mouseX, mouseY ) );
        snprintf( buf,
                  sizeof( buf ),
                  "%d / %d  %s",
                  data.soundSetCount > 0 ? state.selectedSetIndex + 1 : 0,
                  data.soundSetCount,
                  set ? set->name : "No sets loaded" );
        const float textMaxW = (std::max)( 0.0f, state.previousSetButton.x - contentX - 12.0f );
        DrawFittedPickerText( draw,
                              contentX,
                              scrolledY + SOUND_SET_PICKER_Y + 5.0f,
                              11.0f,
                              0.84f,
                              0.92f,
                              0.94f,
                              buf,
                              textMaxW );
    }

    if ( !set )
    {
        return;
    }

    snprintf( buf,
              sizeof( buf ),
              "materials #%08x / #%08x  samples %u  range %.0f",
              set->materialA,
              set->materialB,
              set->sampleCount,
              set->maxDistance );
    DrawLabelValueAt( draw,
                      contentY,
                      contentH,
                      contentX,
                      scrolledY + SOUND_SET_META_Y,
                      "Map",
                      buf,
                      0.68f,
                      0.78f,
                      0.82f );
}

} // namespace SoundTab
} // namespace UI
} // namespace SkullbonezCore
