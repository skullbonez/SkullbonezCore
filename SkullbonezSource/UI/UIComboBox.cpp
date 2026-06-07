#include "UIComboBox.h"
#include "../SkullbonezText.h"

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
    const float cx = field.x + field.w - 12.0f;
    const float cy = field.y + field.h * 0.5f;
    const float step = 2.0f;

    for ( int i = 0; i < 3; ++i )
    {
        const float offset = static_cast<float>( i ) * step;
        const float y = open ? cy + 3.0f - offset : cy - 3.0f + offset;
        draw.Rect( cx - 4.0f + offset, y, 2.0f, 2.0f, 0.82f, 0.98f, 1.0f, 0.96f );
        draw.Rect( cx + 2.0f - offset, y, 2.0f, 2.0f, 0.82f, 0.98f, 1.0f, 0.96f );
    }
}
} // namespace

void UIComboBox::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
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
    const float fieldW = (std::max)( 54.0f, m_bounds.w - COMBO_LABEL_W );
    return { m_bounds.x + COMBO_LABEL_W, m_bounds.y + COMBO_FIELD_Y, fieldW, COMBO_FIELD_H };
}


UIRect UIComboBox::DropdownRect( int optionCount ) const
{
    const UIRect field = FieldRect();
    const float dropdownH = COMBO_OPTION_H * static_cast<float>( (std::max)( 1, optionCount ) );
    const float dropdownY = m_dropUp ? field.y - dropdownH - COMBO_DROPDOWN_GAP : field.y + field.h + COMBO_DROPDOWN_GAP;
    return { field.x, dropdownY, field.w, dropdownH };
}


void UIComboBox::Draw( const UIDrawContext& draw, const char* label, const char* const* options, int optionCount, int selectedIndex, int mouseX, int mouseY, uint32_t disabledOptionMask ) const
{
    const char* selectedText = "";
    if ( selectedIndex >= 0 && selectedIndex < optionCount && options )
    {
        selectedText = options[selectedIndex];
    }
    Draw( draw, label, selectedText, options, optionCount, selectedIndex, mouseX, mouseY, disabledOptionMask );
}


void UIComboBox::Draw( const UIDrawContext& draw, const char* label, const char* selectedText, const char* const* options, int optionCount, int selectedIndex, int mouseX, int mouseY, uint32_t disabledOptionMask ) const
{
    const UIRect field = FieldRect();
    const UIRect dropdown = DropdownRect( optionCount );
    const bool fieldHovered = field.Contains( mouseX, mouseY );
    const bool selectedDisabled = selectedIndex >= 0 && selectedIndex < 32 && ( disabledOptionMask & ( 1u << selectedIndex ) ) != 0;
    draw.Text( m_bounds.x, m_bounds.y + 4.0f, 10.5f, 0.74f, 0.82f, 0.84f, label );
    draw.Rect( field.x, field.y, field.w, field.h, fieldHovered ? 0.130f : 0.040f, fieldHovered ? 0.105f : 0.120f, fieldHovered ? 0.026f : 0.150f, 0.94f );
    draw.Outline( field.x, field.y, field.w, field.h, fieldHovered ? 1.0f : 0.34f, fieldHovered ? 0.84f : 0.91f, fieldHovered ? 0.34f : 1.0f, fieldHovered ? 0.96f : 0.78f );
    if ( selectedText && selectedText[0] != '\0' )
    {
        draw.Text( field.x + 6.0f,
                   field.y + 3.0f,
                   10.0f,
                   selectedDisabled ? 0.38f : ( fieldHovered ? 1.0f : 0.86f ),
                   selectedDisabled ? 0.48f : ( fieldHovered ? 0.88f : 0.98f ),
                   selectedDisabled ? 0.52f : ( fieldHovered ? 0.42f : 1.0f ),
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

    draw.Rect( dropdown.x - 3.0f, dropdown.y - 3.0f, dropdown.w + 6.0f, dropdown.h + 6.0f, 0.004f, 0.012f, 0.018f, 1.0f );
    draw.Rect( dropdown.x, dropdown.y, dropdown.w, dropdown.h, 0.012f, 0.030f, 0.040f, 1.0f );
    draw.Outline( dropdown.x, dropdown.y, dropdown.w, dropdown.h, 0.34f, 0.91f, 1.0f, 0.86f );
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
            draw.Rect( dropdown.x + 2.0f, optionY + 2.0f, dropdown.w - 4.0f, optionH - 4.0f,
                       isDisabled ? 0.026f : ( isHovered ? 0.150f : 0.050f ),
                       isDisabled ? 0.050f : ( isHovered ? 0.118f : 0.250f ),
                       isDisabled ? 0.064f : ( isHovered ? 0.032f : 0.330f ),
                       isDisabled ? 0.70f : ( isHovered ? 0.92f : 0.86f ) );
        }
        draw.Text( dropdown.x + 10.0f, optionY + 4.0f, 10.5f,
                   isDisabled ? 0.34f : ( isHovered ? 1.0f : ( isSelected ? 0.78f : 0.70f ) ),
                   isDisabled ? 0.42f : ( isHovered ? 0.88f : ( isSelected ? 0.98f : 0.84f ) ),
                   isDisabled ? 0.46f : ( isHovered ? 0.38f : ( isSelected ? 1.0f : 0.88f ) ),
                   options[i] );
    }
}

} // namespace UI
} // namespace SkullbonezCore
