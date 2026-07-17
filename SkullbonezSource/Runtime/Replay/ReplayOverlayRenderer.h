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

Related:
  - SkullbonezSource/Runtime/RunUiTextPass.cpp
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#pragma once

#include "ReplayAuthoring.h"
#include "ReplayPrediction.h"
#include "ReplayPresentation.h"
#include "ReplayScrubber.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <vector>

namespace SkullbonezCore::Rendering
{
class IRenderCommandContext;
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
// Lifetime: a synchronous, read-only replay publication. References and sample
// pointers remain valid only until the next replay update; UI composition must
// not retain this value after the late pass.
struct ReplayOverlayStateView
{
    ReplayScrubberView scrubber;
    ReplayPredictionPresentationView prediction;
    const RunReplayPathVisualizerState& pathVisualizer;
    const RunReplayVelocityEditState& velocityEdit;
    const RunReplayCauseTreeState& causeTree;
    ReplayRecorderStats solverStats;
    const ReplayPresentationSample* selectedPresentation = nullptr;
    const ReplayPresentationSample* latestPresentation = nullptr;
    const ReplaySolverFrameSample* selectedSolver = nullptr;
    const ReplaySolverFrameSample* latestSolver = nullptr;
    const RunReplayPredictionFrame* selectedPrediction = nullptr;
    const ReplayPresentationSample* currentPresentation = nullptr;
    const ReplaySolverFrameSample* currentSolver = nullptr;
    float solverPresentTrackPosition = 1.0f;
    bool loadedPresentation = false;
    bool predictionTimelineAvailable = false;
    bool shouldRenderScrubber = false;
};

struct ReplayOverlayRenderContext
{
    // Lifetime: borrowed from the current UI/text pass; overlay code must not
    // store it after the draw call returns.
    Rendering::IRenderCommandContext& renderCommands;
    ReplayScrubberView scrubber;
    const ReplayPredictionPresentationView& prediction;
    const RunReplayPathVisualizerState& pathVisualizer;
    const RunReplayVelocityEditState& velocityEdit;
    const RunReplayCauseTreeState& causeTree;
    ReplayRecorderStats solverStats;
    const ReplayPresentationSample* selectedPresentation = nullptr;
    const ReplayPresentationSample* latestPresentation = nullptr;
    const ReplaySolverFrameSample* selectedSolver = nullptr;
    const ReplaySolverFrameSample* latestSolver = nullptr;
    const RunReplayPredictionFrame* selectedPrediction = nullptr;
    const ReplayPresentationSample* currentPresentation = nullptr;
    const ReplaySolverFrameSample* currentSolver = nullptr;
    float solverPresentTrackPosition = 1.0f;
    bool loadedPresentation = false;
    bool predictionTimelineAvailable = false;
    bool shouldRenderScrubber = false;
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
    // pass. Prediction scheduling and presentation-cache preparation have
    // already published for this frame, so drawing receives prediction as a
    // read-only borrow and cannot reach worker or reveal-clock authority.
    const ReplayPredictionPresentationView& prediction;
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
