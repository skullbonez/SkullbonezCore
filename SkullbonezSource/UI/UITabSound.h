/*
File: SkullbonezSource/UI/UITabSound.h
Purpose:
  Declares the in-engine Sound tab state and input helpers.

Summary:
  The Sound tab is a deliberately tiny surface over contact audio: an enable
  toggle, a Volume slider, a Thud-threshold slider, a sample-library picker
  for auditioning thud samples, and a material-set picker for assigning the
  chosen sample. It reads a per-frame snapshot and emits one-frame parameter
  requests; the run loop applies those requests to ContactAudioService.

Glossary:
  Thud threshold: Minimum impact energy (joules) a collision needs before a
    sound plays. The only gate the user tunes; everything else is fixed
    policy inside ContactAudioService.
  Sound set: Material-pair recipe loaded from contact_audio.materials.json.
  Sample library: Decoded candidate sounds exposed for preview and assignment.

Invariants:
  - UI widgets never mutate the audio service directly.
  - Slider ids stay in the Sound tab range so shared UI drag handling can
    route them without colliding with other tabs.

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

// Exactly the two user-facing knobs: Volume and Thud threshold.
constexpr int SOUND_GLOBAL_SLIDER_COUNT = 2;

struct UISoundTabState
{
    UICheckBox enabledToggle;
    UIRect previousSetButton;
    UIRect nextSetButton;
    UIRect previousSampleButton;
    UIRect nextSampleButton;
    UIButton previewSampleButton;
    UIButton selectSampleButton;
    UISlider globalSliders[SOUND_GLOBAL_SLIDER_COUNT];
    float previewGlobalValues[SOUND_GLOBAL_SLIDER_COUNT];
    int selectedSetIndex = 0;
    int selectedSampleIndex = 0;
    int lastSetCount = 0;
    int lastSampleCount = 0;
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
