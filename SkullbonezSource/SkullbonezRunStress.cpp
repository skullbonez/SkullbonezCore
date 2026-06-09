// --- Includes ---
#include "SkullbonezRunInternal.h"

// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

unsigned int SkullbonezRun::NextUIStressRandom()
{
    m_uiStress.randomState = m_uiStress.randomState * 1664525u + 1013904223u;
    return m_uiStress.randomState;
}


int SkullbonezRun::NextUIStressInt( int maxExclusive )
{
    if ( maxExclusive <= 0 )
    {
        return 0;
    }
    return static_cast<int>( NextUIStressRandom() % static_cast<unsigned int>( maxExclusive ) );
}


float SkullbonezRun::NextUIStressFloat( float minValue, float maxValue )
{
    const float unit = static_cast<float>( NextUIStressRandom() & 0xFFFFu ) / 65535.0f;
    return minValue + ( maxValue - minValue ) * unit;
}


void SkullbonezRun::RunUIStressActions()
{
    if ( !m_uiStress.enabled || !m_systems.window )
    {
        return;
    }

    ++m_uiStress.framesRun;
    const double UINow = m_timers.simulationTimer.GetTotalTime();
    const int screenW = (std::max)( 1, static_cast<int>( m_systems.window->m_sWindowDimensions.x ) );
    const int screenH = (std::max)( 1, static_cast<int>( m_systems.window->m_sWindowDimensions.y ) );

    m_UI.SetVisible( true, UINow );
    m_UI.SetMinimized( false, UINow );

    m_UI.SetMouseOverride( true, NextUIStressInt( screenW ), NextUIStressInt( screenH ) );

    if ( m_uiStress.framesRun == 18 )
    {
        ApplyUIModelCountOverride( 96 + NextUIStressInt( 160 ) );
    }
    if ( m_uiStress.framesRun == 42 )
    {
        const int balls = 24 + NextUIStressInt( 220 );
        const int boxes = NextUIStressInt( 1000 - balls + 1 );
        ApplyUISolverObjectCounts( balls, boxes );
    }
    if ( m_uiStress.framesRun % 34 == 0 )
    {
        SwitchRenderer( GetNextRendererType( GetCurrentRendererType() ) );
    }

    const int actionCount = std::clamp( m_uiStress.actionsPerFrame, 1, 32 );
    for ( int i = 0; i < actionCount; ++i )
    {
        switch ( NextUIStressInt( 24 ) )
        {
        case 0:
            m_UI.SetActiveTab( static_cast<InGameUITab>( NextUIStressInt( static_cast<int>( InGameUITab::Count ) ) ) );
            break;
        case 1:
            m_UI.SetScrollY( NextUIStressFloat( 0.0f, 900.0f ) );
            break;
        case 2:
            m_UI.SetBlurEnabled( NextUIStressInt( 2 ) != 0 );
            break;
        case 3:
            m_UI.SetProfilerTimelineEnabled( NextUIStressInt( 2 ) != 0 );
            break;
        case 4:
            m_UI.SetPerformanceHistogramEnabled( NextUIStressInt( 2 ) != 0 );
            break;
        case 5:
            m_UI.SetRendererComboOpen( NextUIStressInt( 2 ) != 0 );
            break;
        case 6:
            m_UI.SetWaterComboOpen( NextUIStressInt( 2 ) != 0 );
            break;
        case 7:
            m_UI.SetSceneComboOpen( NextUIStressInt( 2 ) != 0 );
            break;
        case 8:
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
            break;
        case 9:
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            break;
        case 10:
        {
            static const uint32_t kFlags[] = { PHYSICS_DEBUG_AXES, PHYSICS_DEBUG_CONTACTS, PHYSICS_DEBUG_SLEEP, PHYSICS_DEBUG_ALL };
            m_debug.physicsDebugFlags = kFlags[NextUIStressInt( 4 )];
            break;
        }
        case 11:
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            break;
        case 12:
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            break;
        case 13:
            m_scene.isFixedStep = !m_scene.isFixedStep;
            m_timers.physicsAccumulator = 0.0f;
            m_timers.fixedStepTickAccumulator = 0.0f;
            break;
        case 14:
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            break;
        case 15:
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
            break;
        case 16:
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
            break;
        case 17:
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            break;
        case 18:
        {
            const int mode = GetCurrentRendererType() == RuntimeRendererType::DX12 ? NextUIStressInt( 3 ) : ( NextUIStressInt( 2 ) == 0 ? 0 : 2 );
            m_debug.isWaterRTReflect = mode == 1;
            m_debug.isWaterNoReflect = mode == 2;
            break;
        }
        case 19:
            m_UITimeScaleOverride = NextUIStressFloat( 0.10f, 4.00f );
            m_scene.timeScale = m_UITimeScaleOverride;
            m_timers.physicsAccumulator = 0.0f;
            m_timers.fixedStepTickAccumulator = 0.0f;
            break;
        case 20:
            m_debug.physicsDebugAlpha = NextUIStressFloat( 0.05f, 1.00f );
            break;
        case 21:
            m_debug.physicsDebugContactLinger = NextUIStressFloat( 0.00f, 5.00f );
            break;
        case 22:
            ApplyUIWorldOverride( -NextUIStressFloat( 0.0f, 80.0f ),
                                  NextUIStressFloat( -40.0f, 140.0f ),
                                  NextUIStressFloat( 0.0f, 5.0f ) );
            break;
        case 23:
            m_UI.SetActiveTab( static_cast<InGameUITab>( NextUIStressInt( static_cast<int>( InGameUITab::Count ) ) ) );
            break;
        default:
            break;
        }
    }
}
