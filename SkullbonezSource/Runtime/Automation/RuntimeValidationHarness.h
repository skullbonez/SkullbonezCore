/*
File: RuntimeValidationHarness.h
Purpose:
  Owns opt-in live-style and deterministic graphics-stress harness state.

Summary:
  RuntimeValidationHarness groups external validation controls that mutate or
  observe runtime state. Run sequences startup, frame, capture, scene-reload,
  gate observation, and exit operations without retaining concrete controller
  state elsewhere. Scene-dependent validation state reacts once to the
  SceneController-owned lifecycle generation.

Glossary:
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
  - Gate replacement and graphics-stress resume each run at most once for their
    relevant phase of a lifecycle generation.

Related:
  - SkullbonezSource/Runtime/App/Run.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "../Capture/GraphicsStressController.h"
#include "../Direction/LiveStyleController.h"
#include "../Scene/SceneAutomationGateConfiguration.h"
#include "../Scene/SceneLifecycle.h"
#include "../../Physics/PhysicsBroadphaseDebugView.h"
#include "../../Physics/PhysicsDebugData.h"

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Core
{
class SbResult;
} // namespace Core
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
} // namespace Physics
namespace Runtime
{
class AuthoredScene;
class ReplayRuntime;
class SceneController;
struct RunLaunchOptions;
struct RunStartupOverrides;

struct SceneAutomationGatePhysicsView
{
    // Lifetime: App constructs this immutable post-physics view for one
    // synchronous observation; the tracker stores no scene or store pointer.
    const Physics::PhysicsBodyStore& bodyStore;
    const Physics::ColliderStore& colliderStore;
    std::span<const Physics::PhysicsDebugContact> debugContacts;
};

// Owner: validation harness. These rows are automation observations, not scene
// topology. Authored setup appends resolved requirements through commands and
// frame code can only update/query the private rows through this typed owner.
class SceneAutomationGateTracker
{
  public:
    void ObserveSceneLifecycle( const SceneLifecyclePacket& packet, SceneAutomationGateConfiguration&& configuration );

    void UpdateRequiredContacts( SceneAutomationGatePhysicsView physics, float contactEpsilon );
    void UpdateRequiredSleepingDynamicBodies( std::span<const uint8_t> awakeBodies );

    // Returns true only while an authored broadphase requirement still needs a
    // live active-cell observation; callers may skip snapshot copies otherwise.
    bool RequiresBroadphaseXCellObservation() const;
    void UpdateRequiredBroadphaseXCells( std::span<const Physics::PhysicsBroadphaseActiveCell> activeCells );
    SceneAutomationGateStatus Status() const;
    void PrintMissingRequirements() const;

  private:
    void ApplyConfiguration( SceneAutomationGateConfiguration configuration );
    bool RequiredContactsComplete() const;
    bool RequiredSleepingDynamicBodiesComplete() const;
    bool RequiredBroadphaseXCellsComplete() const;

    SceneAutomationGateConfiguration m_configuration;
    SceneLifecycleGenerationObserver m_sceneLifecycleObserver;
};

class RuntimeValidationHarness
{
  public:
    explicit RuntimeValidationHarness( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics )
        : m_resultDiagnostics( resultDiagnostics )
    {
    }

    static std::unique_ptr<RuntimeValidationHarness>
    CreateForStartup( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics );

    bool ConfigureStartup( const RunStartupOverrides& overrides, RunLaunchOptions& launchOptions );
    void MarkLiveStyleReady();
    bool PollLiveStyle( const Assets::AssetSystem& assets, AuthoredScene& outStyle );
    void MarkLiveStyleApplied();
    bool HasPendingLiveStyleCapture() const;
    const char* PendingLiveStyleCapturePath() const;
    void CompleteLiveStyleCapture( const SkullbonezCore::Core::SbResult& result );

    void ObserveSceneLifecycle( const SceneLifecyclePacket& packet, const RunLaunchOptions& launchOptions );
    void PrintGraphicsStressExitSummary( int currentSceneFrame ) const;
    GraphicsStressController& GraphicsStress()
    {
        return m_graphicsStress;
    }
    const GraphicsStressController& GraphicsStress() const
    {
        return m_graphicsStress;
    }
    SceneAutomationGateTracker& SceneGates();

  private:
    void ResumeGraphicsStressAfterSceneLoad( const RunLaunchOptions& launchOptions );
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    LiveStyleController m_liveStyle;
    GraphicsStressController m_graphicsStress;
    SceneAutomationGateTracker m_sceneGates;
    SceneLifecycleGenerationObserver m_graphicsStressSceneObserver;
};
} // namespace Runtime
} // namespace SkullbonezCore
