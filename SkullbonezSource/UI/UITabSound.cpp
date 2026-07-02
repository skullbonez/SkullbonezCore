/*
File: SkullbonezSource/UI/UITabSound.cpp
Purpose:
  Implements the in-engine Sound tab for live contact-audio tuning.

Mental model:
  Drawing and hit testing share one layout. Sliders write previews locally while
  dragging and emit a one-frame command for the run loop to apply to the audio
  service. Nothing here persists config files or mutates audio objects directly.

Glossary:
  Global parameter: Contact-audio master gain or distance scale applied before
    material-set tuning.
  Set parameter: Material-pair threshold, cooldown, gain, pitch, distance, or
    voice-count control.
  Band parameter: Impulse-tier override inside a selected set.
  Sample library: Decoded contact-audio candidate sounds that can be previewed
    or assigned to the selected material set.
  Fitted picker text: One-line selector label clipped before the right-aligned
    previous/next buttons.

Invariants:
  - Selected set indices are clamped against the current frame snapshot before
    hit testing or drawing.
  - Cooldown sliders use milliseconds because that is the authored JSON unit.

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
using UISoundBandParam = SkullbonezCore::UI::UISoundBandParam;
using UISoundParam = SkullbonezCore::UI::UISoundParam;

namespace
{

constexpr int UI_SOUND_GLOBAL_SLIDER_BASE = 8000;
constexpr int UI_SOUND_SET_SLIDER_BASE = 8010;
constexpr int UI_SOUND_BAND_SLIDER_BASE = 8100;
constexpr float SOUND_HEADER_Y = 16.0f;
constexpr float SOUND_TOGGLE_Y = 42.0f;
constexpr float SOUND_TOGGLE_ROW2_Y = 72.0f;
constexpr float SOUND_STATS_Y = 112.0f;
constexpr float SOUND_GLOBAL_TITLE_Y = 160.0f;
constexpr float SOUND_GLOBAL_SLIDER_Y = 186.0f;
constexpr float SOUND_SAMPLE_TITLE_Y = 402.0f;
constexpr float SOUND_SAMPLE_PICKER_Y = 428.0f;
constexpr float SOUND_SAMPLE_ACTION_Y = 462.0f;
constexpr float SOUND_SAMPLE_BUTTON_W = 72.0f;
constexpr float SOUND_SAMPLE_BUTTON_H = 26.0f;
constexpr float SOUND_SET_TITLE_Y = 512.0f;
constexpr float SOUND_SET_PICKER_Y = 538.0f;
constexpr float SOUND_SET_META_Y = 572.0f;
constexpr float SOUND_SET_SLIDER_Y = 614.0f;
constexpr float SOUND_BAND_TITLE_Y = 1004.0f;
constexpr float SOUND_BAND_BLOCK_H = 238.0f;
constexpr float SOUND_SLIDER_H = 34.0f;
constexpr float SOUND_SLIDER_STEP_Y = 40.0f;

struct SoundSliderSpec
{
    UISoundParam param;
    const char* label;
    const char* format;
    float minValue;
    float maxValue;
    float step;
};

struct SoundBandSliderSpec
{
    UISoundBandParam param;
    const char* label;
    const char* format;
    float minValue;
    float maxValue;
    float step;
};

constexpr SoundSliderSpec kGlobalSliders[] = {
    { UISoundParam::MasterGain, "Master gain", "%.2f", 0.0f, 4.0f, 0.05f },
    { UISoundParam::MaxDistanceScale, "Distance scale", "%.2fx", 0.01f, 16.0f, 0.05f },
    { UISoundParam::MinClosingSpeed, "Min closing speed", "%.2f", 0.0f, 20.0f, 0.05f },
    { UISoundParam::MinImpactScore, "Min impact score", "%.1f", 0.0f, 5000.0f, 1.0f },
    { UISoundParam::ImpactScoreRangeSeconds, "Impact score range", "%.2fs", 0.001f, 10.0f, 0.05f },
};

constexpr SoundSliderSpec kSetSliders[] = {
    { UISoundParam::SetMinImpulse, "Min impulse", "%.2f", 0.0f, 100.0f, 0.05f },
    { UISoundParam::SetImpulseRange, "Impulse range", "%.2f", 0.05f, 100.0f, 0.05f },
    { UISoundParam::SetCooldownMs, "Cooldown / sleep ms", "%.0f ms", 0.0f, 1000.0f, 5.0f },
    { UISoundParam::SetOverrideCooldownMs, "Override sleep ms", "%.0f ms", 0.0f, 1000.0f, 5.0f },
    { UISoundParam::SetMaxDistance, "Max distance", "%.0f", 1.0f, 500.0f, 1.0f },
    { UISoundParam::SetBaseGain, "Base gain", "%.2f", 0.0f, 4.0f, 0.05f },
    { UISoundParam::SetPitchMin, "Pitch min", "%.2f", 0.25f, 4.0f, 0.01f },
    { UISoundParam::SetPitchMax, "Pitch max", "%.2f", 0.25f, 4.0f, 0.01f },
    { UISoundParam::SetMaxVoices, "Max voices", "%.0f", 1.0f, 32.0f, 1.0f },
};

constexpr SoundBandSliderSpec kBandSliders[] = {
    { UISoundBandParam::MinImpulse, "Min impulse", "%.2f", 0.0f, 100.0f, 0.05f },
    { UISoundBandParam::ImpulseRange, "Impulse range", "%.2f", 0.05f, 100.0f, 0.05f },
    { UISoundBandParam::BaseGain, "Base gain", "%.2f", 0.0f, 4.0f, 0.05f },
    { UISoundBandParam::PitchMin, "Pitch min", "%.2f", 0.25f, 4.0f, 0.01f },
    { UISoundBandParam::PitchMax, "Pitch max", "%.2f", 0.25f, 4.0f, 0.01f },
};

static_assert( sizeof( kGlobalSliders ) / sizeof( kGlobalSliders[0] ) ==
                   SkullbonezCore::UI::SoundTab::SOUND_GLOBAL_SLIDER_COUNT,
               "Global Sound tab slider specs must match Sound tab state." );
static_assert( sizeof( kSetSliders ) / sizeof( kSetSliders[0] ) == SkullbonezCore::UI::SoundTab::SOUND_SET_SLIDER_COUNT,
               "Sound set slider specs must match Sound tab state." );
static_assert( sizeof( kBandSliders ) / sizeof( kBandSliders[0] ) ==
                   SkullbonezCore::UI::SoundTab::SOUND_BAND_SLIDER_COUNT,
               "Sound band slider specs must match Sound tab state." );
static_assert( SkullbonezCore::UI::SoundTab::SOUND_UI_BAND_MAX == SkullbonezCore::UI::UI_SOUND_BAND_MAX,
               "Sound tab and frame-data band limits must match." );

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
    return index >= 0 && index < static_cast<int>( sizeof( kGlobalSliders ) / sizeof( kGlobalSliders[0] ) ) ? index
                                                                                                            : -1;
}

int SetSliderIndexFromActiveSlider( int activeSlider )
{
    const int index = activeSlider - UI_SOUND_SET_SLIDER_BASE;
    return index >= 0 && index < SkullbonezCore::UI::SoundTab::SOUND_SET_SLIDER_COUNT ? index : -1;
}

int BandSliderIndexFromActiveSlider( int activeSlider, int& outBandIndex )
{
    const int encoded = activeSlider - UI_SOUND_BAND_SLIDER_BASE;
    if ( encoded < 0 )
    {
        outBandIndex = -1;
        return -1;
    }
    const int bandIndex = encoded / 10;
    const int sliderIndex = encoded % 10;
    if ( bandIndex < 0 || bandIndex >= SkullbonezCore::UI::SoundTab::SOUND_UI_BAND_MAX || sliderIndex < 0 ||
         sliderIndex >= SkullbonezCore::UI::SoundTab::SOUND_BAND_SLIDER_COUNT )
    {
        outBandIndex = -1;
        return -1;
    }
    outBandIndex = bandIndex;
    return sliderIndex;
}

float SetSliderValue( const SkullbonezCore::UI::UISoundSetFrameData& set, int sliderIndex )
{
    switch ( kSetSliders[sliderIndex].param )
    {
    case UISoundParam::SetMinImpulse:
        return set.minImpulse;
    case UISoundParam::SetImpulseRange:
        return set.impulseRange;
    case UISoundParam::SetCooldownMs:
        return set.cooldownMs;
    case UISoundParam::SetOverrideCooldownMs:
        return set.overrideCooldownMs;
    case UISoundParam::SetMaxDistance:
        return set.maxDistance;
    case UISoundParam::SetBaseGain:
        return set.baseGain;
    case UISoundParam::SetPitchMin:
        return set.pitchMin;
    case UISoundParam::SetPitchMax:
        return set.pitchMax;
    case UISoundParam::SetMaxVoices:
        return static_cast<float>( set.maxVoices );
    default:
        return 0.0f;
    }
}

float BandSliderValue( const SkullbonezCore::UI::UISoundBandFrameData& band, int sliderIndex )
{
    switch ( kBandSliders[sliderIndex].param )
    {
    case UISoundBandParam::MinImpulse:
        return band.minImpulse;
    case UISoundBandParam::ImpulseRange:
        return band.impulseRange;
    case UISoundBandParam::BaseGain:
        return band.baseGain;
    case UISoundBandParam::PitchMin:
        return band.pitchMin;
    case UISoundBandParam::PitchMax:
        return band.pitchMax;
    default:
        return 0.0f;
    }
}

void FormatSliderValue( char* out, size_t outSize, const char* format, float value )
{
    snprintf( out, outSize, format, value );
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
    case UISoundParam::MaxDistanceScale:
        return data.contactAudioMaxDistanceScale;
    case UISoundParam::MinClosingSpeed:
        return data.contactAudioMinClosingSpeed;
    case UISoundParam::MinImpactScore:
        return data.contactAudioMinImpactScore;
    case UISoundParam::ImpactScoreRangeSeconds:
        return data.contactAudioImpactScoreRangeSeconds;
    default:
        return 0.0f;
    }
}

float SetDisplayValue( const SkullbonezCore::UI::SoundTab::UISoundTabState& state,
                       const SkullbonezCore::UI::UISoundSetFrameData& set,
                       int activeSlider,
                       int index )
{
    if ( activeSlider == UI_SOUND_SET_SLIDER_BASE + index && state.previewSetValues[index] >= 0.0f )
    {
        return state.previewSetValues[index];
    }
    return SetSliderValue( set, index );
}

float BandDisplayValue( const SkullbonezCore::UI::SoundTab::UISoundTabState& state,
                        const SkullbonezCore::UI::UISoundBandFrameData& band,
                        int activeSlider,
                        int bandIndex,
                        int sliderIndex )
{
    int activeBandIndex = -1;
    const int activeBandSlider = BandSliderIndexFromActiveSlider( activeSlider, activeBandIndex );
    if ( activeBandIndex == bandIndex && activeBandSlider == sliderIndex &&
         state.previewBandValues[bandIndex][sliderIndex] >= 0.0f )
    {
        return state.previewBandValues[bandIndex][sliderIndex];
    }
    return BandSliderValue( band, sliderIndex );
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

void SetSetSliderResult( SkullbonezCore::UI::SoundTab::UISoundTabState& state,
                         SkullbonezCore::UI::InGameUIInputResult& result,
                         int sliderIndex,
                         int mouseX )
{
    const SoundSliderSpec& spec = kSetSliders[sliderIndex];
    const float value = state.setSliders[sliderIndex].ValueFromMouse( mouseX, spec.minValue, spec.maxValue, spec.step );
    state.previewSetValues[sliderIndex] = value;
    result.commands.sound.requestedSetIndex = state.selectedSetIndex;
    result.commands.sound.requestedParam = spec.param;
    result.commands.sound.requestedValue = value;
}

void SetBandSliderResult( SkullbonezCore::UI::SoundTab::UISoundTabState& state,
                          SkullbonezCore::UI::InGameUIInputResult& result,
                          int bandIndex,
                          int sliderIndex,
                          int mouseX )
{
    const SoundBandSliderSpec& spec = kBandSliders[sliderIndex];
    const float value =
        state.bandSliders[bandIndex][sliderIndex].ValueFromMouse( mouseX, spec.minValue, spec.maxValue, spec.step );
    state.previewBandValues[bandIndex][sliderIndex] = value;
    result.commands.sound.requestedSetIndex = state.selectedSetIndex;
    result.commands.sound.requestedBandIndex = bandIndex;
    result.commands.sound.requestedBandParam = spec.param;
    result.commands.sound.requestedValue = value;
}

void SetContentBounds( SkullbonezCore::UI::SoundTab::UISoundTabState& state,
                       float contentX,
                       float scrolledY,
                       float contentW,
                       int bandCount )
{
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    state.enabledToggle.SetBounds( contentX, scrolledY + SOUND_TOGGLE_Y, colW, 24.0f );
    state.debugCountersToggle.SetBounds( contentX + colW + 18.0f, scrolledY + SOUND_TOGGLE_Y, colW, 24.0f );
    state.flashOnSubmitToggle.SetBounds( contentX, scrolledY + SOUND_TOGGLE_ROW2_Y, colW, 24.0f );
    for ( int i = 0; i < SkullbonezCore::UI::SoundTab::SOUND_GLOBAL_SLIDER_COUNT; ++i )
    {
        state.globalSliders[i].SetBounds(
            contentX,
            scrolledY + SOUND_GLOBAL_SLIDER_Y + static_cast<float>( i ) * SOUND_SLIDER_STEP_Y,
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
    for ( int i = 0; i < SkullbonezCore::UI::SoundTab::SOUND_SET_SLIDER_COUNT; ++i )
    {
        state.setSliders[i].SetBounds( contentX,
                                       scrolledY + SOUND_SET_SLIDER_Y + static_cast<float>( i ) * SOUND_SLIDER_STEP_Y,
                                       contentW,
                                       SOUND_SLIDER_H );
    }
    for ( int bandIndex = 0; bandIndex < bandCount && bandIndex < SkullbonezCore::UI::SoundTab::SOUND_UI_BAND_MAX;
          ++bandIndex )
    {
        const float bandY = scrolledY + SOUND_BAND_TITLE_Y + static_cast<float>( bandIndex ) * SOUND_BAND_BLOCK_H;
        for ( int sliderIndex = 0; sliderIndex < SkullbonezCore::UI::SoundTab::SOUND_BAND_SLIDER_COUNT; ++sliderIndex )
        {
            state.bandSliders[bandIndex][sliderIndex].SetBounds(
                contentX,
                bandY + 48.0f + static_cast<float>( sliderIndex ) * SOUND_SLIDER_STEP_Y,
                contentW,
                SOUND_SLIDER_H );
        }
    }
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
    return static_cast<int>( SOUND_BAND_TITLE_Y + SOUND_BAND_BLOCK_H * static_cast<float>( SOUND_UI_BAND_MAX ) +
                             18.0f );
}


void ResetPreviewState( UISoundTabState& state )
{
    for ( float& value : state.previewGlobalValues )
    {
        value = -1.0f;
    }
    for ( float& value : state.previewSetValues )
    {
        value = -1.0f;
    }
    for ( int bandIndex = 0; bandIndex < SOUND_UI_BAND_MAX; ++bandIndex )
    {
        for ( int sliderIndex = 0; sliderIndex < SOUND_BAND_SLIDER_COUNT; ++sliderIndex )
        {
            state.previewBandValues[bandIndex][sliderIndex] = -1.0f;
        }
    }
}


void ClampSelection( UISoundTabState& state, const InGameUIFrameData& data )
{
    if ( data.soundSetCount <= 0 )
    {
        state.selectedSetIndex = 0;
        state.lastSetCount = 0;
        state.lastBandCount = 0;
    }
    else
    {
        state.selectedSetIndex = std::clamp( state.selectedSetIndex, 0, data.soundSetCount - 1 );
        state.lastSetCount = std::clamp( data.soundSetCount, 0, UI_SOUND_SET_MAX );
        const UISoundSetFrameData& set = data.soundSets[state.selectedSetIndex];
        state.lastBandCount = std::clamp( static_cast<int>( set.bandCount ), 0, SOUND_UI_BAND_MAX );
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
    DrawHitboxRect( draw, state.enabledToggle.Bounds(), r, g, b );
    DrawHitboxRect( draw, state.debugCountersToggle.Bounds(), r, g, b );
    DrawHitboxRect( draw, state.flashOnSubmitToggle.Bounds(), r, g, b );
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
    const UISoundSetFrameData* set = SelectedSet( state, data );
    if ( !set )
    {
        return;
    }
    for ( int i = 0; i < SOUND_SET_SLIDER_COUNT; ++i )
    {
        DrawHitboxRect( draw, state.setSliders[i].Bounds(), r, g, b );
    }
    const int bandCount = std::clamp( static_cast<int>( set->bandCount ), 0, SOUND_UI_BAND_MAX );
    for ( int bandIndex = 0; bandIndex < bandCount; ++bandIndex )
    {
        for ( int sliderIndex = 0; sliderIndex < SOUND_BAND_SLIDER_COUNT; ++sliderIndex )
        {
            DrawHitboxRect( draw, state.bandSliders[bandIndex][sliderIndex].Bounds(), r, g, b );
        }
    }
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
    const int bandCount = std::clamp( state.lastBandCount, 0, SOUND_UI_BAND_MAX );
    SetContentBounds( state, contentX, scrolledY, contentW, bandCount );

    if ( state.enabledToggle.HitTest( mouseX, mouseY ) )
    {
        result.commands.sound.toggleEnabled = true;
        return false;
    }
    if ( state.debugCountersToggle.HitTest( mouseX, mouseY ) )
    {
        result.commands.sound.toggleDebugCounters = true;
        return false;
    }
    if ( state.flashOnSubmitToggle.HitTest( mouseX, mouseY ) )
    {
        result.commands.sound.toggleFlashOnSubmit = true;
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
        ResetPreviewState( state );
        return false;
    }
    if ( state.nextSetButton.Contains( mouseX, mouseY ) && state.lastSetCount > 0 )
    {
        state.selectedSetIndex = ( state.selectedSetIndex + 1 ) % state.lastSetCount;
        ResetPreviewState( state );
        return false;
    }
    if ( state.lastSetCount <= 0 )
    {
        return false;
    }
    for ( int i = 0; i < SOUND_SET_SLIDER_COUNT; ++i )
    {
        if ( state.setSliders[i].HitTest( mouseX, mouseY ) )
        {
            activeSlider = UI_SOUND_SET_SLIDER_BASE + i;
            SetSetSliderResult( state, result, i, mouseX );
            return true;
        }
    }
    for ( int bandIndex = 0; bandIndex < bandCount; ++bandIndex )
    {
        for ( int sliderIndex = 0; sliderIndex < SOUND_BAND_SLIDER_COUNT; ++sliderIndex )
        {
            if ( state.bandSliders[bandIndex][sliderIndex].HitTest( mouseX, mouseY ) )
            {
                activeSlider = UI_SOUND_BAND_SLIDER_BASE + bandIndex * 10 + sliderIndex;
                SetBandSliderResult( state, result, bandIndex, sliderIndex, mouseX );
                return true;
            }
        }
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
    const int setIndex = SetSliderIndexFromActiveSlider( activeSlider );
    if ( setIndex >= 0 )
    {
        SetSetSliderResult( state, result, setIndex, mouseX );
        return true;
    }
    int bandIndex = -1;
    const int bandSliderIndex = BandSliderIndexFromActiveSlider( activeSlider, bandIndex );
    if ( bandSliderIndex >= 0 )
    {
        SetBandSliderResult( state, result, bandIndex, bandSliderIndex, mouseX );
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
    const int setIndex = SetSliderIndexFromActiveSlider( activeSlider );
    if ( setIndex >= 0 && state.previewSetValues[setIndex] >= 0.0f )
    {
        result.commands.sound.requestedSetIndex = state.selectedSetIndex;
        result.commands.sound.requestedParam = kSetSliders[setIndex].param;
        result.commands.sound.requestedValue = state.previewSetValues[setIndex];
        return true;
    }
    int bandIndex = -1;
    const int bandSliderIndex = BandSliderIndexFromActiveSlider( activeSlider, bandIndex );
    if ( bandSliderIndex >= 0 && state.previewBandValues[bandIndex][bandSliderIndex] >= 0.0f )
    {
        result.commands.sound.requestedSetIndex = state.selectedSetIndex;
        result.commands.sound.requestedBandIndex = bandIndex;
        result.commands.sound.requestedBandParam = kBandSliders[bandSliderIndex].param;
        result.commands.sound.requestedValue = state.previewBandValues[bandIndex][bandSliderIndex];
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
    const int bandCount = set ? std::clamp( static_cast<int>( set->bandCount ), 0, SOUND_UI_BAND_MAX ) : 0;
    SetContentBounds( state, contentX, scrolledY, contentW, bandCount );

    char buf[160];
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    const float col1 = contentX;
    const float col2 = contentX + colW + 18.0f;
    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + SOUND_HEADER_Y, 16.0f, "Sound" );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.enabledToggle,
                       col1,
                       scrolledY + SOUND_TOGGLE_Y,
                       colW,
                       "Contact audio",
                       data.contactAudioEnabled );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.debugCountersToggle,
                       col2,
                       scrolledY + SOUND_TOGGLE_Y,
                       colW,
                       "Debug counters",
                       data.contactAudioDebugCounters );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.flashOnSubmitToggle,
                       col1,
                       scrolledY + SOUND_TOGGLE_ROW2_Y,
                       colW,
                       "Flash emitters",
                       data.contactAudioFlashOnSubmit );

    snprintf( buf,
              sizeof( buf ),
              "%u seen  %u played  %u quiet  %u cooldown  %u dropped",
              data.contactAudioEventsSeen,
              data.contactAudioSubmittedVoices,
              data.contactAudioRejectedByThreshold,
              data.contactAudioRejectedByCooldown,
              data.contactAudioDroppedVoices );
    DrawLabelValueAt( draw,
                      contentY,
                      contentH,
                      contentX,
                      scrolledY + SOUND_STATS_Y,
                      data.contactAudioAvailable ? "Runtime" : "Runtime",
                      data.contactAudioAvailable ? buf : "Not available",
                      data.contactAudioAvailable ? 0.78f : 0.95f,
                      data.contactAudioAvailable ? 0.88f : 0.55f,
                      data.contactAudioAvailable ? 0.91f : 0.32f );

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + SOUND_GLOBAL_TITLE_Y, 12.0f, "Global" );
    for ( int i = 0; i < static_cast<int>( sizeof( kGlobalSliders ) / sizeof( kGlobalSliders[0] ) ); ++i )
    {
        const float sliderY = scrolledY + SOUND_GLOBAL_SLIDER_Y + static_cast<float>( i ) * SOUND_SLIDER_STEP_Y;
        const float value = GlobalDisplayValue( state, data, activeSlider, i );
        FormatSliderValue( buf, sizeof( buf ), kGlobalSliders[i].format, value );
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

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + SOUND_SET_TITLE_Y, 12.0f, "Material Set" );
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
              "materials #%08x / #%08x  samples %u  bands %u",
              set->materialA,
              set->materialB,
              set->sampleCount,
              set->bandCount );
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

    for ( int i = 0; i < SOUND_SET_SLIDER_COUNT; ++i )
    {
        const float sliderY = scrolledY + SOUND_SET_SLIDER_Y + static_cast<float>( i ) * SOUND_SLIDER_STEP_Y;
        const float value = SetDisplayValue( state, *set, activeSlider, i );
        FormatSliderValue( buf, sizeof( buf ), kSetSliders[i].format, value );
        if ( IsRowVisible( contentY, contentH, sliderY, SOUND_SLIDER_H ) )
        {
            state.setSliders[i]
                .Draw( draw, kSetSliders[i].label, buf, value, kSetSliders[i].minValue, kSetSliders[i].maxValue );
        }
    }

    if ( bandCount > 0 )
    {
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + SOUND_BAND_TITLE_Y, 12.0f, "Impact Bands" );
    }

    for ( int bandIndex = 0; bandIndex < bandCount; ++bandIndex )
    {
        const UISoundBandFrameData& band = set->bands[bandIndex];
        const float bandY = scrolledY + SOUND_BAND_TITLE_Y + static_cast<float>( bandIndex ) * SOUND_BAND_BLOCK_H;
        if ( IsRowVisible( contentY, contentH, bandY + 24.0f, 18.0f ) )
        {
            snprintf( buf, sizeof( buf ), "%s  samples %u", band.name, band.sampleCount );
            DrawLabelValueAt( draw, contentY, contentH, contentX, bandY + 24.0f, "Band", buf, 0.84f, 0.92f, 0.94f );
        }
        for ( int sliderIndex = 0; sliderIndex < SOUND_BAND_SLIDER_COUNT; ++sliderIndex )
        {
            const float sliderY = bandY + 48.0f + static_cast<float>( sliderIndex ) * SOUND_SLIDER_STEP_Y;
            const float value = BandDisplayValue( state, band, activeSlider, bandIndex, sliderIndex );
            FormatSliderValue( buf, sizeof( buf ), kBandSliders[sliderIndex].format, value );
            if ( IsRowVisible( contentY, contentH, sliderY, SOUND_SLIDER_H ) )
            {
                state.bandSliders[bandIndex][sliderIndex].Draw( draw,
                                                                kBandSliders[sliderIndex].label,
                                                                buf,
                                                                value,
                                                                kBandSliders[sliderIndex].minValue,
                                                                kBandSliders[sliderIndex].maxValue );
            }
        }
    }
}

} // namespace SoundTab
} // namespace UI
} // namespace SkullbonezCore
