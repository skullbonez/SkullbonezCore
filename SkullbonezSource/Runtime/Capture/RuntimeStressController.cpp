/*
File: SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp
Purpose:
  Runs stress and automation paths for validation-oriented launches.

Summary:
  Stress controllers execute deterministic validation churn through non-copyable
  per-call frame views of the owners each action may mutate.

Glossary:
  Lane R result: Recoverable scene-load or GPU-drain failure surfaced through
    the stress action result instead of being counted as successful churn.

Invariants:
  - UI stress randomness is deterministic from UIStressState so crashes can be
    reproduced from the same launch options.
  - UI stress keeps runtime churn disabled; graphics stress intentionally flips
    render/runtime churn on so DX12 state tracking gets exercised.
  - A generated-scene drain failure ends the stress action before later churn.
  - Stress helpers never retain a frame view or a referenced runtime owner.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RuntimeStressController.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../Camera/AttachedCameraController.h"
#include "../App/InputFrame.h"
#include "../Input/InputRouter.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Diagnostics/SceneMemoryDiagnostics.h"
#include "../Render/RuntimeRenderHost.h"
#include "../Render/RuntimeRenderer.h"
#include "../App/ReplayRuntime.h"
#include "../Render/RenderDefaultsStore.h"
#include "../Camera/CameraControlState.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../App/RunLaunchOptions.h"
#include "../App/RunStartupState.h"
#include "../App/RunTimerState.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Interaction/OperatorCommandApplier.h"
#include "../Scene/SceneController.h"
#include "../Scene/SceneLoadTransaction.h"
#include "../Scene/SceneRuntimeCoordinator.h"
#include "../Scene/SceneRuntimeGeneratedControls.h"
#include "../Scene/SceneRuntimeStyle.h"
#include "../Tools/RuntimeTools.h"
#include "../App/Window.h"
#include "../../Assets/AssetSystem.h"
#include "../Simulation/SimulationSystem.h"
#include "../../Rendering/DX12/RenderDeviceDX12.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Scene/AuthoredScene.h"
#include "../../Core/WorkerPool.h"
#include "../../UI/UI.h"
#include "../../Core/Profiler.h"

#include <cstdio>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayTimelineOperations;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::RunInternal;
using SkullbonezCore::UI::InGameUITab;

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


// Lifetime: every borrow is consumed by one deterministic action and cannot be
// retained as a replacement shell context.
void ApplyUIStressAction( SkullbonezCore::UI::InGameUI& ui, RuntimeFrameSceneView& sceneOwners, RuntimeRenderer& renderer,
                          ReplayRuntime& replayRuntime, UIStressState& stress, bool allowRuntimeChurn )
{
    RuntimeOverlayPresentationEdit presentationEdit = sceneOwners.overlays.EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    SceneController& sceneController = sceneOwners.sceneController;
    SceneSessionState& scene = sceneController.State();
    RunTimerState& timers = sceneOwners.timers;
    SimulationSystem& simulation = sceneOwners.simulation;
    SkullbonezCore::Environment::WorldEnvironment& world = sceneController.Scene().Environment();

    switch ( StressHarness::NextAction( stress ) )
    {
    case 0:
        ui.SetActiveTab( static_cast<InGameUITab>( StressHarness::NextInt( stress, static_cast<int>( InGameUITab::Count ) ) ) );
        break;
    case 1:
        ui.SetScrollY( StressHarness::NextFloat( stress, 0.0f, 900.0f ) );
        break;
    case 2:

        // Keep the PRNG sequence stable while leaving backdrop blur to validate_ui.bat.
        // Stress runs churn control state; blur's DX12 readback path has its own pixel gate.
        (void)StressHarness::NextInt( stress, 2 );
        break;
    case 3:
        ui.SetProfilerTimelineEnabled( StressHarness::NextInt( stress, 2 ) != 0 );
        break;
    case 4:
        ui.SetPerformanceHistogramEnabled( StressHarness::NextInt( stress, 2 ) != 0 );
        break;
    case 5:
        ui.SetRendererComboOpen( StressHarness::NextInt( stress, 2 ) != 0 );
        break;
    case 6:
        ui.SetWaterComboOpen( StressHarness::NextInt( stress, 2 ) != 0 );
        break;
    case 7:
        ui.SetSceneComboOpen( StressHarness::NextInt( stress, 2 ) != 0 );
        break;
    case 8:
        renderer.SetVsyncEnabled( !renderer.VsyncEnabled() );

        renderer.RenderDevice().SetVsyncEnabled( renderer.VsyncEnabled() );

        break;
    case 9:

        if ( allowRuntimeChurn )
        {
            debug.isCollisionVisualizer = !debug.isCollisionVisualizer;
        }

        break;
    case 10:
    {
        static const uint32_t kFlags[] = { PHYSICS_DEBUG_AXES, PHYSICS_DEBUG_CONTACTS, PHYSICS_DEBUG_SLEEP,
                                           PHYSICS_DEBUG_ALL };

        const int flagIndex = StressHarness::NextInt( stress, 4 );

        if ( allowRuntimeChurn )
        {
            debug.physicsDebugFlags = kFlags[flagIndex];
        }

        break;
    }
    case 11:

        if ( allowRuntimeChurn )
        {
            debug.isPhysicsDebugTransparent = !debug.isPhysicsDebugTransparent;
        }

        break;
    case 12:

        if ( allowRuntimeChurn )
        {
            debug.isBroadphaseOverlay = !debug.isBroadphaseOverlay;
        }

        break;
    case 13:

        if ( allowRuntimeChurn )
        {
            scene.isFixedStep = !scene.isFixedStep;
            simulation.Reset();
        }

        break;
    case 14:

        if ( allowRuntimeChurn )
        {
            debug.isTerrainHidden = !debug.isTerrainHidden;
        }

        break;
    case 15:

        if ( allowRuntimeChurn )
        {
            debug.isWaterHidden = !debug.isWaterHidden;
        }

        break;
    case 16:

        if ( allowRuntimeChurn )
        {
            debug.isWaterFreezeDebug = !debug.isWaterFreezeDebug;

            if ( debug.isWaterFreezeDebug )
            {
                debug.frozenWaterTime = static_cast<float>( timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }

        break;
    case 17:

        if ( allowRuntimeChurn )
        {
            debug.isWaterFlatDebug = !debug.isWaterFlatDebug;
        }

        break;
    case 18:
    {
        const int mode = StressHarness::NextInt( stress, 3 );

        if ( allowRuntimeChurn )
        {
            debug.isWaterRTReflect = mode == 1;
            debug.isWaterNoReflect = mode == 2;
        }

        break;
    }
    case 19:
    {
        const float timeScale = StressHarness::NextFloat( stress, 0.10f, 4.00f );

        if ( allowRuntimeChurn )
        {

            // Concept: Scene-tab churn writes the UI-owned navigation model so
            // reset preservation and generated-scene rebuilds see one owner.
            ui.SceneNavigation().overrides.timeScaleOverride = timeScale;
            scene.timeScale = ui.SceneNavigation().overrides.timeScaleOverride;
            simulation.Reset();
        }

        break;
    }
    case 20:
    {
        const float alpha = StressHarness::NextFloat( stress, 0.05f, 1.00f );

        if ( allowRuntimeChurn )
        {
            debug.physicsDebugAlpha = alpha;
        }

        break;
    }
    case 21:
    {
        const float contactLinger = StressHarness::NextFloat( stress, 0.00f, 5.00f );

        if ( allowRuntimeChurn )
        {
            debug.physicsDebugContactLinger = contactLinger;
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
            const WorldOverrideChange change = ApplyUIWorldOverride( world, gravity, fluidHeight, fluidDensity );
            replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildWorldOverride( change.previousGravity, change.previousFluidHeight,
                                                                                         change.previousFluidDensity, change.gravity,
                                                                                         change.fluidHeight, change.fluidDensity ) );
        }

        break;
    }
    case 23:
        ui.SetActiveTab( static_cast<InGameUITab>( StressHarness::NextInt( stress, static_cast<int>( InGameUITab::Count ) ) ) );
        break;
    default:
        break;
    }
}


void ApplyGraphicsStressAction( const SkullbonezCore::Assets::AssetSystem& assets,
                                RuntimeFrameInteractionView& interactionOwners, RuntimeFrameSceneView& sceneOwners,
                                const RenderDefaultsStore& renderDefaults, RuntimeRenderer& renderer,
                                ReplayRuntime& replayRuntime, GraphicsStressController& stress )
{
    RunLaunchOptions& launchOptions = sceneOwners.launchOptions;
    SkullbonezCore::Core::EngineConfig& config = sceneOwners.config;
    RuntimeOverlayPresentationEdit presentationEdit = sceneOwners.overlays.EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    SceneController& sceneController = sceneOwners.sceneController;
    SceneSessionState& scene = sceneController.State();
    RunTimerState& timers = sceneOwners.timers;
    CameraControlState& camera = interactionOwners.camera;
    SkullbonezCore::UI::InGameUI& ui = interactionOwners.operatorUi;
    const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender = renderDefaults.CinematicBaseline();
    SimulationSystem& simulation = sceneOwners.simulation;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    SkullbonezCore::Environment::WorldEnvironment& world = sceneController.Scene().Environment();
    SkullbonezCore::Runtime::SceneController& models = sceneController;

    switch ( stress.NextAction() )
    {
    case 0:
    {
        SkullbonezCore::Core::CinematicRenderConfig& cinematic = ActiveSceneCinematicConfig( scene, config );
        cinematic.enabled = !cinematic.enabled;
        launchOptions.hasCinematicRenderingOverride = false;

        if ( scene.isSceneMode )
        {
            scene.hasCinematicRenderingOverride = true;
            scene.isCinematicRenderingEnabled = cinematic.enabled;
            scene.cinematicOverrideMask |= SCENE_CINE_RENDERING;
            scene.uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
        }

        break;
    }
    case 1:
    {
        SkullbonezCore::Core::CinematicRenderConfig& cinematic = ActiveSceneCinematicConfig( scene, config );
        const UICinematicFeature feature = static_cast<UICinematicFeature>( stress.NextInt( static_cast<int>( UICinematicFeature::Count ) ) );

        if ( feature == UICinematicFeature::Shadows )
        {
            launchOptions.hasCinematicShadowsOverride = false;
        }

        ToggleCinematicUIFeature( cinematic, scene, feature );
        break;
    }
    case 2:
    {
        SkullbonezCore::Core::CinematicRenderConfig& cinematic = ActiveSceneCinematicConfig( scene, config );
        const UICinematicParam param = static_cast<UICinematicParam>( stress.NextInt( static_cast<int>( UICinematicParam::Count ) ) );

        ApplyCinematicUIParam( cinematic, scene, param, stress.RandomCinematicParamValue( param ) );
        break;
    }
    case 3:
    {
        const int browserCount = static_cast<int>( ui.SceneNavigation().browser.paths.size() );
        const int browserIndex = ( browserCount > 0 && stress.NextInt( 5 ) != 0 ) ? stress.NextInt( browserCount ) : -1;
        (void)sceneController.ApplyCinematicBrowserStyle( launchOptions, ui.SceneNavigation().browser, assets,
                                                          ActiveSceneCinematicConfig( scene, config ),
                                                          defaultCinematicRender, browserIndex );

        break;
    }
    case 4:
        renderer.SetVsyncEnabled( !renderer.VsyncEnabled() );
        break;
    case 5:
        renderer.SetPipelineSyncEnabled( !renderer.PipelineSyncEnabled() );
        break;
    case 6:
        debug.isTerrainHidden = !debug.isTerrainHidden;
        break;
    case 7:
        debug.isWaterHidden = !debug.isWaterHidden;
        break;
    case 8:
        debug.isWaterFreezeDebug = !debug.isWaterFreezeDebug;

        if ( debug.isWaterFreezeDebug )
        {
            debug.frozenWaterTime = static_cast<float>( timers.simulationTimer.GetTimeSinceLastStart() );
        }

        break;
    case 9:
        debug.isWaterFlatDebug = !debug.isWaterFlatDebug;
        break;
    case 10:
    {
        const int mode = stress.NextInt( 3 );
        debug.isWaterRTReflect = mode == 1;
        debug.isWaterNoReflect = mode == 2;
        break;
    }
    case 11:
        debug.isCollisionVisualizer = !debug.isCollisionVisualizer;
        break;
    case 12:
        debug.isBroadphaseOverlay = !debug.isBroadphaseOverlay;
        break;
    case 13:
    {
        static const uint32_t kFlags[] = {
            PHYSICS_DEBUG_NONE, PHYSICS_DEBUG_AXES, PHYSICS_DEBUG_CONTACTS, PHYSICS_DEBUG_SLEEP, PHYSICS_DEBUG_ALL,
        };

        debug.physicsDebugFlags = kFlags[stress.NextInt( static_cast<int>( sizeof( kFlags ) / sizeof( kFlags[0] ) ) )];
        break;
    }
    case 14:
        debug.isPhysicsDebugTransparent = !debug.isPhysicsDebugTransparent;
        debug.physicsDebugAlpha = stress.NextFloat( 0.05f, 1.0f );
        debug.physicsDebugContactLinger = stress.NextFloat( 0.0f, 5.0f );
        break;
    case 15:
    {
        const float timeScale = stress.NextFloat( 0.05f, 4.0f );
        ui.SceneNavigation().overrides.timeScaleOverride = timeScale;
        scene.timeScale = timeScale;
        simulation.Reset();
        break;
    }
    case 16:
    {
        const WorldOverrideChange change = ApplyUIWorldOverride( world, -stress.NextFloat( 0.0f, 80.0f ),
                                                                 stress.NextFloat( -80.0f, 160.0f ),
                                                                 stress.NextFloat( 0.0f, 5.0f ) );

        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildWorldOverride( change.previousGravity, change.previousFluidHeight,
                                                                                     change.previousFluidDensity, change.gravity,
                                                                                     change.fluidHeight, change.fluidDensity ) );

        break;
    }
    case 17:
        ui.SceneNavigation().overrides.modelCountOverride = 32 + stress.NextInt( 512 );
        break;
    case 18:
        launchOptions.generatedObjectTypeOverride = static_cast<GeneratedObjectTypeOverride>( stress.NextInt( 3 ) );
        break;
    case 19:
    {
        SkullbonezCore::Gameplay::TornadoFieldConfig tornadoField = models.Scene().Tornado().GetFieldConfig();
        tornadoField.enabled = stress.NextInt( 2 ) != 0;
        tornadoField.visualizeVelocityField = stress.NextInt( 2 ) != 0;
        models.Scene().Tornado().SetVisualEnabled( stress.NextInt( 2 ) != 0 );
        models.Scene().Tornado().SetFieldConfig( tornadoField );
        break;
    }
    case 20:
    {
        SkullbonezCore::Gameplay::TornadoVisualSettings tornadoVisual = models.Scene().Tornado().VisualSettings();
        tornadoVisual.shellAlpha = stress.NextFloat( 0.02f, 0.40f );
        tornadoVisual.dustAlpha = stress.NextFloat( 0.02f, 0.55f );
        tornadoVisual.ribbonWidth = stress.NextFloat( 1.0f, 12.0f );
        tornadoVisual.ribbonCount = 1 + stress.NextInt( 10 );
        tornadoVisual.particleCount = 16 + stress.NextInt( 240 );
        models.Scene().Tornado().SetVisualSettings( tornadoVisual );
        break;
    }
    case 21:
        ui.SetActiveTab( static_cast<InGameUITab>( stress.NextInt( static_cast<int>( InGameUITab::Count ) ) ) );
        ui.SetScrollY( stress.NextFloat( 0.0f, 1200.0f ) );
        break;
    case 22:
        scene.isFixedStep = !scene.isFixedStep;
        simulation.Reset();
        break;
    case 23:
        models.Scene().Physics().SetSleepEnabled( !models.Scene().Physics().IsSleepEnabled() );
        break;
    case 24:
        debug.isTopTextHidden = !debug.isTopTextHidden;
        break;
    case 25:
        debug.overlayMode = static_cast<OverlayMode>( stress.NextInt( 6 ) );
        break;
    case 26:
        runtimeTools.Laser().Update( 0.0f );
        break;
    case 27:
        camera.trackHeight = stress.NextFloat( 8.0f, 500.0f );
        break;
    case 28:
        launchOptions.generatedObjectTypeOverride = static_cast<GeneratedObjectTypeOverride>( stress.NextInt( 3 ) );
        break;
    case 29:
        ui.SetProfilerTimelineEnabled( stress.NextInt( 2 ) != 0 );
        ui.SetPerformanceHistogramEnabled( stress.NextInt( 2 ) != 0 );
        break;
    case 30:
        ui.SetRendererComboOpen( stress.NextInt( 2 ) != 0 );
        ui.SetWaterComboOpen( stress.NextInt( 2 ) != 0 );
        ui.SetSceneComboOpen( stress.NextInt( 2 ) != 0 );
        break;
    case 31:
        debug.isUITestPattern = stress.NextInt( 2 ) != 0;
        break;
    default:
        break;
    }
}


} // namespace


void GraphicsStressController::Configure( unsigned int seed, int actionsPerFrame, int sceneIntervalFrames,
                                          int memoryLogIntervalFrames )
{
    m_enabled = true;
    m_randomState = seed;
    m_actionsPerFrame = actionsPerFrame;
    m_sceneIntervalFrames = sceneIntervalFrames;
    m_memoryLogIntervalFrames = memoryLogIntervalFrames;
    m_descriptorBaseline = 0;
    m_descriptorResizeCount = 0;
    m_lastRecreationGeneration = 0;
    m_acknowledgedResizeCount = 0;
    m_textureChurnCount = 0;
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


bool GraphicsStressController::InDescriptorChurnQuietWindow() const
{
    return m_framesRun <= DESCRIPTOR_VERIFY_FRAME;
}


bool GraphicsStressController::ShouldCaptureDescriptorBaseline() const
{
    return m_framesRun == DESCRIPTOR_BASELINE_FRAME;
}


bool GraphicsStressController::ShouldIssueDescriptorResize() const
{
    return m_framesRun >= DESCRIPTOR_RESIZE_FIRST_FRAME && m_framesRun <= DESCRIPTOR_RESIZE_LAST_FRAME;
}


bool GraphicsStressController::ShouldVerifyDescriptorChurn() const
{
    return m_framesRun == DESCRIPTOR_VERIFY_FRAME;
}


void GraphicsStressController::CaptureDescriptorBaseline( unsigned int staticUsed, uint64_t recreationGeneration )
{
    m_descriptorBaseline = staticUsed;
    m_descriptorResizeCount = 0;
    m_lastRecreationGeneration = recreationGeneration;
    m_acknowledgedResizeCount = 0;
    m_textureChurnCount = 0;
}


void GraphicsStressController::ObserveRecreationGeneration( uint64_t recreationGeneration )
{

    if ( recreationGeneration > m_lastRecreationGeneration )
    {
        m_acknowledgedResizeCount += static_cast<int>( recreationGeneration - m_lastRecreationGeneration );
        m_lastRecreationGeneration = recreationGeneration;
    }
}


void GraphicsStressController::RecordDescriptorResize()
{
    ++m_descriptorResizeCount;
}


void GraphicsStressController::RecordTextureChurn()
{
    ++m_textureChurnCount;
}


bool GraphicsStressController::DescriptorChurnMatchesBaseline( unsigned int staticUsed ) const
{
    return m_acknowledgedResizeCount > 128 && m_textureChurnCount > 128 && staticUsed == m_descriptorBaseline;
}


unsigned int GraphicsStressController::DescriptorBaseline() const
{
    return m_descriptorBaseline;
}


int GraphicsStressController::DescriptorResizeCount() const
{
    return m_descriptorResizeCount;
}


int GraphicsStressController::AcknowledgedResizeCount() const
{
    return m_acknowledgedResizeCount;
}


int GraphicsStressController::TextureChurnCount() const
{
    return m_textureChurnCount;
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
    case UICinematicParam::SunAzimuth:
    case UICinematicParam::SunElevation:
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


// Lifetime: UI stress is a validation harness over synchronous owner borrows.
// It keeps only deterministic counters in DiagnosticsRuntime and retains no
// scene, UI, renderer, or input owner after the action batch returns.
SkullbonezCore::Core::SbResult
SkullbonezCore::Runtime::RunUIStressActions( RuntimeFrameHostView& host, RuntimeFrameInteractionView& interactionOwners,
                                             RuntimeFrameSceneView& sceneOwners, RuntimeRenderer& renderer,
                                             ReplayRuntime& replayRuntime, RunCameraMode replayRestoreCameraMode )
{
    DiagnosticsRuntime& diagnosticsRuntime = host.diagnosticsRuntime;
    Window* window = &host.window;
    RunTimerState& timers = sceneOwners.timers;
    SkullbonezCore::UI::InGameUI& ui = interactionOwners.operatorUi;
    SceneController& sceneController = sceneOwners.sceneController;
    CameraControlState& camera = interactionOwners.camera;
    SkullbonezCore::Core::EngineConfig& config = sceneOwners.config;
    SimulationSystem& simulation = sceneOwners.simulation;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    const RunLaunchOptions& launchOptions = sceneOwners.launchOptions;
    InputRouter& inputRouter = interactionOwners.inputRouter;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    UIStressState& stress = diagnosticsRuntime.UIStress();

    if ( !stress.enabled || !window )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    ++stress.framesRun;
    const double UINow = timers.simulationTimer.GetTotalTime();
    const int screenW = (std::max)( 1, window->ClientWidth() );
    const int screenH = (std::max)( 1, window->ClientHeight() );

    ui.SetVisible( true, UINow );
    ui.SetMinimized( false, UINow );

    ui.SetMouseOverride( true, StressHarness::NextInt( stress, screenW ), StressHarness::NextInt( stress, screenH ) );

    // This gate is a UI control-state crash sweep. Runtime rebuilds and world
    // debug toggles belong to render/physics validation, so they stay frozen here.
    const bool allowRuntimeChurn = StressHarness::AllowsRuntimeChurn();
    const int generatedObjectCapacity = SkullbonezCore::Core::ActiveSceneObjectCapacity( config );

    const auto executeSceneGeneratedControlAction = [&]( const SceneRuntimeGeneratedControlAction& action ) -> SkullbonezCore::Core::SbResult
    {

        if ( !action.status.ok )
        {

            // Lane R: resources remain intact; return before later stress churn
            // and let the input boundary report and end the run.
            return action.status;
        }

        if ( action.resetReplayTimeline )
        {
            const ReplaySceneTimelineResetInput
                reset = DescribeReplaySceneTimeline( sceneController, ui.SceneNavigation().overrides,
                                                     sceneController.State(),
                                                     SkullbonezCore::Core::ActiveSceneObjectCapacity( config ),
                                                     static_cast<uint32_t>( launchOptions.generatedObjectTypeOverride ) );

            replayRuntime.ResetSceneTimeline( reset, inputRouter, interaction, &sceneController.Scene().Cameras(),
                                              sceneController.Scene().Terrain().Get(), camera, replayRestoreCameraMode,
                                              attachedCamera.State().activeFollow, camera.director.grabbed );
        }

        if ( action.scheduleProfileReset )
        {
            PROFILE_SCHEDULE_RESET( host.profiler );
        }

        return SkullbonezCore::Core::SbResult::Success();
    };

    if ( stress.framesRun == 18 )
    {
        const int modelCount = 96 + StressHarness::NextInt( stress, 160 );

        if ( allowRuntimeChurn )
        {
            SceneGeneratedControlTransaction
                transaction = SceneGeneratedControlTransaction::ModelCount( modelCount,
                                                                            launchOptions.generatedObjectTypeOverride,
                                                                            generatedObjectCapacity );

            const SkullbonezCore::Core::SbResult actionResult = executeSceneGeneratedControlAction( transaction
                                                                                                        .Execute( config, sceneController, ui.SceneNavigation().overrides, camera, simulation, runtimeTools,
                                                                                                                  &renderer.RenderFrame() )
                                                                                                        .action );

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
            SceneGeneratedControlTransaction
                transaction = SceneGeneratedControlTransaction::SolverCounts( balls, boxes,
                                                                              launchOptions.generatedObjectTypeOverride,
                                                                              generatedObjectCapacity );

            const SkullbonezCore::Core::SbResult actionResult = executeSceneGeneratedControlAction( transaction
                                                                                                        .Execute( config, sceneController, ui.SceneNavigation().overrides, camera, simulation, runtimeTools,
                                                                                                                  &renderer.RenderFrame() )
                                                                                                        .action );

            if ( !actionResult.ok )
            {
                return actionResult;
            }
        }
    }

    const int actionCount = StressHarness::ActionCount( stress );

    for ( int i = 0; i < actionCount; ++i )
    {
        ApplyUIStressAction( ui, sceneOwners, renderer, replayRuntime, stress, allowRuntimeChurn );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


void RuntimeValidationHarness::ExecuteGraphicsStressFrame( RuntimeFrameHostView& host, RuntimeFrameInteractionView& interactionOwners, RuntimeFrameSceneView& sceneOwners,
                                                           RuntimeFramePresentationView& presentationOwners, ReplayRuntime& replayRuntime,
                                                           const Rendering::Dx12Diagnostics& renderDiagnostics, bool legacyDevelopmentUiActive )
{
    GraphicsStressController& stress = m_graphicsStress;
    Window* window = &host.window;
    SkullbonezCore::Core::EngineConfig& config = sceneOwners.config;
    RunLaunchOptions& launchOptions = sceneOwners.launchOptions;
    const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender = presentationOwners.renderDefaults
                                                                                    .CinematicBaseline();

    const RunStartupState& startup = sceneOwners.startup;
    DiagnosticsRuntime& diagnosticsRuntime = host.diagnosticsRuntime;
    RunTimerState& timers = sceneOwners.timers;
    Assets::AssetSystem& assets = host.assets;
    Threading::WorkerPool& workerPool = host.workerPool;
    InputRouter& inputRouter = interactionOwners.inputRouter;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    CameraControlState& camera = interactionOwners.camera;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    UI::InGameUI& ui = interactionOwners.operatorUi;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    RuntimeRenderer& renderer = presentationOwners.renderer;
    SceneController& sceneController = sceneOwners.sceneController;

    // Concept: graphics stress is a deterministic fuzzer over scene loading,
    // cinematic controls, render-path toggles, and heavy generated-scene resets.
    // Keep every mutation reproducible from the launch seed so a crash line in
    // latest_stdout.txt can be replayed exactly under cdb.

    if ( !stress.IsEnabled() || !window )
    {
        return;
    }

    stress.BeginFrame();

    if ( stress.FramesRun() == 1 )
    {
        printf( "[graphics-stress] Running seed=%u actions=%d scene_interval_frames=%d\n", stress.RandomState(),
                stress.ActionsPerFrame(), stress.SceneIntervalFrames() );

        fflush( stdout );
    }

    auto executeSceneLoadRequest = [&]( const SceneLoadRequest& request ) -> bool
    {

        if ( !request.accepted )
        {
            return false;
        }

        SceneLoadTransaction sceneLoad;
        sceneLoad.CaptureSubmittedState( camera, CaptureSceneLoadNavigationState( ui.SceneNavigation() ),
                                         sceneOwners.overlays.PresentationSnapshot(), renderer.RendererName(),
                                         timers.simulationTimer.GetTotalTime() );

        const bool loaded = sceneLoad
                                .Load( sceneController, request, config, launchOptions, defaultCinematicRender, startup,
                                       assets, workerPool, diagnosticsRuntime, &renderer.RenderFrame(),
                                       &renderer.RenderResources(), renderer )
                                .ok;

        if ( !legacyDevelopmentUiActive )
        {

            // Invariant: scene churn may update diagnostics and runtime state,
            // but it cannot reactivate the mutually exclusive Legacy surface.
            sceneLoad.PreserveInactiveDevelopmentUi();
        }

        sceneLoad.ApplyRuntimeReactions( launchOptions, timers, sceneOwners.overlays, sceneController, inputRouter,
                                         interaction, camera, attachedCamera, runtimeTools, replayRuntime );

        sceneLoad.ApplyPresentationOutputs( *window, ui, presentationOwners.validationHarness, launchOptions,
                                            &renderer.RenderDevice(), renderer.VsyncEnabled(), sceneController );

        return loaded;
    };

    const SkullbonezCore::Rendering::RenderMemoryStats preActionRenderStats = renderDiagnostics.GetRenderMemoryStats();

    if ( stress.ShouldCaptureDescriptorBaseline() )
    {
        stress.CaptureDescriptorBaseline( preActionRenderStats.srvStaticDescriptorsUsed,
                                          preActionRenderStats.recreationGeneration );

        printf( "[graphics-stress-descriptor-churn] baseline=%u frame=%d\n", stress.DescriptorBaseline(),
                stress.FramesRun() );

        fflush( stdout );
    }

    if ( stress.ShouldIssueDescriptorResize() )
    {

        // A request counts only after diagnostics observes the backend's
        // publication generation advance on a later frame.
        stress.ObserveRecreationGeneration( preActionRenderStats.recreationGeneration );
        const int edge = ( stress.DescriptorResizeCount() & 1 ) == 0 ? 1280 : 1281;

        if ( !SetWindowPos( window->NativeWindowHandle(), nullptr, 0, 0, edge, 720, SWP_NOMOVE | SWP_NOZORDER ) )
        {
            SB_FATAL( "GraphicsStress", "Descriptor churn SetWindowPos failed. resize=%d", stress.DescriptorResizeCount() );
        }

        stress.RecordDescriptorResize();

        const uint8_t churnPixel[4] = { 255u, 0u, 255u, 255u };

        const uint32_t churnTexture = renderer.RenderTextures().CreateTexture2D( churnPixel, 1, 1, 4,
                                                                                 Rendering::TextureMipPolicy::SingleLevel,
                                                                                 Rendering::TextureFilterPolicy::Nearest );

        if ( churnTexture == 0 )
        {
            SB_FATAL( "GraphicsStress", "Descriptor churn texture creation failed. request=%d",
                      stress.DescriptorResizeCount() );
        }

        renderer.RenderTextures().DeleteTexture( churnTexture );
        stress.RecordTextureChurn();
    }

    if ( stress.ShouldVerifyDescriptorChurn() )
    {
        stress.ObserveRecreationGeneration( preActionRenderStats.recreationGeneration );

        if ( !stress.DescriptorChurnMatchesBaseline( preActionRenderStats.srvStaticDescriptorsUsed ) )
        {
            SB_FATAL( "GraphicsStress",
                      "Static descriptor churn did not return to baseline. baseline=%u current=%u requested=%d "
                      "acknowledged=%d textures=%d",
                      stress.DescriptorBaseline(), preActionRenderStats.srvStaticDescriptorsUsed,
                      stress.DescriptorResizeCount(), stress.AcknowledgedResizeCount(), stress.TextureChurnCount() );
        }

        printf( "[graphics-stress-descriptor-churn] PASS baseline=%u current=%u requested=%d acknowledged=%d "
                "textures=%d high_water=%u\n",
                stress.DescriptorBaseline(), preActionRenderStats.srvStaticDescriptorsUsed, stress.DescriptorResizeCount(),
                stress.AcknowledgedResizeCount(), stress.TextureChurnCount(),
                preActionRenderStats.srvStaticDescriptorsHighWater );

        fflush( stdout );
    }

    if ( !stress.InDescriptorChurnQuietWindow() && stress.SceneLoadDue() )
    {

        // Hazard: suite order is the durable test contract. Browser fallback is
        // useful for manual app launches, but automated repros must prefer the
        // checked-in graphics_stress.suite.json scene queue.
        SceneLoadRequest request = SceneLoadRequest::None();
        int selectedSceneIndex = -1;
        const char* selectedSceneSource = "none";

        if ( sceneController.QueueSize() > 0 )
        {
            selectedSceneIndex = stress.NextInt( sceneController.QueueSize() );
            selectedSceneSource = "queue";
            request = SceneLoadRequest::Load( selectedSceneIndex, true, true, stress.NextInt( 2 ) != 0, true );
        }
        else if ( !ui.SceneNavigation().browser.paths.empty() )
        {
            selectedSceneIndex = stress.NextInt( static_cast<int>( ui.SceneNavigation().browser.paths.size() ) );
            selectedSceneSource = "browser";
            request = LoadSceneFromBrowserIndex( ui.SceneNavigation(), selectedSceneIndex, sceneController.Runtime() );
        }

        if ( executeSceneLoadRequest( request ) )
        {
            stress.RecordSceneLoad();
            printf( "[graphics-stress] scene_load=%d frame=%d source=%s selected_index=%d action_index=%d\n",
                    stress.SceneLoadsRequested(), stress.FramesRun(), selectedSceneSource, selectedSceneIndex,
                    request.index );

            fflush( stdout );
        }
        else
        {
            printf( "[graphics-stress] scene_load_skipped frame=%d source=%s selected_index=%d\n", stress.FramesRun(),
                    selectedSceneSource, selectedSceneIndex );

            fflush( stdout );
        }
    }

    if ( legacyDevelopmentUiActive )
    {
        ui.SetVisible( true, timers.simulationTimer.GetTotalTime() );
        ui.SetMinimized( false, timers.simulationTimer.GetTotalTime() );
    }

    sceneController.EnterInteractiveRun();

    const int actionCount = stress.InDescriptorChurnQuietWindow() ? 0 : stress.ActionCount();

    // Invariant: random values stay inside the same broad ranges exposed by the
    // runtime UI. The stress test should crash bad DX12 lifetime/state tracking,
    // not manufacture impossible physics or render data.

    for ( int i = 0; i < actionCount; ++i )
    {
        ApplyGraphicsStressAction( assets, interactionOwners, sceneOwners, presentationOwners.renderDefaults, renderer,
                                   replayRuntime, stress );
    }

    if ( stress.ShouldPrintFrameSummary() )
    {
        printf( "[graphics-stress] frame=%d scene_loads=%d rng=%u\n", stress.FramesRun(), stress.SceneLoadsRequested(),
                stress.RandomState() );

        fflush( stdout );
    }

    if ( stress.ShouldLogMemory() )
    {

        // Why: long stress runs need memory attribution before shutdown. If the
        // process is killed after a climb, this stdout line survives with the
        // same seed/frame/scene-load position as the repro log.
        const SkullbonezCore::Core::MainMemoryStats& memoryStats = diagnosticsRuntime.RefreshMainMemoryStats( replayRuntime.CollectMemoryStats(),
                                                                                                              CollectSceneMemoryStats( SceneMemoryDiagnosticsView { sceneController.Scene().Entities(),
                                                                                                                                                                    sceneController.Scene().CollectGameplayMemoryBytes(),
                                                                                                                                                                    sceneController.Scene().CollectGameplayDebugMemoryBytes(),
                                                                                                                                                                    sceneController.Scene().Physics(),
                                                                                                                                                                    sceneController.Scene().RenderInstances() } ),
                                                                                                              timers.simulationTimer.GetTotalTime(), true );

        const SkullbonezCore::Rendering::RenderMemoryStats renderStats = renderDiagnostics.GetRenderMemoryStats();
        printf( "[graphics-stress-memory] frame=%d scene_loads=%d task_manager_bytes=%llu "
                "working_set_bytes=%llu private_working_set_bytes=%llu private_commit_bytes=%llu pagefile_bytes=%llu "
                "tracked_engine_bytes=%llu replay_bytes=%llu game_object_bytes=%llu unattributed_process_bytes=%llu "
                "render_available=%d render_adapter_available=%d dxgi_local_usage_bytes=%llu "
                "dxgi_nonlocal_usage_bytes=%llu dxgi_local_budget_bytes=%llu dxgi_nonlocal_budget_bytes=%llu "
                "upload_capacity_bytes=%llu upload_used_bytes=%llu upload_peak_bytes=%llu timer_readback_bytes=%llu "
                "upload_constants_peak_bytes=%llu upload_dynamic_peak_bytes=%llu upload_instances_peak_bytes=%llu "
                "upload_textures_peak_bytes=%llu upload_overlay_peak_bytes=%llu upload_flushes=%llu upload_drops=%llu "
                "textures=%zu texture_capacity=%zu psos=%zu pso_hits=%llu pso_misses=%llu "
                "pso_precompiled=%llu graph_transients=%zu graph_transient_capacity=%zu "
                "rtv_used=%u rtv_capacity=%u dsv_used=%u dsv_capacity=%u srv_static_used=%u srv_static_capacity=%u "
                "srv_static_high_water=%u "
                "srv_transient_used=%u srv_transient_capacity=%u srv_transient_peak=%u\n",
                stress.FramesRun(), stress.SceneLoadsRequested(),
                static_cast<unsigned long long>( memoryStats.process.taskManagerBytes ),
                static_cast<unsigned long long>( memoryStats.process.workingSetBytes ),
                static_cast<unsigned long long>( memoryStats.process.privateWorkingSetBytes ),
                static_cast<unsigned long long>( memoryStats.process.privateCommitBytes ),
                static_cast<unsigned long long>( memoryStats.process.pagefileUsageBytes ),
                static_cast<unsigned long long>( memoryStats.trackedEngineBytes ),
                static_cast<unsigned long long>( memoryStats.replay.totalBytes ),
                static_cast<unsigned long long>( memoryStats.gameObjects.totalBytes ),
                static_cast<unsigned long long>( memoryStats.unattributedProcessBytes ), renderStats.available ? 1 : 0,
                renderStats.adapterMemoryAvailable ? 1 : 0,
                static_cast<unsigned long long>( renderStats.localCurrentUsageBytes ),
                static_cast<unsigned long long>( renderStats.nonLocalCurrentUsageBytes ),
                static_cast<unsigned long long>( renderStats.localBudgetBytes ),
                static_cast<unsigned long long>( renderStats.nonLocalBudgetBytes ),
                static_cast<unsigned long long>( renderStats.uploadCapacityBytes ),
                static_cast<unsigned long long>( renderStats.uploadUsedBytes ),
                static_cast<unsigned long long>( renderStats.uploadPeakBytes ),
                static_cast<unsigned long long>( renderStats.timerReadbackBytes ),
                static_cast<unsigned long long>( renderStats.uploadCategoryPeakBytes[static_cast<std::size_t>( SkullbonezCore::Rendering::RenderUploadCategory::Constants )] ),
                static_cast<unsigned long long>( renderStats.uploadCategoryPeakBytes[static_cast<std::size_t>( SkullbonezCore::Rendering::RenderUploadCategory::DynamicVertex )] ),
                static_cast<unsigned long long>( renderStats.uploadCategoryPeakBytes[static_cast<std::size_t>( SkullbonezCore::Rendering::RenderUploadCategory::InstanceData )] ),
                static_cast<unsigned long long>( renderStats.uploadCategoryPeakBytes[static_cast<std::size_t>( SkullbonezCore::Rendering::RenderUploadCategory::TextureRows )] ),
                static_cast<unsigned long long>( renderStats.uploadCategoryPeakBytes[static_cast<std::size_t>( SkullbonezCore::Rendering::RenderUploadCategory::RetainedGeometry )] ),
                static_cast<unsigned long long>( renderStats.uploadFlushCount ),
                static_cast<unsigned long long>( renderStats.uploadDropCount ), renderStats.textureRegistryCount,
                renderStats.textureRegistryCapacity, renderStats.psoCacheCount,
                static_cast<unsigned long long>( renderStats.psoCacheHitCount ),
                static_cast<unsigned long long>( renderStats.psoCacheMissCount ),
                static_cast<unsigned long long>( renderStats.precompiledPsoCount ), renderStats.graphTransientCount,
                renderStats.graphTransientCapacity, renderStats.rtvDescriptorsUsed, renderStats.rtvDescriptorsCapacity,
                renderStats.dsvDescriptorsUsed, renderStats.dsvDescriptorsCapacity, renderStats.srvStaticDescriptorsUsed,
                renderStats.srvStaticDescriptorsCapacity, renderStats.srvStaticDescriptorsHighWater,
                renderStats.srvTransientDescriptorsUsedThisFrame, renderStats.srvTransientDescriptorsCapacityPerFrame,
                renderStats.srvTransientDescriptorsPeakThisRun );

        fflush( stdout );
    }
}
