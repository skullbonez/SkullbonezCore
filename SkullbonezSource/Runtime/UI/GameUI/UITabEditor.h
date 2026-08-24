/*
File: SkullbonezSource/Runtime/UI/GameUI/UITabEditor.h
Purpose:
  Implements object placement and selection controls for the in-engine editor tab.

Summary:
  The tab emits editor commands only. Runtime owns selection, placement, and
  physics mutation so UI widgets never touch scene objects directly.

Glossary:
  Widget state: Per-control hover, press, selection, and text state retained
  across frames.

Invariants:
  - Object ids must stay aligned with runtime editor placement handling.
  - UIEditorTabState owns widget state only; runtime owns scene mutation.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UITabEditor.cpp
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../../UI/UICheckBox.h"
#include "../../../UI/UIComboBox.h"
#include "../../Interaction/OperatorUiCommands.h"
#include "../../Interaction/OperatorEditorObjectCatalog.h"
#include "../../../UI/UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

struct UIEditorTabFrameView;

namespace EditorTab
{

struct UIEditorTabState
{
    UICheckBox editorModeToggle;
    UICheckBox placementModeToggle;
    UICheckBox staticObjectToggle;
    UIComboBox objectCombo;
    int selectedObjectType = OBJECT_BOX;
};

int ContentHeight();
bool HandleContentClick( UIEditorTabState& state, InGameUIInputResult& result, int mouseX, int mouseY, float contentX,
                         float rowBase, float contentW );

void Draw( UIEditorTabState& state, const UIDrawContext& draw, const UIEditorTabFrameView& data, float contentX,
           float contentY, float contentW, float contentH, float scrolledY, int mouseX, int mouseY );

} // namespace EditorTab
} // namespace UI
} // namespace SkullbonezCore
