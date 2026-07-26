/*
File: SkullbonezSource/Runtime/Interaction/OperatorCommandApplier.h
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
  - SkullbonezSource/Runtime/App/InputFrame.cpp
  - SkullbonezSource/UI/UICommands.h
*/
#pragma once

#include "../Render/RenderDefaultsStore.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../Scene/SceneRuntimeStyle.h"
#include "../../UI/UICommands.h"

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
    // reporting in InputFrame mirrors the accepted UI packet.
    int applySettingsActionCount = 0;
};

struct RuntimePresentationUICommandResult
{

    // Invariant: flags report accepted UI commands for InputFrame transition
    // recording; they are not change-detection flags for render/debug state.
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

    // Invariant: flags report accepted UI commands for InputFrame transition
    // recording; feature/param setters may clamp or no-op invalid enum values.
    bool toggledFeature = false;
    bool appliedParam = false;
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

    // Invariant: accepted means mode is a real enum value. InputRouter applies
    // normalization/pointer transitions; InputController records the action.
    bool accepted = false;
    RunCameraMode mode = RunCameraMode::Demo;
};

void ApplyWorkerThreadCountOverride( SkullbonezCore::Core::EngineConfig& config, Threading::WorkerPool& workerPool,
                                     int requestedWorkerThreads );
bool ApplyRenderVsyncUICommand( RuntimeRenderer& renderer, Rendering::Dx12RenderDevice* renderDevice,
                                const UI::UIRendererCommands& commands );
bool ApplySceneFixedStepUICommand( SceneSessionState& scene, SimulationSystem& simulation,
                                   const UI::UISceneOptionCommands& commands );
RunCameraModeUICommandResult DecodeRunCameraModeUICommand( const UI::UIRunCommands& commands );
RunSimulationUICommandResult
ApplyRunSimulationUICommands( SceneSessionState& scene, SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                              SkullbonezCore::Core::EngineConfig& config, Threading::WorkerPool& workerPool,
                              const UI::UISceneOptionCommands& sceneOptions, const UI::UIRunCommands& run,
                              const UI::UIProfilerCommands& profiler );
WorldOverrideChange ApplyUIWorldOverride( WorldEnvironment& world, float gravity, float fluidHeight, float fluidDensity );
bool ApplyWorldWaterUICommands( WorldEnvironment& world, const UI::UIWaterCommands& commands,
                                WorldOverrideChange& outChange );
void ApplyCinematicUIParam( SkullbonezCore::Core::CinematicRenderConfig& cinematic, SceneSessionState& scene,
                            UICinematicParam param, float rawValue );
void SetCinematicShadowsEnabledFromUI( SkullbonezCore::Core::CinematicRenderConfig& cinematic, SceneSessionState& scene,
                                       bool enabled );
void ApplyOrdinaryRenderUIParam( SkullbonezCore::Core::OrdinaryRenderConfig& ordinary, UIRenderParam param, float rawValue );
bool ApplyRuntimeTextOnlyUICommand( OverlayDebugState& debug, const UI::UISceneOptionCommands& commands );
RuntimePresentationUICommandResult
ApplyRuntimePresentationUICommands( OverlayDebugState& debug, SceneSessionState& scene,
                                    SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions,
                                    RenderDefaultsStore& renderDefaults, bool graphicsReady, double simulationSeconds,
                                    const UI::UISceneOptionCommands& sceneOptions, const UI::UIRenderCommands& renderTuning,
                                    const UI::UIWaterCommands& water );
bool ApplyCinematicRenderingToggleUICommand( RunLaunchOptions& launchOptions, SceneSessionState& scene,
                                             SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                             const UI::UICinematicCommands& commands );
bool QueueCinematicSkyDefaultsUICommand( RenderDefaultsStore& renderDefaults, const UI::UICinematicCommands& commands );
bool HasCinematicModeUICommand( const UI::UICinematicCommands& commands );
bool ApplyCinematicModeUICommand( SceneRuntimeStyleContext context, const UI::UICinematicCommands& commands );
CinematicTuningUICommandResult ApplyCinematicTuningUICommands( RunLaunchOptions& launchOptions, SceneSessionState& scene,
                                                               SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                                               const UI::UICinematicCommands& commands );
bool ApplyPhysicsSleepPolicyUICommand( SceneWorld& world, const UI::UIPhysicsCommands& commands );
PhysicsFrictionUICommandResult ApplyPhysicsFrictionUICommands( SkullbonezCore::Core::EngineConfig& config, SceneWorld& world,
                                                               const UI::UIPhysicsCommands& commands );
TornadoUICommandResult ApplyTornadoUICommands( SceneWorld& world, const UI::UIPhysicsCommands& commands );
void ToggleCinematicUIFeature( SkullbonezCore::Core::CinematicRenderConfig& cinematic, SceneSessionState& scene,
                               UICinematicFeature feature );
} // namespace RunInternal
} // namespace Runtime
} // namespace SkullbonezCore
