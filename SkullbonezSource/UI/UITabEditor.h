/*
File: SkullbonezSource/UI/UITabEditor.h
Purpose:
  Implements fixed-object placement controls for the in-engine editor tab.

Mental model:
  The tab emits placement commands only. Runtime owns the active placement mode
  and converts mouse clicks into fixed physics objects.

Related:
  - SkullbonezSource/UI/UITabEditor.cpp
  - SkullbonezSource/SkullbonezRunInput.cpp
*/
#pragma once

#include "UICheckBox.h"
#include "UIComboBox.h"
#include "UICommands.h"
#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

struct InGameUIFrameData;

namespace EditorTab
{

constexpr int FIXED_BOX = 0;
constexpr int FIXED_BALL = 1;
constexpr int FIXED_SPHERE = 2;
constexpr int FIXED_HULL_WEDGE = 3;
constexpr int FIXED_HULL_TRI_PRISM = 4;
constexpr int FIXED_HULL_TAPERED_BLOCK = 5;
constexpr int FIXED_HULL_PYRAMID = 6;
constexpr int FIXED_HULL_HEX_PRISM = 7;
constexpr int FIXED_HULL_DIAMOND = 8;
constexpr int FIXED_TYPE_COUNT = 9;

struct UIEditorTabState
{
    UICheckBox placementToggle;
    UIComboBox objectCombo;
    int selectedFixedObjectType = FIXED_BOX;
};

int ContentHeight();
bool HandleContentClick( UIEditorTabState& state,
                         InGameUIInputResult& result,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW );

void Draw( UIEditorTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int mouseX,
           int mouseY );

} // namespace EditorTab
} // namespace UI
} // namespace SkullbonezCore
