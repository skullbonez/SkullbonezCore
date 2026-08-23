/*
File: SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h
Purpose:
  Publishes synchronous Replay overlay state and viewport values used by
  Runtime UI composition.

Summary:
  ReplayRuntime selects immutable owner views once. UI composition may copy or
  borrow those values plus a detached viewport for the current late pass. The
  cause-inspection view may contain spans into Planning-owned fixed arrays, but
  the packet cannot reach Replay mutation or prediction scheduling through them.

Invariants:
  - References, sample pointers, and cause-detail spans remain valid for one
    synchronous late pass.
  - Packets contain no mutable Replay owner and must never be retained.

Related:
  - ReplayOverlayRenderer.h owns the draw operations that consume these packets.
  - ReplayRuntime.h publishes ReplayOverlayStateView.
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Replay/ReplayCapturePackets.h"
#include "../Replay/ReplayAuthoringPackets.h"
#include "ReplayInterceptReadout.h"
#include "ReplayCauseInspection.h"
#include "ReplayPorkchopPanel.h"
#include "ReplayTripPlanner.h"
#include "../Prediction/ReplayPredictionView.h"
#include "../Replay/ReplayPathPackets.h"
#include "../Replay/ReplayPresentationPackets.h"
#include "../Replay/ReplayTimelinePackets.h"

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
struct ReplayOverlayViewport
{
    // One presentation value keeps width and height coupled at every overlay
    // call site; neither dimension has meaning without the other.
    int width = 1;
    int height = 1;
};

struct ReplayOverlayGestureView
{
    bool scrubDrag = false;
    bool predictionHorizonDrag = false;
};

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
    ReplayCauseInspectionView causeInspection;
    ReplayRecorderStats solverStats;

    // Concept: overlay consumers receive the lower Presentation selection plus
    // the sibling Prediction row selected by App. Planning retains neither.
    ReplayPresentationSelection selection;
    const RunReplayPredictionFrame* selectedPrediction = nullptr;
    bool predictionTimelineAvailable = false;
    bool shouldRenderScrubber = false;
    bool recordingConfigured = false;
    bool recordingEnabled = false;
    bool recordingLockedByHashLog = false;
};

} // namespace SkullbonezCore::Runtime::ReplayOverlay
