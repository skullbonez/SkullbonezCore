/*
File: SkullbonezSource/Runtime/RuntimeTuning.cpp
Purpose:
  Owns UI-driven runtime tuning for cinematic rendering, ordinary rendering,
  contact-audio presentation, tornado physics settings, and worker-thread overrides.

Mental model:
  Runtime input decides when a UI command is accepted. This file decides how
  accepted values clamp or delegate to bounded owner APIs, mutate config, and
  persist as scene overrides where relevant.

Glossary:
  Cinematic config: HDR/post-processing and style settings for the active look.
  Ordinary render config: Non-cinematic renderer settings saved in engine.cfg.
  Override mask: Bitset recording which UI-touched scene values should persist.
  Run camera command: One-frame Run-tab packet that requests an operator camera mode.
  Sound command: One-frame UI packet that edits contact-audio presentation state.
  Tornado command: One-frame Physics-tab packet that edits live vortex settings.
  Worker override: Runtime request for the worker-pool thread count.

Invariants:
  - Render and cinematic UI values are clamped before they mutate live config.
  - Scene override masks must be updated with the value they describe.
  - Sound commands delegate value limits to ContactAudioService setters.
  - Tornado commands sync runtime settings back to physics after field edits.

Related:
  - SkullbonezSource/Runtime/RuntimeTuning.h
  - SkullbonezSource/Runtime/RunInput.cpp
*/
#include "RuntimeTuning.h"

#include "../Core/WorkerPool.h"
#include "../GameObjects/GameModelCollection.h"
#include "../Physics/SimulationSystem.h"
#include "../Rendering/IRenderDeviceLifecycle.h"
#include "../UI/UILayout.h"
#include "../World/WorldEnvironment.h"
#include "Replay/ReplayRuntime.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
namespace
{
constexpr const char* CONTACT_AUDIO_MATERIAL_MAP_PATH = "SkullbonezData/audio/contact_audio.materials.json";

ContactAudioFlashMode NextContactAudioFlashMode( ContactAudioFlashMode mode )
{
    constexpr int MODE_COUNT = static_cast<int>( ContactAudioFlashMode::Count );
    const int rawMode = static_cast<int>( mode );
    if ( rawMode < 0 || rawMode >= MODE_COUNT )
    {
        return ContactAudioFlashMode::Emitted;
    }
    return static_cast<ContactAudioFlashMode>( ( rawMode + 1 ) % MODE_COUNT );
}

bool EnsureContactAudioReady( SoundUICommandContext context )
{
    if ( context.contactAudioDisabledByLaunch )
    {
        return false;
    }
    return context.contactAudio.IsAvailable() ||
           ( context.contactAudio.Initialize() &&
             context.contactAudio.LoadContactAudioMap( CONTACT_AUDIO_MATERIAL_MAP_PATH ) );
}

void ApplyTornadoFieldValue( RunRuntimeSettings& runtimeSettings,
                             bool hasTornadoSystem,
                             float Physics::TornadoFieldConfig::* field,
                             float value )
{
    if ( hasTornadoSystem )
    {
        for ( Physics::TornadoVortexConfig& vortex : runtimeSettings.tornadoSystem.vortices )
        {
            vortex.field.*field = value;
        }
    }
    else
    {
        runtimeSettings.tornadoField.*field = value;
    }
}
} // namespace

uint64_t CinematicOverrideMaskForUIParam( UICinematicParam param )
{
    switch ( param )
    {
    case UICinematicParam::Exposure:
        return SCENE_CINE_EXPOSURE;
    case UICinematicParam::Gamma:
        return SCENE_CINE_GAMMA;
    case UICinematicParam::SkyMode:
    case UICinematicParam::TerrainMode:
    case UICinematicParam::ObjectStyle:
    case UICinematicParam::WaterMode:
        return SCENE_CINE_STYLE_MODES;
    case UICinematicParam::StyleSaturation:
    case UICinematicParam::StyleContrast:
    case UICinematicParam::StyleVignette:
        return SCENE_CINE_STYLE_GRADE;
    case UICinematicParam::SunX:
        return SCENE_CINE_SUN_SCREEN_X;
    case UICinematicParam::SunY:
        return SCENE_CINE_SUN_SCREEN_Y;
    case UICinematicParam::SunBrightness:
        return SCENE_CINE_SUN_INTENSITY;
    case UICinematicParam::SunRed:
        return SCENE_CINE_SUN_COLOR_R;
    case UICinematicParam::SunGreen:
        return SCENE_CINE_SUN_COLOR_G;
    case UICinematicParam::SunBlue:
        return SCENE_CINE_SUN_COLOR_B;
    case UICinematicParam::SkyGlow:
        return SCENE_CINE_SKY_GLOW_STRENGTH;
    case UICinematicParam::HorizonRed:
        return SCENE_CINE_SKY_HORIZON_R;
    case UICinematicParam::HorizonGreen:
        return SCENE_CINE_SKY_HORIZON_G;
    case UICinematicParam::HorizonBlue:
        return SCENE_CINE_SKY_HORIZON_B;
    case UICinematicParam::ZenithRed:
        return SCENE_CINE_SKY_ZENITH_R;
    case UICinematicParam::ZenithGreen:
        return SCENE_CINE_SKY_ZENITH_G;
    case UICinematicParam::ZenithBlue:
        return SCENE_CINE_SKY_ZENITH_B;
    case UICinematicParam::CloudCoverage:
        return SCENE_CINE_CLOUD_COVERAGE;
    case UICinematicParam::CloudSoftness:
        return SCENE_CINE_CLOUD_SOFTNESS;
    case UICinematicParam::CloudScale:
        return SCENE_CINE_CLOUD_SCALE;
    case UICinematicParam::CloudIntensity:
        return SCENE_CINE_CLOUD_INTENSITY;
    case UICinematicParam::ShaftStrength:
        return SCENE_CINE_SUN_SHAFT_STRENGTH;
    case UICinematicParam::ShaftFalloff:
        return SCENE_CINE_SUN_SHAFT_FALLOFF;
    case UICinematicParam::VolumetricStrength:
        return SCENE_CINE_VOLUMETRIC_STRENGTH;
    case UICinematicParam::VolumetricDensity:
        return SCENE_CINE_VOLUMETRIC_DENSITY;
    case UICinematicParam::VolumetricDecay:
        return SCENE_CINE_VOLUMETRIC_DECAY;
    case UICinematicParam::BloomThreshold:
        return SCENE_CINE_BLOOM_THRESHOLD;
    case UICinematicParam::BloomKnee:
        return SCENE_CINE_BLOOM_KNEE;
    case UICinematicParam::BloomStrength:
        return SCENE_CINE_BLOOM_STRENGTH;
    case UICinematicParam::BloomRadius:
        return SCENE_CINE_BLOOM_RADIUS;
    case UICinematicParam::TerrainRelief:
        return SCENE_CINE_TERRAIN_RELIEF;
    case UICinematicParam::TerrainTintRed:
    case UICinematicParam::TerrainTintGreen:
    case UICinematicParam::TerrainTintBlue:
        return SCENE_CINE_TERRAIN_TINT;
    case UICinematicParam::TerrainAccentRed:
    case UICinematicParam::TerrainAccentGreen:
    case UICinematicParam::TerrainAccentBlue:
        return SCENE_CINE_TERRAIN_ACCENT;
    case UICinematicParam::TerrainGridScale:
    case UICinematicParam::TerrainGridStrength:
        return SCENE_CINE_TERRAIN_GRID;
    case UICinematicParam::WaterTintRed:
    case UICinematicParam::WaterTintGreen:
    case UICinematicParam::WaterTintBlue:
        return SCENE_CINE_WATER_TINT;
    case UICinematicParam::WaterAlpha:
    case UICinematicParam::WaterReflection:
    case UICinematicParam::WaterGlint:
        return SCENE_CINE_WATER_PROFILE;
    case UICinematicParam::BasinCenterX:
    case UICinematicParam::BasinCenterZ:
    case UICinematicParam::BasinRadiusX:
    case UICinematicParam::BasinRadiusZ:
    case UICinematicParam::BasinFeather:
        return SCENE_CINE_BASIN_MASK;
    case UICinematicParam::BasinDepth:
        return SCENE_CINE_BASIN_DEPTH;
    case UICinematicParam::BasinRimLift:
        return SCENE_CINE_BASIN_RIM_LIFT;
    case UICinematicParam::FogDensity:
        return SCENE_CINE_FOG_DENSITY;
    case UICinematicParam::FogOpacity:
        return SCENE_CINE_FOG_MAX_OPACITY;
    case UICinematicParam::FogStart:
        return SCENE_CINE_FOG_START;
    case UICinematicParam::FogEnd:
        return SCENE_CINE_FOG_END;
    case UICinematicParam::FogRed:
        return SCENE_CINE_FOG_COLOR_R;
    case UICinematicParam::FogGreen:
        return SCENE_CINE_FOG_COLOR_G;
    case UICinematicParam::FogBlue:
        return SCENE_CINE_FOG_COLOR_B;
    default:
        return 0;
    }
}

