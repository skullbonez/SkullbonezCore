/*
File: SkullbonezSource/UI/UITabEditor.cpp
Purpose:
  Implements fixed-object placement controls for the in-engine editor tab.

Mental model:
  This UI chooses the object and arms/disarms placement. The run loop owns
  actual world placement so mouse raycasts and physics mutation stay out of UI.

Related:
  - SkullbonezSource/UI/UITabEditor.h
  - SkullbonezSource/SkullbonezRunInput.cpp
*/
#include "UITabEditor.h"

#include "SkullbonezUI.h"
#include "UIDrawWidgets.h"
#include "UILayout.h"
#include "UIStyle.h"

#include <algorithm>

using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::UI::Widgets;

namespace
{

constexpr float EDITOR_PLACE_TOGGLE_Y = 42.0f;
constexpr float EDITOR_OBJECT_COMBO_Y = 86.0f;
constexpr float EDITOR_STATUS_Y = 126.0f;

const char* const kFixedObjectOptions[] = {
    "Box",
    "Ball",
    "Sphere",
    "Hull wedge",
    "Hull tri prism",
    "Hull tapered",
    "Hull pyramid",
    "Hull hex prism",
    "Hull diamond",
};

void SetContentBounds( SkullbonezCore::UI::EditorTab::UIEditorTabState& state,
                       float contentX,
                       float rowBase,
                       float contentW )
{
    const float contentBaseY = rowBase - EDITOR_PLACE_TOGGLE_Y;
    state.placementToggle.SetBounds( contentX, contentBaseY + EDITOR_PLACE_TOGGLE_Y, (std::max)( 148.0f, contentW * 0.46f ), 24.0f );
    state.objectCombo.SetBounds( contentX, contentBaseY + EDITOR_OBJECT_COMBO_Y, (std::max)( 190.0f, contentW * 0.55f ), 24.0f );
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
    return 170;
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
        const int option = state.objectCombo.HitOption( mouseX, mouseY, FIXED_TYPE_COUNT );
        if ( option >= 0 && option < FIXED_TYPE_COUNT )
        {
            state.selectedFixedObjectType = option;
            result.commands.editor.requestedFixedObjectType = option;
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

    if ( state.placementToggle.HitTest( mouseX, mouseY ) )
    {
        result.commands.editor.toggleFixedPlacement = true;
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
                       state.placementToggle,
                       contentX,
                       scrolledY + EDITOR_PLACE_TOGGLE_Y,
                       colW,
                       "Fixed placement",
                       data.editorFixedPlacementEnabled );

    state.objectCombo.SetBounds( contentX, scrolledY + EDITOR_OBJECT_COMBO_Y, (std::max)( 190.0f, contentW * 0.55f ), 24.0f );
    state.selectedFixedObjectType = std::clamp( state.selectedFixedObjectType, 0, FIXED_TYPE_COUNT - 1 );
    if ( IsRowVisible( contentY, contentH, scrolledY + EDITOR_OBJECT_COMBO_Y, 24.0f ) || state.objectCombo.IsOpen() )
    {
        state.objectCombo.Draw( draw,
                                "Object",
                                kFixedObjectOptions,
                                FIXED_TYPE_COUNT,
                                state.selectedFixedObjectType,
                                mouseX,
                                mouseY );
    }

    DrawLabelValueAt( draw,
                      contentY,
                      contentH,
                      contentX,
                      scrolledY + EDITOR_STATUS_Y,
                      "Viewport",
                      data.editorViewportLookActive ? "Look" : ( data.editorFixedPlacementEnabled ? "Place" : "Cursor" ),
                      palette.accentStrong.r,
                      palette.accentStrong.g,
                      palette.accentStrong.b );
}

} // namespace EditorTab
} // namespace UI
} // namespace SkullbonezCore
