/*
File: RuntimeOverlayDiagnostics.h
Purpose:
  Owns the operator UI, presentation policy, and physics debug visualizers.

Summary:
  RuntimeOverlayDiagnostics is the cohesive process-lifetime owner for the
  debug presentation surface. Run constructs it during startup and sequences
  its domain operations without retaining the individual UI or visualizer
  objects.

Glossary:
  Presentation state: Operator-selected overlay, water, terrain, and physics
    debug policy sampled into render values each frame.
  Debug visualizers: CPU-side broadphase, collision, and physics line data
    refreshed after committed physics work and borrowed by RuntimeRenderer.
  Operator UI: The interactive diagnostics window and its backend resources.

Invariants:
  - The owner is allocated only during the explicit Startup phase and lives
    until renderer shutdown has released every borrowed backend resource.
  - Post-physics refresh reads committed scene stores and publishes only
    presentation data; it does not advance simulation or mutate topology.
  - Renderer-facing policy is copied into a value record before submission.

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
#include "../UI/UI.h"

namespace SkullbonezCore
{
namespace Runtime
{
class SceneController;
struct RunLaunchOptions;
struct RunStartupOverrides;
struct RuntimeRenderFramePolicy;

class RuntimeOverlayDiagnostics
{
  public:
    static std::unique_ptr<RuntimeOverlayDiagnostics> CreateForStartup();

    void ApplyStartupPolicy( const RunStartupOverrides& overrides, RunLaunchOptions& launchOptions );
    void UpdatePostPhysics( SceneController& scene, float contactEpsilon, double secondsPerFrame );
    RuntimeRenderFramePolicy BuildFramePolicy( double simulationSeconds, double totalSimulationSeconds ) const;

    UI::InGameUI& OperatorUi();
    const UI::InGameUI& OperatorUi() const;
    RunDebugState& PresentationState();
    const RunDebugState& PresentationState() const;
    Physics::BroadphaseVisualizer& BroadphaseOverlay();
    Physics::CollisionVisualizer& CollisionOverlay();
    Physics::PhysicsDebugVisualizer& PhysicsDebugOverlay();

  private:
    UI::InGameUI m_ui;
    RunDebugState m_presentationState;
    Physics::BroadphaseVisualizer m_broadphaseOverlay;
    Physics::CollisionVisualizer m_collisionOverlay;
    Physics::PhysicsDebugVisualizer m_physicsDebugOverlay;
};
} // namespace Runtime
} // namespace SkullbonezCore