uint64_t CinematicOverrideMaskForUIFeature( UICinematicFeature feature )
{
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        return SCENE_CINE_SKY_ATMOSPHERE;
    case UICinematicFeature::Clouds:
        return SCENE_CINE_CLOUDS;
    case UICinematicFeature::GodRays:
        return SCENE_CINE_GOD_RAYS;
    case UICinematicFeature::VolumetricLight:
        return SCENE_CINE_VOLUMETRIC_LIGHTING;
    case UICinematicFeature::Bloom:
        return SCENE_CINE_BLOOM;
    case UICinematicFeature::Fog:
        return SCENE_CINE_FOG;
    case UICinematicFeature::TerrainRelief:
        return SCENE_CINE_TERRAIN_RELIEF_ENABLED;
    case UICinematicFeature::Shadows:
        return SCENE_CINE_SHADOWS;
    default:
        return 0;
    }
}

Vector3 CinematicSkySunDirection( const CinematicRenderConfig& cinematic )
{
    constexpr float twoPi = 6.28318530718f;
    const float azimuth = std::clamp( cinematic.sunScreenX, 0.0f, 1.0f ) * twoPi;
    const float elevation = -0.08f + std::clamp( cinematic.sunScreenY, 0.0f, 1.0f ) * 1.13f;
    const float cosElevation = cosf( elevation );
    Vector3 direction( sinf( azimuth ) * cosElevation, sinf( elevation ), cosf( azimuth ) * cosElevation );
    direction.Normalise();
    return direction;
}

void ApplyWorkerThreadCountOverride( EngineConfig& config,
                                     SkullbonezCore::Threading::WorkerPool& workerPool,
                                     int requestedWorkerThreads )
{
    const int clampedWorkerThreads =
        requestedWorkerThreads < 0
            ? -1
            : std::clamp( requestedWorkerThreads, 0, SkullbonezCore::Threading::WorkerPool::MaxThreadCount() );
    const int resolvedWorkerThreads = SkullbonezCore::Threading::WorkerPool::ResolveThreadCount( clampedWorkerThreads );
    config.workerThreads = clampedWorkerThreads;
    if ( workerPool.GetThreadCount() != resolvedWorkerThreads )
    {
        workerPool.Initialise( clampedWorkerThreads );
    }
}

bool ApplyRenderVsyncUICommand( RenderDeviceUICommandContext context, const UI::UIRendererCommands& commands )
{
    if ( !commands.toggleVsync )
    {
        return false;
    }

    context.runtimeSettings.isVsyncEnabled = !context.runtimeSettings.isVsyncEnabled;
    if ( context.deviceLifecycle )
    {
        context.deviceLifecycle->SetVsyncEnabled( context.runtimeSettings.isVsyncEnabled );
    }
    return true;
}

bool ApplySceneFixedStepUICommand( SceneFixedStepUICommandContext context, const UI::UISceneOptionCommands& commands )
{
    if ( !commands.toggleFixedStep )
    {
        return false;
    }

    context.scene.isFixedStep = !context.scene.isFixedStep;
    context.simulation.Reset();
    return true;
}


RunCameraModeUICommandResult DecodeRunCameraModeUICommand( const UI::UIRunCommands& commands )
{
    RunCameraModeUICommandResult result;
    if ( commands.requestedCameraMode < 0 || commands.requestedCameraMode >= static_cast<int>( RunCameraMode::Count ) )
    {
        return result;
    }

    result.accepted = true;
    result.mode = static_cast<RunCameraMode>( commands.requestedCameraMode );
    return result;
}


