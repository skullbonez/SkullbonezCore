/*
File: SkullbonezSource/Runtime/RuntimeTuning.h
Purpose:
  Declares runtime tuning helpers for UI-driven render, audio, physics, and worker settings.

Mental model:
  UI emits raw parameter changes. Runtime tuning clamps those values or
  delegates them to bounded owner APIs, updates live config, and records scene
  override bits where persistence remains explicit.

Glossary:
  Cinematic config: HDR/post-processing and style settings for the active look.
  Ordinary render config: Non-cinematic renderer settings saved in engine.cfg.
  Override mask: Bitset recording which UI-touched scene values should persist.
  Render device command: One-frame UI packet that edits live backend presentation policy.
  Cinematic command: One-frame UI packet that edits cinematic rendering, style,
    feature, or parameter state.
  Presentation command: One-frame UI packet that edits debug visibility, render
    tuning, shadow, or water-reflection presentation state.
  Scene fixed-step command: One-frame UI packet that changes physics tick cadence.
  Run simulation command: One-frame UI packet that edits time scale, random seed,
    or worker-thread count.
  Sound command: One-frame UI packet that edits contact-audio presentation state.
  Physics friction command: One-frame Physics-tab packet that edits live friction config.
  Physics sleep command: One-frame Physics-tab packet that toggles sleep policy.
  Run camera command: One-frame Run-tab packet that requests an operator camera mode.
  Tornado command: One-frame Physics-tab packet that edits live vortex settings.
  World water command: One-frame Water-tab packet that edits gravity, fluid
    surface height, or fluid density.
  Worker override: Runtime request for the worker-pool thread count.

Invariants:
  - Render and cinematic helpers clamp raw UI values before writing runtime config.
  - Scene override bits and the changed value must stay paired.
  - Sound commands delegate value limits to ContactAudioService setters.
  - Physics config edits are mirrored into SceneController immediately so
    existing and newly added bodies share the same runtime policy.
  - Tornado commands commit copied field/system values back to the physics owner.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/UI/UICommands.h
*/
#pragma once

#include "RenderDefaultsStore.h"
#include "RunDebugState.h"
#include "RuntimeCameraMode.h"
#include "Scene/SceneRuntimeStyle.h"
#include "../UI/UICommands.h"

