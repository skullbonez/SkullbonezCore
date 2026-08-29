/*
File: SkullbonezSource/Runtime/Startup/RunLaunchOptions.h
Purpose:
  Owns CLI/startup launch policy that Run reapplies across scene loads.

Summary:
  Launch options are process-start requests, not live subsystem authority. Run
  captures them once from CLI/UI startup paths, then scene loading and runtime
  tuning read the values when they need to restore deterministic overrides after
  a reload.

Glossary:
  Launch override: CLI/startup value that should be applied again when a scene
  reloads or generated demo content is rebuilt.
  Allocation guard mode: Runtime allocation measurement mode selected before
  steady gameplay begins.
  Generated object type override: Debug/validation selector that forces
  generated demo scenes to all balls or all boxes.
  Development UI mode: Process-lifetime selection between the built-in GameUI
    game/level-editor surface and optional ImGui development surface; omitted
    command-line input selects GameUI.

Invariants:
  - These values are policy inputs; subsystems own the live state they modify.
  - Zero or false generally means "not provided" so scene defaults keep working.
  - Physics debug overrides affect visualization only and must not change solver
    ordering or deterministic physics state.
  - Development UI modes are exclusive; there is no simultaneous Both state.

Related:
  - SkullbonezSource/Runtime/App/Run.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Physics/PhysicsDebugData.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"

#include <cstdint>
#include <cstddef>

namespace SkullbonezCore
{
namespace Runtime
{
enum class GeneratedObjectTypeOverride
{
    Mixed,
    AllBalls,
    AllBoxes
};

enum class StartupOverlayMode : uint8_t
{
    None,
    Timers
};

struct RuntimeRendererOption
{
    const char* name;
    const char* alias;
};

inline constexpr RuntimeRendererOption kRuntimeRendererOptions[] = {
    { "dx12", "d3d12" },
};

inline constexpr std::size_t kRuntimeRendererOptionCount = sizeof( kRuntimeRendererOptions ) /
                                                           sizeof( kRuntimeRendererOptions[0] );

enum class DevelopmentUiMode : uint8_t
{
    GameUI = 0,
    ImGui
};

constexpr bool DevelopmentUiModeShowsGameUI( DevelopmentUiMode mode ) noexcept
{
    return mode == DevelopmentUiMode::GameUI;
}

constexpr bool DevelopmentUiModeShowsImGui( DevelopmentUiMode mode ) noexcept
{
    return mode == DevelopmentUiMode::ImGui;
}

struct RunLaunchOptions
{
    float timeScaleOverride = 0.0f; // CLI --time-scale override applied after each scene load (0 = not set)
    bool fixedStep = false; // Explicit startup render-frame-lockstep request (flag or deterministic diagnostic/probe policy)
    unsigned int seedOverride = 0; // CLI --seed override applied after each scene load (0 = not set)
    bool noWater = false;          // CLI --no-water starts fluid below terrain
    bool noSleep = false;          // Startup CLI --no-sleep request; live policy can still be toggled from the Physics tab
    bool hasTornadoOverride = false;
    bool tornadoEnabled = false;
    bool tornadoVectors = false;
    bool hasCinematicRenderingOverride = false;
    bool cinematicRendering = false;
    bool hasCinematicShadowsOverride = false;
    bool cinematicShadows = false;
    bool demoHeroStyle = false;                    // CLI --demohero applies the low-poly hero look to generated demo mode
    bool dumpTextureAssets = false;                // CLI --dump-assets prints the startup-built texture registry once.
    bool interactiveSceneRun = false;              // CLI --interactive/--hold keeps scene automation from quitting the app
    int frameCountOverride = -1;                   // CLI --frames override applied after each scene load
    char perfLogPath[256] = {};                    // CLI --perf-log override reapplied after each scene load
    bool uiStress = false;                         // CLI --ui-stress enables generated/demo stress without a scene file
    unsigned int uiStressSeed = 0;                 // CLI --ui-stress-seed
    int uiStressActions = 5;                       // CLI --ui-stress-actions
    bool graphicsStress = false;                   // CLI --graphics-stress enables render-setting and scene-load churn
    unsigned int graphicsStressSeed = 0;           // CLI --graphics-stress-seed
    int graphicsStressActions = 12;                // CLI --graphics-stress-actions
    int graphicsStressSceneIntervalFrames = 45;    // CLI --graphics-stress-scene-interval
    int graphicsStressMemoryIntervalFrames = 1800; // CLI --graphics-stress-memory-interval
    bool replayGuideArcsAtStartup = false; // CLI --guide-arcs re-enables the default-off GameUI guide after scene load.
    int interactionRecordMaxMinutes = 1;   // F8 tape hard limit; 1..60 minute chunks.

    // Concept: a prediction launch request, not live prediction authority.
    // ReplayPrediction still owns enablement, horizon, and the build; Run only
    // replays the operator's scrubber/predict/target/pause sequence once the
    // scene has bodies. Empty predictTargetName means --predict was omitted.
    char predictTargetName[64] = {};    // CLI --predict <body display name>
    float predictHorizonSeconds = 0.0f; // CLI --predict-seconds (0 = owner default)
    bool predictPauseOnStart = true;    // CLI --predict-running clears this

    // Runtime allocation policy: startup selects measurement/fatal behavior
    // before owner registration and steady gameplay begin.
    SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode
        allocationGuardMode = SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode::Off; // CLI --allocation-guard

    GeneratedObjectTypeOverride generatedObjectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    bool hasPhysicsDebugFlagsOverride = false;
    uint32_t physicsDebugFlagsOverride = Physics::PHYSICS_DEBUG_NONE;
    bool hasPhysicsDebugTransparentOverride = false;
    bool physicsDebugTransparentOverride = false;
    bool hasPhysicsDebugAlphaOverride = false;
    float physicsDebugAlphaOverride = 0.28f;
    bool hasPhysicsDebugContactLingerOverride = false;
    float physicsDebugContactLingerOverride = 0.45f;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // CLI --dev-ui imgui opts into the docked editor; omitted remains GameUI.
    // Invariant: exactly one development UI owns window focus and input for a
    // process and there is no parallel/Both mode.
    DevelopmentUiMode developmentUiMode = DevelopmentUiMode::GameUI;
    bool developmentUiModeExplicit = false;
#endif
};

struct RunStartupOverrides
{
    // Concept: Init builds this packet from parsed CLI state and immediately
    // hands it to Run. Path pointers borrow ParsedArgs storage only for that
    // synchronous apply call; Run-owned systems copy or consume paths before it
    // returns.
    RunLaunchOptions launch;
    const char* liveStyleControlDirectory = nullptr; // CLI --live-style-control-dir
    const char* mainMemoryDumpPath = nullptr;        // CLI --memory-dump
    const char* interactionScriptPath = nullptr;     // CLI interaction harness script copied by its owner.
    const char* interactionReportPath = nullptr;     // Optional interaction report destination.
    const char* interactionTracePath = nullptr;      // Optional incremental JSONL turn trace.
    const char* interactionRecordPath = nullptr;     // CLI --record-automation output destination.
    int interactionRecordMaxMinutes = 1;             // CLI recorder startup copy of the same launch limit.
    bool configureReplayRecording = false;           // True when replay capture or hash logging must be configured
    bool replayRecordingEnabled = true;
    int replayRetentionSeconds = 0;
    const char* replayHashLogPath = nullptr;
    const char* replayLoadPath = nullptr;
    bool replayLoadProbe = false;
#ifdef _DEBUG
    const char* replayRestoreFileProbePath = nullptr;
    const char* replayRestoreTargetFileProbePath = nullptr;
    const char* replayRestoreBranchFileProbePath = nullptr;
    const char* replayRestoreFailureFileProbePath = nullptr;
#endif
    bool hasInitialOverlayMode = false;
    StartupOverlayMode initialOverlayMode = StartupOverlayMode::None;
    bool hideTopText = false;
    bool showBroadphaseVisualizer = false;

#ifdef _DEBUG
    bool replayScrubProbe = false;
    float replayScrubProbeNormalized = 0.25f;
    bool replayRestoreProbe = false;
    float replayRestoreProbeNormalized = 0.25f;
    bool replaySaveProbe = false;
    const char* replaySaveProbePath = nullptr;
    const char* physicsRegressionLogPath = nullptr;
    const char* physicsCollisionTimeLogPath = nullptr;
    const char* physicsDiagnosticsPath = nullptr;
    bool physicsDiagnosticsRenderFrameLockstepForced = false; // True when --physics-diag supplied the explicit

    // render-frame-lockstep request.
#endif
};

} // namespace Runtime
} // namespace SkullbonezCore