RunSimulationUICommandResult ApplyRunSimulationUICommands( RunSimulationUICommandContext context,
                                                           const UI::UISceneOptionCommands& sceneOptions,
                                                           const UI::UIRunCommands& run,
                                                           const UI::UIProfilerCommands& profiler )
{
    RunSimulationUICommandResult result;
    if ( sceneOptions.requestedTimeScale > 0.0f )
    {
        context.uiOverrides.timeScaleOverride = std::clamp( sceneOptions.requestedTimeScale, 0.10f, 10.00f );
        context.scene.timeScale = context.uiOverrides.timeScaleOverride;
        result.setTimeScale = true;
    }
    if ( run.requestedSeed > 0 )
    {
        // Invariant: seed edits reset rngState immediately so generated rebuilds
        // and live frame code observe the same deterministic starting point.
        context.scene.rngSeed = static_cast<unsigned int>( std::clamp( run.requestedSeed, 1, 999999 ) );
        context.scene.rngState = context.scene.rngSeed;
        result.setRunSeed = true;
    }
    if ( profiler.requestedWorkerThreads >= -1 )
    {
        ApplyWorkerThreadCountOverride( context.config, context.workerPool, profiler.requestedWorkerThreads );
        result.setWorkerThreads = true;
    }
    return result;
}

void ApplyUIWorldOverride( WorldEnvironment& world,
                           ReplayRuntime& replayRuntime,
                           float gravity,
                           float fluidHeight,
                           float fluidDensity )
{
    const float previousGravity = world.GetGravity();
    const float previousFluidHeight = world.GetFluidSurfaceHeight();
    const float previousFluidDensity = world.GetFluidDensity();
    world.SetGravity( gravity );
    world.SetFluidSurfaceHeight( fluidHeight );
    world.SetFluidDensity( fluidDensity );
    replayRuntime.RecordWorldOverrideEvent( previousGravity,
                                            previousFluidHeight,
                                            previousFluidDensity,
                                            gravity,
                                            fluidHeight,
                                            fluidDensity );
}

bool ApplyWorldWaterUICommands( WorldEnvironment& world,
                                ReplayRuntime& replayRuntime,
                                const UI::UIWaterCommands& commands )
{
    if ( !commands.requestWorldGravity && !commands.requestWorldFluidHeight && !commands.requestWorldFluidDensity )
    {
        return false;
    }

    // Invariant: Water-tab sliders are partial requests. Unspecified fields keep
    // their current world values so a gravity edit does not rewrite fluid policy.
    const float gravity = commands.requestWorldGravity ? commands.requestedWorldGravity : world.GetGravity();
    const float fluidHeight =
        commands.requestWorldFluidHeight ? commands.requestedWorldFluidHeight : world.GetFluidSurfaceHeight();
    const float fluidDensity =
        commands.requestWorldFluidDensity ? commands.requestedWorldFluidDensity : world.GetFluidDensity();
    ApplyUIWorldOverride( world,
                          replayRuntime,
                          std::clamp( gravity, -100.0f, 0.0f ),
                          std::clamp( fluidHeight, -100.0f, 200.0f ),
                          std::clamp( fluidDensity, 0.0f, 5.0f ) );
    return true;
}

bool ApplyRuntimeTextOnlyUICommand( RunDebugState& debug, const UI::UISceneOptionCommands& commands )
{
    if ( !commands.toggleTextOnly )
    {
        return false;
    }

    debug.isTextOnly = !debug.isTextOnly;
    return true;
}

RuntimePresentationUICommandResult ApplyRuntimePresentationUICommands( RuntimePresentationUICommandContext context,
                                                                       const UI::UISceneOptionCommands& sceneOptions,
                                                                       const UI::UIRenderCommands& renderTuning,
                                                                       const UI::UIWaterCommands& water )
{
    RuntimePresentationUICommandResult result;
    RunDebugState& debug = context.debug;
    EngineConfig& config = context.config;
    if ( sceneOptions.toggleTerrainHidden )
    {
        debug.isTerrainHidden = !debug.isTerrainHidden;
        result.toggledTerrainHidden = true;
    }
    if ( sceneOptions.toggleWaterHidden )
    {
        debug.isWaterHidden = !debug.isWaterHidden;
        result.toggledWaterHidden = true;
    }
    if ( sceneOptions.toggleWaterFreeze )
    {
        debug.isWaterFreezeDebug = !debug.isWaterFreezeDebug;
        if ( debug.isWaterFreezeDebug )
        {
            debug.frozenWaterTime = static_cast<float>( context.simulationSeconds );
        }
        result.toggledWaterFreeze = true;
    }
    if ( sceneOptions.toggleWaterFlat )
    {
        debug.isWaterFlatDebug = !debug.isWaterFlatDebug;
        result.toggledWaterFlat = true;
    }
    if ( sceneOptions.toggleShadows )
    {
        if ( RuntimeCinematicRenderingEnabled( context.scene,
                                               config,
                                               context.launchOptions,
                                               debug,
                                               context.graphicsReady ) )
        {
            const bool shadowsActive = RuntimeActiveCinematicConfig( context.scene, config ).shadowsEnabled;
            context.launchOptions.hasCinematicShadowsOverride = false;
            SetCinematicShadowsEnabledFromUI( RuntimeActiveCinematicConfig( context.scene, config ),
                                              context.scene,
                                              !shadowsActive );
        }
        else
        {
            config.ordinaryRender.shadowsEnabled = !config.ordinaryRender.shadowsEnabled;
        }
        result.toggledSceneShadows = true;
    }
    if ( renderTuning.toggleShadows )
    {
        config.ordinaryRender.shadowsEnabled = !config.ordinaryRender.shadowsEnabled;
        result.toggledRenderShadows = true;
    }
    if ( renderTuning.saveDefaults )
    {
        context.renderDefaults.SubmitOrdinarySave();
        result.queuedRenderDefaultsSave = true;
    }
    if ( renderTuning.requestedParam != UIRenderParam::None )
    {
        ApplyOrdinaryRenderUIParam( config.ordinaryRender, renderTuning.requestedParam, renderTuning.requestedValue );
        result.appliedRenderTuning = true;
    }
    if ( water.toggleWaterReflection )
    {
        if ( debug.isWaterNoReflect )
        {
            debug.isWaterNoReflect = false;
        }
        else
        {
            debug.isWaterNoReflect = true;
            debug.isWaterRTReflect = false;
        }
        result.toggledWaterReflection = true;
    }
    if ( water.requestedWaterReflectionMode >= 0 )
    {
        const int mode = std::clamp( water.requestedWaterReflectionMode, 0, 2 );
        debug.isWaterRTReflect = mode == 1;
        debug.isWaterNoReflect = mode == 2;
        result.setWaterReflectionMode = true;
    }
    return result;
}

