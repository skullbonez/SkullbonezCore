/*
File: RuntimeValidationHarness.h
Purpose:
  Owns authored scene-gate validation state.

Summary:
  RuntimeValidationHarness retains only Automation-owned scene requirements.
  App owns and sequences live-style and graphics-stress controllers because
  those controllers apply effects across Direction, Capture, Scene, and Render.

Glossary:
  Scene gate: Authored validation requirement that observes committed physics
    state without becoming scene business state.
  Resume: Scene-load transition that preserves the stress random stream and
    counters while restoring launch cadence.

Invariants:
  - The owner is allocated only during Startup and lives for the process.
  - Scene-gate rows are private to the harness and rebuilt for each load.
  - Gate replacement runs at most once for its relevant lifecycle generation.

Related:
  - SkullbonezSource/Runtime/App/Run.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - SkullbonezSource/Runtime/App/GraphicsStressApplication.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "../Scene/SceneAutomationGateConfiguration.h"
#include "../Scene/SceneLifecycle.h"
#include "../../Physics/PhysicsBroadphaseDebugView.h"
#include "../../Physics/PhysicsDebugData.h"

namespace SkullbonezCore
{
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
    static std::unique_ptr<RuntimeValidationHarness> CreateForStartup();
    SceneAutomationGateTracker& SceneGates();

  private:
    SceneAutomationGateTracker m_sceneGates;
};
} // namespace Runtime
} // namespace SkullbonezCore
