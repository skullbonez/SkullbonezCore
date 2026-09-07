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
#include "../../Maths/Matrix4.h"

#include <algorithm>
#include <cmath>

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
    // Viewport dimensions and the active world projection jointly define
    // screen placement for this presentation frame.
    int width = 1;
    int height = 1;
    Math::Transformation::Matrix4 viewProjection;
};

struct ReplayOverlayGestureView
{
    bool scrubDrag = false;
    bool predictionHorizonDrag = false;
};

// Lifetime: this synchronous subview borrows only the owners needed to draw
// the replay scrubber. Intercept, trip-planning, porkchop, and cause surfaces
// stay outside it, so the scrubber phase cannot reach unrelated overlay state.
struct ReplayScrubberPresentationView
{
    ReplayScrubberView scrubber;
    ReplayPredictionTimelineView predictionTimeline;
    ReplayPredictionTopologyView predictionTopology;
    ReplayPredictionControlsView predictionControls;
    ReplayPredictionDiagnosticsView predictionDiagnostics;
    const RunReplayPathVisualizerState& pathVisualizer;
    const RunReplayVelocityEditState& velocityEdit;
    ReplayRecorderStats solverStats;
    ReplayPresentationSelection selection;
    const RunReplayPredictionFrame* selectedPrediction = nullptr;
    bool predictionTimelineAvailable = false;
    bool shouldRender = false;
};

struct ReplayOverlayTimelineView
{
    ReplayScrubberView scrubber;
    ReplayPredictionPresentationView prediction;
    const RunReplayPathVisualizerState& pathVisualizer;
    const RunReplayVelocityEditState& velocityEdit;
    ReplayRecorderStats solverStats;

    // Replay owns this presentation interval. Planning carries the two scalar
    // values only so sibling operator surfaces cannot recover Replay authority.
    float predictionHorizonMinimum = 0.0f;
    float predictionHorizonMaximum = 0.0f;

    // Concept: overlay consumers receive the lower Presentation selection plus
    // the sibling Prediction row selected by App. Planning retains neither.
    ReplayPresentationSelection selection;
    const RunReplayPredictionFrame* selectedPrediction = nullptr;
    bool predictionTimelineAvailable = false;
    bool shouldRenderScrubber = false;
    bool recordingConfigured = false;
    bool recordingEnabled = false;
    bool recordingLockedByHashLog = false;

    ReplayScrubberPresentationView ScrubberPresentation() const noexcept
    {
        return { scrubber,
                 prediction.timeline,
                 prediction.topology,
                 prediction.controls,
                 prediction.diagnostics,
                 pathVisualizer,
                 velocityEdit,
                 solverStats,
                 selection,
                 selectedPrediction,
                 predictionTimelineAvailable,
                 shouldRenderScrubber };
    }
};

struct ReplayOverlayPlanningSurfacesView
{
    ReplayInterceptView intercept;
    const ReplayPorkchopPanelView& porkchop;
    const ReplayTripPlannerView& tripPlanner;
};

struct ReplayCauseLoadingView
{
    bool active = false;
    float progress = 0.0f;
};

inline ReplayCauseLoadingView BuildReplayCauseLoadingView( const ReplayPredictionTimelineView& timeline,
                                                           const ReplayPredictionTopologyView& topology,
                                                           const ReplayPredictionControlsView& controls,
                                                           const RunReplayPathVisualizerState& path,
                                                           ReplayPredictionDetailMode detailMode ) noexcept
{
    ReplayCauseLoadingView loading;

    if ( !controls.enabled || !path.hasTarget || path.targetId.value == 0 || detailMode != ReplayPredictionDetailMode::High )
    {
        return loading;
    }

    const bool matchingTarget = topology.targetId == path.targetId;
    loading.active = !matchingTarget || !timeline.complete || !topology.treeReady;

    // Invariant: a completed prefix from another target must never advance this
    // request's bar or admit its rows. Collision resolution uses simulation time,
    // independently of the user's future-path reveal speed.
    if ( matchingTarget && timeline.frames.size() >= 2 && controls.horizonSeconds > 0.0f &&
         ( !controls.building || timeline.usingBuildFrames ) )
    {
        const double seconds = timeline.frames.back().simulationSeconds - timeline.frames.front().simulationSeconds;
        if ( std::isfinite( seconds ) )
        {
            loading.progress = std::clamp( static_cast<float>( seconds / controls.horizonSeconds ), 0.0f,
                                           loading.active ? 0.99f : 1.0f );
        }
    }

    return loading;
}

struct ReplayOverlayCausalityView
{
    const RunReplayCauseTreeState& tree;
    ReplayCauseInspectionView inspection;
    ReplayPredictionDetailMode predictionDetailMode = ReplayPredictionDetailMode::High;
    ReplayCauseLoadingView loading;
};

struct ReplayOverlayStateView
{
    // Concept: this is the App composition envelope. Renderers, development
    // tools, and automation receive timeline, planning-surface, or causality
    // children rather than the complete Replay overlay state.
    ReplayOverlayTimelineView timeline;
    ReplayOverlayPlanningSurfacesView planning;
    ReplayOverlayCausalityView causality;
};

} // namespace SkullbonezCore::Runtime::ReplayOverlay
