/*
File: SkullbonezSource/Runtime/Replay/ReplayProbeState.h
Purpose:
  Owns cold startup workflow configuration and debug-only replay probe state.

Summary:
  These probes are launch-requested diagnostics that drive replay scrub,
  restore, and save coverage after the scene has enough captured samples. They
  share one probe runner lifecycle so configuration, one-shot completion, and
  bounded failure reporting stay beside the workflows they control.

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
  - Failure text is bounded and stored here so WinMain can return a nonzero
    probe result after the frame loop exits.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayValidation.Probes.cpp
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
struct ReplayArtifactTopologyOwners;
struct ReplayLiveRestoreOutcome;
struct ReplayLiveRestoreRequest;
struct ReplayRestoreTransaction;
struct ReplayV2SolverCheckpointLoadResult;
struct RunReplayV2TargetRestoreResult;
class ReplayAuthoring;
class ReplayPrediction;
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
        if ( result.ok || failure.failed )
        {
            return;
        }

        const char* failureOwner =
            result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "ReplayProbe";
        const char* failureMessage =
            result.error.message[0] != '\0' ? result.error.message : "replay probe failed without a failure message";
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

class ReplayProbeRunner
{
  public:
    // Returns whether live prediction generation remains permitted after the
    // startup capability request is installed.
    bool Configure( const ReplayStartupRequest& request );
    const ReplayStartupWorkflowState& Startup() const noexcept
    {
        return m_startup;
    }
#ifdef _DEBUG
    // Installs Debug-only CLI probe state after Configure has copied the
    // product load request and capability bit.
    void ConfigureDebug( const ReplayStartupRequest& request );
    SkullbonezCore::Core::SbResult TickScrubProbe( const ReplayRestoreTransaction& transaction,
                                                   const ReplayTimeline& timeline,
                                                   ReplayPresentation& presentation );
    ReplayProbeRestoreRequest PrepareRestoreProbe( const ReplayTimeline& timeline );
    SkullbonezCore::Core::SbResult
    CompleteRestoreProbe( const ReplayProbeRestoreRequest& request, bool restored, const char* reason );
    ReplayProbeSaveRequest PrepareSaveProbe( const ReplayTimeline& timeline );
    void CompleteSaveProbe( const ReplayProbeSaveRequest& request, const SkullbonezCore::Core::SbResult& result );
    SkullbonezCore::Core::SbResult CurrentFailure() const;
    void RecordFailure( const SkullbonezCore::Core::SbResult& result );
    SkullbonezCore::Core::SbResult VerifyLoadedPresentation( ReplayTimeline& timeline,
                                                             ReplayScrubber& scrubber,
                                                             ReplayPresentation& presentation,
                                                             ReplayAuthoring& authoring,
                                                             ReplayPrediction& prediction,
                                                             const ReplayRestoreTransaction& transaction,
                                                             RunMousePickupState& mousePickup,
                                                             RunCameraMode normalizedCurrentMode,
                                                             double now,
                                                             float normalized );
    SkullbonezCore::Core::SbResult PrepareCheckpointFileProbe( const char* path,
                                                               ReplaySolverFrameSample& outCheckpoint,
                                                               ReplayV2SolverCheckpointLoadResult& outLoadResult );
    SkullbonezCore::Core::SbResult CompleteCheckpointFileProbe( const char* path,
                                                                const ReplaySolverFrameSample& checkpoint,
                                                                const ReplayV2SolverCheckpointLoadResult& loadResult,
                                                                bool restored,
                                                                const char* reason );
    SkullbonezCore::Core::SbResult CompleteTargetFileProbe( const char* path,
                                                            const RunReplayV2TargetRestoreResult& result,
                                                            bool restored,
                                                            const char* reason );
    ReplayFailureProbeRequest BeginFailureFileProbe( const char* path );
    ReplayFailureProbeRequest AdvanceFailureFileProbe( const ReplayFailureProbeRequest& request,
                                                       const ReplayFailureProbeStepResult& result );
    SkullbonezCore::Core::SbResult PrepareBranchFileProbe( ReplayTimeline& timeline,
                                                           ReplayScrubber& scrubber,
                                                           ReplayPresentation& presentation,
                                                           ReplayAuthoring& authoring,
                                                           ReplayPrediction& prediction,
                                                           const ReplayRestoreTransaction& transaction,
                                                           RunMousePickupState& mousePickup,
                                                           RunCameraMode normalizedCurrentMode,
                                                           double now,
                                                           const char* path,
                                                           ReplayLiveRestoreRequest& outRequest );
    SkullbonezCore::Core::SbResult CompleteBranchFileProbe( const char* path, const ReplayLiveRestoreOutcome& outcome );
#endif

  private:
    ReplayStartupWorkflowState m_startup;
#ifdef _DEBUG
    ReplayProbeState m_probes;
    ReplayFailureFileProbeState m_failureFile;
#endif
};
} // namespace Runtime
} // namespace SkullbonezCore
