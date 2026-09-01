/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntimePackets.h
Purpose:
  Publishes detached replay selection and render-frame values.

Summary:
  Replay owns the recorded-side publication identity while App may attach an
  optional future-frame pointer from Prediction. The packet itself retains no
  owner and Render consumes it only during the current frame.

Invariants:
  - Every pointer expires at the next Replay or Prediction mutation.
  - Render may inspect values but cannot mutate Replay-family owners.
  - The packet includes no App header, callback, or retained authority.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
*/
#pragma once

#include "ReplayPresentationPackets.h"
#include "ReplayVisualPacket.h"
#include "../../Rendering/ContactManifoldPresentation.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore::Runtime
{
struct RunReplayPredictionFrame;

struct ReplayFrameSelection
{
    ReplayPresentationSelection replay;
    const RunReplayPredictionFrame* selectedPrediction = nullptr;
    bool predictionTimelineAvailable = false;
};

struct ReplayRenderTimeView
{
    const ReplayPresentationSample* presentationSample = nullptr;
    const ReplaySolverFrameSample* solverSample = nullptr;
    const RunReplayPredictionFrame* predictionFrame = nullptr;
    bool liveAdvanceHeld = false;
};

struct ReplayRenderFrameView
{
    const ReplayVisualPacket* visualPacket = nullptr;
    const std::vector<uint8_t>* focusModelMask = nullptr;
    Rendering::ContactManifoldPresentation contactPresentation;
    bool focusFadeActive = false;
};

struct ReplayRenderFrameViews
{
    ReplayRenderTimeView time;
    ReplayRenderFrameView render;
};
} // namespace SkullbonezCore::Runtime
