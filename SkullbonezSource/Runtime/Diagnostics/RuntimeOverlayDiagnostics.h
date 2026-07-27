/*
File: RuntimeOverlayDiagnostics.h
Purpose:
  Owns debug presentation policy and its physics visualization resources.

Summary:
  RuntimeOverlayDiagnostics is the cohesive process-lifetime owner for debug
  presentation policy and its three visualizers. Mutations cross a stack-only
  value transaction; renderer borrows cross one opaque resource capability.

Glossary:
  Presentation state: Operator-selected overlay, water, terrain, and physics
    debug policy sampled into render values each frame.
  Debug visualizers: CPU-side broadphase, collision, and physics line data
    refreshed after committed physics work and borrowed by RuntimeRenderer.
  Presentation edit: Stack-only copy committed atomically when its scope ends.
  Lifecycle generation: Scene-load attempt identity used to publish a detached
    presentation value at most once after clearing.

Invariants:
  - The owner is allocated only during the explicit Startup phase and lives
    until renderer shutdown has released every borrowed backend resource.
  - Post-physics refresh reads committed scene stores and publishes only
    presentation data; it does not advance simulation or mutate topology.
  - Renderer-facing policy is copied into a value record before submission.
  - No caller receives the owner's mutable policy record or an individual
    visualizer; edits and renderer resources cross typed capability boundaries.

Related:
  - SkullbonezSource/Runtime/App/Run.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - SkullbonezSource/Runtime/App/RunRender.cpp
*/
#pragma once

#include "../Scene/SceneLifecycle.h"

#include <cstdint>
#include <memory>

#include "../Debug/BroadphaseVisualizer.h"
#include "../Debug/CollisionVisualizer.h"
#include "../Debug/PhysicsDebugVisualizer.h"
#include "OverlayDebugState.h"

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class SceneWorld;
class RuntimeValidationHarness;
class RuntimeOverlayDiagnostics;
class RuntimeRenderer;
struct RunLaunchOptions;
struct RunStartupOverrides;
struct RuntimeRenderFramePolicy;

// Lifetime: this edit owns a copy, not a borrow into owner state. Its destructor
// publishes the complete presentation value and synchronizes visualizer policy.
class RuntimeOverlayPresentationEdit
{
  public:
    ~RuntimeOverlayPresentationEdit();
    RuntimeOverlayPresentationEdit( const RuntimeOverlayPresentationEdit& ) = delete;
    RuntimeOverlayPresentationEdit& operator=( const RuntimeOverlayPresentationEdit& ) = delete;
    OverlayDebugState& State();
    void Commit();
    void Refresh();

  private:
    friend class RuntimeOverlayDiagnostics;
    RuntimeOverlayPresentationEdit( RuntimeOverlayDiagnostics& owner, const OverlayDebugState& state );

    RuntimeOverlayDiagnostics& m_owner;
    OverlayDebugState m_state;
};

// Capability: only RuntimeRenderer may unpack these process-lifetime resources.
// Run can pass the binding but cannot recover the individual visualizers.
class RuntimeOverlayRenderResources
{
  private:
    friend class RuntimeOverlayDiagnostics;
    friend class RuntimeRenderer;

    Physics::BroadphaseVisualizer m_broadphaseOverlay;
    Physics::CollisionVisualizer m_collisionOverlay;
    Physics::PhysicsDebugVisualizer m_physicsDebugOverlay;
};

class RuntimeOverlayDiagnostics
{
  public:
    static std::unique_ptr<RuntimeOverlayDiagnostics> CreateForStartup( Core::Profiler* profiler );

    explicit RuntimeOverlayDiagnostics( Core::Profiler* profiler ) : m_profiler( profiler )
    {
    }

    void ApplyStartupPolicy( const RunStartupOverrides& overrides, RunLaunchOptions& launchOptions,
                             UI::InGameUI& operatorUi );
    void UpdatePostPhysics( SceneWorld& scene, RuntimeValidationHarness& validationHarness, float contactEpsilon,
                            double secondsPerFrame );
    RuntimeRenderFramePolicy BuildFramePolicy( double simulationSeconds, double totalSimulationSeconds ) const;

    // Publishes the detached scene presentation once after a load generation
    // reaches the clear boundary. The load transaction never receives this owner.
    void ObserveSceneLifecycle( const SceneLifecyclePacket& packet, const OverlayDebugState& scenePresentation );
    OverlayDebugState PresentationSnapshot() const;
    RuntimeOverlayPresentationEdit EditPresentation();
    RuntimeOverlayRenderResources& RenderResources();

  private:
    friend class RuntimeOverlayPresentationEdit;
    void CommitPresentation( const OverlayDebugState& state );

    // Lifetime: startup-bound diagnostics borrow; null when profiling is disabled.
    Core::Profiler* m_profiler;
    OverlayDebugState m_presentationState;
    RuntimeOverlayRenderResources m_renderResources;
    SceneLifecycleGenerationObserver m_scenePresentationObserver;
};
} // namespace Runtime
} // namespace SkullbonezCore
