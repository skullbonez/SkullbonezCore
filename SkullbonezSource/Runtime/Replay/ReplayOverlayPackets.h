/*
File: ReplayOverlayPackets.h
Purpose:
  Publishes the synchronous replay overlay state and draw-context packets used by Runtime UI composition.

Summary:
  ReplayRuntime selects immutable owner views once. UI composition may copy or
  borrow those values for the current late pass but cannot reach replay mutation
  or prediction scheduling through these packets.

Glossary:
  Overlay state view: Read-only Replay publication for one UI composition turn.
  Render context: Overlay values plus the current render-command target and window facts.

Invariants:
  - References and sample pointers remain valid for one synchronous late pass.
  - Packets contain no mutable Replay owner and must never be retained.

Related:
  - ReplayOverlayRenderer.h owns the draw operations that consume these packets.
  - ReplayRuntime.h publishes ReplayOverlayStateView.
*/
#pragma once

#include "ReplayCapturePackets.h"
#include "ReplayAuthoringPackets.h"
#include "../Planning/ReplayInterceptReadout.h"
#include "../Planning/ReplayPorkchopPanel.h"
#include "../Planning/ReplayTripPlanner.h"
#include "../Prediction/ReplayPredictionView.h"
#include "ReplayPathPackets.h"
#include "ReplayPresentationPackets.h"
#include "ReplayTimelinePackets.h"
#include "../Interaction/RuntimeInteractionController.h"

namespace SkullbonezCore::Rendering
{
class Dx12GeometryOwner;
class Dx12Diagnostics;
class Dx12TextureOwner;
} // namespace SkullbonezCore::Rendering

namespace SkullbonezCore::Core
{
class Profiler;
}

namespace SkullbonezCore::Runtime
{
struct ReplayPresentationSample;
struct ReplaySolverFrameSample;
struct RunReplayCauseTreeState;
struct RunReplayPathVisualizerState;
struct RunReplayPredictionFrame;
struct RunReplayVelocityEditState;
} // namespace SkullbonezCore::Runtime

namespace SkullbonezCore::Runtime::ReplayOverlay
{
struct ReplayOverlayStateView
{
    ReplayScrubberView scrubber;
    ReplayPredictionPresentationView prediction;
    ReplayInterceptView intercept;
    const ReplayPorkchopPanelView& porkchop;
    const ReplayTripPlannerView& tripPlanner;
    const RunReplayPathVisualizerState& pathVisualizer;
    const RunReplayVelocityEditState& velocityEdit;
    const RunReplayCauseTreeState& causeTree;
    ReplayRecorderStats solverStats;
    // Concept: overlay consumers receive the Presentation owner's one answer to
    // selection; they do not flatten or independently resolve timeline state.
    ReplayPresentationSelection selection;
    bool shouldRenderScrubber = false;
    bool recordingConfigured = false;
    bool recordingEnabled = false;
    bool recordingLockedByHashLog = false;
};

struct ReplayOverlayRenderContext
{
    Rendering::Dx12TextureOwner& renderTextures;
    Rendering::Dx12GeometryOwner& renderCommands;
    Rendering::Dx12Diagnostics& renderDiagnostics;
    Core::Profiler* profiler = nullptr;
    // Lifetime: this immutable view is consumed synchronously by the UI-text
    // graph callback and cannot outlive the frame-local selection borrows.
    const ReplayOverlayStateView& replay;
    bool legacyReplaySurfaceActive = true;
    bool editorModeEnabled = false;
    bool uiVisible = false;
    bool uiMinimized = false;
    bool scenePhysicsEnabled = false;
    RuntimeInteractionGestureKind gesture = RuntimeInteractionGestureKind::None;
    int screenW = 0;
    int screenH = 0;
    double nowSeconds = 0.0;
};
} // namespace SkullbonezCore::Runtime::ReplayOverlay
