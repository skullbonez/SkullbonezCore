/*
File: SkullbonezSource/UI/UITabEditor.h
Purpose:
  Implements object placement and selection controls for the in-engine editor tab.

Mental model:
  The tab emits editor commands only. Runtime owns selection, placement, and
  physics mutation so UI widgets never touch scene objects directly.

Glossary:
  Editor command: Intent emitted by a widget and applied later by runtime code.
  Widget state: Per-control hover, press, selection, and text state retained
  across frames.

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

constexpr int OBJECT_BOX = 0;
constexpr int OBJECT_BALL = 1;
constexpr int OBJECT_SPHERE = 2;
constexpr int OBJECT_HULL_WEDGE = 3;
constexpr int OBJECT_HULL_TRI_PRISM = 4;
constexpr int OBJECT_HULL_TAPERED_BLOCK = 5;
constexpr int OBJECT_HULL_PYRAMID = 6;
constexpr int OBJECT_HULL_HEX_PRISM = 7;
constexpr int OBJECT_HULL_DIAMOND = 8;
constexpr int OBJECT_ROCK_SLAB = 9;
constexpr int OBJECT_ROCK_LUMP = 10;
constexpr int OBJECT_ROCK_SHARD = 11;
constexpr int OBJECT_ROCK_CHIPPED = 12;
constexpr int OBJECT_ROOT_SMALL = 13;
constexpr int OBJECT_ROOT_LARGE = 14;
constexpr int OBJECT_TREE_SMALL = 15;
constexpr int OBJECT_TREE_BIG = 16;
constexpr int OBJECT_TREE_CEDAR = 17;
constexpr int OBJECT_TREE_SMALL_SLOPE = 18;
constexpr int OBJECT_TREE_BIG_SLOPE = 19;
constexpr int OBJECT_TREE_CEDAR_SLOPE = 20;
constexpr int OBJECT_TREE_SMALL_SLEEP = 21;
constexpr int OBJECT_TREE_BIG_SLEEP = 22;
constexpr int OBJECT_TREE_CEDAR_SLEEP = 23;
constexpr int OBJECT_TREE_SMALL_ROOTED = 24;
constexpr int OBJECT_TREE_BIG_ROOTED = 25;
constexpr int OBJECT_TREE_CEDAR_ROOTED = 26;
constexpr int OBJECT_TREE_PINE_SHEDDING = 27;
constexpr int OBJECT_TYPE_COUNT = 28;

struct UIEditorTabState
{
    UICheckBox editorModeToggle;
    UICheckBox placementModeToggle;
    UICheckBox staticObjectToggle;
    UIComboBox objectCombo;
    int selectedObjectType = OBJECT_BOX;
};

int ContentHeight();
const char* ObjectLabel( int objectType );
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
