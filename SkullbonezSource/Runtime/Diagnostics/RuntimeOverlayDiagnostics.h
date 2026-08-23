/*
File: RuntimeOverlayDiagnostics.h
Purpose:
  Owns debug presentation policy and its physics visualization resources.

Summary:
  RuntimeOverlayDiagnostics is the cohesive process-lifetime owner for debug
  presentation policy and its three visualizers. Mutations cross a stack-only
  value transaction; renderer borrows cross one opaque resource capability.

Glossary:
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
  - SkullbonezSource/Runtime/App/Run.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - SkullbonezSource/Runtime/App/RunRender.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

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
namespace Rendering
{
class RenderInstanceStore;
}
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
class PhysicsEngine;
}
namespace Runtime
{
class RuntimeOverlayDiagnostics;
class RuntimeRenderer;
struct RunLaunchOptions;
struct RunStartupOverrides;

struct RuntimeOverlayFramePolicy
{
    bool textOnly = false;
    bool terrainHidden = false;
    bool collisionVisualizer = false;
    bool physicsDebugTransparent = false;
    float physicsDebugAlpha = 1.0f;
    bool waterHidden = false;
    bool waterFlatDebug = false;
    bool waterNoReflect = false;
    bool waterRTReflect = false;
    bool waterFreezeDebug = false;
    float frozenWaterTime = 0.0f;
    bool broadphaseOverlay = false;
    uint32_t physicsDebugFlags = 0u;
    int physicsDebugPipelineStageCursor = 0;
    float physicsDebugContactLinger = 0.0f;
    double simulationSeconds = 0.0;
    double totalSimulationSeconds = 0.0;
};

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
    void UpdatePostPhysics( Physics::PhysicsEngine& physics, const Physics::PhysicsBodyStore& bodyStore,
                            const Physics::ColliderStore& colliders,
                            const Rendering::RenderInstanceStore& renderInstances, double secondsPerFrame );
    RuntimeOverlayFramePolicy BuildFramePolicy( double simulationSeconds, double totalSimulationSeconds ) const;

    void ApplyScenePresentation( const OverlayDebugState& scenePresentation );
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
};
} // namespace Runtime
} // namespace SkullbonezCore
