/*
File: SkullbonezSource/Runtime/RunStress.cpp
Purpose:
  Runs stress and automation paths for validation-oriented launches.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - UI stress randomness is deterministic from UIStressState so crashes can be
    reproduced from the same launch options.
  - Runtime churn remains disabled in this sweep unless the matching render and
    physics validation gates are intentionally selected.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "RuntimeTuning.h"
#include "Scene/SceneRuntimeGeneratedControls.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
using UIStressState = DiagnosticsRuntime::UIStressState;

unsigned int NextUIStressRandom( UIStressState& stress )
{
    stress.randomState = stress.randomState * 1664525u + 1013904223u;
    return stress.randomState;
}


int NextUIStressInt( UIStressState& stress, int maxExclusive )
{
    if ( maxExclusive <= 0 )
    {
        return 0;
    }
    return static_cast<int>( NextUIStressRandom( stress ) % static_cast<unsigned int>( maxExclusive ) );
}


float NextUIStressFloat( UIStressState& stress, float minValue, float maxValue )
{
    const float unit = static_cast<float>( NextUIStressRandom( stress ) & 0xFFFFu ) / 65535.0f;
    return minValue + ( maxValue - minValue ) * unit;
}
} // namespace


void Run::RunUIStressActions()
{
    UIStressState& stress = m_diagnosticsRuntime.UIStress();
    if ( !stress.enabled || !m_systems.window )
    {
        return;
    }

    ++stress.framesRun;
    const double UINow = m_timers.simulationTimer.GetTotalTime();
    const int screenW = (std::max)( 1, static_cast<int>( m_systems.window->m_sWindowDimensions.x ) );
    const int screenH = (std::max)( 1, static_cast<int>( m_systems.window->m_sWindowDimensions.y ) );

    m_UI.SetVisible( true, UINow );
    m_UI.SetMinimized( false, UINow );

    m_UI.SetMouseOverride( true, NextUIStressInt( stress, screenW ), NextUIStressInt( stress, screenH ) );

    // This gate is a UI control-state crash sweep. Runtime rebuilds and world
    // debug toggles belong to render/physics validation, so they stay frozen here.
    const bool allowRuntimeChurn = false;
    const auto makeSceneGeneratedControlContext = [this]() -> SceneRuntimeGeneratedControlContext
    {
        return SceneRuntimeGeneratedControlContext{ SceneState(),
                                                    m_sceneUIOverrides,
                                                    m_camera,
                                                    m_sceneController,
                                                    Cfg(),
                                                    m_cWorldEnvironment,
                                                    m_systems.terrain.get(),
                                                    m_cGameModelCollection,
                                                    m_simulation,
                                                    m_runtimeTools,
                                                    IsGfxReady() ? &Gfx() : nullptr,
                                                    m_launchOptions.generatedObjectTypeOverride,
                                                    ActiveGameModelCapacity() };
    };
    const auto executeSceneGeneratedControlAction = [this]( const SceneRuntimeGeneratedControlAction& action )
    {
        if ( action.resetReplayTimeline )
        {
            ResetReplayTimelineForActiveScene();
        }
        if ( action.scheduleProfileReset )
        {
            PROFILE_SCHEDULE_RESET();
        }
    };
    if ( stress.framesRun == 18 )
    {
        const int modelCount = 96 + NextUIStressInt( stress, 160 );
        if ( allowRuntimeChurn )
        {
            executeSceneGeneratedControlAction(
                ApplyUIModelCountOverride( makeSceneGeneratedControlContext(), modelCount ) );
        }
    }
    if ( stress.framesRun == 42 )
    {
        const int balls = 24 + NextUIStressInt( stress, 220 );
        const int boxes = NextUIStressInt( stress, 1000 - balls + 1 );
        if ( allowRuntimeChurn )
        {
            executeSceneGeneratedControlAction(
                ApplyUISolverObjectCounts( makeSceneGeneratedControlContext(), balls, boxes ) );
        }
    }
    const int actionCount = std::clamp( stress.actionsPerFrame, 1, 32 );
    for ( int i = 0; i < actionCount; ++i )
    {
        switch ( NextUIStressInt( stress, 24 ) )
        {
        case 0:
            m_UI.SetActiveTab(
                static_cast<InGameUITab>( NextUIStressInt( stress, static_cast<int>( InGameUITab::Count ) ) ) );
            break;
        case 1:
            m_UI.SetScrollY( NextUIStressFloat( stress, 0.0f, 900.0f ) );
            break;
        case 2:
            // Keep the PRNG sequence stable while leaving backdrop blur to validate_ui.bat.
            // Stress runs churn control state; blur's DX12 readback path has its own pixel gate.
            (void)NextUIStressInt( stress, 2 );
            break;
        case 3:
            m_UI.SetProfilerTimelineEnabled( NextUIStressInt( stress, 2 ) != 0 );
            break;
        case 4:
            m_UI.SetPerformanceHistogramEnabled( NextUIStressInt( stress, 2 ) != 0 );
            break;
        case 5:
            m_UI.SetRendererComboOpen( NextUIStressInt( stress, 2 ) != 0 );
            break;
        case 6:
            m_UI.SetWaterComboOpen( NextUIStressInt( stress, 2 ) != 0 );
            break;
        case 7:
            m_UI.SetSceneComboOpen( NextUIStressInt( stress, 2 ) != 0 );
            break;
        case 8:
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
            break;
        case 9:
            if ( allowRuntimeChurn )
            {
                m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            }
            break;
        case 10:
        {
            static const uint32_t kFlags[] = { PHYSICS_DEBUG_AXES,
                                               PHYSICS_DEBUG_CONTACTS,
                                               PHYSICS_DEBUG_SLEEP,
                                               PHYSICS_DEBUG_ALL };
            const int flagIndex = NextUIStressInt( stress, 4 );
            if ( allowRuntimeChurn )
            {
                m_debug.physicsDebugFlags = kFlags[flagIndex];
            }
            break;
        }
        case 11:
            if ( allowRuntimeChurn )
            {
                m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            }
            break;
        case 12:
            if ( allowRuntimeChurn )
            {
                m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            }
            break;
        case 13:
            if ( allowRuntimeChurn )
            {
                SceneState().isFixedStep = !SceneState().isFixedStep;
                m_simulation.Reset();
            }
            break;
        case 14:
            if ( allowRuntimeChurn )
            {
                m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            }
            break;
        case 15:
            if ( allowRuntimeChurn )
            {
                m_debug.isWaterHidden = !m_debug.isWaterHidden;
            }
            break;
        case 16:
            if ( allowRuntimeChurn )
            {
                m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
                if ( m_debug.isWaterFreezeDebug )
                {
                    m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
                }
            }
            break;
        case 17:
            if ( allowRuntimeChurn )
            {
                m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            }
            break;
        case 18:
        {
            const int mode = NextUIStressInt( stress, 3 );
            if ( allowRuntimeChurn )
            {
                m_debug.isWaterRTReflect = mode == 1;
                m_debug.isWaterNoReflect = mode == 2;
            }
            break;
        }
        case 19:
        {
            const float timeScale = NextUIStressFloat( stress, 0.10f, 4.00f );
            if ( allowRuntimeChurn )
            {
                m_sceneUIOverrides.timeScaleOverride = timeScale;
                SceneState().timeScale = m_sceneUIOverrides.timeScaleOverride;
                m_simulation.Reset();
            }
            break;
        }
        case 20:
        {
            const float alpha = NextUIStressFloat( stress, 0.05f, 1.00f );
            if ( allowRuntimeChurn )
            {
                m_debug.physicsDebugAlpha = alpha;
            }
            break;
        }
        case 21:
        {
            const float contactLinger = NextUIStressFloat( stress, 0.00f, 5.00f );
            if ( allowRuntimeChurn )
            {
                m_debug.physicsDebugContactLinger = contactLinger;
            }
            break;
        }
        case 22:
        {
            const float gravity = -NextUIStressFloat( stress, 0.0f, 80.0f );
            const float fluidHeight = NextUIStressFloat( stress, -40.0f, 140.0f );
            const float fluidDensity = NextUIStressFloat( stress, 0.0f, 5.0f );
            if ( allowRuntimeChurn )
            {
                ApplyUIWorldOverride( m_cWorldEnvironment, m_replayRuntime, gravity, fluidHeight, fluidDensity );
            }
            break;
        }
        case 23:
            m_UI.SetActiveTab(
                static_cast<InGameUITab>( NextUIStressInt( stress, static_cast<int>( InGameUITab::Count ) ) ) );
            break;
        default:
            break;
        }
    }
}
