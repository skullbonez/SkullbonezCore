/*
File: SkullbonezSource/UI/UIComboBox.cpp
Purpose:
  Implements UI ComboBox widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  The UI is immediate-mode-style: each frame reads engine state, computes hit
  boxes, emits draw commands, and returns requests for the run loop to apply.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIComboBox.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UIComboBox.h"
#include "../Rendering/Text.h"
#include "UIStyle.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace UI
{

namespace
{
constexpr float COMBO_LABEL_W = 66.0f;
constexpr float COMBO_FIELD_Y = 4.0f;
constexpr float COMBO_FIELD_H = 18.0f;
constexpr float COMBO_DROPDOWN_GAP = 4.0f;
constexpr float COMBO_OPTION_H = 20.0f;

void DrawComboChevron( const UIDrawContext& draw, const UIRect& field, bool open )
{
    const Style::UIPalette& palette = Style::Palette();
    const float cx = field.x + field.w - 12.0f;
    const float cy = field.y + field.h * 0.5f;
    const float step = 2.0f;

    for ( int i = 0; i < 3; ++i )
    {
        const float offset = static_cast<float>( i ) * step;
        const float y = open ? cy + 3.0f - offset : cy - 3.0f + offset;
        draw.Rect( cx - 4.0f + offset,
                   y,
                   2.0f,
                   2.0f,
                   palette.textSecondary.r,
                   palette.textSecondary.g,
                   palette.textSecondary.b,
                   0.96f );
        draw.Rect( cx + 2.0f - offset,
                   y,
                   2.0f,
                   2.0f,
                   palette.textSecondary.r,
                   palette.textSecondary.g,
                   palette.textSecondary.b,
                   0.96f );
    }
}
} // namespace

void UIComboBox::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


UIRect UIComboBox::Bounds() const
{
    return m_bounds;
}


UIRect UIComboBox::DropdownBounds( int optionCount ) const
{
    return DropdownRect( optionCount );
}


bool UIComboBox::HitBox( int mouseX, int mouseY ) const
{
    return m_bounds.Contains( mouseX, mouseY );
}


int UIComboBox::HitOption( int mouseX, int mouseY, int optionCount ) const
{
    const UIRect dropdown = DropdownRect( optionCount );
    if ( !m_isOpen || optionCount <= 0 || !dropdown.Contains( mouseX, mouseY ) )
    {
        return -1;
    }
    const float optionH = dropdown.h / static_cast<float>( optionCount );
    const int option = static_cast<int>( ( static_cast<float>( mouseY ) - dropdown.y ) / optionH );
    return option >= 0 && option < optionCount ? option : -1;
}


bool UIComboBox::IsOpen() const
{
    return m_isOpen;
}


void UIComboBox::SetOpen( bool open )
{
    m_isOpen = open;
}


void UIComboBox::SetDropUp( bool dropUp )
{
    m_dropUp = dropUp;
}


void UIComboBox::SetLabelVisible( bool visible )
{
    m_labelVisible = visible;
}


void UIComboBox::ToggleOpen()
{
    m_isOpen = !m_isOpen;
}


void UIComboBox::Close()
{
    m_isOpen = false;
}


UIRect UIComboBox::FieldRect() const
{
    const float labelW = m_labelVisible ? COMBO_LABEL_W : 0.0f;
    const float fieldW = (std::max)( 54.0f, m_bounds.w - labelW );
    return { m_bounds.x + labelW, m_bounds.y + COMBO_FIELD_Y, fieldW, COMBO_FIELD_H };
}


UIRect UIComboBox::DropdownRect( int optionCount ) const
{
    const UIRect field = FieldRect();
    const float dropdownH = COMBO_OPTION_H * static_cast<float>( (std::max)( 1, optionCount ) );
    const float dropdownY =
        m_dropUp ? field.y - dropdownH - COMBO_DROPDOWN_GAP : field.y + field.h + COMBO_DROPDOWN_GAP;
    return { field.x, dropdownY, field.w, dropdownH };
}


void UIComboBox::Draw( const UIDrawContext& draw,
                       const char* label,
                       const char* const* options,
                       int optionCount,
                       int selectedIndex,
                       int mouseX,
                       int mouseY,
                       uint32_t disabledOptionMask ) const
{
    const char* selectedText = "";
    if ( selectedIndex >= 0 && selectedIndex < optionCount && options )
    {
        selectedText = options[selectedIndex];
    }
    Draw( draw, label, selectedText, options, optionCount, selectedIndex, mouseX, mouseY, disabledOptionMask );
}


void UIComboBox::Draw( const UIDrawContext& draw,
                       const char* label,
                       const char* selectedText,
                       const char* const* options,
                       int optionCount,
                       int selectedIndex,
                       int mouseX,
                       int mouseY,
                       uint32_t disabledOptionMask ) const
{
    const Style::UIPalette& palette = Style::Palette();
    const float radius = Style::Radii().control;
    const UIRect field = FieldRect();
    const UIRect dropdown = DropdownRect( optionCount );
    const bool fieldHovered = field.Contains( mouseX, mouseY );
    const bool selectedDisabled =
        selectedIndex >= 0 && selectedIndex < 32 && ( disabledOptionMask & ( 1u << selectedIndex ) ) != 0;
    if ( m_labelVisible && label && label[0] != '\0' )
    {
        draw.Text( m_bounds.x,
                   m_bounds.y + 4.0f,
                   10.5f,
                   palette.textSecondary.r,
                   palette.textSecondary.g,
                   palette.textSecondary.b,
                   label );
    }
    draw.RoundedPanel( field,
                       radius,
                       fieldHovered ? palette.controlHover : palette.control,
                       fieldHovered ? palette.innerBorder : palette.border );
    if ( selectedText && selectedText[0] != '\0' )
    {
        draw.Text(
            field.x + 6.0f,
            field.y + 3.0f,
            10.0f,
            selectedDisabled ? palette.textMuted.r : ( fieldHovered ? palette.textPrimary.r : palette.textSecondary.r ),
            selectedDisabled ? palette.textMuted.g : ( fieldHovered ? palette.textPrimary.g : palette.textSecondary.g ),
            selectedDisabled ? palette.textMuted.b : ( fieldHovered ? palette.textPrimary.b : palette.textSecondary.b ),
            selectedText );
    }
    DrawComboChevron( draw, field, m_isOpen );

    if ( !m_isOpen )
    {
        return;
    }

    // Open combos are overlay surfaces.  Flush anything already queued so the
    // dropdown backer can cover earlier labels before its option text is added.
    Text::Text2d::FlushQuads();
    Text::Text2d::FlushText();

    draw.RoundedRect( dropdown.x - 4.0f,
                      dropdown.y - 4.0f,
                      dropdown.w + 8.0f,
                      dropdown.h + 8.0f,
                      radius + 2.0f,
                      0.0f,
                      0.0f,
                      0.0f,
                      0.26f );
    draw.RoundedPanel( dropdown, radius, palette.windowRaised, palette.border );
    const float optionH = optionCount > 0 ? dropdown.h / static_cast<float>( optionCount ) : 0.0f;
    const int hoveredOption = HitOption( mouseX, mouseY, optionCount );
    for ( int i = 0; i < optionCount; ++i )
    {
        if ( !options )
        {
            break;
        }
        const float optionY = dropdown.y + static_cast<float>( i ) * optionH;
        const bool isDisabled = i < 32 && ( disabledOptionMask & ( 1u << i ) ) != 0;
        const bool isSelected = i == selectedIndex;
        const bool isHovered = i == hoveredOption && !isDisabled;
        if ( isSelected || isHovered )
        {
            const Style::UIColor rowFill =
                isDisabled ? palette.windowSubtle : ( isHovered ? palette.controlHover : palette.control );
            draw.RoundedRect( dropdown.x + 2.0f,
                              optionY + 2.0f,
                              dropdown.w - 4.0f,
                              optionH - 4.0f,
                              radius - 2.0f,
                              rowFill.r,
                              rowFill.g,
                              rowFill.b,
                              rowFill.a );
        }
        draw.Text( dropdown.x + 10.0f,
                   optionY + 4.0f,
                   10.5f,
                   isDisabled ? palette.textMuted.r
                              : ( isHovered ? palette.textPrimary.r
                                            : ( isSelected ? palette.accentStrong.r : palette.textSecondary.r ) ),
                   isDisabled ? palette.textMuted.g
                              : ( isHovered ? palette.textPrimary.g
                                            : ( isSelected ? palette.accentStrong.g : palette.textSecondary.g ) ),
                   isDisabled ? palette.textMuted.b
                              : ( isHovered ? palette.textPrimary.b
                                            : ( isSelected ? palette.accentStrong.b : palette.textSecondary.b ) ),
                   options[i] );
    }
}

} // namespace UI
} // namespace SkullbonezCore
