/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntimeOwnerViews.h
Purpose:
  Defines frame-scoped owner views used by replay startup and restore.

Summary:
  ReplayRuntime owns replay decisions while Run owns application composition.
  These values are short-lived borrow packets assembled at that boundary; they
  expose only the owners required by one replay operation and are never stored.

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

#include "ReplayRuntime.h"

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
    Assets::AssetSystem& assets;
    Threading::WorkerPool& workerPool;
    GeneratedObjectTypeOverride& generatedObjectTypeOverride;
    int gameModelCapacity = 0;
};

} // namespace Basics
} // namespace SkullbonezCore
