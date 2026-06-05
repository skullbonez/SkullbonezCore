#include "UiComboBox.h"

namespace SkullbonezCore
{
namespace Ui
{

void UiComboBox::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


void UiComboBox::SetDropdownBounds( float x, float y, float w, float h )
{
    m_dropdownBounds = { x, y, w, h };
}


bool UiComboBox::HitBox( int mouseX, int mouseY ) const
{
    return m_bounds.Contains( mouseX, mouseY );
}


int UiComboBox::HitOption( int mouseX, int mouseY, int optionCount ) const
{
    if ( !m_isOpen || optionCount <= 0 || !m_dropdownBounds.Contains( mouseX, mouseY ) )
    {
        return -1;
    }
    const float optionH = m_dropdownBounds.h / static_cast<float>( optionCount );
    const int option = static_cast<int>( ( static_cast<float>( mouseY ) - m_dropdownBounds.y ) / optionH );
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


void UiComboBox::ToggleOpen()
{
    m_isOpen = !m_isOpen;
}


void UiComboBox::Close()
{
    m_isOpen = false;
}


void UiComboBox::Draw( const UiDrawContext& draw, const char* label, const char* const* options, int optionCount, int selectedIndex ) const
{
    draw.Text( m_bounds.x, m_bounds.y + 4.0f, 10.5f, 0.74f, 0.82f, 0.84f, label );
    draw.Rect( m_bounds.x + 66.0f, m_bounds.y + 4.0f, 54.0f, 18.0f, 0.04f, 0.10f, 0.12f, 0.92f );
    draw.Outline( m_bounds.x + 66.0f, m_bounds.y + 4.0f, 54.0f, 18.0f, 0.98f, 0.74f, 0.24f, 0.78f );
    if ( selectedIndex >= 0 && selectedIndex < optionCount )
    {
        draw.Text( m_bounds.x + 72.0f, m_bounds.y + 7.0f, 10.0f, 1.0f, 0.86f, 0.38f, options[selectedIndex] );
    }
    draw.Text( m_bounds.x + 110.0f, m_bounds.y + 6.0f, 10.0f, 0.82f, 0.98f, 1.0f, m_isOpen ? "^" : "v" );

    if ( !m_isOpen )
    {
        return;
    }

    draw.Rect( m_dropdownBounds.x, m_dropdownBounds.y, m_dropdownBounds.w, m_dropdownBounds.h, 0.012f, 0.030f, 0.040f, 0.96f );
    draw.Outline( m_dropdownBounds.x, m_dropdownBounds.y, m_dropdownBounds.w, m_dropdownBounds.h, 0.34f, 0.91f, 1.0f, 0.86f );
    const float optionH = optionCount > 0 ? m_dropdownBounds.h / static_cast<float>( optionCount ) : 0.0f;
    for ( int i = 0; i < optionCount; ++i )
    {
        const float optionY = m_dropdownBounds.y + static_cast<float>( i ) * optionH;
        if ( i == selectedIndex )
        {
            draw.Rect( m_dropdownBounds.x + 2.0f, optionY + 2.0f, m_dropdownBounds.w - 4.0f, optionH - 4.0f, 0.05f, 0.25f, 0.33f, 0.86f );
        }
        draw.Text( m_dropdownBounds.x + 10.0f, optionY + 4.0f, 10.5f,
                   i == selectedIndex ? 0.96f : 0.70f,
                   i == selectedIndex ? 0.98f : 0.84f,
                   i == selectedIndex ? 1.0f : 0.88f,
                   options[i] );
    }
}

} // namespace Ui
} // namespace SkullbonezCore