bool ApplyCinematicRenderingToggleUICommand( CinematicUICommandContext context,
                                             const UI::UICinematicCommands& commands )
{
    if ( !commands.toggleRendering )
    {
        return false;
    }

    // Master Cine switch. Clearing the launch override lets the runtime toggle
    // become the new source of truth after launch arguments have been consumed.
    const bool currentlyEnabled = context.launchOptions.hasCinematicRenderingOverride
                                      ? context.launchOptions.cinematicRendering
                                      : context.cinematic.enabled;
    context.cinematic.enabled = !currentlyEnabled;
    context.launchOptions.hasCinematicRenderingOverride = false;
    if ( context.scene.isSceneMode )
    {
        context.scene.hasCinematicRenderingOverride = true;
        context.scene.isCinematicRenderingEnabled = context.cinematic.enabled;
        context.scene.cinematicOverrideMask |= SCENE_CINE_RENDERING;
        context.scene.uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
    }
    return true;
}

bool QueueCinematicSkyDefaultsUICommand( CinematicUICommandContext context, const UI::UICinematicCommands& commands )
{
    if ( !commands.saveSkyDefaults )
    {
        return false;
    }

    context.renderDefaults.SubmitCinematicSave();
    return true;
}


bool HasCinematicModeUICommand( const UI::UICinematicCommands& commands )
{
    return commands.requestedModeSceneIndex >= -1;
}


bool ApplyCinematicModeUICommand( SceneRuntimeStyleContext context, const UI::UICinematicCommands& commands )
{
    if ( !HasCinematicModeUICommand( commands ) )
    {
        return false;
    }

    // Invariant: input action reporting tracks the accepted UI request. The
    // underlying style loader can fail closed for a bad/missing scene, but the
    // previous RunInput path still recorded the selection action for the request.
    (void)ApplyCinematicModeFromBrowserIndex( context, commands.requestedModeSceneIndex );
    return true;
}

CinematicTuningUICommandResult ApplyCinematicTuningUICommands( CinematicUICommandContext context,
                                                               const UI::UICinematicCommands& commands )
{
    CinematicTuningUICommandResult result;
    if ( commands.requestedFeature != UICinematicFeature::None )
    {
        if ( commands.requestedFeature == UICinematicFeature::Shadows )
        {
            context.launchOptions.hasCinematicShadowsOverride = false;
        }
        ToggleCinematicUIFeature( context.cinematic, context.scene, commands.requestedFeature );
        result.toggledFeature = true;
    }
    if ( commands.requestedParam != UICinematicParam::None )
    {
        ApplyCinematicUIParam( context.cinematic, context.scene, commands.requestedParam, commands.requestedValue );
        result.appliedParam = true;
    }
    return result;
}

