/*
File: SkullbonezSource/Runtime/UI/GameUI/UITabEditor.cpp
Purpose:
  Implements object placement and selection controls for the in-engine editor tab.

Summary:
  This UI chooses the object and editor toggles. The run loop owns actual
  selection, placement, and physics mutation so mouse raycasts stay out of UI.

Glossary:
  Placement mode: Editor state where a picked object kind can be inserted into
  the active scene.

Invariants:
  - Object labels must stay in the same order as `EditorTab::OBJECT_*`.
  - The tab emits command intents only; it never mutates scene objects directly.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UITabEditor.h
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "UITabEditor.h"

#include "UI.h"
#include "../../../UI/UIDrawWidgets.h"
#include "GameUILayout.h"
#include "../../../UI/UIStyle.h"

#include <algorithm>
#include <cstdio>

using namespace SkullbonezCore::UI::GameLayout;
using namespace SkullbonezCore::UI::OperatorControlPolicy;
using namespace SkullbonezCore::UI::Widgets;

namespace
{

constexpr float EDITOR_MODE_TOGGLE_Y = 42.0f;
constexpr float EDITOR_PLACE_TOGGLE_Y = 76.0f;
constexpr float EDITOR_STATIC_TOGGLE_Y = 110.0f;
constexpr float EDITOR_OBJECT_COMBO_Y = 154.0f;
constexpr float EDITOR_STATUS_Y = 194.0f;
constexpr float EDITOR_HISTORY_STATUS_Y = 222.0f;

void SetContentBounds( SkullbonezCore::UI::EditorTab::UIEditorTabState& state, float contentX, float rowBase,
                       float contentW )
{
    const float contentBaseY = rowBase - EDITOR_MODE_TOGGLE_Y;
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    state.editorModeToggle.SetBounds( contentX, contentBaseY + EDITOR_MODE_TOGGLE_Y, colW, 24.0f );
    state.placementModeToggle.SetBounds( contentX, contentBaseY + EDITOR_PLACE_TOGGLE_Y, colW, 24.0f );
    state.staticObjectToggle.SetBounds( contentX, contentBaseY + EDITOR_STATIC_TOGGLE_Y, colW, 24.0f );
    state.objectCombo.SetBounds( contentX, contentBaseY + EDITOR_OBJECT_COMBO_Y, (std::max)( 190.0f, contentW * 0.55f ),
                                 24.0f );
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace EditorTab
{

int ContentHeight()
{
    return 238;
}


bool HandleContentClick( UIEditorTabState& state, InGameUIInputResult& result, int mouseX, int mouseY, float contentX,
                         float rowBase, float contentW )
{
    SetContentBounds( state, contentX, rowBase, contentW );

    if ( state.objectCombo.IsOpen() )
    {
        const int option = state.objectCombo.HitOption( mouseX, mouseY, OBJECT_TYPE_COUNT );

        if ( option >= 0 && option < OBJECT_TYPE_COUNT )
        {
            state.selectedObjectType = option;
            result.commands.editor.requestedObjectType = option;
            result.commands.editor.enterPlacementMode = true;
            state.objectCombo.Close();
            return true;
        }

        if ( state.objectCombo.HitBox( mouseX, mouseY ) )
        {
            state.objectCombo.ToggleOpen();
            return true;
        }

        state.objectCombo.Close();
        return true;
    }

    if ( state.editorModeToggle.HitTest( mouseX, mouseY ) )
    {
        result.commands.editor.toggleEditorMode = true;
        return true;
    }

    if ( state.placementModeToggle.HitTest( mouseX, mouseY ) )
    {
        result.commands.editor.togglePlacementMode = true;
        return true;
    }

    if ( state.staticObjectToggle.HitTest( mouseX, mouseY ) )
    {
        result.commands.editor.togglePlaceStatic = true;
        return true;
    }

    if ( state.objectCombo.HitBox( mouseX, mouseY ) )
    {
        state.objectCombo.ToggleOpen();
        return true;
    }

    return false;
}


void Draw( UIEditorTabState& state, const UIDrawContext& draw, const UIEditorTabFrameView& data, float contentX,
           float contentY, float contentW, float contentH, float scrolledY, int mouseX, int mouseY )
{
    const Style::UIPalette& palette = Style::Palette();
    const float colW = (std::max)( 148.0f, contentW * 0.46f );

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Editor" );
    DrawContentToggle( draw, contentY, contentH, state.editorModeToggle, contentX, scrolledY + EDITOR_MODE_TOGGLE_Y, colW,
                       "Editor mode", data.editorModeEnabled );

    DrawContentToggle( draw, contentY, contentH, state.placementModeToggle, contentX, scrolledY + EDITOR_PLACE_TOGGLE_Y,
                       colW, "Place mode", data.editorPlacementMode );

    DrawContentToggle( draw, contentY, contentH, state.staticObjectToggle, contentX, scrolledY + EDITOR_STATIC_TOGGLE_Y,
                       colW, "Static object", data.editorPlaceStatic );

    state.objectCombo.SetBounds( contentX, scrolledY + EDITOR_OBJECT_COMBO_Y, (std::max)( 190.0f, contentW * 0.55f ),
                                 24.0f );

    state.selectedObjectType = std::clamp( data.editorObjectType, 0, OBJECT_TYPE_COUNT - 1 );

    if ( IsRowVisible( contentY, contentH, scrolledY + EDITOR_OBJECT_COMBO_Y, 24.0f ) || state.objectCombo.IsOpen() )
    {
        state.objectCombo.Draw( draw, "Object", OBJECT_LABELS, OBJECT_TYPE_COUNT, state.selectedObjectType, mouseX, mouseY );
    }

    const char* viewportState = "Cursor";

    if ( data.editorViewportLookActive )
    {
        viewportState = "Look";
    }
    else if ( data.editorModeEnabled )
    {
        viewportState = data.editorPlacementMode ? ( data.editorPlaceStatic ? "Place static" : "Place dynamic" ) : "Gizmo";
    }

    DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + EDITOR_STATUS_Y, "Viewport", viewportState,
                      palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b );

    char historyText[64];
    snprintf( historyText, sizeof( historyText ), "%d undo / %d redo", data.editorUndoDepth, data.editorRedoDepth );
    DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + EDITOR_HISTORY_STATUS_Y, "History", historyText,
                      palette.textMuted.r, palette.textMuted.g, palette.textMuted.b );
}

} // namespace EditorTab
} // namespace UI
} // namespace SkullbonezCore
