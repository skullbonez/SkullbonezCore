/*
File: SkullbonezSource/Runtime/App/ReplayRuntimePackets.h
Purpose:
  Defines application-level packets that aggregate Replay, Prediction, and Planning publications.

Summary:
  Runtime/App is the composition boundary for the three replay-family packages.
  These packets may name all three siblings, while ReplayCoordination remains a
  lower Replay-only seam.

Glossary:
  Replay family: The Replay, Prediction, and Planning sibling packages.
  Intent: Authority-free command value applied by the owning package.

Invariants:
  - References and spans are synchronous evidence and expire at the next owner mutation.
  - Intent values contain no callback, owner pointer, or retained authority.
  - Lower Replay headers never include this application aggregation.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayCoordination.h
*/
#pragma once

#include "../Replay/ReplayCoordination.h"
#include "../Replay/ReplayPresentationPackets.h"
#include "../Prediction/ReplayPrediction.h"
#include "../Planning/ReplayPlanningRuntime.h"

#include <vector>

namespace SkullbonezCore::Runtime
{
// Concept: App composes the lower Replay selection with the sibling Prediction
// row selected on the future side of the shared scrub track. Neither sibling
// header needs to name the other's retained publication.
// Lifetime: every pointer expires at the next Replay or Prediction mutation.
struct ReplayFrameSelection
{
    ReplayPresentationSelection replay;
    const RunReplayPredictionFrame* selectedPrediction = nullptr;
    bool predictionTimelineAvailable = false;
};

// Render consumes one App-composed view after Replay pose selection and
// Prediction visual publication are both complete for the frame.
struct ReplayRenderFrameView
{
    const ReplayPresentationSample* presentationSample = nullptr;
    const ReplaySolverFrameSample* solverSample = nullptr;
    const RunReplayPredictionFrame* predictionFrame = nullptr;
    const ReplayVisualPacket* visualPacket = nullptr;
    const std::vector<uint8_t>* focusModelMask = nullptr;
    bool predictionEnabled = false;
    bool liveAdvanceHeld = false;
    bool focusFadeActive = false;
};

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
struct ReplayAutomationView
{
    const RunReplayPredictionState& prediction;
    const ReplayPorkchopPanelView& porkchop;
    const ReplayTripPlannerView& tripPlanner;
    const RunReplayPathVisualizerState& path;
    ReplayInterceptView intercept;
    const ReplayRecorder& presentationRecorder;
    const ReplaySolverRecorder& solverRecorder;
    const ReplayEventRecorder& eventRecorder;
    std::span<const RunReplayPredictionFrame> activePredictionFrames;
    ReplayScrubberView scrubber;
    ReplayRecorderStats solverStats;
    const ReplaySolverFrameSample* latestSolverSample = nullptr;
    const ReplaySolverFrameSample* currentSolverSample = nullptr;
    const RunReplayPredictionFrame* currentPredictionFrame = nullptr;
    ReplayVisualPacket visualPacket;
    ReplayTrajectorySubmissionProbeStats trajectorySubmission;
    uint64_t predictionAppearanceInvalidationCount = 0;
    SkullbonezCore::Core::MainMemoryReplayStats memoryStats;
    ReplayInputView input;
    float solverTrackPosition = 0.0f;
    float solverPresentTrackPosition = 0.0f;
};
#endif

struct ReplayFrameIntent
{
    bool setScrubberVisibility = false;
    bool scrubberVisible = false;
    double scrubberNow = 0.0;
    double scrubberHoldSeconds = 0.0;
    bool setPredictionEnabled = false;
    bool predictionEnabled = false;
    bool setPredictionHorizon = false;
    float predictionHorizonSeconds = 0.0f;
    bool prepareVelocityMutationBaseline = false;
    bool commitVelocityMutation = false;
    bool clearVelocityEditInputState = false;
    bool queryDeterministicRevealReady = false;
    bool armDeterministicReveal = false;
    ReplayFrameIndex revealFrame = 0;
    bool resetPresentedRevealFrame = false;
    bool applyPredictionRevealRate = false;
    double predictionRevealRate = 1.0;
    bool setPathTarget = false;
    Physics::PhysicsSceneObjectId pathTargetId;
    Physics::ModelRowHint pathTargetModelRow;
    char pathTargetName[64] = {};
    bool setInterceptTarget = false;
    Physics::PhysicsSceneObjectId interceptTargetId;
    Physics::ModelRowHint interceptTargetModelRow;
    bool hasTripPlannerCommand = false;
    ReplayTripPlannerCommand tripPlannerCommand;
};

struct ReplayFrameIntentResult
{
    bool velocityMutationBaselinePrepared = false;
    bool deterministicRevealReady = false;
};
} // namespace SkullbonezCore::Runtime
