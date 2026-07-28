/*
File: SkullbonezSource/Runtime/Replay/ReplayProbeState.h
Purpose:
  Defines cold startup workflow configuration and debug-only Replay probe values.

Summary:
  These probes are launch-requested diagnostics that drive replay scrub,
  restore, and save coverage after the scene has enough captured samples. They
  share bounded values so App can sequence configuration, one-shot completion,
  and failure reporting without placing Prediction contracts in Replay.

Glossary:
  Scrub probe: Debug launch path that seeks into captured replay history.
  Restore probe: Debug launch path that restores a historical replay sample
  into the live scene.
  Save probe: Debug launch path that writes a replay artifact after enough
  timeline coverage exists.
  Probe failure: CLI-visible diagnostic result that should make validation
  return nonzero without routing through the fatal-exception path.

Invariants:
  - Probe state exists only in debug builds and is driven by CLI test paths.
  - Completion flags are one-shot guards so a successful probe does not repeat
    every frame after its minimum sample count is reached.
  - Failure text is bounded so WinMain can return a nonzero probe result after
    the frame loop exits.
  - Replay values never name the App probe runner or the Prediction owner.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp
*/
#pragma once

#include "../../Core/SbResult.h"
#include "ReplayIdentity.h"

#include <cstring>

namespace SkullbonezCore
{
namespace Runtime
{
struct ReplayStartupRequest;
struct ReplayLiveRestoreOutcome;
struct ReplayLiveRestoreRequest;
class ReplayRestoreTransaction;
struct ReplayV2SolverCheckpointLoadResult;
struct RunReplayV2TargetRestoreResult;
class ReplayAuthoring;
class ReplayPresentation;
class ReplayScrubber;
class ReplayTimeline;
struct RunMousePickupState;
enum class RunCameraMode;
struct ReplaySolverFrameSample;

struct ReplayStartupWorkflowState
{
    char loadPath[260] = {};
    bool loadProbe = false;
#ifdef _DEBUG
    char checkpointProbePath[260] = {};
    char targetProbePath[260] = {};
    char branchProbePath[260] = {};
    char failureProbePath[260] = {};
#endif
};

#ifdef _DEBUG
struct RunReplayScrubProbeState
{
    bool enabled = false;
    bool completed = false;
    float normalized = 0.25f;
    int minSampleCount = 24;
    float minDistanceSquared = 0.0001f;
};

struct RunReplayRestoreProbeState
{
    bool enabled = false;
    bool completed = false;
    float normalized = 0.25f;
    int minSampleCount = 24;
};

struct RunReplaySaveProbeState
{
    bool enabled = false;
    bool completed = false;
    bool runtimeResetCoverageInjected = false;
    bool eventCoverageInjected = false;
    int minSampleCount = 24;
    char path[260] = {};
};

struct RunReplayProbeFailureState
{
    bool failed = false;
    char owner[64] = {};
    char message[512] = {};
};

struct ReplayProbeState
{
    void RecordFailure( const SkullbonezCore::Core::SbResult& result )
    {

        if ( result.Ok() || failure.failed )
        {
            return;
        }

        const char* failureOwner = result.ErrorOwner() && result.ErrorOwner()[0] != '\0' ? result.ErrorOwner()
                                                                                         : "ReplayProbe";
        const char* failureMessage = result.ErrorMessage()[0] != '\0' ? result.ErrorMessage()
                                                                      : "replay probe failed without a failure message";
        failure.failed = true;
        strcpy_s( failure.owner, sizeof( failure.owner ), failureOwner );
        strcpy_s( failure.message, sizeof( failure.message ), failureMessage );
    }

    bool Failed() const
    {
        return failure.failed;
    }

    const char* FailureOwner() const
    {
        return failure.owner[0] != '\0' ? failure.owner : "ReplayProbe";
    }

    const char* FailureMessage() const
    {
        return failure.message[0] != '\0' ? failure.message : "replay probe failed";
    }

    RunReplayScrubProbeState scrub;
    RunReplayRestoreProbeState restore;
    RunReplaySaveProbeState save;
    RunReplayProbeFailureState failure;
};

struct ReplayProbeRestoreRequest
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    const ReplaySolverFrameSample* sample = nullptr;
    uint64_t selectedFrame = 0;
    uint64_t latestFrame = 0;
    uint64_t selectedHash = 0;
};

enum class ReplayProbeSaveAction
{
    None,
    ResetScene,
    InjectEventCoverage,
    ValidateArtifact
};

struct ReplayProbeSaveRequest
{
    ReplayProbeSaveAction action = ReplayProbeSaveAction::None;
    char path[260] = {};
};

enum class ReplayFailureProbeAction : uint8_t
{
    None,
    RestoreMissingTarget,
    CaptureRollbackSample,
    RestoreCorruptedTarget,
    CaptureRollbackHash
};

// Value command emitted by the probe owner. rollbackReference is a synchronous
// borrow of the caller's captured sample and remains valid only while the
// startup workflow advances this command chain.
struct ReplayFailureProbeRequest
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    ReplayFailureProbeAction action = ReplayFailureProbeAction::None;
    ReplayFrameIndex targetFrame = 0;
    const ReplaySolverFrameSample* rollbackReference = nullptr;
    bool forceHashMismatch = false;
};

// Result facts for exactly one requested primitive. Text and sample pointers
// are consumed synchronously by AdvanceFailureFileProbe and are never retained.
struct ReplayFailureProbeStepResult
{
    const char* reason = nullptr;
    const ReplaySolverFrameSample* capturedSample = nullptr;
    uint64_t solverHash = 0;
    bool succeeded = false;
};

// Bounded diagnostic text retained across the four-step expected-failure
// workflow. Live solver samples remain caller-owned synchronous borrows.
struct ReplayFailureFileProbeState
{
    char path[260] = {};
    char missingTargetReason[256] = {};
    char hashFailureReason[256] = {};
    ReplayFrameIndex missingTargetFrame = 0;
};
#endif

} // namespace Runtime
} // namespace SkullbonezCore
