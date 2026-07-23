/*
File: SkullbonezSource/Runtime/OperatorCommandApplier.h
Purpose:
  Declares stateless application helpers for one-frame operator commands.

Summary:
  UI emits raw parameter changes. The helpers clamp those values or delegate
  them to bounded owner APIs, update live config, and record scene override bits
  where persistence remains explicit. They retain no cross-frame state.

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
  - Physics config edits are mirrored into SceneWorld immediately so
    existing and newly added bodies share the same runtime policy.
  - Tornado commands mutate Gameplay-owned field/system/visual values in place.
  - This module owns command validation and application only; subsystem state
    remains with the explicitly borrowed render, physics, scene, and
    worker owners.

Related:
  - SkullbonezSource/Runtime/InputRouter.Interactions.cpp
  - SkullbonezSource/UI/UICommands.h
*/
#pragma once

#include "RenderDefaultsStore.h"
#include "OverlayDebugState.h"
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
class Dx12RenderDevice;
}
namespace Runtime
{
class SceneWorld;
class SimulationSystem;
class RuntimeRenderer;
using Environment::WorldEnvironment;
using UI::UICinematicFeature;
using UI::UICinematicParam;
using UI::UIRenderParam;

namespace RunInternal
{
uint64_t CinematicOverrideMaskForUIParam( UICinematicParam param );
uint64_t CinematicOverrideMaskForUIFeature( UICinematicFeature feature );
Math::Vector::Vector3 CinematicSkySunDirection( const SkullbonezCore::Core::CinematicRenderConfig& cinematic );
struct TornadoUICommandContext
{
    // Lifetime: borrowed only while one Physics-tab tornado command packet is applied.
    // The helper applies bounded edits through the SceneWorld-owned Gameplay
    // module; it copies no authored vector and borrows no renderer authority.
    SceneWorld& world;
};

struct PhysicsSleepPolicyUICommandContext
{
    // Lifetime: borrowed only while one Physics-tab sleep-policy toggle is applied.
    // The helper toggles the policy directly on the scene-world physics owner.
    SceneWorld& world;
};

struct PhysicsFrictionUICommandContext
{
    // Lifetime: borrowed only while one Physics-tab friction packet is applied.
    // The helper writes live config and immediately reapplies physics runtime policy.
    SkullbonezCore::Core::EngineConfig& config;
    SceneWorld& world;
};

struct RuntimePresentationUICommandContext
{
    // Lifetime: borrowed only while one scene/render/water UI packet is applied.
    // Simulation-step reset stays in RunInput; this helper owns presentation and
    // render-config mutation plus queued render-default save intent.
    OverlayDebugState& debug;
    SceneSessionState& scene;
    SkullbonezCore::Core::EngineConfig& config;
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
    SceneSessionState& scene;
    SkullbonezCore::Core::CinematicRenderConfig& cinematic;
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
    SceneSessionState& scene;
    RunSceneUIOverrideState& uiOverrides;
    SkullbonezCore::Core::EngineConfig& config;
    Threading::WorkerPool& workerPool;
};

struct RunSimulationUICommandResult
{
    bool setTimeScale = false;
    bool setRunSeed = false;
    bool setWorkerThreads = false;
};

// Value result of one accepted world tuning mutation. Replay may record these
// facts, but the world-tuning owner does not receive replay authority.
struct WorldOverrideChange
{
    float previousGravity = 0.0f;
    float previousFluidHeight = 0.0f;
    float previousFluidDensity = 0.0f;
    float gravity = 0.0f;
    float fluidHeight = 0.0f;
    float fluidDensity = 0.0f;
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
    // device pointer may be null while backend resources are not active.
    RuntimeRenderer& renderer;
    Rendering::Dx12RenderDevice* renderDevice = nullptr;
};

struct SceneFixedStepUICommandContext
{
    // Lifetime: borrowed only while one Scene-tab fixed-step command is applied.
    // The simulation reset is immediate so the next frame cannot retain old
    // accumulator state under the new tick policy.
    SceneSessionState& scene;
    SimulationSystem& simulation;
};

void ApplyWorkerThreadCountOverride( SkullbonezCore::Core::EngineConfig& config,
                                     Threading::WorkerPool& workerPool,
                                     int requestedWorkerThreads );
bool ApplyRenderVsyncUICommand( RenderDeviceUICommandContext context, const UI::UIRendererCommands& commands );
bool ApplySceneFixedStepUICommand( SceneFixedStepUICommandContext context, const UI::UISceneOptionCommands& commands );
RunCameraModeUICommandResult DecodeRunCameraModeUICommand( const UI::UIRunCommands& commands );
RunSimulationUICommandResult ApplyRunSimulationUICommands( RunSimulationUICommandContext context,
                                                           const UI::UISceneOptionCommands& sceneOptions,
                                                           const UI::UIRunCommands& run,
                                                           const UI::UIProfilerCommands& profiler );
WorldOverrideChange
ApplyUIWorldOverride( WorldEnvironment& world, float gravity, float fluidHeight, float fluidDensity );
bool ApplyWorldWaterUICommands( WorldEnvironment& world,
                                const UI::UIWaterCommands& commands,
                                WorldOverrideChange& outChange );
void ApplyCinematicUIParam( SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                            SceneSessionState& scene,
                            UICinematicParam param,
                            float rawValue );
void SetCinematicShadowsEnabledFromUI( SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                       SceneSessionState& scene,
                                       bool enabled );
void ApplyOrdinaryRenderUIParam( SkullbonezCore::Core::OrdinaryRenderConfig& ordinary,
                                 UIRenderParam param,
                                 float rawValue );
bool ApplyRuntimeTextOnlyUICommand( OverlayDebugState& debug, const UI::UISceneOptionCommands& commands );
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
void ToggleCinematicUIFeature( SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                               SceneSessionState& scene,
                               UICinematicFeature feature );
} // namespace RunInternal
} // namespace Runtime
} // namespace SkullbonezCore
