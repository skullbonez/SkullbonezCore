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

#include <algorithm>

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
    return Widgets::ResolveComboLayout( m_bounds, m_labelVisible, ResolveDropUp( optionCount ),
                                        VisibleOptionCount( optionCount ) )
        .popupBounds;
}

void UIComboBox::SetPopupViewport( const UIRect& viewport )
{
    m_popupViewport = viewport;
}

bool UIComboBox::ResolveDropUp( int optionCount ) const
{
    if ( m_popupViewport.h <= 0.0f )
    {
        return m_dropUp;
    }
    const UIRect down = Widgets::ResolveComboLayout( m_bounds, m_labelVisible, false, optionCount ).popupBounds;
    const UIRect up = Widgets::ResolveComboLayout( m_bounds, m_labelVisible, true, optionCount ).popupBounds;
    // Drawing and picking resolve the same direction from the current field,
    // so a bottom drawer popup can use the space above its trigger.
    if ( down.y + down.h > m_popupViewport.y + m_popupViewport.h && up.y >= m_popupViewport.y )
    {
        return true;
    }
    if ( m_scrollable && down.y + down.h > m_popupViewport.y + m_popupViewport.h && up.y < m_popupViewport.y )
    {
        return m_bounds.y - m_popupViewport.y > m_popupViewport.y + m_popupViewport.h - down.y;
    }
    return m_dropUp && up.y >= m_popupViewport.y;
}


void UIComboBox::SetScrollable( bool enabled )
{
    m_scrollable = enabled;
    if ( !enabled )
    {
        m_firstVisibleOption = 0;
    }
}

int UIComboBox::VisibleOptionCount( int optionCount ) const
{
    if ( !m_scrollable || m_popupViewport.h <= 0.0f || optionCount <= 0 )
    {
        return (std::max)( 0, optionCount );
    }
    const bool up = ResolveDropUp( optionCount );
    const UIRect row = Widgets::ResolveComboLayout( m_bounds, m_labelVisible, up, 1 ).popupBounds;
    const float space = up ? row.y + row.h - m_popupViewport.y : m_popupViewport.y + m_popupViewport.h - row.y;
    return std::clamp( static_cast<int>( (std::max)( 0.0f, space ) / row.h ), 1, optionCount );
}

int UIComboBox::FirstVisibleOption( int optionCount ) const
{
    return m_scrollable
               ? std::clamp( m_firstVisibleOption, 0, (std::max)( 0, optionCount - VisibleOptionCount( optionCount ) ) )
               : 0;
}

void UIComboBox::ScrollOptions( int rows, int optionCount )
{
    if ( m_scrollable )
    {
        m_firstVisibleOption = std::clamp( FirstVisibleOption( optionCount ) + rows, 0,
                                           (std::max)( 0, optionCount - VisibleOptionCount( optionCount ) ) );
    }
}

bool UIComboBox::HitBox( int mouseX, int mouseY ) const
{
    constexpr UIVisualState kState = UIVisualState::Visible | UIVisualState::Enabled;
    const Widgets::ComboLayout layout = Widgets::ResolveComboLayout( m_bounds, m_labelVisible, m_dropUp, 0 );
    return Widgets::CanActivateComponent( layout.interactionBounds, kState, mouseX, mouseY );
}


int UIComboBox::HitOption( int mouseX, int mouseY, int optionCount ) const
{
    if ( m_popupViewport.h > 0.0f && !m_popupViewport.Contains( mouseX, mouseY ) )
    {
        return -1;
    }
    const UIVisualState state = m_isOpen ? UIVisualState::Visible : UIVisualState::None;
    const int visible = VisibleOptionCount( optionCount );
    const int row = Widgets::ComboOptionAtPointer( DropdownBounds( optionCount ), state, mouseX, mouseY, visible );
    return row >= 0 ? FirstVisibleOption( optionCount ) + row : -1;
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
    const int visible = VisibleOptionCount( optionCount );
    const int first = FirstVisibleOption( optionCount );
    const Widgets::ComboLayout layout = Widgets::ResolveComboLayout( m_bounds, m_labelVisible, ResolveDropUp( optionCount ),
                                                                     visible );
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

    const int hoveredOption = Widgets::ComboOptionAtPointer( layout.popupBounds, state, pointer.x, pointer.y, visible );
    const UIComboPresentationView visibleOptions { presentation.options.subspan( static_cast<std::size_t>( first ),
                                                                                 static_cast<std::size_t>( visible ) ),
                                                   presentation.selectedIndex - first,
                                                   first < 32 ? presentation.disabledOptionMask >> first : 0u };
    draw.BeginForeground();
    if ( m_popupViewport.h > 0.0f )
    {
        draw.PushClip( m_popupViewport );
    }
    Widgets::DrawComboPopup( draw, layout, visibleOptions, hoveredOption, state, Widgets::ComponentAppearance::Established );
    if ( visible < optionCount )
    {
        const float thumbHeight = layout.popupBounds.h * static_cast<float>( visible ) / static_cast<float>( optionCount );
        const float thumbY = layout.popupBounds.y + ( layout.popupBounds.h - thumbHeight ) * static_cast<float>( first ) /
                                                        static_cast<float>( optionCount - visible );
        draw.RoundedRect( layout.popupBounds.x + layout.popupBounds.w - 4.0f, thumbY, 3.0f, thumbHeight, 1.0f, 0.55f, 0.58f,
                          0.62f, 1.0f );
    }
    if ( m_popupViewport.h > 0.0f )
    {
        draw.PopClip();
    }
    draw.EndForeground();
}

} // namespace UI
} // namespace SkullbonezCore
