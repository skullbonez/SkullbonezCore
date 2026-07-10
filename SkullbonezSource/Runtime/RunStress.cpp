/*
File: SkullbonezSource/Runtime/RunStress.cpp
Purpose:
  Runs stress and automation paths for validation-oriented launches.

Mental model:
  RunStress.cpp runs stress and automation paths for validation-oriented
  launches. As an implementation unit, keep edits anchored on local owner
  boundaries and call direction and on the glossary/invariants below.

Glossary:
  Lane R result: Recoverable scene-load or GPU-drain failure surfaced through
    the stress action result instead of being counted as successful churn.

Invariants:
  - UI stress randomness is deterministic from UIStressState so crashes can be
    reproduced from the same launch options.
  - UI stress keeps runtime churn disabled; graphics stress intentionally flips
    render/runtime churn on so DX12 state tracking gets exercised.
  - A generated-scene drain failure ends the stress action before later churn.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "RuntimeTuning.h"
#include "Scene/SceneRuntimeGeneratedControls.h"
#include "Scene/SceneRuntimeStyle.h"

#include <cstdio>

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


class StressHarness
{
  public:
    // Concept: UI stress is a deterministic action picker. It emits UI/runtime
    // policy decisions from the seed while Run applies them to live owners.
    static int NextInt( UIStressState& stress, int maxExclusive )
    {
        if ( maxExclusive <= 0 )
        {
            return 0;
        }
        return static_cast<int>( NextRandom( stress ) % static_cast<unsigned int>( maxExclusive ) );
    }

    static float NextFloat( UIStressState& stress, float minValue, float maxValue )
    {
        const float unit = static_cast<float>( NextRandom( stress ) & 0xFFFFu ) / 65535.0f;
        return minValue + ( maxValue - minValue ) * unit;
    }

    static int ActionCount( const UIStressState& stress )
    {
        return std::clamp( stress.actionsPerFrame, 1, 32 );
    }

    static int NextAction( UIStressState& stress )
    {
        return NextInt( stress, 24 );
    }

    static bool AllowsRuntimeChurn()
    {
        return false;
    }

  private:
    static unsigned int NextRandom( UIStressState& stress )
    {
        return NextStressRandom( stress.randomState );
    }
};


// Concept: The extracted action helper still names every live owner explicitly
// instead of growing Run's public/private method surface.
struct UIStressActionContext
{
    SkullbonezCore::UI::InGameUI& ui;
    RunRuntimeSettings& runtimeSettings;
    RuntimeRenderBackendView& renderBackendView;
    RunDebugState& debug;
    RunSceneState& scene;
    RunTimerState& timers;
    SimulationSystem& simulation;
    SceneController& sceneController;
    SkullbonezCore::Environment::WorldEnvironment& world;
    ReplayRuntime& replayRuntime;
};


void ApplyUIStressAction( UIStressActionContext& context, UIStressState& stress, bool allowRuntimeChurn )
{
    switch ( StressHarness::NextAction( stress ) )
    {
    case 0:
        context.ui.SetActiveTab(
            static_cast<InGameUITab>( StressHarness::NextInt( stress, static_cast<int>( InGameUITab::Count ) ) ) );
        break;
    case 1:
        context.ui.SetScrollY( StressHarness::NextFloat( stress, 0.0f, 900.0f ) );
        break;
    case 2:
        // Keep the PRNG sequence stable while leaving backdrop blur to validate_ui.bat.
        // Stress runs churn control state; blur's DX12 readback path has its own pixel gate.
        (void)StressHarness::NextInt( stress, 2 );
        break;
    case 3:
        context.ui.SetProfilerTimelineEnabled( StressHarness::NextInt( stress, 2 ) != 0 );
        break;
    case 4:
        context.ui.SetPerformanceHistogramEnabled( StressHarness::NextInt( stress, 2 ) != 0 );
        break;
    case 5:
        context.ui.SetRendererComboOpen( StressHarness::NextInt( stress, 2 ) != 0 );
        break;
    case 6:
        context.ui.SetWaterComboOpen( StressHarness::NextInt( stress, 2 ) != 0 );
        break;
    case 7:
        context.ui.SetSceneComboOpen( StressHarness::NextInt( stress, 2 ) != 0 );
        break;
    case 8:
        context.runtimeSettings.isVsyncEnabled = !context.runtimeSettings.isVsyncEnabled;
        if ( context.renderBackendView.deviceLifecycle )
        {
            context.renderBackendView.deviceLifecycle->SetVsyncEnabled( context.runtimeSettings.isVsyncEnabled );
        }
        break;
    case 9:
        if ( allowRuntimeChurn )
        {
            context.debug.isCollisionVisualizer = !context.debug.isCollisionVisualizer;
        }
        break;
    case 10:
    {
        static const uint32_t kFlags[] = { PHYSICS_DEBUG_AXES,
                                           PHYSICS_DEBUG_CONTACTS,
                                           PHYSICS_DEBUG_SLEEP,
                                           PHYSICS_DEBUG_ALL };
        const int flagIndex = StressHarness::NextInt( stress, 4 );
        if ( allowRuntimeChurn )
        {
            context.debug.physicsDebugFlags = kFlags[flagIndex];
        }
        break;
    }
    case 11:
        if ( allowRuntimeChurn )
        {
            context.debug.isPhysicsDebugTransparent = !context.debug.isPhysicsDebugTransparent;
        }
        break;
    case 12:
        if ( allowRuntimeChurn )
        {
            context.debug.isBroadphaseOverlay = !context.debug.isBroadphaseOverlay;
        }
        break;
    case 13:
        if ( allowRuntimeChurn )
        {
            context.scene.isFixedStep = !context.scene.isFixedStep;
            context.simulation.Reset();
        }
        break;
    case 14:
        if ( allowRuntimeChurn )
        {
            context.debug.isTerrainHidden = !context.debug.isTerrainHidden;
        }
        break;
    case 15:
        if ( allowRuntimeChurn )
        {
            context.debug.isWaterHidden = !context.debug.isWaterHidden;
        }
        break;
    case 16:
        if ( allowRuntimeChurn )
        {
            context.debug.isWaterFreezeDebug = !context.debug.isWaterFreezeDebug;
            if ( context.debug.isWaterFreezeDebug )
            {
                context.debug.frozenWaterTime =
                    static_cast<float>( context.timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        break;
    case 17:
        if ( allowRuntimeChurn )
        {
            context.debug.isWaterFlatDebug = !context.debug.isWaterFlatDebug;
        }
        break;
    case 18:
    {
        const int mode = StressHarness::NextInt( stress, 3 );
        if ( allowRuntimeChurn )
        {
            context.debug.isWaterRTReflect = mode == 1;
            context.debug.isWaterNoReflect = mode == 2;
        }
        break;
    }
    case 19:
    {
        const float timeScale = StressHarness::NextFloat( stress, 0.10f, 4.00f );
        if ( allowRuntimeChurn )
        {
            // Concept: Scene-tab churn goes through the scene controller so
            // reset preservation and generated-scene rebuilds see one owner.
            context.sceneController.UIOverrides().timeScaleOverride = timeScale;
            context.scene.timeScale = context.sceneController.UIOverrides().timeScaleOverride;
            context.simulation.Reset();
        }
        break;
    }
    case 20:
    {
        const float alpha = StressHarness::NextFloat( stress, 0.05f, 1.00f );
        if ( allowRuntimeChurn )
        {
            context.debug.physicsDebugAlpha = alpha;
        }
        break;
    }
    case 21:
    {
        const float contactLinger = StressHarness::NextFloat( stress, 0.00f, 5.00f );
        if ( allowRuntimeChurn )
        {
            context.debug.physicsDebugContactLinger = contactLinger;
        }
        break;
    }
    case 22:
    {
        const float gravity = -StressHarness::NextFloat( stress, 0.0f, 80.0f );
        const float fluidHeight = StressHarness::NextFloat( stress, -40.0f, 140.0f );
        const float fluidDensity = StressHarness::NextFloat( stress, 0.0f, 5.0f );
        if ( allowRuntimeChurn )
        {
            ApplyUIWorldOverride( context.world, context.replayRuntime, gravity, fluidHeight, fluidDensity );
        }
        break;
    }
    case 23:
        context.ui.SetActiveTab(
            static_cast<InGameUITab>( StressHarness::NextInt( stress, static_cast<int>( InGameUITab::Count ) ) ) );
        break;
    default:
        break;
    }
}


// Concept: Graphics stress mutates a wider slice of runtime state than UI
// stress. The context keeps those owners named at the extraction boundary while
// Run stays the frame coordinator.
struct GraphicsStressActionContext
{
    RunLaunchOptions& launchOptions;
    EngineConfig& config;
    RunRuntimeSettings& runtimeSettings;
    RunDebugState& debug;
    RunSceneState& scene;
    RunTimerState& timers;
    RunCameraState& camera;
    SkullbonezCore::UI::InGameUI& ui;
    SceneController& sceneController;
    const SkullbonezCore::Assets::AssetSystem& assets;
    CinematicRenderConfig& defaultCinematicRender;
    SimulationSystem& simulation;
    RuntimeTools& runtimeTools;
    SkullbonezCore::Environment::WorldEnvironment& world;
    ReplayRuntime& replayRuntime;
    SkullbonezCore::GameObjects::GameModelCollection& models;
};


SceneRuntimeStyleContext BuildGraphicsStressStyleContext( GraphicsStressActionContext& context )
{
    return SceneRuntimeStyleContext{ context.launchOptions,
                                     context.scene,
                                     context.sceneController.Browser(),
                                     context.models,
                                     context.sceneController.Entities(),
                                     context.assets,
                                     RuntimeActiveCinematicConfig( context.scene, context.config ),
                                     context.defaultCinematicRender };
}


void ApplyGraphicsStressAction( GraphicsStressActionContext& context, GraphicsStressController& stress )
{
    switch ( stress.NextAction() )
    {
    case 0:
    {
        CinematicRenderConfig& cinematic = RuntimeActiveCinematicConfig( context.scene, context.config );
        cinematic.enabled = !cinematic.enabled;
        context.launchOptions.hasCinematicRenderingOverride = false;
        if ( context.scene.isSceneMode )
        {
            context.scene.hasCinematicRenderingOverride = true;
            context.scene.isCinematicRenderingEnabled = cinematic.enabled;
            context.scene.cinematicOverrideMask |= SCENE_CINE_RENDERING;
            context.scene.uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
        }
        break;
    }
    case 1:
    {
        CinematicRenderConfig& cinematic = RuntimeActiveCinematicConfig( context.scene, context.config );
        const UICinematicFeature feature =
            static_cast<UICinematicFeature>( stress.NextInt( static_cast<int>( UICinematicFeature::Count ) ) );
        if ( feature == UICinematicFeature::Shadows )
        {
            context.launchOptions.hasCinematicShadowsOverride = false;
        }
        ToggleCinematicUIFeature( cinematic, context.scene, feature );
        break;
    }
    case 2:
    {
        CinematicRenderConfig& cinematic = RuntimeActiveCinematicConfig( context.scene, context.config );
        const UICinematicParam param =
            static_cast<UICinematicParam>( stress.NextInt( static_cast<int>( UICinematicParam::Count ) ) );
        ApplyCinematicUIParam( cinematic, context.scene, param, stress.RandomCinematicParamValue( param ) );
        break;
    }
    case 3:
    {
        const int browserCount = static_cast<int>( context.sceneController.Browser().paths.size() );
        const int browserIndex = ( browserCount > 0 && stress.NextInt( 5 ) != 0 ) ? stress.NextInt( browserCount ) : -1;
        (void)ApplyCinematicModeFromBrowserIndex( BuildGraphicsStressStyleContext( context ), browserIndex );
        break;
    }
    case 4:
        context.runtimeSettings.isVsyncEnabled = !context.runtimeSettings.isVsyncEnabled;
        break;
    case 5:
        context.runtimeSettings.isPipelineSyncEnabled = !context.runtimeSettings.isPipelineSyncEnabled;
        break;
    case 6:
        context.debug.isTerrainHidden = !context.debug.isTerrainHidden;
        break;
    case 7:
        context.debug.isWaterHidden = !context.debug.isWaterHidden;
        break;
    case 8:
        context.debug.isWaterFreezeDebug = !context.debug.isWaterFreezeDebug;
        if ( context.debug.isWaterFreezeDebug )
        {
            context.debug.frozenWaterTime =
                static_cast<float>( context.timers.simulationTimer.GetTimeSinceLastStart() );
        }
        break;
    case 9:
        context.debug.isWaterFlatDebug = !context.debug.isWaterFlatDebug;
        break;
    case 10:
    {
        const int mode = stress.NextInt( 3 );
        context.debug.isWaterRTReflect = mode == 1;
        context.debug.isWaterNoReflect = mode == 2;
        break;
    }
    case 11:
        context.debug.isCollisionVisualizer = !context.debug.isCollisionVisualizer;
        break;
    case 12:
        context.debug.isBroadphaseOverlay = !context.debug.isBroadphaseOverlay;
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
        context.debug.physicsDebugFlags =
            kFlags[stress.NextInt( static_cast<int>( sizeof( kFlags ) / sizeof( kFlags[0] ) ) )];
        break;
    }
    case 14:
        context.debug.isPhysicsDebugTransparent = !context.debug.isPhysicsDebugTransparent;
        context.debug.physicsDebugAlpha = stress.NextFloat( 0.05f, 1.0f );
        context.debug.physicsDebugContactLinger = stress.NextFloat( 0.0f, 5.0f );
        break;
    case 15:
    {
        const float timeScale = stress.NextFloat( 0.05f, 4.0f );
        context.sceneController.UIOverrides().timeScaleOverride = timeScale;
        context.scene.timeScale = timeScale;
        context.simulation.Reset();
        break;
    }
    case 16:
        ApplyUIWorldOverride( context.world,
                              context.replayRuntime,
                              -stress.NextFloat( 0.0f, 80.0f ),
                              stress.NextFloat( -80.0f, 160.0f ),
                              stress.NextFloat( 0.0f, 5.0f ) );
        break;
    case 17:
        context.sceneController.UIOverrides().modelCountOverride = 32 + stress.NextInt( 512 );
        break;
    case 18:
        context.launchOptions.generatedObjectTypeOverride =
            static_cast<GeneratedObjectTypeOverride>( stress.NextInt( 3 ) );
        break;
    case 19:
        context.runtimeSettings.tornadoField.enabled = stress.NextInt( 2 ) != 0;
        context.runtimeSettings.tornadoField.visualizeVelocityField = stress.NextInt( 2 ) != 0;
        context.runtimeSettings.tornadoVisual.enabled = stress.NextInt( 2 ) != 0;
        SyncTornadoRuntimeSettingsToPhysics( context.models, context.runtimeSettings );
        break;
    case 20:
        context.runtimeSettings.tornadoVisual.shellAlpha = stress.NextFloat( 0.02f, 0.40f );
        context.runtimeSettings.tornadoVisual.dustAlpha = stress.NextFloat( 0.02f, 0.55f );
        context.runtimeSettings.tornadoVisual.ribbonWidth = stress.NextFloat( 1.0f, 12.0f );
        context.runtimeSettings.tornadoVisual.ribbonCount = 1 + stress.NextInt( 10 );
        context.runtimeSettings.tornadoVisual.particleCount = 16 + stress.NextInt( 240 );
        break;
    case 21:
        context.ui.SetActiveTab( static_cast<InGameUITab>( stress.NextInt( static_cast<int>( InGameUITab::Count ) ) ) );
        context.ui.SetScrollY( stress.NextFloat( 0.0f, 1200.0f ) );
        break;
    case 22:
        context.scene.isFixedStep = !context.scene.isFixedStep;
        context.simulation.Reset();
        break;
    case 23:
        context.runtimeSettings.isPhysicsSleepEnabled = !context.runtimeSettings.isPhysicsSleepEnabled;
        context.models.SetPhysicsSleepEnabled( context.runtimeSettings.isPhysicsSleepEnabled );
        break;
    case 24:
        context.debug.isTopTextHidden = !context.debug.isTopTextHidden;
        break;
    case 25:
        context.debug.overlayMode = static_cast<OverlayMode>( stress.NextInt( 6 ) );
        break;
    case 26:
        context.runtimeTools.Laser().Update( 0.0f );
        break;
    case 27:
        context.camera.trackHeight = stress.NextFloat( 8.0f, 500.0f );
        break;
    case 28:
        context.launchOptions.generatedObjectTypeOverride =
            static_cast<GeneratedObjectTypeOverride>( stress.NextInt( 3 ) );
        break;
    case 29:
        context.ui.SetProfilerTimelineEnabled( stress.NextInt( 2 ) != 0 );
        context.ui.SetPerformanceHistogramEnabled( stress.NextInt( 2 ) != 0 );
        break;
    case 30:
        context.ui.SetRendererComboOpen( stress.NextInt( 2 ) != 0 );
        context.ui.SetWaterComboOpen( stress.NextInt( 2 ) != 0 );
        context.ui.SetSceneComboOpen( stress.NextInt( 2 ) != 0 );
        break;
    case 31:
        context.debug.isUITestPattern = stress.NextInt( 2 ) != 0;
        break;
    default:
        break;
    }
}


} // namespace


void GraphicsStressController::Configure( unsigned int seed,
                                          int actionsPerFrame,
                                          int sceneIntervalFrames,
                                          int memoryLogIntervalFrames )
{
    m_enabled = true;
    m_randomState = seed;
    m_actionsPerFrame = actionsPerFrame;
    m_sceneIntervalFrames = sceneIntervalFrames;
    m_memoryLogIntervalFrames = memoryLogIntervalFrames;
}


void GraphicsStressController::ResumeAfterSceneLoad( unsigned int seed, int actionsPerFrame, int sceneIntervalFrames )
{
    m_enabled = true;
    if ( m_randomState == 0 )
    {
        m_randomState = seed;
    }
    m_actionsPerFrame = actionsPerFrame;
    m_sceneIntervalFrames = sceneIntervalFrames;
}


bool GraphicsStressController::IsEnabled() const
{
    return m_enabled;
}


unsigned int GraphicsStressController::RandomState() const
{
    return m_randomState;
}


int GraphicsStressController::ActionsPerFrame() const
{
    return m_actionsPerFrame;
}


int GraphicsStressController::SceneIntervalFrames() const
{
    return m_sceneIntervalFrames;
}


int GraphicsStressController::FramesRun() const
{
    return m_framesRun;
}


int GraphicsStressController::SceneLoadsRequested() const
{
    return m_sceneLoadsRequested;
}


void GraphicsStressController::BeginFrame()
{
    ++m_framesRun;
}


void GraphicsStressController::RecordSceneLoad()
{
    m_lastSceneLoadFrame = m_framesRun;
    ++m_sceneLoadsRequested;
}


int GraphicsStressController::NextInt( int maxExclusive )
{
    if ( maxExclusive <= 0 )
    {
        return 0;
    }
    m_randomState = NextStressRandom( m_randomState );
    return static_cast<int>( m_randomState % static_cast<unsigned int>( maxExclusive ) );
}


float GraphicsStressController::NextFloat( float minValue, float maxValue )
{
    m_randomState = NextStressRandom( m_randomState );
    const float unit = static_cast<float>( m_randomState & 0xFFFFu ) / 65535.0f;
    return minValue + ( maxValue - minValue ) * unit;
}


int GraphicsStressController::ActionCount() const
{
    return std::clamp( m_actionsPerFrame, 1, 64 );
}


int GraphicsStressController::NextAction()
{
    return NextInt( 32 );
}


bool GraphicsStressController::SceneLoadDue() const
{
    return m_framesRun - m_lastSceneLoadFrame >= m_sceneIntervalFrames;
}


bool GraphicsStressController::ShouldPrintFrameSummary() const
{
    return m_framesRun % 60 == 0;
}


bool GraphicsStressController::ShouldLogMemory() const
{
    return m_memoryLogIntervalFrames > 0 && ( m_framesRun == 1 || m_framesRun % m_memoryLogIntervalFrames == 0 );
}


float GraphicsStressController::RandomCinematicParamValue( UI::UICinematicParam param )
{
    switch ( param )
    {
    case UICinematicParam::Exposure:
        return NextFloat( 0.05f, 3.00f );
    case UICinematicParam::Gamma:
        return NextFloat( 1.00f, 3.00f );
    case UICinematicParam::SkyMode:
    case UICinematicParam::TerrainMode:
    case UICinematicParam::ObjectStyle:
        return NextFloat( 0.0f, 32.0f );
    case UICinematicParam::WaterMode:
        return NextFloat( 0.0f, 4.0f );
    case UICinematicParam::StyleSaturation:
    case UICinematicParam::StyleContrast:
        return NextFloat( 0.0f, 2.50f );
    case UICinematicParam::StyleVignette:
    case UICinematicParam::SunX:
    case UICinematicParam::SunY:
    case UICinematicParam::CloudCoverage:
    case UICinematicParam::FogOpacity:
    case UICinematicParam::BasinFeather:
    case UICinematicParam::WaterAlpha:
    case UICinematicParam::WaterReflection:
        return NextFloat( 0.0f, 1.0f );
    case UICinematicParam::SunBrightness:
        return NextFloat( 0.0f, 40.0f );
    case UICinematicParam::SunRed:
    case UICinematicParam::SunGreen:
    case UICinematicParam::SunBlue:
        return NextFloat( 0.0f, 2.0f );
    case UICinematicParam::SkyGlow:
        return NextFloat( 0.0f, 8.0f );
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
        return NextFloat( 0.0f, 1.50f );
    case UICinematicParam::CloudSoftness:
        return NextFloat( 0.01f, 0.65f );
    case UICinematicParam::CloudScale:
        return NextFloat( 0.50f, 12.0f );
    case UICinematicParam::ShaftStrength:
        return NextFloat( 0.0f, 3.0f );
    case UICinematicParam::ShaftFalloff:
        return NextFloat( 0.25f, 5.0f );
    case UICinematicParam::VolumetricStrength:
        return NextFloat( 0.0f, 2.0f );
    case UICinematicParam::VolumetricDensity:
        return NextFloat( 0.0f, 2.50f );
    case UICinematicParam::VolumetricDecay:
        return NextFloat( 0.800f, 0.995f );
    case UICinematicParam::BloomThreshold:
        return NextFloat( 0.0f, 4.0f );
    case UICinematicParam::BloomKnee:
        return NextFloat( 0.01f, 2.0f );
    case UICinematicParam::BloomStrength:
        return NextFloat( 0.0f, 2.0f );
    case UICinematicParam::BloomRadius:
        return NextFloat( 0.25f, 8.0f );
    case UICinematicParam::TerrainRelief:
        return NextFloat( 0.0f, 1.50f );
    case UICinematicParam::TerrainGridScale:
        return NextFloat( 0.10f, 120.0f );
    case UICinematicParam::TerrainGridStrength:
    case UICinematicParam::WaterGlint:
        return NextFloat( 0.0f, 4.0f );
    case UICinematicParam::BasinCenterX:
    case UICinematicParam::BasinCenterZ:
        return NextFloat( 0.0f, 1200.0f );
    case UICinematicParam::BasinRadiusX:
    case UICinematicParam::BasinRadiusZ:
        return NextFloat( 1.0f, 500.0f );
    case UICinematicParam::BasinDepth:
        return NextFloat( 0.0f, 80.0f );
    case UICinematicParam::BasinRimLift:
        return NextFloat( 0.0f, 60.0f );
    case UICinematicParam::FogDensity:
        return NextFloat( 0.0f, 0.006f );
    case UICinematicParam::FogStart:
        return NextFloat( 0.0f, 500.0f );
    case UICinematicParam::FogEnd:
        return NextFloat( 100.0f, 4000.0f );
    case UICinematicParam::None:
    case UICinematicParam::Count:
    default:
        return 0.0f;
    }
}


SbResult Run::RunUIStressActions()
{
    UIStressState& stress = m_diagnosticsRuntime.UIStress();
    if ( !stress.enabled || !m_systems.window )
    {
        return SbResult::Success();
    }

    ++stress.framesRun;
    const double UINow = m_timers.simulationTimer.GetTotalTime();
    const int screenW = (std::max)( 1, m_systems.window->ClientWidth() );
    const int screenH = (std::max)( 1, m_systems.window->ClientHeight() );

    m_UI.SetVisible( true, UINow );
    m_UI.SetMinimized( false, UINow );

    m_UI.SetMouseOverride( true, StressHarness::NextInt( stress, screenW ), StressHarness::NextInt( stress, screenH ) );

    // This gate is a UI control-state crash sweep. Runtime rebuilds and world
    // debug toggles belong to render/physics validation, so they stay frozen here.
    const bool allowRuntimeChurn = StressHarness::AllowsRuntimeChurn();
    const auto makeSceneGeneratedControlContext = [this]() -> SceneRuntimeGeneratedControlContext
    {
        return SceneRuntimeGeneratedControlContext{ SceneState(),
                                                    m_sceneController.UIOverrides(),
                                                    m_camera,
                                                    m_sceneController,
                                                    m_config,
                                                    m_sceneController.World(),
                                                    m_systems.terrain.get(),
                                                    m_sceneController.Models(),
                                                    m_simulation,
                                                    m_runtimeTools,
                                                    m_renderBackendView.deviceLifecycle,
                                                    m_launchOptions.generatedObjectTypeOverride,
                                                    m_startup.gameModelCapacity };
    };
    const auto executeSceneGeneratedControlAction =
        [this]( const SceneRuntimeGeneratedControlAction& action ) -> SbResult
    {
        if ( !action.status.ok )
        {
            // Lane R: resources remain intact; return before later stress churn
            // and let the input boundary report and end the run.
            return action.status;
        }
        if ( action.resetReplayTimeline )
        {
            const ReplayRuntime::SceneTimelineResetInput reset = ReplayRuntime::DescribeSceneTimeline(
                m_sceneController,
                SceneState(),
                m_startup.gameModelCapacity,
                static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
            m_replayRuntime.ResetSceneTimeline(
                reset,
                ReplayRuntime::SceneTimelineResetOwners{
                    m_inputRouter,
                    m_interaction,
                    &m_sceneController.Cameras(),
                    m_systems.terrain.get(),
                    m_camera,
                    NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
                    m_attachedCamera.activeFollow,
                    m_camera.director.grabbed } );
        }
        if ( action.scheduleProfileReset )
        {
            PROFILE_SCHEDULE_RESET();
        }
        return SbResult::Success();
    };
    if ( stress.framesRun == 18 )
    {
        const int modelCount = 96 + StressHarness::NextInt( stress, 160 );
        if ( allowRuntimeChurn )
        {
            const SbResult actionResult = executeSceneGeneratedControlAction(
                ApplyUIModelCountOverride( makeSceneGeneratedControlContext(), modelCount ) );
            if ( !actionResult.ok )
            {
                return actionResult;
            }
        }
    }
    if ( stress.framesRun == 42 )
    {
        const int balls = 24 + StressHarness::NextInt( stress, 220 );
        const int boxes = StressHarness::NextInt( stress, 1000 - balls + 1 );
        if ( allowRuntimeChurn )
        {
            const SbResult actionResult = executeSceneGeneratedControlAction(
                ApplyUISolverObjectCounts( makeSceneGeneratedControlContext(), balls, boxes ) );
            if ( !actionResult.ok )
            {
                return actionResult;
            }
        }
    }
    UIStressActionContext actionContext{ m_UI,
                                         m_runtimeSettings,
                                         m_renderBackendView,
                                         m_debug,
                                         SceneState(),
                                         m_timers,
                                         m_simulation,
                                         m_sceneController,
                                         m_sceneController.World(),
                                         m_replayRuntime };
    const int actionCount = StressHarness::ActionCount( stress );
    for ( int i = 0; i < actionCount; ++i )
    {
        ApplyUIStressAction( actionContext, stress, allowRuntimeChurn );
    }
    return SbResult::Success();
}


void Run::RunGraphicsStressActions( const Rendering::IRenderDiagnostics& renderDiagnostics )
{
    // Concept: graphics stress is a deterministic fuzzer over scene loading,
    // cinematic controls, render-path toggles, and heavy generated-scene resets.
    // Keep every mutation reproducible from the launch seed so a crash line in
    // latest_stdout.txt can be replayed exactly under cdb.
    GraphicsStressController& stress = m_graphicsStress;
    if ( !stress.IsEnabled() || !m_systems.window )
    {
        return;
    }

    stress.BeginFrame();
    if ( stress.FramesRun() == 1 )
    {
        printf( "[graphics-stress] Running seed=%u actions=%d scene_interval_frames=%d\n",
                stress.RandomState(),
                stress.ActionsPerFrame(),
                stress.SceneIntervalFrames() );
        fflush( stdout );
    }

    auto executeSceneLoadRequest = [&]( const SceneLoadRequest& request ) -> bool
    {
        if ( request.enterInteractiveSceneRun )
        {
            EnterInteractiveSceneRun();
        }
        if ( !request.accepted )
        {
            return false;
        }
        return !request.HasLoad() || LoadScene( request.index,
                                                request.preserveUIState,
                                                request.suppressExitOnComplete,
                                                request.preserveRuntimeState )
                                         .ok;
    };

    if ( stress.SceneLoadDue() )
    {
        // Hazard: suite order is the durable test contract. Browser fallback is
        // useful for manual app launches, but automated repros must prefer the
        // checked-in graphics_stress.suite.json scene queue.
        SceneLoadRequest request = SceneLoadRequest::None();
        int selectedSceneIndex = -1;
        const char* selectedSceneSource = "none";
        if ( m_sceneController.QueueSize() > 0 )
        {
            selectedSceneIndex = stress.NextInt( m_sceneController.QueueSize() );
            selectedSceneSource = "queue";
            request = SceneLoadRequest::Load( selectedSceneIndex, true, true, stress.NextInt( 2 ) != 0, true );
        }
        else if ( !m_sceneController.Browser().paths.empty() )
        {
            selectedSceneIndex = stress.NextInt( static_cast<int>( m_sceneController.Browser().paths.size() ) );
            selectedSceneSource = "browser";
            request = m_sceneController.LoadSceneFromBrowserIndex( selectedSceneIndex );
        }

        if ( executeSceneLoadRequest( request ) )
        {
            stress.RecordSceneLoad();
            printf( "[graphics-stress] scene_load=%d frame=%d source=%s selected_index=%d action_index=%d\n",
                    stress.SceneLoadsRequested(),
                    stress.FramesRun(),
                    selectedSceneSource,
                    selectedSceneIndex,
                    request.index );
            fflush( stdout );
        }
        else
        {
            printf( "[graphics-stress] scene_load_skipped frame=%d source=%s selected_index=%d\n",
                    stress.FramesRun(),
                    selectedSceneSource,
                    selectedSceneIndex );
            fflush( stdout );
        }
    }

    m_UI.SetVisible( true, m_timers.simulationTimer.GetTotalTime() );
    m_UI.SetMinimized( false, m_timers.simulationTimer.GetTotalTime() );
    SceneState().isInteractiveRun = true;
    SceneState().isExitOnComplete = false;

    GraphicsStressActionContext actionContext{ m_launchOptions,
                                               m_config,
                                               m_runtimeSettings,
                                               m_debug,
                                               SceneState(),
                                               m_timers,
                                               m_camera,
                                               m_UI,
                                               m_sceneController,
                                               m_systems.assets,
                                               m_defaultCinematicRender,
                                               m_simulation,
                                               m_runtimeTools,
                                               m_sceneController.World(),
                                               m_replayRuntime,
                                               m_sceneController.Models() };
    const int actionCount = stress.ActionCount();
    // Invariant: random values stay inside the same broad ranges exposed by the
    // runtime UI. The stress test should crash bad DX12 lifetime/state tracking,
    // not manufacture impossible physics or render data.
    for ( int i = 0; i < actionCount; ++i )
    {
        ApplyGraphicsStressAction( actionContext, stress );
    }

    if ( stress.ShouldPrintFrameSummary() )
    {
        printf( "[graphics-stress] frame=%d scene_loads=%d rng=%u\n",
                stress.FramesRun(),
                stress.SceneLoadsRequested(),
                stress.RandomState() );
        fflush( stdout );
    }

    if ( stress.ShouldLogMemory() )
    {
        // Why: long stress runs need memory attribution before shutdown. If the
        // process is killed after a climb, this stdout line survives with the
        // same seed/frame/scene-load position as the repro log.
        const MainMemoryStats& memoryStats =
            m_diagnosticsRuntime.RefreshMainMemoryStats( m_replayRuntime,
                                                         m_sceneController.Models(),
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
                stress.FramesRun(),
                stress.SceneLoadsRequested(),
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