bool ApplySoundUICommands( SoundUICommandContext context, const UI::UISoundCommands& commands )
{
    // Why: contact audio is presentation-only and may be disabled at launch or
    // unavailable on a machine. The Sound tab can retry initialization, but
    // failure must not affect simulation, input mode, or validation.
    bool soundTuningChanged = false;
    Runtime::Audio::ContactAudioService& contactAudio = context.contactAudio;
    RunRuntimeSettings& runtimeSettings = context.runtimeSettings;
    if ( commands.toggleEnabled )
    {
        if ( contactAudio.IsEnabled() )
        {
            contactAudio.SetEnabled( false );
        }
        else if ( !context.contactAudioDisabledByLaunch )
        {
            const bool ready = EnsureContactAudioReady( context );
            contactAudio.SetEnabled( ready );
        }
        soundTuningChanged = true;
    }
    if ( commands.toggleDebugCounters )
    {
        runtimeSettings.contactAudioDebugCounters = !runtimeSettings.contactAudioDebugCounters;
        soundTuningChanged = true;
    }
    if ( commands.cycleFlashMode )
    {
        runtimeSettings.contactAudioFlashMode = NextContactAudioFlashMode( runtimeSettings.contactAudioFlashMode );
        soundTuningChanged = true;
    }
    if ( commands.toggleSimpleMode )
    {
        contactAudio.SetSimpleModeEnabled( !contactAudio.SimpleModeEnabled() );
        soundTuningChanged = true;
    }
    if ( commands.requestedParam != UISoundParam::None )
    {
        using Runtime::Audio::ContactAudioSetParam;
        switch ( commands.requestedParam )
        {
        case UISoundParam::SimpleMinLinearEnergy:
            contactAudio.SetSimpleMinLinearEnergy( commands.requestedValue );
            break;
        case UISoundParam::SimpleMinLinearDeltaSpeed:
            contactAudio.SetSimpleMinLinearDeltaSpeed( commands.requestedValue );
            break;
        case UISoundParam::SimpleLinearEnergyRange:
            contactAudio.SetSimpleLinearEnergyRange( commands.requestedValue );
            break;
        case UISoundParam::MasterGain:
            contactAudio.SetMasterGain( commands.requestedValue );
            break;
        case UISoundParam::MaxDistanceScale:
            contactAudio.SetMaxDistanceScale( commands.requestedValue );
            break;
        case UISoundParam::MinClosingSpeed:
            contactAudio.SetMinClosingSpeed( commands.requestedValue );
            break;
        case UISoundParam::MinImpactScore:
            contactAudio.SetMinImpactScore( commands.requestedValue );
            break;
        case UISoundParam::ImpactScoreRangeSeconds:
            contactAudio.SetImpactScoreRangeSeconds( commands.requestedValue );
            break;
        case UISoundParam::BurstVoicesPerWindow:
            contactAudio.SetBurstVoicesPerWindow( static_cast<uint32_t>( commands.requestedValue ) );
            break;
        case UISoundParam::RollingLevelDb:
            contactAudio.SetRollingLevelDb( commands.requestedValue );
            break;
        case UISoundParam::RollingMaxDistance:
            contactAudio.SetRollingMaxDistance( commands.requestedValue );
            break;
        case UISoundParam::RollingMinSlipSpeed:
            contactAudio.SetRollingMinSlipSpeed( commands.requestedValue );
            break;
        case UISoundParam::RollingVoicesPerWindow:
            contactAudio.SetRollingVoicesPerWindow( static_cast<uint32_t>( commands.requestedValue ) );
            break;
        case UISoundParam::SetMinImpulse:
            contactAudio.SetSoundSetParam( commands.requestedSetIndex,
                                           ContactAudioSetParam::MinImpulse,
                                           commands.requestedValue );
            break;
        case UISoundParam::SetImpulseRange:
            contactAudio.SetSoundSetParam( commands.requestedSetIndex,
                                           ContactAudioSetParam::ImpulseRange,
                                           commands.requestedValue );
            break;
        case UISoundParam::SetCooldownMs:
            contactAudio.SetSoundSetParam( commands.requestedSetIndex,
                                           ContactAudioSetParam::CooldownMs,
                                           commands.requestedValue );
            break;
        case UISoundParam::SetOverrideCooldownMs:
            contactAudio.SetSoundSetParam( commands.requestedSetIndex,
                                           ContactAudioSetParam::OverrideCooldownMs,
                                           commands.requestedValue );
            break;
        case UISoundParam::SetMaxDistance:
            contactAudio.SetSoundSetParam( commands.requestedSetIndex,
                                           ContactAudioSetParam::MaxDistance,
                                           commands.requestedValue );
            break;
        case UISoundParam::SetBaseGain:
            contactAudio.SetSoundSetParam( commands.requestedSetIndex,
                                           ContactAudioSetParam::BaseGain,
                                           commands.requestedValue );
            break;
        case UISoundParam::SetPitchMin:
            contactAudio.SetSoundSetParam( commands.requestedSetIndex,
                                           ContactAudioSetParam::PitchMin,
                                           commands.requestedValue );
            break;
        case UISoundParam::SetPitchMax:
            contactAudio.SetSoundSetParam( commands.requestedSetIndex,
                                           ContactAudioSetParam::PitchMax,
                                           commands.requestedValue );
            break;
        case UISoundParam::SetMaxVoices:
            contactAudio.SetSoundSetParam( commands.requestedSetIndex,
                                           ContactAudioSetParam::MaxVoices,
                                           commands.requestedValue );
            break;
        default:
            break;
        }
        soundTuningChanged = true;
    }
    if ( commands.requestedBandParam != UISoundBandParam::None )
    {
        using Runtime::Audio::ContactAudioBandParam;
        switch ( commands.requestedBandParam )
        {
        case UISoundBandParam::MinImpulse:
            contactAudio.SetSoundBandParam( commands.requestedSetIndex,
                                            commands.requestedBandIndex,
                                            ContactAudioBandParam::MinImpulse,
                                            commands.requestedValue );
            break;
        case UISoundBandParam::ImpulseRange:
            contactAudio.SetSoundBandParam( commands.requestedSetIndex,
                                            commands.requestedBandIndex,
                                            ContactAudioBandParam::ImpulseRange,
                                            commands.requestedValue );
            break;
        case UISoundBandParam::BaseGain:
            contactAudio.SetSoundBandParam( commands.requestedSetIndex,
                                            commands.requestedBandIndex,
                                            ContactAudioBandParam::BaseGain,
                                            commands.requestedValue );
            break;
        case UISoundBandParam::PitchMin:
            contactAudio.SetSoundBandParam( commands.requestedSetIndex,
                                            commands.requestedBandIndex,
                                            ContactAudioBandParam::PitchMin,
                                            commands.requestedValue );
            break;
        case UISoundBandParam::PitchMax:
            contactAudio.SetSoundBandParam( commands.requestedSetIndex,
                                            commands.requestedBandIndex,
                                            ContactAudioBandParam::PitchMax,
                                            commands.requestedValue );
            break;
        default:
            break;
        }
        soundTuningChanged = true;
    }
    if ( commands.previewSampleIndex >= 0 )
    {
        if ( EnsureContactAudioReady( context ) )
        {
            contactAudio.PreviewSoundSample( commands.previewSampleIndex, 0.85f );
        }
        soundTuningChanged = true;
    }
    if ( commands.selectSampleIndex >= 0 )
    {
        if ( EnsureContactAudioReady( context ) &&
             contactAudio.SetSoundSetSample( commands.requestedSetIndex, commands.selectSampleIndex ) )
        {
            contactAudio.PreviewSoundSample( commands.selectSampleIndex, 0.85f );
        }
        soundTuningChanged = true;
    }
    return soundTuningChanged;
}

bool ApplyPhysicsSleepPolicyUICommand( PhysicsSleepPolicyUICommandContext context,
                                       const UI::UIPhysicsCommands& commands )
{
    if ( !commands.togglePhysicsSleepPolicy )
    {
        return false;
    }

    RunRuntimeSettings& runtimeSettings = context.runtimeSettings;
    runtimeSettings.isPhysicsSleepEnabled = !runtimeSettings.isPhysicsSleepEnabled;
    context.modelCollection.SetPhysicsSleepEnabled( runtimeSettings.isPhysicsSleepEnabled );
    return true;
}

PhysicsFrictionUICommandResult ApplyPhysicsFrictionUICommands( PhysicsFrictionUICommandContext context,
                                                               const UI::UIPhysicsCommands& commands )
{
    PhysicsFrictionUICommandResult result;
    EngineConfig& liveConfig = context.config;
    bool runtimePhysicsConfigChanged = false;
    if ( commands.requestTerrainFrictionCoeff )
    {
        liveConfig.frictionCoeff = std::clamp( commands.requestedTerrainFrictionCoeff,
                                               UI::Layout::UI_FRICTION_COEFF_MIN,
                                               UI::Layout::UI_FRICTION_COEFF_MAX );
        runtimePhysicsConfigChanged = true;
        ++result.applySettingsActionCount;
    }
    if ( commands.requestObjectFrictionCoeff )
    {
        liveConfig.objectFrictionCoeff = std::clamp( commands.requestedObjectFrictionCoeff,
                                                     UI::Layout::UI_FRICTION_COEFF_MIN,
                                                     UI::Layout::UI_FRICTION_COEFF_MAX );
        runtimePhysicsConfigChanged = true;
        ++result.applySettingsActionCount;
    }
    if ( commands.requestRollingFrictionCoeff )
    {
        liveConfig.rollingFrictionCoeff = std::clamp( commands.requestedRollingFrictionCoeff,
                                                      UI::Layout::UI_ROLLING_FRICTION_COEFF_MIN,
                                                      UI::Layout::UI_ROLLING_FRICTION_COEFF_MAX );
        runtimePhysicsConfigChanged = true;
        ++result.applySettingsActionCount;
    }
    if ( runtimePhysicsConfigChanged )
    {
        // Invariant: GameModelCollection caches per-model runtime tuning so
        // existing bodies and newly added bodies must observe the same live
        // physics settings immediately after UI config edits.
        context.modelCollection.ApplyRuntimeConfig( liveConfig );
    }
    return result;
}

