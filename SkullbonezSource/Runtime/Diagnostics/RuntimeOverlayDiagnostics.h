/*
File: RuntimeOverlayDiagnostics.h
Purpose:
  Owns debug presentation policy published to App and Render.

Summary:
  RuntimeOverlayDiagnostics retains CPU presentation choices only. App copies
  those choices into Render values; Render owns all visualizer caches and GPU
  submission state.

Glossary:
  Presentation edit: Stack-only copy committed atomically when its scope ends.

Invariants:
  - The owner is allocated only during the explicit Startup phase.
  - Renderer-facing policy is copied into a value record before submission.
  - No caller receives the owner's mutable policy record.

Related:
  - SkullbonezSource/Runtime/App/Run.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - SkullbonezSource/Runtime/App/RunRender.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>
#include <memory>

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
class RuntimeOverlayDiagnostics;
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

class RuntimeOverlayDiagnostics
{
  public:
    static std::unique_ptr<RuntimeOverlayDiagnostics> CreateForStartup();

    void ApplyStartupPolicy( const RunStartupOverrides& overrides, RunLaunchOptions& launchOptions,
                             UI::InGameUI& operatorUi );
    RuntimeOverlayFramePolicy BuildFramePolicy( double simulationSeconds, double totalSimulationSeconds ) const;

    void ApplyScenePresentation( const OverlayDebugState& scenePresentation );
    OverlayDebugState PresentationSnapshot() const;
    RuntimeOverlayPresentationEdit EditPresentation();
  private:
    friend class RuntimeOverlayPresentationEdit;
    void CommitPresentation( const OverlayDebugState& state );

    OverlayDebugState m_presentationState;
};
} // namespace Runtime
} // namespace SkullbonezCore
