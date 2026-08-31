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

// Lifetime: this synchronous subview borrows only the owners needed to draw
// the replay scrubber. Intercept, trip-planning, porkchop, and cause surfaces
// stay outside it, so the scrubber phase cannot reach unrelated overlay state.
class ReplayScrubberPresentationView
{
  public:
    ReplayScrubberPresentationView( ReplayScrubberView scrubber, ReplayPredictionPresentationView prediction,
                                    const RunReplayPathVisualizerState& pathVisualizer,
                                    const RunReplayVelocityEditState& velocityEdit, ReplayRecorderStats solverStats,
                                    ReplayPresentationSelection selection,
                                    const RunReplayPredictionFrame* selectedPrediction, bool predictionTimelineAvailable,
                                    bool shouldRender ) noexcept
        : m_scrubber( scrubber ), m_prediction( prediction ), m_pathVisualizer( pathVisualizer ),
          m_velocityEdit( velocityEdit ), m_solverStats( solverStats ), m_selection( selection ),
          m_selectedPrediction( selectedPrediction ), m_predictionTimelineAvailable( predictionTimelineAvailable ),
          m_shouldRender( shouldRender )
    {
    }

    const ReplayScrubberView& Scrubber() const noexcept
    {
        return m_scrubber;
    }
    const ReplayPredictionPresentationView& Prediction() const noexcept
    {
        return m_prediction;
    }
    const RunReplayPathVisualizerState& PathVisualizer() const noexcept
    {
        return m_pathVisualizer;
    }
    const RunReplayVelocityEditState& VelocityEdit() const noexcept
    {
        return m_velocityEdit;
    }
    const ReplayRecorderStats& SolverStats() const noexcept
    {
        return m_solverStats;
    }
    const ReplayPresentationSelection& Selection() const noexcept
    {
        return m_selection;
    }
    const RunReplayPredictionFrame* SelectedPrediction() const noexcept
    {
        return m_selectedPrediction;
    }
    bool PredictionTimelineAvailable() const noexcept
    {
        return m_predictionTimelineAvailable;
    }
    bool ShouldRender() const noexcept
    {
        return m_shouldRender;
    }

  private:
    ReplayScrubberView m_scrubber;
    ReplayPredictionPresentationView m_prediction;
    const RunReplayPathVisualizerState& m_pathVisualizer;
    const RunReplayVelocityEditState& m_velocityEdit;
    ReplayRecorderStats m_solverStats;
    ReplayPresentationSelection m_selection;
    const RunReplayPredictionFrame* m_selectedPrediction = nullptr;
    bool m_predictionTimelineAvailable = false;
    bool m_shouldRender = false;
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
                 prediction,
                 pathVisualizer,
                 velocityEdit,
                 solverStats,
                 selection,
                 selectedPrediction,
                 predictionTimelineAvailable,
                 shouldRenderScrubber };
    }
};

} // namespace SkullbonezCore::Runtime::ReplayOverlay
