/*
File: SkullbonezSource/Runtime/Automation/ReplayAutomationView.h
Purpose:
  Defines the detached replay-family evidence consumed by Automation reports.

Summary:
  App composes live Replay, Prediction, and Planning publications into one
  synchronous read-only view. Automation may inspect that view while writing a
  report but cannot retain or mutate any contributing owner.

Invariants:
  - Every reference and span expires when the composing App call returns.
  - The view carries no callback and exposes no mutable owner operation.
  - Memory and evidence counters describe the same sampled frame.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/Automation/InteractionAutomationController.h
*/
#pragma once

#include "../Planning/ReplayPlanningRuntime.h"
#include "../Prediction/ReplayPrediction.h"
#include "../Replay/ReplayCoordination.h"

namespace SkullbonezCore::Runtime
{
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
struct ReplayAutomationView
{
    const RunReplayPredictionState& prediction;
    const ReplayPredictionSolverEvidenceStore& predictionEvidence;
    ReplayPredictionDetailMode predictionDetailMode = ReplayPredictionDetailMode::Low;
    const ReplayPorkchopPanelView& porkchop;
    const ReplayTripPlannerView& tripPlanner;
    const RunReplayCauseTreeState& causeTree;
    ReplayCauseInspectionView causeInspection;
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
    ReplayPredictionSolverEvidenceCaptureStats predictionEvidenceCapture;
    ReplayPredictionSolverEvidenceBanksMemoryStats predictionEvidenceMemory;
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
