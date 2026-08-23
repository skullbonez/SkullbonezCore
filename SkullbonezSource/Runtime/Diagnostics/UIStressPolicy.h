/*
File: UIStressPolicy.h
Purpose:
  Owns deterministic UI-stress sequencing and its bounded command publication.

Summary:
  Diagnostics retains only the reproducible random cursor and frame count. Each
  active frame publishes value commands for App to apply to GameUI or Render;
  no UI, renderer, Scene, or process owner crosses this boundary.

Invariants:
  - The linear-congruential random sequence preserves the established UI-stress order.
  - At most 32 commands are published because the authored action count is clamped to 32.
  - Planning never retains a pointer, span, callback, or mutable subsystem borrow.

Related:
  - Runtime/App/RunUiStress.cpp
  - Runtime/Diagnostics/DiagnosticsRuntime.h
*/
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Runtime
{
enum class UIStressCommandKind : uint8_t
{
    SetActiveTab,
    SetScrollY,
    SetProfilerTimeline,
    SetPerformanceHistogram,
    SetRendererComboOpen,
    SetWaterComboOpen,
    SetSceneComboOpen,
    ToggleVsync
};

struct UIStressCommand
{
    UIStressCommandKind kind = UIStressCommandKind::SetActiveTab;
    int intValue = 0;
    float floatValue = 0.0f;
    bool boolValue = false;
};

struct UIStressFramePlan
{
    static constexpr std::size_t commandCapacity = 32u;
    std::array<UIStressCommand, commandCapacity> commands = {};
    std::size_t commandCount = 0u;
    int mouseX = 0;
    int mouseY = 0;
    bool active = false;
};

class UIStressPolicyOwner
{
  public:
    void Reset()
    {
        m_enabled = false;
        m_randomState = 0x7F4A7C15u;
        m_actionsPerFrame = 4;
        m_framesRun = 0;
    }

    void SetEnabled( bool enabled ) { m_enabled = enabled; }
    void SetRandomState( unsigned int seed ) { m_randomState = seed; }
    void SetActionsPerFrame( int count ) { m_actionsPerFrame = std::clamp( count, 1, 32 ); }

    void Configure( bool enabled, unsigned int seed, int actionsPerFrame )
    {
        SetEnabled( enabled );
        SetRandomState( seed );
        SetActionsPerFrame( actionsPerFrame );
    }

    bool Enabled() const { return m_enabled; }
    unsigned int RandomState() const { return m_randomState; }
    int ActionsPerFrame() const { return m_actionsPerFrame; }
    int FramesRun() const { return m_framesRun; }

    UIStressFramePlan PlanFrame( int screenWidth, int screenHeight, int tabCount )
    {
        UIStressFramePlan plan;

        if ( !m_enabled )
        {
            return plan;
        }

        plan.active = true;
        ++m_framesRun;
        plan.mouseX = NextInt( (std::max)( 1, screenWidth ) );
        plan.mouseY = NextInt( (std::max)( 1, screenHeight ) );

        // Invariant: generated-control churn remains disabled for UI stress,
        // but its historical random draws still advance the reproducible cursor.
        if ( m_framesRun == 18 )
        {
            (void)NextInt( 160 );
        }

        if ( m_framesRun == 42 )
        {
            const int balls = 24 + NextInt( 220 );
            (void)NextInt( 1000 - balls + 1 );
        }

        for ( int index = 0; index < m_actionsPerFrame; ++index )
        {
            PlanAction( NextInt( 24 ), tabCount, plan );
        }

        return plan;
    }

  private:
    unsigned int NextRandom()
    {
        if ( m_randomState == 0u )
        {
            m_randomState = 0xC11E2026u;
        }

        m_randomState = m_randomState * 1664525u + 1013904223u;
        return m_randomState;
    }

    int NextInt( int maxExclusive )
    {
        return maxExclusive > 0 ? static_cast<int>( NextRandom() % static_cast<unsigned int>( maxExclusive ) ) : 0;
    }

    float NextFloat( float minimum, float maximum )
    {
        const float unit = static_cast<float>( NextRandom() & 0xFFFFu ) / 65535.0f;
        return minimum + ( maximum - minimum ) * unit;
    }

    void Append( UIStressFramePlan& plan, UIStressCommand command )
    {
        if ( plan.commandCount < plan.commands.size() )
        {
            plan.commands[plan.commandCount++] = command;
        }
    }

    void PlanAction( int action, int tabCount, UIStressFramePlan& plan )
    {
        switch ( action )
        {
        case 0:
        case 23:
            Append( plan, { UIStressCommandKind::SetActiveTab, NextInt( tabCount ) } );
            break;
        case 1:
            Append( plan, { UIStressCommandKind::SetScrollY, 0, NextFloat( 0.0f, 900.0f ) } );
            break;
        case 2:
            (void)NextInt( 2 );
            break;
        case 3:
            Append( plan, { UIStressCommandKind::SetProfilerTimeline, 0, 0.0f, NextInt( 2 ) != 0 } );
            break;
        case 4:
            Append( plan, { UIStressCommandKind::SetPerformanceHistogram, 0, 0.0f, NextInt( 2 ) != 0 } );
            break;
        case 5:
            Append( plan, { UIStressCommandKind::SetRendererComboOpen, 0, 0.0f, NextInt( 2 ) != 0 } );
            break;
        case 6:
            Append( plan, { UIStressCommandKind::SetWaterComboOpen, 0, 0.0f, NextInt( 2 ) != 0 } );
            break;
        case 7:
            Append( plan, { UIStressCommandKind::SetSceneComboOpen, 0, 0.0f, NextInt( 2 ) != 0 } );
            break;
        case 8:
            Append( plan, { UIStressCommandKind::ToggleVsync } );
            break;
        case 10:
            (void)NextInt( 4 );
            break;
        case 18:
            (void)NextInt( 3 );
            break;
        case 19:
            (void)NextFloat( 0.10f, 4.00f );
            break;
        case 20:
            (void)NextFloat( 0.05f, 1.00f );
            break;
        case 21:
            (void)NextFloat( 0.00f, 5.00f );
            break;
        case 22:
            (void)NextFloat( 0.0f, 80.0f );
            (void)NextFloat( -40.0f, 140.0f );
            (void)NextFloat( 0.0f, 5.0f );
            break;
        default:
            break;
        }
    }

    bool m_enabled = false;
    unsigned int m_randomState = 0x7F4A7C15u;
    int m_actionsPerFrame = 4;
    int m_framesRun = 0;
};
} // namespace SkullbonezCore::Runtime
