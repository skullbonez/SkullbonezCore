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

int UIComboPresentationView::OptionCount() const noexcept
{
    return static_cast<int>( options.size() );
}


const char* UIComboPresentationView::SelectedText() const noexcept
{
    if ( selectedTextOverride )
    {
        return selectedTextOverride;
    }

    return selectedIndex >= 0 && selectedIndex < OptionCount() && options[static_cast<std::size_t>( selectedIndex )]
               ? options[static_cast<std::size_t>( selectedIndex )]
               : "";
}


bool UIComboPresentationView::SelectedOptionEnabled() const noexcept
{
    return selectedIndex < 0 || selectedIndex >= 32 || ( disabledOptionMask & ( 1u << selectedIndex ) ) == 0;
}

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


void UIComboBox::Draw( const UIDrawContext& draw, const char* label, const UIComboPresentationView& presentation,
                       UIPointerPosition pointer ) const
{
    const int optionCount = presentation.OptionCount();
    const Widgets::ComboLayout layout = Widgets::ResolveComboLayout( m_bounds, m_labelVisible, m_dropUp, optionCount );
    UIVisualState state = UIVisualState::Visible | UIVisualState::Enabled;

    if ( Widgets::ContainsComponent( layout.fieldBounds, state, pointer.x, pointer.y ) )
    {
        state |= UIVisualState::Hovered;
    }

    Widgets::DrawComboField( draw, layout, label, presentation.SelectedText(), m_labelVisible, m_isOpen, state,
                             presentation.SelectedOptionEnabled(), Widgets::ComponentAppearance::Established );

    if ( !m_isOpen )
    {
        return;
    }

    const int hoveredOption = Widgets::ComboOptionAtPointer( layout.popupBounds, state, pointer.x, pointer.y, optionCount );
    Widgets::DrawComboPopup( draw, layout, presentation, hoveredOption, state, Widgets::ComponentAppearance::Established );
}

} // namespace UI
} // namespace SkullbonezCore
