/*
File: SkullbonezSource/UI/UITabScene.h
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
  - SkullbonezSource/UI/UITabScene.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UICommands.h"

namespace SkullbonezCore
{
namespace UI
{

class UIButton;
class UIComboBox;
class UIDrawContext;
struct InGameUIFrameData;

namespace SceneTab
{

constexpr int DEMO_SCENE_BROWSER_INDEX = -2;
constexpr const char* DEMO_SCENE_OPTION = "Demo Scene";

struct UISceneTabState
{
    char filter[64] = {};
    bool filterKeyWasDown[256] = {};
    int comboScroll = 0;
};

bool FilterMatches( const char* option, const char* filter );
int CountFilteredOptions( const char* const* options, int optionCount, const char* filter );
int FindFilteredOptionIndex( const char* const* options, int optionCount, const char* filter, int filteredIndex );
int FilteredPositionForIndex( const char* const* options, int optionCount, const char* filter, int optionIndex );

void ClearFilter( UISceneTabState& state );
void SetFilter( UISceneTabState& state, const char* filter );
void CloseCombo( UISceneTabState& state, UIComboBox& combo );
void CaptureFilterKeyState( UISceneTabState& state );

void UpdateFilterTyping( UISceneTabState& state,
                         UIComboBox& combo,
                         InGameUIInputResult& result,
                         const char* const* sceneOptions,
                         int sceneOptionCount );

bool HandleComboWheel( UISceneTabState& state,
                       UIComboBox& combo,
                       const char* const* sceneOptions,
                       int sceneOptionCount,
                       int mouseX,
                       int mouseY,
                       int wheelDelta,
                       float contentX,
                       float rowBase,
                       float contentW );

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
                           float contentW );

bool HandleContentClick( UISceneTabState& state,
                         UIComboBox& combo,
                         UIButton& resetSceneButton,
                         UIButton& resetDefaultsButton,
                         UIButton& saveDefaultsButton,
                         InGameUIInputResult& result,
                         const char* const* sceneOptions,
                         int sceneOptionCount,
                         int selectedSceneOption,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW );

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
           int mouseY );

} // namespace SceneTab
} // namespace UI
} // namespace SkullbonezCore
