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
  - UI stress keeps runtime churn disabled; graphics stress intentionally flips
    render/runtime churn on so DX12 state tracking gets exercised.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "RuntimeTuning.h"
#include "Scene/SceneRuntimeGeneratedControls.h"
#include "Scene/SceneRuntimeStyle.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
using UIStressState = DiagnosticsRuntime::UIStressState;

unsigned int NextStressRandom( unsigned int& state )
{
    if ( state == 0 )
    {
        state = 0xC11E2026u;
    }
    state = state * 1664525u + 1013904223u;
    return state;
}


unsigned int NextUIStressRandom( UIStressState& stress )
{
    return NextStressRandom( stress.randomState );
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


int NextGraphicsStressInt( RunGraphicsStressState& stress, int maxExclusive )
{
    if ( maxExclusive <= 0 )
    {
        return 0;
    }
    return static_cast<int>( NextStressRandom( stress.randomState ) % static_cast<unsigned int>( maxExclusive ) );
}


float NextGraphicsStressFloat( RunGraphicsStressState& stress, float minValue, float maxValue )
{
    const float unit = static_cast<float>( NextStressRandom( stress.randomState ) & 0xFFFFu ) / 65535.0f;
    return minValue + ( maxValue - minValue ) * unit;
}


float RandomCinematicParamValue( RunGraphicsStressState& stress, UICinematicParam param )
{
    switch ( param )
    {
    case UICinematicParam::Exposure:
        return NextGraphicsStressFloat( stress, 0.05f, 3.00f );
    case UICinematicParam::Gamma:
        return NextGraphicsStressFloat( stress, 1.00f, 3.00f );
    case UICinematicParam::SkyMode:
    case UICinematicParam::TerrainMode:
    case UICinematicParam::ObjectStyle:
        return NextGraphicsStressFloat( stress, 0.0f, 32.0f );
    case UICinematicParam::WaterMode:
        return NextGraphicsStressFloat( stress, 0.0f, 4.0f );
    case UICinematicParam::StyleSaturation:
    case UICinematicParam::StyleContrast:
        return NextGraphicsStressFloat( stress, 0.0f, 2.50f );
    case UICinematicParam::StyleVignette:
    case UICinematicParam::SunX:
    case UICinematicParam::SunY:
    case UICinematicParam::CloudCoverage:
    case UICinematicParam::FogOpacity:
    case UICinematicParam::BasinFeather:
    case UICinematicParam::WaterAlpha:
    case UICinematicParam::WaterReflection:
        return NextGraphicsStressFloat( stress, 0.0f, 1.0f );
    case UICinematicParam::SunBrightness:
        return NextGraphicsStressFloat( stress, 0.0f, 40.0f );
    case UICinematicParam::SunRed:
    case UICinematicParam::SunGreen:
    case UICinematicParam::SunBlue:
        return NextGraphicsStressFloat( stress, 0.0f, 2.0f );
    case UICinematicParam::SkyGlow:
        return NextGraphicsStressFloat( stress, 0.0f, 8.0f );
    case UICinematicParam::HorizonRed:
    case UICinematicParam::HorizonGreen:
    case UICinematicParam::HorizonBlue:
    case UICinematicParam::ZenithRed:
    case UICinematicParam::ZenithGreen:
    case UICinematicParam::ZenithBlue:
    case UICinematicParam::CloudIntensity:
    case UICinematicParam::TerrainTintRed:
    case UICinematicParam::TerrainTintGreen:
    case UICinematicParam::TerrainTintBlue:
    case UICinematicParam::TerrainAccentRed:
    case UICinematicParam::TerrainAccentGreen:
    case UICinematicParam::TerrainAccentBlue:
    case UICinematicParam::WaterTintRed:
    case UICinematicParam::WaterTintGreen:
    case UICinematicParam::WaterTintBlue:
    case UICinematicParam::FogRed:
    case UICinematicParam::FogGreen:
    case UICinematicParam::FogBlue:
        return NextGraphicsStressFloat( stress, 0.0f, 1.50f );
    case UICinematicParam::CloudSoftness:
        return NextGraphicsStressFloat( stress, 0.01f, 0.65f );
    case UICinematicParam::CloudScale:
        return NextGraphicsStressFloat( stress, 0.50f, 12.0f );
    case UICinematicParam::ShaftStrength:
        return NextGraphicsStressFloat( stress, 0.0f, 3.0f );
    case UICinematicParam::ShaftFalloff:
        return NextGraphicsStressFloat( stress, 0.25f, 5.0f );
    case UICinematicParam::VolumetricStrength:
        return NextGraphicsStressFloat( stress, 0.0f, 2.0f );
    case UICinematicParam::VolumetricDensity:
        return NextGraphicsStressFloat( stress, 0.0f, 2.50f );
    case UICinematicParam::VolumetricDecay:
        return NextGraphicsStressFloat( stress, 0.800f, 0.995f );
    case UICinematicParam::BloomThreshold:
        return NextGraphicsStressFloat( stress, 0.0f, 4.0f );
    case UICinematicParam::BloomKnee:
        return NextGraphicsStressFloat( stress, 0.01f, 2.0f );
    case UICinematicParam::BloomStrength:
        return NextGraphicsStressFloat( stress, 0.0f, 2.0f );
    case UICinematicParam::BloomRadius:
        return NextGraphicsStressFloat( stress, 0.25f, 8.0f );
    case UICinematicParam::TerrainRelief:
        return NextGraphicsStressFloat( stress, 0.0f, 1.50f );
    case UICinematicParam::TerrainGridScale:
        return NextGraphicsStressFloat( stress, 0.10f, 120.0f );
    case UICinematicParam::TerrainGridStrength:
    case UICinematicParam::WaterGlint:
        return NextGraphicsStressFloat( stress, 0.0f, 4.0f );
    case UICinematicParam::BasinCenterX:
    case UICinematicParam::BasinCenterZ:
        return NextGraphicsStressFloat( stress, 0.0f, 1200.0f );
    case UICinematicParam::BasinRadiusX:
    case UICinematicParam::BasinRadiusZ:
        return NextGraphicsStressFloat( stress, 1.0f, 500.0f );
    case UICinematicParam::BasinDepth:
        return NextGraphicsStressFloat( stress, 0.0f, 80.0f );
    case UICinematicParam::BasinRimLift:
        return NextGraphicsStressFloat( stress, 0.0f, 60.0f );
    case UICinematicParam::FogDensity:
        return NextGraphicsStressFloat( stress, 0.0f, 0.006f );
    case UICinematicParam::FogStart:
        return NextGraphicsStressFloat( stress, 0.0f, 500.0f );
    case UICinematicParam::FogEnd:
        return NextGraphicsStressFloat( stress, 100.0f, 4000.0f );
    case UICinematicParam::None:
    case UICinematicParam::Count:
    default:
        return 0.0f;
    }
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
                                                    m_sceneController.UIOverrides(),
                                                    m_camera,
                                                    m_sceneController,
                                                    m_config,
                                                    m_cWorldEnvironment,
                                                    m_systems.terrain.get(),
                                                    m_cGameModelCollection,
                                                    m_simulation,
                                                    m_runtimeTools,
                                                    IsGfxReady() ? &Gfx() : nullptr,
                                                    m_launchOptions.generatedObjectTypeOverride,
                                                    m_startup.gameModelCapacity };
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
                // Concept: Scene-tab churn goes through the scene controller so
                // reset preservation and generated-scene rebuilds see one owner.
                m_sceneController.UIOverrides().timeScaleOverride = timeScale;
                SceneState().timeScale = m_sceneController.UIOverrides().timeScaleOverride;
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


void Run::RunGraphicsStressActions( const Rendering::IRenderDiagnostics& renderDiagnostics )
{
    // Concept: graphics stress is a deterministic fuzzer over scene loading,
    // cinematic controls, render-path toggles, and heavy generated-scene resets.
    // Keep every mutation reproducible from the launch seed so a crash line in
    // latest_stdout.txt can be replayed exactly under cdb.
    RunGraphicsStressState& stress = m_graphicsStress;
    if ( !stress.enabled || !m_systems.window || !IsGfxReady() )
    {
        return;
    }

    ++stress.framesRun;
    if ( stress.framesRun == 1 )
    {
        printf( "[graphics-stress] Running seed=%u actions=%d scene_interval_frames=%d\n",
                stress.randomState,
                stress.actionsPerFrame,
                stress.sceneIntervalFrames );
        fflush( stdout );
    }

    auto executeSceneControlAction = [&]( const SceneRuntimeControlAction& action ) -> bool
    {
        if ( action.enterInteractiveSceneRun )
        {
            EnterInteractiveSceneRun();
        }

        switch ( action.type )
        {
        case SceneRuntimeControlActionType::ClearCurrentSceneAutomation:
            SceneState().isExitOnComplete = false;
            m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
            return true;
        case SceneRuntimeControlActionType::LoadScene:
            LoadScene( action.index,
                       action.preserveUIState,
                       action.suppressExitOnComplete,
                       action.preserveRuntimeState );
            return true;
        case SceneRuntimeControlActionType::ApplyCinematicModeFromBrowserIndex:
            EnterInteractiveSceneRun();
            return ApplyCinematicModeFromBrowserIndex(
                SceneRuntimeStyleContext{ m_launchOptions,
                                          SceneState(),
                                          m_sceneController.Browser(),
                                          m_cGameModelCollection,
                                          m_systems.assets,
                                          RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                          m_defaultCinematicRender },
                action.index );
        case SceneRuntimeControlActionType::None:
            return false;
        }
        return false;
    };

    const int framesSinceSceneLoad = stress.framesRun - stress.lastSceneLoadFrame;
    const bool sceneLoadDue = framesSinceSceneLoad >= stress.sceneIntervalFrames;
    if ( sceneLoadDue )
    {
        // Hazard: suite order is the durable test contract. Browser fallback is
        // useful for manual app launches, but automated repros must prefer the
        // checked-in graphics_stress.suite.json scene queue.
        SceneRuntimeControlAction action = SceneRuntimeControlAction::None();
        int selectedSceneIndex = -1;
        const char* selectedSceneSource = "none";
        if ( m_sceneController.QueueSize() > 0 )
        {
            selectedSceneIndex = NextGraphicsStressInt( stress, m_sceneController.QueueSize() );
            selectedSceneSource = "queue";
            action = SceneRuntimeControlAction::LoadScene( selectedSceneIndex,
                                                           true,
                                                           true,
                                                           NextGraphicsStressInt( stress, 2 ) != 0,
                                                           true );
        }
        else if ( !m_sceneController.Browser().paths.empty() )
        {
            selectedSceneIndex =
                NextGraphicsStressInt( stress, static_cast<int>( m_sceneController.Browser().paths.size() ) );
            selectedSceneSource = "browser";
            action =
                m_sceneCoordinator.LoadSceneFromBrowserIndex( selectedSceneIndex, m_sceneController.Browser().paths );
        }

        if ( executeSceneControlAction( action ) )
        {
            stress.lastSceneLoadFrame = stress.framesRun;
            ++stress.sceneLoadsRequested;
            printf( "[graphics-stress] scene_load=%d frame=%d source=%s selected_index=%d action_index=%d\n",
                    stress.sceneLoadsRequested,
                    stress.framesRun,
                    selectedSceneSource,
                    selectedSceneIndex,
                    action.index );
            fflush( stdout );
        }
        else
        {
            printf( "[graphics-stress] scene_load_skipped frame=%d source=%s selected_index=%d\n",
                    stress.framesRun,
                    selectedSceneSource,
                    selectedSceneIndex );
            fflush( stdout );
        }
    }

    m_UI.SetVisible( true, m_timers.simulationTimer.GetTotalTime() );
    m_UI.SetMinimized( false, m_timers.simulationTimer.GetTotalTime() );
    SceneState().isInteractiveRun = true;
    SceneState().isExitOnComplete = false;

    const int actionCount = std::clamp( stress.actionsPerFrame, 1, 64 );
    // Invariant: random values stay inside the same broad ranges exposed by the
    // runtime UI. The stress test should crash bad DX12 lifetime/state tracking,
    // not manufacture impossible physics or render data.
    for ( int i = 0; i < actionCount; ++i )
    {
        switch ( NextGraphicsStressInt( stress, 32 ) )
        {
        case 0:
        {
            CinematicRenderConfig& cinematic = RuntimeActiveCinematicConfig( SceneState(), m_config );
            cinematic.enabled = !cinematic.enabled;
            m_launchOptions.hasCinematicRenderingOverride = false;
            if ( SceneState().isSceneMode )
            {
                SceneState().hasCinematicRenderingOverride = true;
                SceneState().isCinematicRenderingEnabled = cinematic.enabled;
                SceneState().cinematicOverrideMask |= SCENE_CINE_RENDERING;
                SceneState().uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
            }
            break;
        }
        case 1:
        {
            CinematicRenderConfig& cinematic = RuntimeActiveCinematicConfig( SceneState(), m_config );
            const UICinematicFeature feature = static_cast<UICinematicFeature>(
                NextGraphicsStressInt( stress, static_cast<int>( UICinematicFeature::Count ) ) );
            if ( feature == UICinematicFeature::Shadows )
            {
                m_launchOptions.hasCinematicShadowsOverride = false;
            }
            ToggleCinematicUIFeature( cinematic, SceneState(), feature );
            break;
        }
        case 2:
        {
            CinematicRenderConfig& cinematic = RuntimeActiveCinematicConfig( SceneState(), m_config );
            const UICinematicParam param = static_cast<UICinematicParam>(
                NextGraphicsStressInt( stress, static_cast<int>( UICinematicParam::Count ) ) );
            ApplyCinematicUIParam( cinematic, SceneState(), param, RandomCinematicParamValue( stress, param ) );
            break;
        }
        case 3:
        {
            const int browserCount = static_cast<int>( m_sceneController.Browser().paths.size() );
            const int browserIndex = ( browserCount > 0 && NextGraphicsStressInt( stress, 5 ) != 0 )
                                         ? NextGraphicsStressInt( stress, browserCount )
                                         : -1;
            (void)ApplyCinematicModeFromBrowserIndex(
                SceneRuntimeStyleContext{ m_launchOptions,
                                          SceneState(),
                                          m_sceneController.Browser(),
                                          m_cGameModelCollection,
                                          m_systems.assets,
                                          RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                          m_defaultCinematicRender },
                browserIndex );
            break;
        }
        case 4:
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            break;
        case 5:
            m_runtimeSettings.isPipelineSyncEnabled = !m_runtimeSettings.isPipelineSyncEnabled;
            break;
        case 6:
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            break;
        case 7:
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
            break;
        case 8:
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
            break;
        case 9:
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            break;
        case 10:
        {
            const int mode = NextGraphicsStressInt( stress, 3 );
            m_debug.isWaterRTReflect = mode == 1;
            m_debug.isWaterNoReflect = mode == 2;
            break;
        }
        case 11:
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            break;
        case 12:
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            break;
        case 13:
        {
            static const uint32_t kFlags[] = {
                PHYSICS_DEBUG_NONE,
                PHYSICS_DEBUG_AXES,
                PHYSICS_DEBUG_CONTACTS,
                PHYSICS_DEBUG_SLEEP,
                PHYSICS_DEBUG_ALL,
            };
            m_debug.physicsDebugFlags =
                kFlags[NextGraphicsStressInt( stress, static_cast<int>( sizeof( kFlags ) / sizeof( kFlags[0] ) ) )];
            break;
        }
        case 14:
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            m_debug.physicsDebugAlpha = NextGraphicsStressFloat( stress, 0.05f, 1.0f );
            m_debug.physicsDebugContactLinger = NextGraphicsStressFloat( stress, 0.0f, 5.0f );
            break;
        case 15:
        {
            const float timeScale = NextGraphicsStressFloat( stress, 0.05f, 4.0f );
            m_sceneController.UIOverrides().timeScaleOverride = timeScale;
            SceneState().timeScale = timeScale;
            m_simulation.Reset();
            break;
        }
        case 16:
            ApplyUIWorldOverride( m_cWorldEnvironment,
                                  m_replayRuntime,
                                  -NextGraphicsStressFloat( stress, 0.0f, 80.0f ),
                                  NextGraphicsStressFloat( stress, -80.0f, 160.0f ),
                                  NextGraphicsStressFloat( stress, 0.0f, 5.0f ) );
            break;
        case 17:
            m_sceneController.UIOverrides().modelCountOverride = 32 + NextGraphicsStressInt( stress, 512 );
            break;
        case 18:
            m_launchOptions.generatedObjectTypeOverride =
                static_cast<GeneratedObjectTypeOverride>( NextGraphicsStressInt( stress, 3 ) );
            break;
        case 19:
            m_runtimeSettings.tornadoField.enabled = NextGraphicsStressInt( stress, 2 ) != 0;
            m_runtimeSettings.tornadoField.visualizeVelocityField = NextGraphicsStressInt( stress, 2 ) != 0;
            m_runtimeSettings.tornadoVisual.enabled = NextGraphicsStressInt( stress, 2 ) != 0;
            SyncTornadoRuntimeSettingsToPhysics( m_cGameModelCollection, m_runtimeSettings );
            break;
        case 20:
            m_runtimeSettings.tornadoVisual.shellAlpha = NextGraphicsStressFloat( stress, 0.02f, 0.40f );
            m_runtimeSettings.tornadoVisual.dustAlpha = NextGraphicsStressFloat( stress, 0.02f, 0.55f );
            m_runtimeSettings.tornadoVisual.ribbonWidth = NextGraphicsStressFloat( stress, 1.0f, 12.0f );
            m_runtimeSettings.tornadoVisual.ribbonCount = 1 + NextGraphicsStressInt( stress, 10 );
            m_runtimeSettings.tornadoVisual.particleCount = 16 + NextGraphicsStressInt( stress, 240 );
            break;
        case 21:
            m_UI.SetActiveTab(
                static_cast<InGameUITab>( NextGraphicsStressInt( stress, static_cast<int>( InGameUITab::Count ) ) ) );
            m_UI.SetScrollY( NextGraphicsStressFloat( stress, 0.0f, 1200.0f ) );
            break;
        case 22:
            SceneState().isFixedStep = !SceneState().isFixedStep;
            m_simulation.Reset();
            break;
        case 23:
            m_runtimeSettings.isPhysicsSleepEnabled = !m_runtimeSettings.isPhysicsSleepEnabled;
            m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
            break;
        case 24:
            m_debug.isTopTextHidden = !m_debug.isTopTextHidden;
            break;
        case 25:
            m_debug.overlayMode = static_cast<OverlayMode>( NextGraphicsStressInt( stress, 6 ) );
            break;
        case 26:
            m_runtimeTools.Laser().Update( 0.0f );
            break;
        case 27:
            m_camera.trackHeight = NextGraphicsStressFloat( stress, 8.0f, 500.0f );
            break;
        case 28:
            m_launchOptions.generatedObjectTypeOverride =
                static_cast<GeneratedObjectTypeOverride>( NextGraphicsStressInt( stress, 3 ) );
            break;
        case 29:
            m_UI.SetProfilerTimelineEnabled( NextGraphicsStressInt( stress, 2 ) != 0 );
            m_UI.SetPerformanceHistogramEnabled( NextGraphicsStressInt( stress, 2 ) != 0 );
            break;
        case 30:
            m_UI.SetRendererComboOpen( NextGraphicsStressInt( stress, 2 ) != 0 );
            m_UI.SetWaterComboOpen( NextGraphicsStressInt( stress, 2 ) != 0 );
            m_UI.SetSceneComboOpen( NextGraphicsStressInt( stress, 2 ) != 0 );
            break;
        case 31:
            m_debug.isUITestPattern = NextGraphicsStressInt( stress, 2 ) != 0;
            break;
        default:
            break;
        }
    }

    if ( stress.framesRun % 60 == 0 )
    {
        printf( "[graphics-stress] frame=%d scene_loads=%d rng=%u\n",
                stress.framesRun,
                stress.sceneLoadsRequested,
                stress.randomState );
        fflush( stdout );
    }

    const int memoryInterval = stress.memoryLogIntervalFrames;
    if ( memoryInterval > 0 && ( stress.framesRun == 1 || stress.framesRun % memoryInterval == 0 ) )
    {
        // Why: long stress runs need memory attribution before shutdown. If the
        // process is killed after a climb, this stdout line survives with the
        // same seed/frame/scene-load position as the repro log.
        const MainMemoryStats& memoryStats =
            m_diagnosticsRuntime.RefreshMainMemoryStats( m_replayRuntime,
                                                         m_cGameModelCollection,
                                                         m_timers.simulationTimer.GetTotalTime(),
                                                         true );
        const SkullbonezCore::Rendering::RenderMemoryStats renderStats = renderDiagnostics.GetRenderMemoryStats();
        printf( "[graphics-stress-memory] frame=%d scene_loads=%d task_manager_bytes=%llu "
                "working_set_bytes=%llu private_working_set_bytes=%llu private_commit_bytes=%llu pagefile_bytes=%llu "
                "tracked_engine_bytes=%llu replay_bytes=%llu game_object_bytes=%llu unattributed_process_bytes=%llu "
                "render_available=%d render_adapter_available=%d dxgi_local_usage_bytes=%llu "
                "dxgi_nonlocal_usage_bytes=%llu dxgi_local_budget_bytes=%llu dxgi_nonlocal_budget_bytes=%llu "
                "upload_capacity_bytes=%llu upload_used_bytes=%llu upload_peak_bytes=%llu timer_readback_bytes=%llu "
                "textures=%zu texture_capacity=%zu psos=%zu graph_transients=%zu graph_transient_capacity=%zu "
                "rtv_used=%u rtv_capacity=%u dsv_used=%u dsv_capacity=%u srv_static_used=%u srv_static_capacity=%u "
                "srv_transient_used=%u srv_transient_capacity=%u srv_transient_peak=%u\n",
                stress.framesRun,
                stress.sceneLoadsRequested,
                static_cast<unsigned long long>( memoryStats.process.taskManagerBytes ),
                static_cast<unsigned long long>( memoryStats.process.workingSetBytes ),
                static_cast<unsigned long long>( memoryStats.process.privateWorkingSetBytes ),
                static_cast<unsigned long long>( memoryStats.process.privateCommitBytes ),
                static_cast<unsigned long long>( memoryStats.process.pagefileUsageBytes ),
                static_cast<unsigned long long>( memoryStats.trackedEngineBytes ),
                static_cast<unsigned long long>( memoryStats.replay.totalBytes ),
                static_cast<unsigned long long>( memoryStats.gameObjects.totalBytes ),
                static_cast<unsigned long long>( memoryStats.unattributedProcessBytes ),
                renderStats.available ? 1 : 0,
                renderStats.adapterMemoryAvailable ? 1 : 0,
                static_cast<unsigned long long>( renderStats.localCurrentUsageBytes ),
                static_cast<unsigned long long>( renderStats.nonLocalCurrentUsageBytes ),
                static_cast<unsigned long long>( renderStats.localBudgetBytes ),
                static_cast<unsigned long long>( renderStats.nonLocalBudgetBytes ),
                static_cast<unsigned long long>( renderStats.uploadCapacityBytes ),
                static_cast<unsigned long long>( renderStats.uploadUsedBytes ),
                static_cast<unsigned long long>( renderStats.uploadPeakBytes ),
                static_cast<unsigned long long>( renderStats.timerReadbackBytes ),
                renderStats.textureRegistryCount,
                renderStats.textureRegistryCapacity,
                renderStats.psoCacheCount,
                renderStats.graphTransientCount,
                renderStats.graphTransientCapacity,
                renderStats.rtvDescriptorsUsed,
                renderStats.rtvDescriptorsCapacity,
                renderStats.dsvDescriptorsUsed,
                renderStats.dsvDescriptorsCapacity,
                renderStats.srvStaticDescriptorsUsed,
                renderStats.srvStaticDescriptorsCapacity,
                renderStats.srvTransientDescriptorsUsedThisFrame,
                renderStats.srvTransientDescriptorsCapacityPerFrame,
                renderStats.srvTransientDescriptorsPeakThisRun );
        fflush( stdout );
    }
}