TornadoUICommandResult ApplyTornadoUICommands( TornadoUICommandContext context, const UI::UIPhysicsCommands& commands )
{
    // Why: RunInput owns input-mode bookkeeping, while this helper owns the
    // physics-facing mutation and single sync point for accepted tornado edits.
    TornadoUICommandResult result;
    RunRuntimeSettings& runtimeSettings = context.runtimeSettings;
    bool tornadoFieldChanged = false;
    const bool hasTornadoSystem = !runtimeSettings.tornadoSystem.vortices.empty();

    if ( commands.toggleTornado )
    {
        bool tornadoEnabled = false;
        if ( hasTornadoSystem )
        {
            runtimeSettings.tornadoSystem.enabled = !runtimeSettings.tornadoSystem.enabled;
            tornadoEnabled = runtimeSettings.tornadoSystem.enabled;
        }
        else
        {
            runtimeSettings.tornadoField.enabled = !runtimeSettings.tornadoField.enabled;
            tornadoEnabled = runtimeSettings.tornadoField.enabled;
        }
        if ( runtimeSettings.tornadoVisual.autoEnableWithTornado )
        {
            runtimeSettings.tornadoVisual.enabled = tornadoEnabled;
        }
        tornadoFieldChanged = true;
        result.toggledTornado = true;
    }
    if ( commands.toggleTornadoVisualShell )
    {
        runtimeSettings.tornadoVisual.enabled = !runtimeSettings.tornadoVisual.enabled;
        result.toggledVisualShell = true;
    }
    if ( commands.toggleTornadoFieldVectors )
    {
        if ( hasTornadoSystem )
        {
            runtimeSettings.tornadoSystem.visualizeVelocityField =
                !runtimeSettings.tornadoSystem.visualizeVelocityField;
        }
        else
        {
            runtimeSettings.tornadoField.visualizeVelocityField = !runtimeSettings.tornadoField.visualizeVelocityField;
        }
        tornadoFieldChanged = true;
        result.toggledFieldVectors = true;
    }
    if ( commands.requestTornadoRadius )
    {
        ApplyTornadoFieldValue( runtimeSettings,
                                hasTornadoSystem,
                                &Physics::TornadoFieldConfig::radius,
                                std::clamp( commands.requestedTornadoRadius,
                                            UI::Layout::UI_TORNADO_RADIUS_MIN,
                                            UI::Layout::UI_TORNADO_RADIUS_MAX ) );
        tornadoFieldChanged = true;
        ++result.applySettingsActionCount;
    }
    if ( commands.requestTornadoHeight )
    {
        ApplyTornadoFieldValue( runtimeSettings,
                                hasTornadoSystem,
                                &Physics::TornadoFieldConfig::height,
                                std::clamp( commands.requestedTornadoHeight,
                                            UI::Layout::UI_TORNADO_HEIGHT_MIN,
                                            UI::Layout::UI_TORNADO_HEIGHT_MAX ) );
        tornadoFieldChanged = true;
        ++result.applySettingsActionCount;
    }
    if ( commands.requestTornadoInward )
    {
        ApplyTornadoFieldValue( runtimeSettings,
                                hasTornadoSystem,
                                &Physics::TornadoFieldConfig::inwardAcceleration,
                                std::clamp( commands.requestedTornadoInward,
                                            UI::Layout::UI_TORNADO_INWARD_MIN,
                                            UI::Layout::UI_TORNADO_INWARD_MAX ) );
        tornadoFieldChanged = true;
        ++result.applySettingsActionCount;
    }
    if ( commands.requestTornadoSwirl )
    {
        ApplyTornadoFieldValue( runtimeSettings,
                                hasTornadoSystem,
                                &Physics::TornadoFieldConfig::swirlAcceleration,
                                std::clamp( commands.requestedTornadoSwirl,
                                            UI::Layout::UI_TORNADO_SWIRL_MIN,
                                            UI::Layout::UI_TORNADO_SWIRL_MAX ) );
        tornadoFieldChanged = true;
        ++result.applySettingsActionCount;
    }
    if ( commands.requestTornadoLift )
    {
        ApplyTornadoFieldValue( runtimeSettings,
                                hasTornadoSystem,
                                &Physics::TornadoFieldConfig::liftAcceleration,
                                std::clamp( commands.requestedTornadoLift,
                                            UI::Layout::UI_TORNADO_LIFT_MIN,
                                            UI::Layout::UI_TORNADO_LIFT_MAX ) );
        tornadoFieldChanged = true;
        ++result.applySettingsActionCount;
    }
    if ( tornadoFieldChanged )
    {
        SyncTornadoRuntimeSettingsToPhysics( context.modelCollection, runtimeSettings );
    }
    return result;
}

