/*
File: SkullbonezSource/Runtime/RunLaunchOptions.h
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
  Development UI mode: Process-lifetime selection between the Legacy and ImGui
    operator surfaces; omitted command-line input selects Legacy.

Invariants:
  - These values are policy inputs; subsystems own the live state they modify.
  - Zero or false generally means "not provided" so scene defaults keep working.
  - Physics debug overrides affect visualization only and must not change solver
    ordering or deterministic physics state.
  - Development UI modes are exclusive; there is no simultaneous Both state.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Physics/PhysicsDebugData.h"
#include "../Core/Allocation/RuntimeAllocationTracker.h"
#include "RunDebugState.h"
#include "Scene/SceneGeneratedSetup.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
enum class DevelopmentUiMode : uint8_t
{
    Legacy = 0,
    ImGui
};

constexpr bool DevelopmentUiModeShowsLegacy( DevelopmentUiMode mode ) noexcept
{
    return mode == DevelopmentUiMode::Legacy;
}

constexpr bool DevelopmentUiModeShowsImGui( DevelopmentUiMode mode ) noexcept
{
    return mode == DevelopmentUiMode::ImGui;
}
#endif

struct RunLaunchOptions
{
    float timeScaleOverride = 0.0f;                           // CLI --time-scale override applied after each scene load (0 = not set)
    bool fixedStep = false;                                   // CLI --fixed-step override applied after each scene load
    unsigned int seedOverride = 0;                            // CLI --seed override applied after each scene load (0 = not set)
    bool noWater = false;                                     // CLI --no-water starts fluid below terrain
    bool noSleep = false;                                     // Startup CLI --no-sleep request; live policy can still be toggled from the Physics tab
    bool hasTornadoOverride = false;
    bool tornadoEnabled = false;
    bool tornadoVectors = false;
    bool hasCinematicRenderingOverride = false;
    bool cinematicRendering = false;
    bool hasCinematicShadowsOverride = false;
    bool cinematicShadows = false;
    bool demoHeroStyle = false;                               // CLI --demohero applies the low-poly hero look to generated demo mode
    bool dumpTextureAssets = false;                           // CLI --dump-assets prints the startup-built texture registry once.
    bool interactiveSceneRun = false;                         // CLI --interactive/--hold keeps scene automation from quitting the app
    int frameCountOverride = -1;                              // CLI --frames override applied after each scene load
    bool uiStress = false;                                    // CLI --ui-stress enables generated/demo stress without a scene file
    unsigned int uiStressSeed = 0;                            // CLI --ui-stress-seed
    int uiStressActions = 5;                                  // CLI --ui-stress-actions
    bool graphicsStress = false;                              // CLI --graphics-stress enables render-setting and scene-load churn
    unsigned int graphicsStressSeed = 0;                      // CLI --graphics-stress-seed
    int graphicsStressActions = 12;                           // CLI --graphics-stress-actions
    int graphicsStressSceneIntervalFrames = 45;               // CLI --graphics-stress-scene-interval
    int graphicsStressMemoryIntervalFrames = 1800;            // CLI --graphics-stress-memory-interval
    Runtime::Allocation::RuntimeAllocationGuardMode allocationGuardMode =
        Runtime::Allocation::RuntimeAllocationGuardMode::Off; // CLI --allocation-guard tracking mode for runtime heap
                                                              // evidence.
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
    // CLI --dev-ui imgui opts into the docked editor; omitted remains Legacy.
    // Invariant: exactly one development UI owns window focus and input for a
    // process and there is no parallel/Both mode.
    DevelopmentUiMode developmentUiMode = DevelopmentUiMode::Legacy;
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
    const char* liveStyleControlDirectory = nullptr;          // CLI --live-style-control-dir
    const char* mainMemoryDumpPath = nullptr;                 // CLI --memory-dump
    const char* interactionScriptPath = nullptr;              // CLI interaction harness script copied by its owner.
    const char* interactionReportPath = nullptr;              // Optional interaction report destination.
    bool configureReplayRecording = false;                    // True when replay capture or hash logging must be configured
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
    OverlayMode initialOverlayMode = OverlayMode::None;
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
    bool physicsDiagnosticsFixedStepForced = false;
#endif
};

} // namespace Runtime
} // namespace SkullbonezCore
