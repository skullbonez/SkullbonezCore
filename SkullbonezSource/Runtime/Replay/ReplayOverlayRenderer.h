/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h
Purpose:
  Declares replay overlay drawing entry points used by the late UI/text pass.

Mental model:
  RuntimeRenderer decides pass order, but replay owns the UI drawing logic for
  replay scrubber and cause-tree overlays.

Glossary:
  UI (User Interface): Runtime controls and overlays drawn over the 3D scene.
  UI text pass: Late overlay pass that invokes replay overlay drawing after
    scene rendering.
  Replay overlay: UI draw pass for replay timeline, prediction controls, and
    cause-tree inspection.
  Render context: Borrowed state bundle for one overlay draw call.

Invariants:
  - The context borrows live runtime state for one frame only.
  - Overlay functions must not store references from the context.

Related:
  - SkullbonezSource/Runtime/RunUiTextPass.cpp
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#pragma once

#include "ReplayRuntime.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <vector>

namespace SkullbonezCore::Rendering
{
class IRenderCommandContext;
}

namespace SkullbonezCore::Physics
{
class PhysicsBodyStore;
class PhysicsEngine;
struct PhysicsWorldForces;
} // namespace SkullbonezCore::Physics

namespace SkullbonezCore::Threading
{
class WorkerPool;
}

namespace SkullbonezCore::Basics
{
class EngineConfig;
class RunEditorTracer;
} // namespace SkullbonezCore::Basics

namespace SkullbonezCore::Basics::ReplayOverlay
{
struct ReplayOverlayRenderContext
{
    // Lifetime: borrowed from the current UI/text pass; overlay code must not
    // store it after the draw call returns.
    Rendering::IRenderCommandContext& renderCommands;
    ReplayRuntime& replayRuntime;
    const std::vector<Rendering::RenderInstancePresentationRecord>& presentationRecords;
    const Physics::PhysicsBodyStore& bodyStore;
    bool editorModeEnabled = false;
    bool uiVisible = false;
    bool uiMinimized = false;
    bool scenePhysicsEnabled = false;
    int screenW = 0;
    int screenH = 0;
    double nowSeconds = 0.0;
};

struct ReplayPathVisualizerRenderContext
{
    // Lifetime: every reference is a frame-local borrow from Run's render-tool
    // pass. The visualizer may update replay-owned caches, but must not retain
    // owner references after returning.
    ReplayRuntime& replayRuntime;
    SkullbonezCore::Physics::PhysicsEngine& physics;
    const SceneEntityStore& entities;
    const EngineConfig& config;
    const SkullbonezCore::Physics::PhysicsWorldForces& worldForces;
    SkullbonezCore::Threading::WorkerPool& workerPool;
    RunEditorTracer& tracer;
    bool scenePhysicsEnabled = false;
    int sceneCurrentFrame = 0;
    double simulationTimeSinceLastStart = 0.0;
    double simulationTotalTime = 0.0;
};

void RenderReplayScrubberOverlay( const ReplayOverlayRenderContext& context );
void RenderReplayCauseTreeOverlay( const ReplayOverlayRenderContext& context );
void RenderReplayPathVisualizer( const ReplayPathVisualizerRenderContext& context );
} // namespace SkullbonezCore::Basics::ReplayOverlay
