/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h
Purpose:
  Declares replay overlay drawing entry points used by the late UI/text pass.

Summary:
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
} // namespace SkullbonezCore::Physics

namespace SkullbonezCore::Runtime
{
class RunEditorTracer;
} // namespace SkullbonezCore::Runtime

namespace SkullbonezCore::Runtime::ReplayOverlay
{
struct ReplayOverlayRenderContext
{
    // Lifetime: borrowed from the current UI/text pass; overlay code must not
    // store it after the draw call returns.
    Rendering::IRenderCommandContext& renderCommands;
    ReplayRuntime& replayRuntime;
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords;
    const Physics::PhysicsBodyStore& bodyStore;
    bool editorModeEnabled = false;
    bool uiVisible = false;
    bool uiMinimized = false;
    bool scenePhysicsEnabled = false;
    RuntimeInteractionGestureKind gesture = RuntimeInteractionGestureKind::None;
    int screenW = 0;
    int screenH = 0;
    double nowSeconds = 0.0;
};

struct ReplayPathVisualizerRenderContext
{
    // Lifetime: every reference is a frame-local borrow from the render-tool
    // pass. Prediction scheduling has already published for this frame; this
    // context carries only presentation operands and retains nothing.
    ReplayRuntime& replayRuntime;
    SkullbonezCore::Physics::PhysicsEngine& physics;
    const SceneEntityStore& entities;
    RunEditorTracer& tracer;
    int sceneCurrentFrame = 0;
};

void RenderReplayScrubberOverlay( const ReplayOverlayRenderContext& context );
void RenderReplayCauseTreeOverlay( const ReplayOverlayRenderContext& context );
void RenderReplayPathVisualizer( const ReplayPathVisualizerRenderContext& context );
} // namespace SkullbonezCore::Runtime::ReplayOverlay