void ApplyCinematicUIParam( CinematicRenderConfig& cinematic,
                            RunSceneState& scene,
                            UICinematicParam param,
                            float rawValue )
{
    // The UI sends "the user dragged this slider to this raw value." This helper
    // clamps the value into a safe range, writes it into the live cinematic
    // config, and marks the scene override bit so reloads keep the user's tweak.
    const auto clampValue = []( float value, float minValue, float maxValue ) -> float
    { return std::clamp( value, minValue, maxValue ); };
    const auto clampIntValue = []( float value, int minValue, int maxValue ) -> int
    { return std::clamp( static_cast<int>( std::round( value ) ), minValue, maxValue ); };

    switch ( param )
    {
    case UICinematicParam::Exposure:
        cinematic.exposure = clampValue( rawValue, 0.05f, 3.00f );
        scene.hasCinematicExposure = true;
        scene.cinematicExposure = cinematic.exposure;
        scene.cinematicOverrideMask |= SCENE_CINE_EXPOSURE;
        break;
    case UICinematicParam::Gamma:
        cinematic.gamma = clampValue( rawValue, 1.00f, 3.00f );
        scene.hasCinematicGamma = true;
        scene.cinematicGamma = cinematic.gamma;
        scene.cinematicOverrideMask |= SCENE_CINE_GAMMA;
        break;
    case UICinematicParam::SkyMode:
        cinematic.skyMode = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::TerrainMode:
        cinematic.terrainMode = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::ObjectStyle:
        cinematic.objectStyle = clampIntValue( rawValue, 0, 32 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::WaterMode:
        cinematic.waterMode = clampIntValue( rawValue, 0, 4 );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
        break;
    case UICinematicParam::StyleSaturation:
        cinematic.styleSaturation = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::StyleContrast:
        cinematic.styleContrast = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::StyleVignette:
        cinematic.styleVignette = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
        break;
    case UICinematicParam::SunX:
        cinematic.sunScreenX = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SCREEN_X;
        break;
    case UICinematicParam::SunY:
        cinematic.sunScreenY = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SCREEN_Y;
        break;
    case UICinematicParam::SunBrightness:
        cinematic.sunIntensity = clampValue( rawValue, 0.00f, 40.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_INTENSITY;
        break;
    case UICinematicParam::SunRed:
        cinematic.sunColorR = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_R;
        break;
    case UICinematicParam::SunGreen:
        cinematic.sunColorG = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_G;
        break;
    case UICinematicParam::SunBlue:
        cinematic.sunColorB = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_COLOR_B;
        break;
    case UICinematicParam::SkyGlow:
        cinematic.skyGlowStrength = clampValue( rawValue, 0.00f, 8.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_GLOW_STRENGTH;
        break;
    case UICinematicParam::HorizonRed:
        cinematic.skyHorizonR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_R;
        break;
    case UICinematicParam::HorizonGreen:
        cinematic.skyHorizonG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_G;
        break;
    case UICinematicParam::HorizonBlue:
        cinematic.skyHorizonB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_HORIZON_B;
        break;
    case UICinematicParam::ZenithRed:
        cinematic.skyZenithR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_R;
        break;
    case UICinematicParam::ZenithGreen:
        cinematic.skyZenithG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_G;
        break;
    case UICinematicParam::ZenithBlue:
        cinematic.skyZenithB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ZENITH_B;
        break;
    case UICinematicParam::CloudCoverage:
        cinematic.cloudCoverage = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_COVERAGE;
        break;
    case UICinematicParam::CloudSoftness:
        cinematic.cloudSoftness = clampValue( rawValue, 0.01f, 0.65f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_SOFTNESS;
        break;
    case UICinematicParam::CloudScale:
        cinematic.cloudScale = clampValue( rawValue, 0.50f, 12.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_SCALE;
        break;
    case UICinematicParam::CloudIntensity:
        cinematic.cloudIntensity = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUD_INTENSITY;
        break;
    case UICinematicParam::ShaftStrength:
        cinematic.sunShaftStrength = clampValue( rawValue, 0.00f, 3.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SHAFT_STRENGTH;
        break;
    case UICinematicParam::ShaftFalloff:
        cinematic.sunShaftFalloff = clampValue( rawValue, 0.25f, 5.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_SUN_SHAFT_FALLOFF;
        break;
    case UICinematicParam::VolumetricStrength:
        cinematic.volumetricStrength = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_STRENGTH;
        break;
    case UICinematicParam::VolumetricDensity:
        cinematic.volumetricDensity = clampValue( rawValue, 0.00f, 2.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_DENSITY;
        break;
    case UICinematicParam::VolumetricDecay:
        cinematic.volumetricDecay = clampValue( rawValue, 0.800f, 0.995f );
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_DECAY;
        break;
    case UICinematicParam::BloomThreshold:
        cinematic.bloomThreshold = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_THRESHOLD;
        break;
    case UICinematicParam::BloomKnee:
        cinematic.bloomKnee = clampValue( rawValue, 0.01f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_KNEE;
        break;
    case UICinematicParam::BloomStrength:
        cinematic.bloomStrength = clampValue( rawValue, 0.00f, 2.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_STRENGTH;
        break;
    case UICinematicParam::BloomRadius:
        cinematic.bloomRadius = clampValue( rawValue, 0.25f, 8.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM_RADIUS;
        break;
    case UICinematicParam::TerrainRelief:
        cinematic.terrainRelief = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_RELIEF;
        break;
    case UICinematicParam::TerrainTintRed:
        cinematic.terrainTintR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainTintGreen:
        cinematic.terrainTintG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainTintBlue:
        cinematic.terrainTintB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
        break;
    case UICinematicParam::TerrainAccentRed:
        cinematic.terrainAccentR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainAccentGreen:
        cinematic.terrainAccentG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainAccentBlue:
        cinematic.terrainAccentB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
        break;
    case UICinematicParam::TerrainGridScale:
        cinematic.terrainGridScale = clampValue( rawValue, 0.10f, 120.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        break;
    case UICinematicParam::TerrainGridStrength:
        cinematic.terrainGridStrength = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
        break;
    case UICinematicParam::WaterTintRed:
        cinematic.waterTintR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterTintGreen:
        cinematic.waterTintG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterTintBlue:
        cinematic.waterTintB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
        break;
    case UICinematicParam::WaterAlpha:
        cinematic.waterAlpha = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::WaterReflection:
        cinematic.waterReflectionStrength = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::WaterGlint:
        cinematic.waterGlintStrength = clampValue( rawValue, 0.00f, 4.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
        break;
    case UICinematicParam::BasinCenterX:
        cinematic.basinCenterX = clampValue( rawValue, 0.00f, 1200.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinCenterZ:
        cinematic.basinCenterZ = clampValue( rawValue, 0.00f, 1200.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinRadiusX:
        cinematic.basinRadiusX = clampValue( rawValue, 1.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinRadiusZ:
        cinematic.basinRadiusZ = clampValue( rawValue, 1.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinFeather:
        cinematic.basinFeather = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
        break;
    case UICinematicParam::BasinDepth:
        cinematic.basinDepth = clampValue( rawValue, 0.00f, 80.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_DEPTH;
        break;
    case UICinematicParam::BasinRimLift:
        cinematic.basinRimLift = clampValue( rawValue, 0.00f, 60.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_BASIN_RIM_LIFT;
        break;
    case UICinematicParam::FogDensity:
        cinematic.fogDensity = clampValue( rawValue, 0.00000f, 0.00600f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_DENSITY;
        break;
    case UICinematicParam::FogOpacity:
        cinematic.fogMaxOpacity = clampValue( rawValue, 0.00f, 1.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_MAX_OPACITY;
        break;
    case UICinematicParam::FogStart:
        cinematic.fogStart = clampValue( rawValue, 0.00f, 500.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_START;
        break;
    case UICinematicParam::FogEnd:
        cinematic.fogEnd = clampValue( rawValue, 100.00f, 4000.00f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_END;
        break;
    case UICinematicParam::FogRed:
        cinematic.fogColorR = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_R;
        break;
    case UICinematicParam::FogGreen:
        cinematic.fogColorG = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_G;
        break;
    case UICinematicParam::FogBlue:
        cinematic.fogColorB = clampValue( rawValue, 0.00f, 1.50f );
        scene.cinematicOverrideMask |= SCENE_CINE_FOG_COLOR_B;
        break;
    default:
        break;
    }

    const uint64_t touchedMask = CinematicOverrideMaskForUIParam( param );
    if ( touchedMask != 0 )
    {
        scene.cinematicOverrideMask |= touchedMask;
        scene.uiCinematicOverrideMask |= touchedMask;
    }
}

void SetCinematicShadowsEnabledFromUI( CinematicRenderConfig& cinematic, RunSceneState& scene, bool enabled )
{
    // Shadow maps are configured next to the cinematic controls because the
    // original implementation grew from that renderer work, but the depth pass
    // now feeds normal rendering too. Toggling shadows from either the Options
    // tab or the Cine tab must therefore only touch the shadow flag and scene
    // override bits; it must not silently enable the HDR/post-processing stack.
    cinematic.shadowsEnabled = enabled;
    scene.cinematicOverrideMask |= SCENE_CINE_SHADOWS;
    scene.uiCinematicOverrideMask |= SCENE_CINE_SHADOWS;
}

void ApplyOrdinaryRenderUIParam( OrdinaryRenderConfig& ordinary, UIRenderParam param, float rawValue )
{
    switch ( param )
    {
    case UIRenderParam::SunIntensity:
        ordinary.sunIntensity = std::clamp( rawValue, 0.0f, 4.0f );
        break;
    case UIRenderParam::SunRed:
        ordinary.sunColorR = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::SunGreen:
        ordinary.sunColorG = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::SunBlue:
        ordinary.sunColorB = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::AmbientStrength:
        ordinary.ambientStrength = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyRed:
        ordinary.skyAmbientR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyGreen:
        ordinary.skyAmbientG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::SkyBlue:
        ordinary.skyAmbientB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundRed:
        ordinary.groundAmbientR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundGreen:
        ordinary.groundAmbientG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::GroundBlue:
        ordinary.groundAmbientB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::ShadowStrength:
        ordinary.shadowStrength = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::ShadowSoftness:
        ordinary.shadowSoftness = std::clamp( rawValue, 0.25f, 4.0f );
        break;
    case UIRenderParam::ShadowDepthBias:
        ordinary.shadowDepthBias = std::clamp( rawValue, 0.0f, 0.005f );
        break;
    case UIRenderParam::ShadowSlopeBias:
        ordinary.shadowSlopeBias = std::clamp( rawValue, 0.0f, 0.005f );
        break;
    case UIRenderParam::WaterRed:
        ordinary.waterTintR = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterGreen:
        ordinary.waterTintG = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterBlue:
        ordinary.waterTintB = std::clamp( rawValue, 0.0f, 1.5f );
        break;
    case UIRenderParam::WaterAlpha:
        ordinary.waterAlpha = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::WaterReflection:
        ordinary.waterReflectionStrength = std::clamp( rawValue, 0.0f, 1.0f );
        break;
    case UIRenderParam::WaterFresnel:
        ordinary.waterFresnelF0 = std::clamp( rawValue, 0.0f, 0.12f );
        break;
    case UIRenderParam::BallRoughness:
        ordinary.ballRoughnessScale = std::clamp( rawValue, 0.25f, 2.0f );
        break;
    case UIRenderParam::BallSpecular:
        ordinary.ballSpecularScale = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    case UIRenderParam::BoxRoughness:
        ordinary.boxRoughnessScale = std::clamp( rawValue, 0.25f, 2.0f );
        break;
    case UIRenderParam::BoxSpecular:
        ordinary.boxSpecularScale = std::clamp( rawValue, 0.0f, 2.0f );
        break;
    default:
        break;
    }
}

void ToggleCinematicUIFeature( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicFeature feature )
{
    // Feature toggles are boolean pass switches: sky on/off, bloom on/off, etc.
    // Each toggle also marks the matching override bit for scene persistence.
    switch ( feature )
    {
    case UICinematicFeature::Sky:
        cinematic.skyAtmosphereEnabled = !cinematic.skyAtmosphereEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_SKY_ATMOSPHERE;
        break;
    case UICinematicFeature::Clouds:
        cinematic.cloudsEnabled = !cinematic.cloudsEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_CLOUDS;
        break;
    case UICinematicFeature::GodRays:
        cinematic.godRaysEnabled = !cinematic.godRaysEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_GOD_RAYS;
        break;
    case UICinematicFeature::VolumetricLight:
        cinematic.volumetricLightingEnabled = !cinematic.volumetricLightingEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_VOLUMETRIC_LIGHTING;
        break;
    case UICinematicFeature::Bloom:
        cinematic.bloomEnabled = !cinematic.bloomEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_BLOOM;
        break;
    case UICinematicFeature::Fog:
        cinematic.fogEnabled = !cinematic.fogEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_FOG;
        break;
    case UICinematicFeature::TerrainRelief:
        cinematic.terrainReliefEnabled = !cinematic.terrainReliefEnabled;
        scene.cinematicOverrideMask |= SCENE_CINE_TERRAIN_RELIEF_ENABLED;
        break;
    case UICinematicFeature::Shadows:
        SetCinematicShadowsEnabledFromUI( cinematic, scene, !cinematic.shadowsEnabled );
        break;
    default:
        break;
    }

    const uint64_t touchedMask = CinematicOverrideMaskForUIFeature( feature );
    if ( touchedMask != 0 )
    {
        scene.cinematicOverrideMask |= touchedMask;
        scene.uiCinematicOverrideMask |= touchedMask;
    }
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
