/*
File: SkullbonezSource/UI/UITabScene.h
Purpose:
  Implements UI TabScene widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UITabScene.h implements UI TabScene widgets, layout, drawing, or UI state

  for the in-engine controls. As a public header, keep edits anchored on UI
  request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

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
  - SkullbonezSource/UI/UITabScene.cpp
  - Agentic/Reference/comment-style-guide.md
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
    UISlider timeScaleSlider;
    float previewTimeScale = -1.0f;
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

bool UpdateActiveSlider( UISceneTabState& state, int activeSlider, int mouseX, InGameUIInputResult& result );
bool CommitActiveSlider( UISceneTabState& state, int activeSlider, InGameUIInputResult& result );

void Draw( UISceneTabState& state, const UIDrawContext& draw, const InGameUIFrameData& data, float contentX, float contentY,
           float contentW, float contentH, float scrolledY, int mouseX, int mouseY );

} // namespace SceneTab
} // namespace UI
} // namespace SkullbonezCore
