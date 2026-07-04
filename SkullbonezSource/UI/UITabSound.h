/*
File: SkullbonezSource/UI/UITabSound.h
Purpose:
  Declares the in-engine Sound tab state and input helpers.

Mental model:
  The Sound tab is a live tuning surface for presentation-only contact impact
  audio. It reads a per-frame snapshot and emits one-frame parameter requests;
  the run loop applies those requests to ContactAudioService.

Glossary:
  Sound set: Material-pair impact recipe loaded from contact_audio.materials.json.
  Impact band: Light/medium/heavy impulse tier inside a sound set.
  Sample library: Decoded candidate sounds exposed for preview and assignment.
  Contact-audio flash mode: Optional white marker selector for submitted,
    candidate, or rejected contact-audio decisions.
  Simple mode: Linear-energy contact-audio path that emits from body velocity
    changes instead of solver contact rows.
  Burst voices: Cap on submitted impact sounds in each 100 ms audio burst window.
  Cooldown: Per-body-pair timeout that keeps persistent contacts from replaying
    every physics tick.

Invariants:
  - UI widgets never mutate the audio service directly.
  - Slider ids stay in the Sound tab range so shared UI drag handling can route
    them without colliding with other tabs.

Related:
  - SkullbonezSource/UI/UITabSound.cpp
  - SkullbonezSource/Runtime/Audio/ContactAudioService.h
*/
#pragma once

#include "UICheckBox.h"
#include "UIButton.h"
#include "UICommands.h"
#include "UIDraw.h"
#include "UISlider.h"

namespace SkullbonezCore
{
namespace UI
{

struct InGameUIFrameData;

namespace SoundTab
{

constexpr int SOUND_UI_BAND_MAX = 4;
constexpr int SOUND_GLOBAL_SLIDER_COUNT = 13;
constexpr int SOUND_SET_SLIDER_COUNT = 9;
constexpr int SOUND_BAND_SLIDER_COUNT = 5;

struct UISoundTabState
{
    UICheckBox enabledToggle;
    UICheckBox debugCountersToggle;
    UICheckBox flashModeToggle;
    UICheckBox simpleModeToggle;
    UIRect previousSetButton;
    UIRect nextSetButton;
    UIRect previousSampleButton;
    UIRect nextSampleButton;
    UIButton previewSampleButton;
    UIButton selectSampleButton;
    UISlider globalSliders[SOUND_GLOBAL_SLIDER_COUNT];
    UISlider setSliders[SOUND_SET_SLIDER_COUNT];
    UISlider bandSliders[SOUND_UI_BAND_MAX][SOUND_BAND_SLIDER_COUNT];
    float previewGlobalValues[SOUND_GLOBAL_SLIDER_COUNT];
    float previewSetValues[SOUND_SET_SLIDER_COUNT];
    float previewBandValues[SOUND_UI_BAND_MAX][SOUND_BAND_SLIDER_COUNT];
    int selectedSetIndex = 0;
    int selectedSampleIndex = 1;
    int lastSetCount = 0;
    int lastSampleCount = 0;
    int lastBandCount = 0;
};

int ContentHeight();
void ResetPreviewState( UISoundTabState& state );
void ClampSelection( UISoundTabState& state, const InGameUIFrameData& data );

void DrawHitboxes( const UISoundTabState& state,
                   const UIDrawContext& draw,
                   const InGameUIFrameData& data,
                   float r,
                   float g,
                   float b );

bool HandleContentClick( UISoundTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float scrolledY,
                         float contentW );

bool UpdateActiveSlider( UISoundTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );
bool CommitActiveSlider( UISoundTabState& state, int activeSlider, InGameUIInputResult& result );

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
           int mouseY );

} // namespace SoundTab
} // namespace UI
} // namespace SkullbonezCore
