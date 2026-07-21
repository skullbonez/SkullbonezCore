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
  Overlay state view: Read-only replay publication borrowed for one late pass.
  Render context: Overlay state plus the render-command target and window facts.

Invariants:
  - Replay state reaches the context only through the published overlay view.
  - Published references and sample pointers remain valid for one frame only.
  - Overlay functions must not store references from the context.
  - Legacy scrubber and cause-tree pixels draw only while the Legacy
    development surface owns presentation; ImGui consumes the same values in
    its own exclusive surface.

Related:
  - SkullbonezSource/Runtime/UiTextPass.cpp
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#pragma once

#include "ReplayAuthoring.h"
#include "ReplayOverlayPackets.h"
#include "ReplayPrediction.h"
#include "ReplayPresentation.h"
#include "ReplayScrubber.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <vector>

namespace SkullbonezCore::Rendering
{
class Dx12GeometryOwner;
class Dx12TextureOwner;
} // namespace SkullbonezCore::Rendering

namespace SkullbonezCore::Core
{
class Profiler;
}

namespace SkullbonezCore::Text
{
class TextBatch;
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
struct ReplayPathVisualizerRenderContext
{
    // Lifetime: every reference is a frame-local borrow from the render-tool
    // pass. Prediction scheduling and presentation-cache preparation have
    // already published for this frame, so drawing receives prediction as a
    // read-only borrow and cannot reach worker or reveal-clock authority.
    const ReplayPredictionPresentationView& prediction;
    Core::Profiler* profiler = nullptr;
    const RunReplayPathVisualizerState& pathVisualizer;
    SkullbonezCore::Physics::PhysicsEngine& physics;
    const SceneEntityStore& entities;
    RunEditorTracer& tracer;
    ReplayFrameIndex presentFrame = 0;
    bool hasPresentSample = false;
};

struct ReplayPathVisualizerRenderResult
{
    bool retainedRefreshBudgetExpired = false;
};

void RenderReplayScrubberOverlay( Text::TextBatch& textBatch, const ReplayOverlayRenderContext& context );
void RenderReplayCauseTreeOverlay( Text::TextBatch& textBatch, const ReplayOverlayRenderContext& context );
ReplayPathVisualizerRenderResult RenderReplayPathVisualizer( const ReplayPathVisualizerRenderContext& context );
} // namespace SkullbonezCore::Runtime::ReplayOverlay
