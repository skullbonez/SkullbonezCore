/*
File: SkullbonezSource/UI/UIComboBox.cpp
Purpose:
  Adapts retained popup placement and openness to stateless combo geometry,
  hit-testing, and draw contracts.

Summary:
  The wrapper retains only bounds, popup direction, label visibility, and open
  state. UIVisualState and option interaction values remain disposable caller
  facts, while UIDrawWidgets owns all geometry, style, and command recording.

Invariants:
  - One ComboLayout is the sole interaction, field, and popup geometry
    authority for each operation.
  - Disabled options remain pointer-blocking but never become enabled actions.

Related:
  - SkullbonezSource/UI/UIComboBox.h
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UIComboBox.h"
#include "UIDrawWidgets.h"

namespace SkullbonezCore
{
namespace UI
{

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
    return Widgets::ResolveComboLayout( m_bounds, m_labelVisible, m_dropUp, optionCount ).popupBounds;
}


bool UIComboBox::HitBox( int mouseX, int mouseY ) const
{
    constexpr UIVisualState kState = UIVisualState::Visible | UIVisualState::Enabled;
    const Widgets::ComboLayout layout = Widgets::ResolveComboLayout( m_bounds, m_labelVisible, m_dropUp, 0 );
    return Widgets::CanActivateComponent( layout.interactionBounds, kState, mouseX, mouseY );
}


int UIComboBox::HitOption( int mouseX, int mouseY, int optionCount ) const
{
    const UIVisualState state = m_isOpen ? UIVisualState::Visible : UIVisualState::None;
    const Widgets::ComboLayout layout = Widgets::ResolveComboLayout( m_bounds, m_labelVisible, m_dropUp, optionCount );
    return Widgets::ComboOptionAtPointer( layout.popupBounds, state, mouseX, mouseY, optionCount );
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


void UIComboBox::Draw( const UIDrawContext& draw, const char* label, const char* const* options, int optionCount,
                       int selectedIndex, int mouseX, int mouseY, uint32_t disabledOptionMask ) const
{
    const char* selectedText = "";

    if ( selectedIndex >= 0 && selectedIndex < optionCount && options )
    {
        selectedText = options[selectedIndex];
    }

    Draw( draw, label, selectedText, options, optionCount, selectedIndex, mouseX, mouseY, disabledOptionMask );
}


void UIComboBox::Draw( const UIDrawContext& draw, const char* label, const char* selectedText, const char* const* options,
                       int optionCount, int selectedIndex, int mouseX, int mouseY, uint32_t disabledOptionMask ) const
{
    const Widgets::ComboLayout layout = Widgets::ResolveComboLayout( m_bounds, m_labelVisible, m_dropUp, optionCount );
    UIVisualState state = UIVisualState::Visible | UIVisualState::Enabled;

    if ( Widgets::ContainsComponent( layout.fieldBounds, state, mouseX, mouseY ) )
    {
        state |= UIVisualState::Hovered;
    }

    const bool selectedDisabled = selectedIndex >= 0 && selectedIndex < 32 &&
                                  ( disabledOptionMask & ( 1u << selectedIndex ) ) != 0;
    Widgets::DrawComboField( draw, layout, label, selectedText, m_labelVisible, m_isOpen, state, !selectedDisabled,
                             Widgets::ComponentAppearance::Established );

    if ( !m_isOpen )
    {
        return;
    }

    const int hoveredOption = Widgets::ComboOptionAtPointer( layout.popupBounds, state, mouseX, mouseY, optionCount );
    Widgets::DrawComboPopup( draw, layout, options, optionCount, selectedIndex, hoveredOption, disabledOptionMask, state,
                             Widgets::ComponentAppearance::Established );
}

} // namespace UI
} // namespace SkullbonezCore
