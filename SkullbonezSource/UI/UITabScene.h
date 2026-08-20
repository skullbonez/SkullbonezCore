/*
File: SkullbonezSource/UI/UITabScene.h
Purpose:
  Implements UI TabScene widgets, layout, drawing, or UI state for the in-engine controls.

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
  - SkullbonezSource/UI/UITabScene.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIButton.h"
#include "UIComboBox.h"
#include "UICommands.h"
#include "UIInput.h"
#include "UISlider.h"

namespace SkullbonezCore
{
namespace UI
{

class UIDrawContext;
struct InGameUIFrameData;

namespace SceneTab
{

constexpr int DEMO_SCENE_BROWSER_INDEX = -2;
constexpr int NEW_SCENE_BROWSER_INDEX = -3;
constexpr const char* DEMO_SCENE_OPTION = "Demo Scene";
constexpr const char* NEW_SCENE_OPTION = "Create new scene";
constexpr int SLIDER_TIME_SCALE = 30;

// Hazard: slider ids are the active-drag identity, so a duplicate silently
// routes one slider's drag into another's commit.
constexpr int SLIDER_PREDICTION_REVEAL = 31;

struct UISceneTabState
{
    char filter[64] = {};
    bool filterKeyWasDown[256] = {};
    int comboScroll = 0;

    // Concept: scene-selection controls belong to the Scene tab because their
    // open state, bounds, filtering, and commands form one interaction.
    UIComboBox combo;
    UIButton resetSceneButton;
    UIButton resetDefaultsButton;
    UIButton saveDefaultsButton;
    UIButton continuousForecastButton;
    UIButton resetForecastButton;
    UIButton exitForecastButton;
    UISlider timeScaleSlider;

    // Concept: reveal pacing sits beside simulation speed because both answer
    // "how fast does time appear to move" for the operator. It is presentation
    // only - it paces the causal-unfold cursor over an already-computed
    // horizon and never reaches physics, replay samples, or solver restores.
    UISlider predictionRevealSlider;
    float previewTimeScale = -1.0f;
    float previewPredictionReveal = -1.0f;
    bool filterKeySyncPending = true;
};

int ContentHeight();
bool FilterMatches( const char* option, const char* filter );
int CountFilteredOptions( const char* const* options, int optionCount, const char* filter );
int FindFilteredOptionIndex( const char* const* options, int optionCount, const char* filter, int filteredIndex );
int FilteredPositionForIndex( const char* const* options, int optionCount, const char* filter, int optionIndex );

void ClearFilter( UISceneTabState& state );
void SetFilter( UISceneTabState& state, const char* filter );
void CloseCombo( UISceneTabState& state );
void RequestFilterKeySync( UISceneTabState& state );
void ResetPreviewState( UISceneTabState& state );

void UpdateFilterTyping( UISceneTabState& state, InGameUIInputResult& result, const InputControl::UIInputSnapshot& input,
                         const char* const* sceneOptions, int sceneOptionCount );

bool HandleComboWheel( UISceneTabState& state, const char* const* sceneOptions, int sceneOptionCount, int mouseX, int mouseY,
                       int wheelDelta, float contentX, float rowBase, float contentW );

bool HandleOpenComboClick( UISceneTabState& state, InGameUIInputResult& result, const char* const* sceneOptions,
                           int sceneOptionCount, int mouseX, int mouseY, float contentX, float rowBase, float contentW );

bool HandleHeaderClick( UISceneTabState& state, InGameUIInputResult& result, int mouseX, int mouseY, float contentX,
                        float rowBase, float contentW );

bool HandleClosedComboClick( UISceneTabState& state, const InputControl::UIInputSnapshot& input,
                             const char* const* sceneOptions, int sceneOptionCount, int selectedSceneOption, int mouseX,
                             int mouseY );

bool HandleTimeScaleClick( UISceneTabState& state, InGameUIInputResult& result, int& activeSlider, int mouseX, int mouseY,
                           float contentX, float rowBase, float contentW );
bool HandleForecastClick( UISceneTabState& state, InGameUIInputResult& result, int mouseX, int mouseY, float contentX,
                          float rowBase, float contentW );

bool UpdateActiveSlider( UISceneTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );
bool CommitActiveSlider( UISceneTabState& state, int activeSlider, InGameUIInputResult& result );

void Draw( UISceneTabState& state, const UIDrawContext& draw, const InGameUIFrameData& data, float contentX, float contentY,
           float contentW, float contentH, float scrolledY, int mouseX, int mouseY );

} // namespace SceneTab
} // namespace UI
} // namespace SkullbonezCore
