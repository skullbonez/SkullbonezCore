/*
File: RuntimeValidationHarness.h
Purpose:
  Owns opt-in live-style and deterministic graphics-stress harness state.

Summary:
  RuntimeValidationHarness groups external validation controls that mutate or
  observe runtime state. Run sequences startup, frame, capture, scene-reload,
  gate observation, and exit operations without retaining concrete controller
  state elsewhere.

Glossary:
  Live style: Control-folder protocol that applies style JSON and requests a
    screenshot without restarting the process.
  Graphics stress: Seeded DX12/runtime churn used to reproduce resource and
    lifetime faults deterministically.
  Scene gate: Authored validation requirement that observes committed physics
    state without becoming scene business state.
  Resume: Scene-load transition that preserves the stress random stream and
    counters while restoring launch cadence.

Invariants:
  - The owner is allocated only during Startup and lives for the process.
  - Live-style polling stays in the input phase; capture consumption stays
    after render and UI submission.
  - Graphics-stress random state advances only through the owned controller.
  - Scene reload resumes stress without resetting its persistent counters.
  - Scene-gate rows are private to the harness and rebuilt for each load.

Related:
  - SkullbonezSource/Runtime/Run.cpp
  - SkullbonezSource/Runtime/RunFrame.cpp
  - SkullbonezSource/Runtime/RuntimeStressController.cpp
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
*/
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "GraphicsStressController.h"
#include "LiveStyleController.h"
#include "Scene/SceneAutomationGateConfiguration.h"
#include "../Physics/SpatialGrid.h"
#include "../Physics/PhysicsDebugData.h"

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCaptureBackend;
class IRenderDiagnostics;
} // namespace Rendering
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
} // namespace Physics
namespace Runtime
{
class CaptureController;
class ReplayRuntime;
struct RuntimeFrameHostView;
struct RuntimeFrameInteractionView;
struct RuntimeFramePresentationView;
struct RuntimeFrameSceneView;
struct RunLaunchOptions;
struct RunStartupOverrides;
struct SceneRuntimeStyleContext;

struct SceneAutomationGatePhysicsView
{
    // Lifetime: post-physics diagnostics constructs this immutable view for one
    // synchronous observation; the tracker stores no scene or store pointer.
    const Physics::PhysicsBodyStore& bodyStore;
    const Physics::ColliderStore& colliderStore;
    std::span<const Physics::PhysicsDebugContact> debugContacts;
};

struct SceneAutomationGateStatus
{
    // Value-only completion facts consumed by scene advancement. Diagnostic
    // row ownership and missing-requirement reporting remain in validation.
    bool hasRequirements = false;
    bool complete = true;
};

// Owner: validation harness. These rows are automation observations, not scene
// topology. Authored setup appends resolved requirements through commands and
// frame code can only update/query the private rows through this typed owner.
class SceneAutomationGateTracker
{
  public:
    void ApplyConfiguration( SceneAutomationGateConfiguration configuration );

    void UpdateRequiredContacts( SceneAutomationGatePhysicsView physics, float contactEpsilon );
    void UpdateRequiredBroadphaseXCells( const Math::CollisionDetection::SpatialGrid::ActiveCell* activeCells,
                                         int activeCellCount );
    SceneAutomationGateStatus Status() const;
    void PrintMissingRequirements() const;

  private:
    bool RequiredContactsComplete() const;
    bool RequiredBroadphaseXCellsComplete() const;

    SceneAutomationGateConfiguration m_configuration;
};

class RuntimeValidationHarness
{
  public:
    static std::unique_ptr<RuntimeValidationHarness> CreateForStartup();

    bool ConfigureStartup( const RunStartupOverrides& overrides, RunLaunchOptions& launchOptions );
    void MarkLiveStyleReady();
    void TickLiveStyle( SceneRuntimeStyleContext context );
    bool HasPendingLiveStyleCapture() const;
    void SavePendingLiveStyleCapture( CaptureController& capture, Rendering::IRenderCaptureBackend& backend );

    void ResumeGraphicsStressAfterSceneLoad( const RunLaunchOptions& launchOptions );
    void PrintGraphicsStressExitSummary( int currentSceneFrame ) const;
    void ExecuteGraphicsStressFrame( RuntimeFrameHostView& host,
                                     RuntimeFrameInteractionView& interactionOwners,
                                     RuntimeFrameSceneView& sceneOwners,
                                     RuntimeFramePresentationView& presentationOwners,
                                     ReplayRuntime& replayRuntime,
                                     const Rendering::IRenderDiagnostics& renderDiagnostics );
    SceneAutomationGateTracker& SceneGates();
    const SceneAutomationGateTracker& SceneGates() const;

  private:
    LiveStyleController m_liveStyle;
    GraphicsStressController m_graphicsStress;
    SceneAutomationGateTracker m_sceneGates;
};
} // namespace Runtime
} // namespace SkullbonezCore
