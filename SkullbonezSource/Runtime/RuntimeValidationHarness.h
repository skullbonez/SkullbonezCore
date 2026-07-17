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
#include <vector>

#include "GraphicsStressController.h"
#include "LiveStyleController.h"
#include "../Physics/SpatialGrid.h"

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCaptureBackend;
class IRenderDiagnostics;
} // namespace Rendering
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

class SceneController;

// Owner: validation harness. These rows are automation observations, not scene
// topology. Authored setup appends resolved requirements through commands and
// frame code can only update/query the private rows through this typed owner.
class SceneAutomationGateTracker
{
  public:
    void ResetForLoad();
    void ReserveRequiredContacts( std::size_t count );
    void AppendRequiredContact( const char* nameA, const char* nameB, int bodyA, int bodyB );
    void ReserveRequiredBroadphaseXCells( std::size_t count );
    void AppendRequiredBroadphaseXCells( int minCellX, int maxCellX, int cellY, int cellZ );

    void UpdateRequiredContacts( SceneController& scene, float contactEpsilon );
    void UpdateRequiredBroadphaseXCells( const Math::CollisionDetection::SpatialGrid::ActiveCell* activeCells,
                                         int activeCellCount );
    bool HasRequirements() const;
    bool Complete() const;
    void PrintMissingRequirements() const;

  private:
    struct RequiredContactState
    {
        char nameA[64] = {};
        char nameB[64] = {};
        int bodyA = -1;
        int bodyB = -1;
        bool touched = false;
    };

    struct RequiredBroadphaseXCellsState
    {
        int minCellX = 0;
        int maxCellX = 0;
        int cellY = 0;
        int cellZ = 0;
        int lastActiveCellCount = 0;
        int lastObservedMinX = 0;
        int lastObservedMaxX = 0;
        int lastMissingCellX = -1;
        bool hasObservedXRange = false;
        bool activated = false;
    };

    bool RequiredContactsComplete() const;
    bool RequiredBroadphaseXCellsComplete() const;

    std::vector<RequiredContactState> m_requiredContacts;
    std::vector<RequiredBroadphaseXCellsState> m_requiredBroadphaseXCells;
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
