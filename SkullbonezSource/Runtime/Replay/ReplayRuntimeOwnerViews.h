/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntimeOwnerViews.h
Purpose:
  Defines frame-scoped owner views used by replay startup, restore, and probes.

Mental model:
  ReplayRuntime owns replay decisions while Run owns application composition.
  These values are short-lived borrow packets assembled at that boundary; they
  expose only the owners required by one replay operation and are never stored.

Glossary:
  Sample restore: Transaction that applies one solver sample to live owners.
  Topology restore: Cold scene rebuild needed when an artifact's body layout
    differs from the current generated scene.
  Probe world: Debug-only whole-scene fixture used by named automation probes.

Invariants:
  - Production startup cannot borrow solver or scene-rebuild owners.
  - Sample and topology operands remain separate at the production boundary.
  - ReplayProbeWorld is unavailable outside Debug builds.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayRestoreService.h
*/
#pragma once

#include "ReplayRuntime.h"

namespace SkullbonezCore
{
namespace Basics
{
// Lifetime: startup replay loading borrows only interaction/camera owners.
// Solver, scene-rebuild, and diagnostic owners are intentionally excluded so
// a normal artifact load cannot gain the debug probe's authority.
struct ReplayRuntime::ReplayStartupLoadInput
{
    double now = 0.0;
    Environment::CameraCollection* cameras = nullptr;
    RunMousePickupState& mousePickup;
    RunCameraMode normalizedCurrentMode = RunCameraMode::Demo;
    const SceneTimelineResetOwners& timelineOwners;
};

// Concept: live restore composes two narrow operand sets. The sample service
// owns physics/presentation mutation; artifact topology rebuilding separately
// borrows cold scene-construction owners. Neither grants a callback into Run.
struct ReplayRuntime::ReplayRestoreTransaction
{
    ReplaySolverSampleRestoreContext& sampleOwners;
    DiagnosticsRuntime& diagnostics;
    SceneTimelineResetInput timelineReset;
    const SceneTimelineResetOwners& timelineOwners;
};

struct ReplayRuntime::ReplayArtifactTopologyOwners
{
    SimulationSystem& simulation;
    const EngineConfig& config;
    RunSubsystemState& systems;
    GeneratedObjectTypeOverride& generatedObjectTypeOverride;
    int gameModelCapacity = 0;
};

#ifdef _DEBUG
// Debug automation needs a deliberately broad composition fixture to build
// and inspect whole scenes. It is unavailable in production builds and borrowed
// only while a named replay probe runs.
struct ReplayProbeWorld
{
    GameObjects::GameModelCollection& models;
    Environment::WorldEnvironment& world;
    RunSceneState& scene;
    RunRuntimeSettings& runtimeSettings;
    RunDebugState& debug;
    Environment::CameraCollection* cameras = nullptr;
    RuntimeTools& runtimeTools;
    SceneController& sceneController;
    SimulationSystem& simulation;
    const EngineConfig& config;
    RunSubsystemState& systems;
    GeneratedObjectTypeOverride& generatedObjectTypeOverride;
    int gameModelCapacity = 0;
    DiagnosticsRuntime& diagnostics;
    RunMousePickupState& mousePickup;
    RunCameraMode normalizedCurrentMode = RunCameraMode::Demo;
    double now = 0.0;
    ReplayRuntime::SceneTimelineResetInput timelineReset;
    ReplayRuntime::SceneTimelineResetOwners timelineOwners;
};
#endif
} // namespace Basics
} // namespace SkullbonezCore
