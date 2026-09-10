#include "UIPanelTransitions.h"
#include <algorithm>
#include <cmath>

namespace SkullbonezCore::UI
{
namespace
{
constexpr std::array<UIPoint, static_cast<size_t>( UIPanel::Count )> PANEL_DIRECTIONS = { { { 0, 0 },
                                                                                            { -20, 0 },
                                                                                            { 20, 0 },
                                                                                            { 0, 12 },
                                                                                            { 0, 20 },
                                                                                            { 0, 16 },
                                                                                            { 0, 16 },
                                                                                            { 0, 12 },
                                                                                            { 0, 12 },
                                                                                            { 0, 12 },
                                                                                            { 0, 12 },
                                                                                            { 0, -12 },
                                                                                            { 0, -6 } } };
}

float UIPanelMotion::Evaluate( double now ) const
{
    const float destination = m_visible ? 1.0f : 0.0f;
    const double duration = DURATION_SECONDS * std::abs( destination - m_from );
    const float t = duration > 0 ? static_cast<float>( std::clamp( ( now - m_started ) / duration, 0.0, 1.0 ) ) : 1.0f;
    const float eased = m_visible ? 1.0f - ( 1.0f - t ) * ( 1.0f - t ) * ( 1.0f - t ) : t * t * t;
    return m_from + ( destination - m_from ) * eased;
}

float UIPanelMotion::Update( bool visible, double now )
{
    m_value = Evaluate( now );
    if ( visible != m_visible )
    {
        // Why: reverse from the current drawn position, never an endpoint.
        m_from = m_value;
        m_started = now;
        m_visible = visible;
    }
    return m_value;
}

void UIPanelTransitions::Finish( UIPanel panel, bool visible )
{
    const size_t index = static_cast<size_t>( panel );
    if ( index < m_panels.size() )
    {
        m_panels[index].motion.Finish( visible );
    }
}

bool UIPanelTransitions::BlocksPointer( UIPoint point ) const
{
    for ( size_t index = 1; index < m_panels.size(); ++index )
    {
        const Panel& panel = m_panels[index];
        // Header controls remain available for reversing a transition.
        if ( index == static_cast<size_t>( UIPanel::Header ) || !panel.motion.Active() )
        {
            continue;
        }
        const UIPoint displaced = { point.x - PANEL_DIRECTIONS[index].x * ( 1.0f - panel.motion.Value() ),
                                    point.y - PANEL_DIRECTIONS[index].y * ( 1.0f - panel.motion.Value() ) };
        for ( const auto& command : panel.draw.Commands() )
        {
            if ( command.type != UIDrawList::CommandType::Rect && command.type != UIDrawList::CommandType::RoundedRect &&
                 command.type != UIDrawList::CommandType::PreviewImage )
            {
                continue;
            }
            if ( point.x >= command.x0 && point.x < command.x0 + command.w && point.y >= command.y0 &&
                 point.y < command.y0 + command.h )
            {
                return true;
            }
            if ( displaced.x >= command.x0 && displaced.x < command.x0 + command.w && displaced.y >= command.y0 &&
                 displaced.y < command.y0 + command.h )
            {
                return true;
            }
        }
    }
    return false;
}

void UIPanelTransitions::SetClockOverride( double seconds, bool enabled )
{
    // A debugger may pin presentation time without changing simulation time.
    // Switching clock sources preserves the current transition phase.
    m_rebaseClock = m_rebaseClock || enabled != m_clockOverridden;
    m_clockOverridden = enabled;
    m_overrideTime = seconds;
}

void UIPanelTransitions::BeginFrame()
{
    m_incoming.Clear();
}
void UIPanelTransitions::Append( const UIDrawList& draw )
{
    m_incoming.Append( draw );
}

const UIDrawList& UIPanelTransitions::Compose( double now )
{
    now = m_clockOverridden ? m_overrideTime : now;
    if ( m_rebaseClock )
    {
        for ( Panel& panel : m_panels )
        {
            panel.motion.RebaseClock( now - m_lastTime );
        }
        m_rebaseClock = false;
    }
    m_lastTime = now;
    m_output.Clear();
    for ( size_t index = 0; index < m_panels.size(); ++index )
    {
        Panel& panel = m_panels[index];
        const auto id = static_cast<UIPanel>( index );
        const bool visible = m_incoming.HasPanel( id );
        const float opacity = id == UIPanel::None ? 1.0f : panel.motion.Update( visible, now );
        if ( visible )
        {
            panel.draw.CopyPanel( m_incoming, id );
        }
        if ( ( !visible && id == UIPanel::None ) || opacity <= 0.0f )
        {
            continue;
        }
        m_scratch.Clear();
        m_scratch.Append( panel.draw );
        m_scratch.ApplyPresentation( { PANEL_DIRECTIONS[index].x * ( 1.0f - opacity ),
                                       PANEL_DIRECTIONS[index].y * ( 1.0f - opacity ) },
                                     opacity );
        m_output.BeginLayer();
        m_output.Append( m_scratch );
    }
    return m_output;
}

bool UIPanelTransitions::Active() const
{
    for ( const Panel& panel : m_panels )
    {
        if ( panel.motion.Active() )
        {
            return true;
        }
    }
    return false;
}

float UIPanelTransitions::Visibility( UIPanel panel ) const
{
    const size_t index = static_cast<size_t>( panel );
    return index < m_panels.size() ? m_panels[index].motion.Value() : 0.0f;
}
} // namespace SkullbonezCore::UI
