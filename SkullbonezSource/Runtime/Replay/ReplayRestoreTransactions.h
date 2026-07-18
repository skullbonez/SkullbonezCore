/*
File: SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h
Purpose:
  Defines frame-scoped transactions used by replay startup and restore.

Summary:
  These operation-specific values are short-lived borrow packets assembled at the application
  composition boundary. They expose only the owners required by one replay
  operation and are never stored.

Glossary:
  Sample restore: Transaction that applies one solver sample to live owners.
  Topology restore: Cold scene rebuild needed when an artifact's body layout
    differs from the current generated scene.

Invariants:
  - Production startup cannot borrow solver or scene-rebuild owners.
  - Sample and topology operands remain separate at the production boundary.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayRestoreService.h
*/
#pragma once

#include "ReplayCoordination.h"
#include "ReplayRestoreService.h"

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Threading
{
class WorkerPool;
}
namespace Runtime
{
class DiagnosticsRuntime;
class SceneController;
class SimulationSystem;
class RuntimeTools;
enum class GeneratedObjectTypeOverride;
struct RunDebugState;
struct RunSceneState;
struct RunSceneUIOverrideState;
// Lifetime: startup replay loading borrows only interaction/camera owners.
// Solver, scene-rebuild, and diagnostic owners are intentionally excluded so
// a normal artifact load cannot gain the debug probe's authority.
struct ReplayStartupLoadInput
{
    double now = 0.0;
    Environment::CameraCollection* cameras = nullptr;
    RunMousePickupState& mousePickup;
    RunCameraMode normalizedCurrentMode = RunCameraMode::Demo;
    const ReplaySceneTimelineResetOwners& timelineOwners;
};

// Concept: live restore composes two narrow operand sets. The sample service
// owns physics/presentation mutation; artifact topology rebuilding separately
// borrows cold scene-construction owners. Neither grants a callback into Run.
struct ReplayRestoreTransaction
{
    ReplaySolverSampleRestoreContext& sampleOwners;
    DiagnosticsRuntime& diagnostics;
    ReplaySceneTimelineResetInput timelineReset;
    const ReplaySceneTimelineResetOwners& timelineOwners;
};

struct ReplayArtifactTopologyOwners
{
    SimulationSystem& simulation;
    const SkullbonezCore::Core::EngineConfig& config;
    Assets::AssetSystem& assets;
    Threading::WorkerPool& workerPool;
    RunSceneUIOverrideState& uiOverrides;
    GeneratedObjectTypeOverride& generatedObjectTypeOverride;
    int sceneObjectCapacity = 0;
};

} // namespace Runtime
} // namespace SkullbonezCore
