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

Invariants:
  - The owner is allocated only during the explicit Startup phase and lives
    until renderer shutdown has released every borrowed backend resource.
  - Post-physics refresh reads committed scene stores and publishes only
    presentation data; it does not advance simulation or mutate topology.
  - Renderer-facing policy is copied into a value record before submission.
  - No caller receives the owner's mutable policy record or an individual
    visualizer; edits and renderer resources cross typed capability boundaries.

Related:
  - SkullbonezSource/Runtime/Run.cpp
  - SkullbonezSource/Runtime/RunFrame.cpp
  - SkullbonezSource/Runtime/RunRender.cpp
  - SkullbonezSource/Runtime/RuntimeFrameViews.h
*/
#pragma once

#include <memory>

#include "Debug/BroadphaseVisualizer.h"
#include "Debug/CollisionVisualizer.h"
#include "Debug/PhysicsDebugVisualizer.h"
#include "RunDebugState.h"

namespace SkullbonezCore
{
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class SceneController;
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
    RunDebugState& State();
    void Commit();
    void Refresh();

  private:
    friend class RuntimeOverlayDiagnostics;
    RuntimeOverlayPresentationEdit( RuntimeOverlayDiagnostics& owner, const RunDebugState& state );

    RuntimeOverlayDiagnostics& m_owner;
    RunDebugState m_state;
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
    static std::unique_ptr<RuntimeOverlayDiagnostics> CreateForStartup();

    void ApplyStartupPolicy( const RunStartupOverrides& overrides,
                             RunLaunchOptions& launchOptions,
                             UI::InGameUI& operatorUi );
    void UpdatePostPhysics( SceneController& scene, float contactEpsilon, double secondsPerFrame );
    RuntimeRenderFramePolicy BuildFramePolicy( double simulationSeconds, double totalSimulationSeconds ) const;
    RunDebugState PresentationSnapshot() const;
    RuntimeOverlayPresentationEdit EditPresentation();
    RuntimeOverlayRenderResources& RenderResources();

  private:
    friend class RuntimeOverlayPresentationEdit;
    void CommitPresentation( const RunDebugState& state );

    RunDebugState m_presentationState;
    RuntimeOverlayRenderResources m_renderResources;
};
} // namespace Runtime
} // namespace SkullbonezCore
