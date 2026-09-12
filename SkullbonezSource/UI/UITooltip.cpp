#include "UITooltip.h"

#include "UIFontMetrics.h"
#include "UIStyle.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore::UI
{
namespace
{
bool HasText( const char* value )
{
    return value && value[0];
}

void DrawDescription( const UIDrawContext& draw, const UIRect& bounds, const char* text )
{
    if ( !HasText( text ) )
    {
        return;
    }

    const auto& palette = Style::Palette();
    const float available = (std::max)( 0.0f, bounds.w - 20.0f );
    const char* cursor = text;
    for ( int row = 0; row < 3 && *cursor; ++row )
    {
        char line[128] {};
        int count = 0;
        int lastSpace = 0;
        while ( cursor[count] && count < 126 )
        {
            line[count] = cursor[count];
            line[count + 1] = '\0';
            if ( UIFontMetrics::MeasureText( 11.0f, line ) > available )
            {
                line[count] = '\0';
                break;
            }
            if ( cursor[count] == ' ' )
            {
                lastSpace = count;
            }
            ++count;
        }
        if ( cursor[count] && lastSpace > 0 )
        {
            count = lastSpace;
            line[count] = '\0';
        }
        if ( count == 0 )
        {
            break;
        }
        draw.Text( bounds.x + 10.0f, bounds.y + 10.0f + static_cast<float>( row ) * 15.0f, 11.0f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b, line );
        cursor += count;
        while ( *cursor == ' ' )
        {
            ++cursor;
        }
    }
}
} // namespace

void UITooltip::Update( const UITooltipTarget& target, double now, bool gestureActive )
{
    if ( gestureActive || !std::isfinite( now ) || !target.id || ( !target.hovered && !target.focused ) )
    {
        Dismiss();
        return;
    }
    if ( target.id != m_target.id || now < m_hoverStarted )
    {
        m_hoverStarted = now;
    }
    m_target = target;
}

void UITooltip::Dismiss()
{
    m_target = {};
    m_hoverStarted = 0.0;
}

bool UITooltip::Visible( double now ) const
{
    return m_target.id != 0 && std::isfinite( now ) && now >= m_hoverStarted && ( m_target.focused || now - m_hoverStarted >= 0.45 );
}

UIRect UITooltip::Bounds( const UIRect& window ) const
{
    const float width = (std::min)( 360.0f, (std::max)( 0.0f, window.w ) );
    const char* description = m_target.enabled || !HasText( m_target.text.disabledReason ) ? m_target.text.action : m_target.text.disabledReason;
    const bool singleLine = !HasText( description ) || UIFontMetrics::MeasureText( 11.0f, description ) <= (std::max)( 0.0f, width - 20.0f );
    const float descriptionHeight = singleLine ? 15.0f : 45.0f;
    const float detailHeight = ( HasText( m_target.text.units ) ? 15.0f : 0.0f ) + ( HasText( m_target.text.shortcut ) ? 15.0f : 0.0f );
    const float height = (std::min)( 20.0f + descriptionHeight + detailHeight, (std::max)( 0.0f, window.h ) );
    const float x = std::clamp( m_target.bounds.x, window.x, window.x + (std::max)( 0.0f, window.w ) - width );
    const float below = m_target.bounds.y + m_target.bounds.h + 6.0f;
    const float y = below + height <= window.y + window.h ? below : m_target.bounds.y - height - 6.0f;
    return { x, std::clamp( y, window.y, window.y + (std::max)( 0.0f, window.h ) - height ), width, height };
}

void UITooltip::Draw( const UIDrawContext& draw, const UIRect& window, double now ) const
{
    if ( !Visible( now ) || window.w <= 0.0f || window.h <= 0.0f )
    {
        return;
    }
    const auto bounds = Bounds( window );
    const auto& palette = Style::Palette();
    auto fill = palette.windowRaised;
    fill.a = 1.0f;
    draw.BeginForeground();
    draw.PushClip( bounds );
    draw.RoundedPanel( bounds, 5.0f, fill, palette.border );
    DrawDescription( draw, bounds, m_target.enabled || !HasText( m_target.text.disabledReason ) ? m_target.text.action : m_target.text.disabledReason );
    float detailY = bounds.y + bounds.h - 10.0f;
    if ( HasText( m_target.text.shortcut ) )
    {
        detailY -= 15.0f;
        draw.Text( bounds.x + 10.0f, detailY, 10.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, m_target.text.shortcut );
    }
    if ( HasText( m_target.text.units ) )
    {
        detailY -= 15.0f;
        draw.Text( bounds.x + 10.0f, detailY, 10.0f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b, m_target.text.units );
    }
    draw.PopClip();
    draw.EndForeground();
}
} // namespace SkullbonezCore::UI
