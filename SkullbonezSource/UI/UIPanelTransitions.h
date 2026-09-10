#pragma once
#include "UIDrawList.h"
#include <array>

namespace SkullbonezCore::UI
{
// One reversible motion value, independent of simulation time and frame rate.
class UIPanelMotion
{
  public:
    static constexpr double DURATION_SECONDS = 0.160;
    float Update( bool visible, double now );
    void Finish( bool visible )
    {
        m_visible = visible;
        m_value = m_from = visible ? 1.0f : 0.0f;
    }
    void RebaseClock( double delta )
    {
        m_started += delta;
    }
    float Value() const
    {
        return m_value;
    }
    bool Active() const
    {
        return m_value != ( m_visible ? 1.0f : 0.0f );
    }

  private:
    float Evaluate( double now ) const;
    double m_started = 0;
    float m_from = 0;
    float m_value = 0;
    bool m_visible = false;
};

// Lifetime: InGameUI allocates this bounded cache at startup. Retired panel
// commands survive only their exit; no renderer resources or widget pointers do.
class UIPanelTransitions
{
  public:
    void SetClockOverride( double seconds, bool enabled );
    void Finish( UIPanel panel, bool visible );
    bool BlocksPointer( UIPoint point ) const;
    void BeginFrame();
    void Append( const UIDrawList& draw );
    const UIDrawList& Compose( double now );
    bool Active() const;
    UIDrawList::Stats DrawStats() const
    {
        return m_output.GetStats();
    }
    float Visibility( UIPanel panel ) const;

  private:
    struct Panel
    {
        UIDrawList draw;
        UIPanelMotion motion;
    };
    std::array<Panel, static_cast<size_t>( UIPanel::Count )> m_panels;
    UIDrawList m_incoming;
    UIDrawList m_scratch;
    UIDrawList m_output;
    double m_lastTime = 0;
    double m_overrideTime = 0;
    bool m_clockOverridden = false;
    bool m_rebaseClock = false;
};
} // namespace SkullbonezCore::UI
