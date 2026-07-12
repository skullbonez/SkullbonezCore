/*
File: SkullbonezSource/UI/UITabEditor.cpp
Purpose:
  Implements object placement and selection controls for the in-engine editor tab.

Mental model:
  This UI chooses the object and editor toggles. The run loop owns actual
  selection, placement, and physics mutation so mouse raycasts stay out of UI.

Glossary:
  Editor command: Intent emitted by a widget and applied later by runtime code.
  Placement mode: Editor state where a picked object kind can be inserted into
  the active scene.

Invariants:
  - Object labels must stay in the same order as `EditorTab::OBJECT_*`.
  - The tab emits command intents only; it never mutates scene objects directly.

Related:
  - SkullbonezSource/UI/UITabEditor.h
  - SkullbonezSource/Runtime/RunInput.cpp
*/
#include "UITabEditor.h"

#include "UI.h"
#include "UIDrawWidgets.h"
#include "UILayout.h"
#include "UIStyle.h"

#include <algorithm>
#include <cstdio>

using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::UI::Widgets;

namespace
{

constexpr float EDITOR_MODE_TOGGLE_Y = 42.0f;
constexpr float EDITOR_PLACE_TOGGLE_Y = 76.0f;
constexpr float EDITOR_STATIC_TOGGLE_Y = 110.0f;
constexpr float EDITOR_OBJECT_COMBO_Y = 154.0f;
constexpr float EDITOR_STATUS_Y = 194.0f;
constexpr float EDITOR_HISTORY_STATUS_Y = 222.0f;

const char* const kEditorObjectOptions[] = {
    "Box",
    "Ball",
    "Sphere",
    "Hull wedge",
    "Hull tri prism",
    "Hull tapered",
    "Hull pyramid",
    "Hull hex prism",
    "Hull diamond",
    "Rock slab",
    "Rock lump",
    "Rock shard",
    "Rock chipped",
    "Root small",
    "Root large",
    "Tree small",
    "Tree pine",
    "Tree cedar",
    "Tree small slope",
    "Tree pine slope",
    "Tree cedar slope",
    "Tree small sleep",
    "Tree pine sleep",
    "Tree cedar sleep",
    "Tree small rooted",
    "Tree pine rooted",
    "Tree cedar rooted",
    "Pine shedding",
    "Ragdoll",
    "Ragdoll sleep",
    "Brick house low",
    "Brick house high",
    "Cute house low",
    "Cute house high",
    "Triple decker low",
    "Triple decker high",
    "Brick wall 200",
};
// Invariant: This label table is the UI-facing form of the editor object enum.
// Runtime placement uses the integer id, so table order is part of the contract.
static_assert( sizeof( kEditorObjectOptions ) / sizeof( kEditorObjectOptions[0] ) ==
               SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT );

void SetContentBounds( SkullbonezCore::UI::EditorTab::UIEditorTabState& state,
                       float contentX,
                       float rowBase,
                       float contentW )
{
    const float contentBaseY = rowBase - EDITOR_MODE_TOGGLE_Y;
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    state.editorModeToggle.SetBounds( contentX, contentBaseY + EDITOR_MODE_TOGGLE_Y, colW, 24.0f );
    state.placementModeToggle.SetBounds( contentX, contentBaseY + EDITOR_PLACE_TOGGLE_Y, colW, 24.0f );
    state.staticObjectToggle.SetBounds( contentX, contentBaseY + EDITOR_STATIC_TOGGLE_Y, colW, 24.0f );
    state.objectCombo.SetBounds( contentX,
                                 contentBaseY + EDITOR_OBJECT_COMBO_Y,
                                 (std::max)( 190.0f, contentW * 0.55f ),
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


const char* ObjectLabel( int objectType )
{
    const int type = std::clamp( objectType, 0, OBJECT_TYPE_COUNT - 1 );
    return kEditorObjectOptions[type];
}


bool HandleContentClick( UIEditorTabState& state,
                         InGameUIInputResult& result,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float rowBase,
                         float contentW )
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


void Draw( UIEditorTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int mouseX,
           int mouseY )
{
    const Style::UIPalette& palette = Style::Palette();
    const float colW = (std::max)( 148.0f, contentW * 0.46f );

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Editor" );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.editorModeToggle,
                       contentX,
                       scrolledY + EDITOR_MODE_TOGGLE_Y,
                       colW,
                       "Editor mode",
                       data.editorModeEnabled );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.placementModeToggle,
                       contentX,
                       scrolledY + EDITOR_PLACE_TOGGLE_Y,
                       colW,
                       "Place mode",
                       data.editorPlacementMode );
    DrawContentToggle( draw,
                       contentY,
                       contentH,
                       state.staticObjectToggle,
                       contentX,
                       scrolledY + EDITOR_STATIC_TOGGLE_Y,
                       colW,
                       "Static object",
                       data.editorPlaceStatic );

    state.objectCombo.SetBounds( contentX,
                                 scrolledY + EDITOR_OBJECT_COMBO_Y,
                                 (std::max)( 190.0f, contentW * 0.55f ),
                                 24.0f );
    state.selectedObjectType = std::clamp( data.editorObjectType, 0, OBJECT_TYPE_COUNT - 1 );
    if ( IsRowVisible( contentY, contentH, scrolledY + EDITOR_OBJECT_COMBO_Y, 24.0f ) || state.objectCombo.IsOpen() )
    {
        state.objectCombo
            .Draw( draw, "Object", kEditorObjectOptions, OBJECT_TYPE_COUNT, state.selectedObjectType, mouseX, mouseY );
    }

    const char* viewportState = "Cursor";
    if ( data.editorViewportLookActive )
    {
        viewportState = "Look";
    }
    else if ( data.editorModeEnabled )
    {
        viewportState =
            data.editorPlacementMode ? ( data.editorPlaceStatic ? "Place static" : "Place dynamic" ) : "Gizmo";
    }

    DrawLabelValueAt( draw,
                      contentY,
                      contentH,
                      contentX,
                      scrolledY + EDITOR_STATUS_Y,
                      "Viewport",
                      viewportState,
                      palette.accentStrong.r,
                      palette.accentStrong.g,
                      palette.accentStrong.b );
    char historyText[64];
    snprintf( historyText, sizeof( historyText ), "%d undo / %d redo", data.editorUndoDepth, data.editorRedoDepth );
    DrawLabelValueAt( draw,
                      contentY,
                      contentH,
                      contentX,
                      scrolledY + EDITOR_HISTORY_STATUS_Y,
                      "History",
                      historyText,
                      palette.textMuted.r,
                      palette.textMuted.g,
                      palette.textMuted.b );
}

} // namespace EditorTab
} // namespace UI
} // namespace SkullbonezCore
