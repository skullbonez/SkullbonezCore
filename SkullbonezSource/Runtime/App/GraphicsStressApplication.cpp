/*
File: SkullbonezSource/Runtime/App/GraphicsStressApplication.cpp
Purpose:
  Runs stress and automation paths for validation-oriented launches.

Summary:
  App coordinates deterministic Capture policy through focused operations.
  Each operation receives concrete owners for one call and retains none.

Invariants:
  - Graphics stress intentionally flips render/runtime state so backend state
    tracking is exercised under deterministic random input.
  - A generated-scene drain failure ends the stress action before later churn.
  - Stress helpers never retain a referenced runtime owner.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "GraphicsStressApplication.h"
#include "../Capture/GraphicsStressController.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../Scene/AttachedCameraController.h"

#include "../Input/InputRouter.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Diagnostics/SceneMemoryDiagnostics.h"
#include "../Render/RuntimeRenderHost.h"
#include "../Render/RuntimeRenderer.h"
#include "../Render/RenderDefaultsStore.h"
#include "../Camera/CameraControlState.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../Startup/RunLaunchOptions.h"
#include "../Startup/RunStartupState.h"
#include "../RuntimeFrameViews.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Scene/SceneController.h"
#include "../Scene/SceneLoadTransaction.h"
#include "../Scene/SceneLoadRequest.h"
#include "../Scene/SceneGeneratedControlTransaction.h"
#include "../Scene/SceneCinematicPolicy.h"
#include "../Tools/RuntimeTools.h"
#include "../Startup/Window.h"
#include "../../Assets/AssetSystem.h"
#include "../Simulation/SimulationSystem.h"
#include "../../Rendering/DX12/RenderDeviceDX12.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Scene/AuthoredScene.h"
#include "../../Core/WorkerPool.h"
#include "../UI/GameUI/UI.h"
#include "../../Core/Profiler.h"

#include <cstdio>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Environment::WorldOverrideChange;
using SkullbonezCore::UI::InGameUITab;
using SkullbonezCore::UI::UICinematicFeature;
using SkullbonezCore::UI::UICinematicParam;

namespace
{
unsigned int NextStressRandom( unsigned int& state )
{
    if ( state == 0 )
    {
        state = 0xC11E2026u;
    }

    state = state * 1664525u + 1013904223u;
    return state;
}


} // namespace

void SkullbonezCore::Runtime::ApplyGraphicsStressCinematicAction( int action, GraphicsStressController& stress,
                                                                  RunLaunchOptions& launchOptions,
                                                                  SkullbonezCore::Core::EngineConfig& config,
                                                                  SceneController& sceneController )
{
    SceneSessionState& scene = sceneController.State();

    switch ( action )
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
        const UICinematicFeature feature = static_cast<UICinematicFeature>(
            stress.NextInt( static_cast<int>( UICinematicFeature::Count ) ) );

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
        const UICinematicParam param = static_cast<UICinematicParam>(
            stress.NextInt( static_cast<int>( UICinematicParam::Count ) ) );

        ApplyCinematicUIParam( cinematic, scene, param, stress.RandomCinematicParamValue( param ) );
        break;
    }
    default:
        break;
    }
}


void SkullbonezCore::Runtime::ApplyGraphicsStressSceneBrowserAction(
    GraphicsStressController& stress, const SkullbonezCore::Assets::AssetSystem& assets, RunLaunchOptions& launchOptions,
    SkullbonezCore::Core::EngineConfig& config, SceneController& sceneController, SkullbonezCore::UI::InGameUI& ui,
    const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender )
{
    SceneSessionState& scene = sceneController.State();
    const int browserCount = static_cast<int>( ui.SceneNavigation().browser.paths.size() );
    const int browserIndex = ( browserCount > 0 && stress.NextInt( 5 ) != 0 ) ? stress.NextInt( browserCount ) : -1;
    (void)sceneController.ApplyCinematicBrowserStyle( launchOptions, ui.SceneNavigation().browser, assets,
                                                      ActiveSceneCinematicConfig( scene, config ), defaultCinematicRender,
                                                      browserIndex );
}


void SkullbonezCore::Runtime::ApplyGraphicsStressRendererAction( int action, RuntimeRenderer& renderer )
{
    if ( action == 4 )
    {
        renderer.SetVsyncEnabled( !renderer.VsyncEnabled() );
    }
    else if ( action == 5 )
    {
        renderer.SetPipelineSyncEnabled( !renderer.PipelineSyncEnabled() );
    }
}


void SkullbonezCore::Runtime::ApplyGraphicsStressPresentationOverlayAction( int action, GraphicsStressController& stress,
                                                                            RuntimeOverlayDiagnostics& overlays,
                                                                            const RuntimeFrameMetricsSnapshot& timers )
{
    RuntimeOverlayPresentationEdit presentationEdit = overlays.EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();

    switch ( action )
    {
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
            debug.frozenWaterTime = static_cast<float>( timers.sceneElapsedSeconds );
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
    default:
        break;
    }
}

void SkullbonezCore::Runtime::ApplyGraphicsStressTimeScaleAction( GraphicsStressController& stress,
                                                                  SceneController& sceneController,
                                                                  SkullbonezCore::UI::InGameUI& ui,
                                                                  SimulationSystem& simulation )
{
    SceneSessionState& scene = sceneController.State();
    const float timeScale = stress.NextFloat( 0.05f, 4.0f );
    ui.SceneNavigation().overrides.timeScaleOverride = timeScale;
    scene.timeScale = timeScale;
    simulation.Reset();
}


GraphicsStressRuntimeActionResult SkullbonezCore::Runtime::ApplyGraphicsStressWorldAction( GraphicsStressController& stress,
                                                                                           SceneController& sceneController )
{
    SkullbonezCore::Environment::WorldEnvironment& world = sceneController.Scene().Environment();
    const WorldOverrideChange change = world.ApplyOverride( -stress.NextFloat( 0.0f, 80.0f ),
                                                            stress.NextFloat( -80.0f, 160.0f ),
                                                            stress.NextFloat( 0.0f, 5.0f ) );
    return { change.previousGravity,
             change.previousFluidHeight,
             change.previousFluidDensity,
             change.gravity,
             change.fluidHeight,
             change.fluidDensity,
             true };
}


void SkullbonezCore::Runtime::ApplyGraphicsStressGeneratedSceneAction( GraphicsStressController& stress,
                                                                       RunLaunchOptions& launchOptions )
{
    launchOptions.generatedObjectTypeOverride = static_cast<GeneratedObjectTypeOverride>( stress.NextInt( 3 ) );
}


void SkullbonezCore::Runtime::ApplyGraphicsStressTornadoAction( int action, GraphicsStressController& stress,
                                                                SceneController& sceneController )
{
    if ( action == 19 )
    {
        SkullbonezCore::Gameplay::TornadoFieldConfig tornadoField = sceneController.Scene().Tornado().GetFieldConfig();
        tornadoField.enabled = stress.NextInt( 2 ) != 0;
        tornadoField.visualizeVelocityField = stress.NextInt( 2 ) != 0;
        sceneController.Scene().Tornado().SetVisualEnabled( stress.NextInt( 2 ) != 0 );
        sceneController.Scene().Tornado().SetFieldConfig( tornadoField );
    }
    else if ( action == 20 )
    {
        SkullbonezCore::Gameplay::TornadoVisualSettings tornadoVisual = sceneController.Scene().Tornado().VisualSettings();
        tornadoVisual.shellAlpha = stress.NextFloat( 0.02f, 0.40f );
        tornadoVisual.dustAlpha = stress.NextFloat( 0.02f, 0.55f );
        tornadoVisual.ribbonWidth = stress.NextFloat( 1.0f, 12.0f );
        tornadoVisual.ribbonCount = 1 + stress.NextInt( 10 );
        tornadoVisual.particleCount = 16 + stress.NextInt( 240 );
        sceneController.Scene().Tornado().SetVisualSettings( tornadoVisual );
    }
}


void SkullbonezCore::Runtime::ApplyGraphicsStressOperatorUiAction( int action, GraphicsStressController& stress,
                                                                   SkullbonezCore::UI::InGameUI& ui )
{
    switch ( action )
    {
    case 17:
        ui.SceneNavigation().overrides.modelCountOverride = 32 + stress.NextInt( 512 );
        break;
    case 21:
        ui.SetActiveTab( static_cast<InGameUITab>( stress.NextInt( static_cast<int>( InGameUITab::Count ) ) ) );
        ui.SetScrollY( stress.NextFloat( 0.0f, 1200.0f ) );
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
    default:
        break;
    }
}


void SkullbonezCore::Runtime::ApplyGraphicsStressScenePhysicsAction( int action, SceneController& sceneController,
                                                                     SimulationSystem& simulation )
{
    if ( action == 22 )
    {
        SceneSessionState& scene = sceneController.State();
        scene.isFixedStep = !scene.isFixedStep;
        simulation.Reset();
    }
    else if ( action == 23 )
    {
        sceneController.Scene().Physics().SetSleepEnabled( !sceneController.Scene().Physics().IsSleepEnabled() );
    }
}


void SkullbonezCore::Runtime::ApplyGraphicsStressRuntimeToolAction( RuntimeTools& runtimeTools )
{
    runtimeTools.Laser().Update( 0.0f );
}


void SkullbonezCore::Runtime::ApplyGraphicsStressCameraAction( GraphicsStressController& stress, CameraControlState& camera )
{
    camera.trackHeight = stress.NextFloat( 8.0f, 500.0f );
}


void SkullbonezCore::Runtime::ApplyGraphicsStressRuntimeOverlayAction( int action, GraphicsStressController& stress,
                                                                       RuntimeOverlayDiagnostics& overlays )
{
    RuntimeOverlayPresentationEdit presentationEdit = overlays.EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();

    switch ( action )
    {
    case 24:
        debug.isTopTextHidden = !debug.isTopTextHidden;
        break;
    case 25:
        debug.overlayMode = static_cast<OverlayMode>( stress.NextInt( 6 ) );
        break;
    case 31:
        debug.isUITestPattern = stress.NextInt( 2 ) != 0;
        break;
    default:
        break;
    }
}


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

bool SkullbonezCore::Runtime::PrepareGraphicsStressChurn( GraphicsStressController& stress, Window& window,
                                                          RuntimeRenderer& renderer,
                                                          const Rendering::Dx12Diagnostics& renderDiagnostics )
{
    if ( !stress.IsEnabled() )
    {
        return false;
    }

    stress.BeginFrame();

    if ( stress.FramesRun() == 1 )
    {
        printf( "[graphics-stress] Running seed=%u actions=%d scene_interval_frames=%d\n", stress.RandomState(),
                stress.ActionsPerFrame(), stress.SceneIntervalFrames() );

        fflush( stdout );
    }

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
        stress.ObserveRecreationGeneration( preActionRenderStats.recreationGeneration );
        const int edge = ( stress.DescriptorResizeCount() & 1 ) == 0 ? 1280 : 1281;

        if ( !SetWindowPos( window.NativeWindowHandle(), nullptr, 0, 0, edge, 720, SWP_NOMOVE | SWP_NOZORDER ) )
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

    return true;
}

GraphicsStressSceneLoadPlan SkullbonezCore::Runtime::PlanGraphicsStressSceneLoad( GraphicsStressController& stress,
                                                                                  SceneController& sceneController,
                                                                                  SkullbonezCore::UI::InGameUI& ui )
{
    GraphicsStressSceneLoadPlan plan;

    if ( stress.InDescriptorChurnQuietWindow() || !stress.SceneLoadDue() )
    {
        return plan;
    }

    plan.scheduled = true;

    if ( sceneController.QueueSize() > 0 )
    {
        plan.selectedSceneIndex = stress.NextInt( sceneController.QueueSize() );
        plan.selectedSceneSource = "queue";
        plan.request = SceneLoadRequest::Load( plan.selectedSceneIndex, true, true, stress.NextInt( 2 ) != 0, true );
    }
    else if ( !ui.SceneNavigation().browser.paths.empty() )
    {
        plan.selectedSceneIndex = stress.NextInt( static_cast<int>( ui.SceneNavigation().browser.paths.size() ) );
        plan.selectedSceneSource = "browser";
        plan.request = LoadSceneFromBrowserIndex( ui.SceneNavigation(), plan.selectedSceneIndex, sceneController );
    }

    return plan;
}

void SkullbonezCore::Runtime::FinishGraphicsStressFrame( GraphicsStressController& stress,
                                                         DiagnosticsRuntime& diagnosticsRuntime,
                                                         const RuntimeFrameMetricsSnapshot& timers,
                                                         SceneController& sceneController,
                                                         const SkullbonezCore::Core::MainMemoryReplayStats& replayMemory,
                                                         const Rendering::Dx12Diagnostics& renderDiagnostics )
{
    if ( stress.ShouldPrintFrameSummary() )
    {
        printf( "[graphics-stress] frame=%d scene_loads=%d rng=%u\n", stress.FramesRun(), stress.SceneLoadsRequested(),
                stress.RandomState() );

        fflush( stdout );
    }

    if ( !stress.ShouldLogMemory() )
    {
        return;
    }

    const SkullbonezCore::Core::MainMemoryStats&
        memoryStats = diagnosticsRuntime
                          .RefreshMainMemoryStats( replayMemory,
                                                   CollectSceneMemoryStats(
                                                       SceneMemoryDiagnosticsView { sceneController.Scene()
                                                                                        .Entities()
                                                                                        .CapacityBytes(),
                                                                                    sceneController.Scene()
                                                                                        .CollectGameplayMemoryBytes(),
                                                                                    sceneController.Scene()
                                                                                        .CollectGameplayDebugMemoryBytes(),
                                                                                    sceneController.Scene().Physics(),
                                                                                    sceneController.Scene()
                                                                                        .RenderInstances() } ),
                                                   timers.simulationTotalSeconds, true );

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
            static_cast<unsigned long long>( renderStats.uploadCategoryPeakBytes[static_cast<std::size_t>(
                SkullbonezCore::Rendering::RenderUploadCategory::Constants )] ),
            static_cast<unsigned long long>( renderStats.uploadCategoryPeakBytes[static_cast<std::size_t>(
                SkullbonezCore::Rendering::RenderUploadCategory::DynamicVertex )] ),
            static_cast<unsigned long long>( renderStats.uploadCategoryPeakBytes[static_cast<std::size_t>(
                SkullbonezCore::Rendering::RenderUploadCategory::InstanceData )] ),
            static_cast<unsigned long long>( renderStats.uploadCategoryPeakBytes[static_cast<std::size_t>(
                SkullbonezCore::Rendering::RenderUploadCategory::TextureRows )] ),
            static_cast<unsigned long long>( renderStats.uploadCategoryPeakBytes[static_cast<std::size_t>(
                SkullbonezCore::Rendering::RenderUploadCategory::RetainedGeometry )] ),
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
