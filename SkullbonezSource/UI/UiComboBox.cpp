#include "UiComboBox.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Ui
{

namespace
{
constexpr float COMBO_LABEL_W = 66.0f;
constexpr float COMBO_FIELD_Y = 4.0f;
constexpr float COMBO_FIELD_H = 18.0f;
constexpr float COMBO_DROPDOWN_GAP = 4.0f;
constexpr float COMBO_OPTION_H = 20.0f;

void DrawComboChevron( const UiDrawContext& draw, const UiRect& field, bool open )
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

void UiComboBox::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


bool UiComboBox::HitBox( int mouseX, int mouseY ) const
{
    return m_bounds.Contains( mouseX, mouseY );
}


int UiComboBox::HitOption( int mouseX, int mouseY, int optionCount ) const
{
    const UiRect dropdown = DropdownRect( optionCount );
    if ( !m_isOpen || optionCount <= 0 || !dropdown.Contains( mouseX, mouseY ) )
    {
        return -1;
    }
    const float optionH = dropdown.h / static_cast<float>( optionCount );
    const int option = static_cast<int>( ( static_cast<float>( mouseY ) - dropdown.y ) / optionH );
    return option >= 0 && option < optionCount ? option : -1;
}


bool UiComboBox::IsOpen() const
{
    return m_isOpen;
}


void UiComboBox::SetOpen( bool open )
{
    m_isOpen = open;
}


void UiComboBox::SetDropUp( bool dropUp )
{
    m_dropUp = dropUp;
}


void UiComboBox::ToggleOpen()
{
    m_isOpen = !m_isOpen;
}


void UiComboBox::Close()
{
    m_isOpen = false;
}


UiRect UiComboBox::FieldRect() const
{
    const float fieldW = (std::max)( 54.0f, m_bounds.w - COMBO_LABEL_W );
    return { m_bounds.x + COMBO_LABEL_W, m_bounds.y + COMBO_FIELD_Y, fieldW, COMBO_FIELD_H };
}


UiRect UiComboBox::DropdownRect( int optionCount ) const
{
    const UiRect field = FieldRect();
    const float dropdownH = COMBO_OPTION_H * static_cast<float>( (std::max)( 1, optionCount ) );
    const float dropdownY = m_dropUp ? field.y - dropdownH - COMBO_DROPDOWN_GAP : field.y + field.h + COMBO_DROPDOWN_GAP;
    return { field.x, dropdownY, field.w, dropdownH };
}


void UiComboBox::Draw( const UiDrawContext& draw, const char* label, const char* const* options, int optionCount, int selectedIndex, int mouseX, int mouseY ) const
{
    const char* selectedText = "";
    if ( selectedIndex >= 0 && selectedIndex < optionCount && options )
    {
        selectedText = options[selectedIndex];
    }
    Draw( draw, label, selectedText, options, optionCount, selectedIndex, mouseX, mouseY );
}


void UiComboBox::Draw( const UiDrawContext& draw, const char* label, const char* selectedText, const char* const* options, int optionCount, int selectedIndex, int mouseX, int mouseY ) const
{
    const UiRect field = FieldRect();
    const UiRect dropdown = DropdownRect( optionCount );
    const bool fieldHovered = field.Contains( mouseX, mouseY );
    draw.Text( m_bounds.x, m_bounds.y + 4.0f, 10.5f, 0.74f, 0.82f, 0.84f, label );
    draw.Rect( field.x, field.y, field.w, field.h, fieldHovered ? 0.060f : 0.040f, fieldHovered ? 0.160f : 0.100f, fieldHovered ? 0.190f : 0.120f, 0.92f );
    draw.Outline( field.x, field.y, field.w, field.h, fieldHovered ? 0.34f : 0.98f, fieldHovered ? 0.91f : 0.74f, fieldHovered ? 1.0f : 0.24f, fieldHovered ? 0.96f : 0.78f );
    if ( selectedText && selectedText[0] != '\0' )
    {
        draw.Text( field.x + 6.0f, field.y + 3.0f, 10.0f, fieldHovered ? 0.90f : 1.0f, fieldHovered ? 0.98f : 0.86f, fieldHovered ? 1.0f : 0.38f, selectedText );
    }
    DrawComboChevron( draw, field, m_isOpen );

    if ( !m_isOpen )
    {
        return;
    }

    draw.Rect( dropdown.x, dropdown.y, dropdown.w, dropdown.h, 0.012f, 0.030f, 0.040f, 0.96f );
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
        const bool isSelected = i == selectedIndex;
        const bool isHovered = i == hoveredOption;
        if ( isSelected || isHovered )
        {
            draw.Rect( dropdown.x + 2.0f, optionY + 2.0f, dropdown.w - 4.0f, optionH - 4.0f,
                       isHovered ? 0.070f : 0.050f,
                       isHovered ? 0.340f : 0.250f,
                       isHovered ? 0.430f : 0.330f,
                       isHovered ? 0.92f : 0.86f );
        }
        draw.Text( dropdown.x + 10.0f, optionY + 4.0f, 10.5f,
                   isHovered ? 1.0f : ( isSelected ? 0.96f : 0.70f ),
                   isHovered ? 0.98f : ( isSelected ? 0.98f : 0.84f ),
                   isHovered ? 0.82f : ( isSelected ? 1.0f : 0.88f ),
                   options[i] );
    }
}

} // namespace Ui
} // namespace SkullbonezCore