namespace SkullbonezCore
{
namespace Threading
{
class WorkerPool;
}
namespace Rendering
{
class IRenderDeviceLifecycle;
}
namespace Runtime
{
namespace Audio
{
class ContactAudioService;
}
} // namespace Runtime

namespace Basics
{
class SimulationSystem;
class ReplayRuntime;
class RuntimeRenderer;
using Environment::WorldEnvironment;
using UI::UICinematicFeature;
using UI::UICinematicParam;
using UI::UIRenderParam;
using UI::UISoundBandParam;
using UI::UISoundParam;

namespace RunInternal
{
uint64_t CinematicOverrideMaskForUIParam( UICinematicParam param );
uint64_t CinematicOverrideMaskForUIFeature( UICinematicFeature feature );
Math::Vector::Vector3 CinematicSkySunDirection( const CinematicRenderConfig& cinematic );
struct SoundUICommandContext
{
    // Lifetime: borrowed only while one Sound-tab command packet is applied.
    // The helper may lazily initialize contact audio, but it does not store any
    // service or settings references after returning.
    SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio;
    bool contactAudioDisabledByLaunch = false;
};

struct TornadoUICommandContext
{
    // Lifetime: borrowed only while one Physics-tab tornado command packet is applied.
    // The helper copies, edits, and commits deterministic field config through
    // the model/physics owner; render-only art stays with RuntimeRenderer.
    RuntimeRenderer& renderer;
    Basics::SceneController& modelCollection;
};

struct PhysicsSleepPolicyUICommandContext
{
    // Lifetime: borrowed only while one Physics-tab sleep-policy toggle is applied.
    // The helper toggles the policy directly on the model/physics owner.
    Basics::SceneController& modelCollection;
};

struct PhysicsFrictionUICommandContext
{
    // Lifetime: borrowed only while one Physics-tab friction packet is applied.
    // The helper writes live config and immediately reapplies physics runtime policy.
    EngineConfig& config;
    Basics::SceneController& modelCollection;
};

struct RuntimePresentationUICommandContext
{
    // Lifetime: borrowed only while one scene/render/water UI packet is applied.
    // Simulation-step reset stays in RunInput; this helper owns presentation and
    // render-config mutation plus queued render-default save intent.
    RunDebugState& debug;
    RunSceneState& scene;
    EngineConfig& config;
    RunLaunchOptions& launchOptions;
    RenderDefaultsStore& renderDefaults;
    bool graphicsReady = false;
    double simulationSeconds = 0.0;
};

struct CinematicUICommandContext
{
    // Lifetime: borrowed only while one Cinematic-tab command packet is applied.
    // The caller still owns entering interactive scene flow before mode selection.
    RunLaunchOptions& launchOptions;
    RunSceneState& scene;
    CinematicRenderConfig& cinematic;
    RenderDefaultsStore& renderDefaults;
};

struct TornadoUICommandResult
{
    bool toggledTornado = false;
    bool toggledVisualShell = false;
    bool toggledFieldVectors = false;
    int applySettingsActionCount = 0;
};

struct PhysicsFrictionUICommandResult
{
    // Invariant: count accepted requests, not changed values, so input action
    // reporting still mirrors the UI packet that RunInput accepted.
    int applySettingsActionCount = 0;
};

struct RuntimePresentationUICommandResult
{
    // Invariant: flags report accepted UI commands for RunInput action logging;
    // they are not change-detection flags for the underlying render/debug state.
    bool toggledTerrainHidden = false;
    bool toggledWaterHidden = false;
    bool toggledWaterFreeze = false;
    bool toggledWaterFlat = false;
    bool toggledSceneShadows = false;
    bool toggledRenderShadows = false;
    bool queuedRenderDefaultsSave = false;
    bool appliedRenderTuning = false;
    bool toggledWaterReflection = false;
    bool setWaterReflectionMode = false;
};

struct CinematicTuningUICommandResult
{
    // Invariant: flags report accepted UI commands for RunInput action logging;
    // feature/param setters may clamp or no-op invalid enum values internally.
    bool toggledFeature = false;
    bool appliedParam = false;
};

struct RunSimulationUICommandContext
{
    // Lifetime: borrowed only while one UI command packet is applied. Time-scale
    // edits persist through scene UI overrides, seed edits mutate scene RNG, and
    // worker edits delegate immediately to WorkerPool.
    RunSceneState& scene;
    RunSceneUIOverrideState& uiOverrides;
    EngineConfig& config;
    Threading::WorkerPool& workerPool;
};

struct RunSimulationUICommandResult
{
    bool setTimeScale = false;
    bool setRunSeed = false;
    bool setWorkerThreads = false;
};

struct RunCameraModeUICommandResult
{
    // Invariant: accepted means mode is a real enum value; RunInput still owns
    // applying scene normalization, cursor transitions, and action logging.
    bool accepted = false;
    RunCameraMode mode = RunCameraMode::Demo;
};

struct RenderDeviceUICommandContext
{
    // Lifetime: borrowed only while one renderer command packet is applied. The
    // lifecycle pointer may be null while backend resources are not active.
    RuntimeRenderer& renderer;
    Rendering::IRenderDeviceLifecycle* deviceLifecycle = nullptr;
};

struct SceneFixedStepUICommandContext
{
    // Lifetime: borrowed only while one Scene-tab fixed-step command is applied.
    // The simulation reset is immediate so the next frame cannot retain old
    // accumulator state under the new tick policy.
    RunSceneState& scene;
    SimulationSystem& simulation;
};

void ApplyWorkerThreadCountOverride( EngineConfig& config,
                                     Threading::WorkerPool& workerPool,
                                     int requestedWorkerThreads );
bool ApplyRenderVsyncUICommand( RenderDeviceUICommandContext context, const UI::UIRendererCommands& commands );
bool ApplySceneFixedStepUICommand( SceneFixedStepUICommandContext context, const UI::UISceneOptionCommands& commands );
RunCameraModeUICommandResult DecodeRunCameraModeUICommand( const UI::UIRunCommands& commands );
RunSimulationUICommandResult ApplyRunSimulationUICommands( RunSimulationUICommandContext context,
                                                           const UI::UISceneOptionCommands& sceneOptions,
                                                           const UI::UIRunCommands& run,
                                                           const UI::UIProfilerCommands& profiler );
void ApplyUIWorldOverride( WorldEnvironment& world,
                           ReplayRuntime& replayRuntime,
                           float gravity,
                           float fluidHeight,
                           float fluidDensity );
bool ApplyWorldWaterUICommands( WorldEnvironment& world,
                                ReplayRuntime& replayRuntime,
                                const UI::UIWaterCommands& commands );
void ApplyCinematicUIParam( CinematicRenderConfig& cinematic,
                            RunSceneState& scene,
                            UICinematicParam param,
                            float rawValue );
void SetCinematicShadowsEnabledFromUI( CinematicRenderConfig& cinematic, RunSceneState& scene, bool enabled );
void ApplyOrdinaryRenderUIParam( OrdinaryRenderConfig& ordinary, UIRenderParam param, float rawValue );
bool ApplySoundUICommands( SoundUICommandContext context, const UI::UISoundCommands& commands );
bool ApplyRuntimeTextOnlyUICommand( RunDebugState& debug, const UI::UISceneOptionCommands& commands );
RuntimePresentationUICommandResult ApplyRuntimePresentationUICommands( RuntimePresentationUICommandContext context,
                                                                       const UI::UISceneOptionCommands& sceneOptions,
                                                                       const UI::UIRenderCommands& renderTuning,
                                                                       const UI::UIWaterCommands& water );
bool ApplyCinematicRenderingToggleUICommand( CinematicUICommandContext context,
                                             const UI::UICinematicCommands& commands );
bool QueueCinematicSkyDefaultsUICommand( CinematicUICommandContext context, const UI::UICinematicCommands& commands );
bool HasCinematicModeUICommand( const UI::UICinematicCommands& commands );
bool ApplyCinematicModeUICommand( SceneRuntimeStyleContext context, const UI::UICinematicCommands& commands );
CinematicTuningUICommandResult ApplyCinematicTuningUICommands( CinematicUICommandContext context,
                                                               const UI::UICinematicCommands& commands );
bool ApplyPhysicsSleepPolicyUICommand( PhysicsSleepPolicyUICommandContext context,
                                       const UI::UIPhysicsCommands& commands );
PhysicsFrictionUICommandResult ApplyPhysicsFrictionUICommands( PhysicsFrictionUICommandContext context,
                                                               const UI::UIPhysicsCommands& commands );
TornadoUICommandResult ApplyTornadoUICommands( TornadoUICommandContext context, const UI::UIPhysicsCommands& commands );
void ToggleCinematicUIFeature( CinematicRenderConfig& cinematic, RunSceneState& scene, UICinematicFeature feature );
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
