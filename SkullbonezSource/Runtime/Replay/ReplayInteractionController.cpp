/*
File: SkullbonezSource/Runtime/Replay/ReplayInteractionController.cpp
Purpose:
  Implements replay interaction commands that update replay-owned UI state.

Summary:
  This controller handles cold replay commands. It decides which selected
  timeline track should become live, emits a typed restore request for
  ReplayRuntime, then publishes the scrubber status consistently.

Glossary:
  Presentation restore: V2 artifact target restore that can create a live
    branch from a saved replay file.
  Solver restore: In-memory retained solver sample restored into the live scene.
  Scrubber status: Short UI message and visibility timer shown after commands.

Invariants:
  - Successful restores always return the scrubber to the solver live edge.
  - Restore input is consumed whether the selected target succeeds or fails.
  - Restore paths cancel prediction work before mutating live scene authority.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayInteractionController.h
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
#include "ReplayInteractionController.h"
#include "../../Assets/AssetKeys.h"

#include "ReplayOverlayLayout.h"

#include <cstdio>
#include <cstring>

namespace SkullbonezCore
{
namespace Runtime
{
bool ReplayInteractionController::BuildScrubberRestoreRequest( ReplayRuntime& replayRuntime,
                                                               double now,
                                                               ReplayLiveRestoreRequest& outRequest,
                                                               char* outReason,
                                                               std::size_t reasonSize )
{
    outRequest = ReplayLiveRestoreRequest{};
    outRequest.now = now;
    if ( replayRuntime.HasLoadedPresentation() && replayRuntime.Scrubber().historicalSamplePaused &&
         replayRuntime.Scrubber().activeTrack == RunReplayTrack::Presentation )
    {
        replayRuntime.PredictionOwner().CancelJob( false );
        const ReplayPresentationSample* selected = replayRuntime.CurrentScrubSample();
        if ( selected )
        {
            outRequest.kind = ReplayLiveRestoreKind::V2ArtifactTarget;
            outRequest.requestedFrame = selected->frameIndex;
            outRequest.makeLiveBranch = true;
            outRequest.enterInteractive = true;
            outRequest.messageTrack = RunReplayTrack::Presentation;
            strncpy_s( outRequest.path, sizeof( outRequest.path ), replayRuntime.LoadedPresentation().path, _TRUNCATE );
            return true;
        }
    }
    else if ( replayRuntime.Scrubber().historicalSamplePaused &&
              replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver )
    {
        replayRuntime.PredictionOwner().CancelJob( false );
        if ( const ReplaySolverFrameSample* sample = replayRuntime.CurrentSolverScrubSample() )
        {
            outRequest.kind = ReplayLiveRestoreKind::SolverSample;
            outRequest.solverSample = sample;
            outRequest.enterInteractive = true;
            outRequest.messageTrack = RunReplayTrack::Solver;
            return true;
        }
    }

    const char* reason = "no historical replay branch target selected";
    fprintf( stderr, "[replay] Branch restore failed: %s\n", reason );
    PublishScrubberRestoreResult( replayRuntime.Scrubber(), now, false, replayRuntime.Scrubber().activeTrack );
    WriteReason( outReason, reasonSize, reason );
    return false;
}

void ReplayInteractionController::CompleteScrubberRestore( ReplayRuntime& replayRuntime,
                                                           const ReplayLiveRestoreRequest& request,
                                                           bool restored,
                                                           const RunReplayV2TargetRestoreResult& v2Result,
                                                           const char* reason,
                                                           RunReplayV2TargetRestoreResult* outV2Result,
                                                           char* outReason,
                                                           std::size_t reasonSize )
{
    const char* safeReason = reason ? reason : "";
    if ( request.kind == ReplayLiveRestoreKind::V2ArtifactTarget )
    {
        if ( outV2Result )
        {
            *outV2Result = v2Result;
        }
        fprintf( stderr,
                 "[replay] V2 file restore %s target_frame=%llu branch_id=%u%s%s\n",
                 restored ? "applied" : "failed",
                 static_cast<unsigned long long>( request.requestedFrame ),
                 restored ? v2Result.branchId : 0,
                 safeReason[0] != '\0' ? ": " : "",
                 safeReason );
    }
    else
    {
        fprintf( stderr,
                 "[replay] Solver restore %s%s%s\n",
                 restored ? "applied" : "failed",
                 safeReason[0] != '\0' ? ": " : "",
                 safeReason );
    }

    if ( restored )
    {
        // Why: a branch restore makes the selected historical frame the new live
        // timeline. Keep the visible scrubber at the live edge instead of
        // leaving it on the parent timeline's old historical position.
        replayRuntime.Scrubber().activeTrack = RunReplayTrack::Solver;
        replayRuntime.Scrubber().historicalSamplePaused = false;
        replayRuntime.SetAllTrackPositions( 1.0f );
    }
    PublishScrubberRestoreResult( replayRuntime.Scrubber(), request.now, restored, request.messageTrack );
    WriteReason( outReason, reasonSize, safeReason );
}


void ReplayInteractionController::WriteReason( char* outReason, std::size_t reasonSize, const char* reason )
{
    if ( outReason && reasonSize > 0 )
    {
        strncpy_s( outReason, reasonSize, reason ? reason : "restore failed", _TRUNCATE );
    }
}


void ReplayInteractionController::PublishScrubberRestoreResult( RunReplayScrubberState& scrubber,
                                                                double now,
                                                                bool restored,
                                                                RunReplayTrack messageTrack )
{
    scrubber.restoreConsumedThisFrame = true;
    scrubber.saveMessageTrack = messageTrack;
    sprintf_s( scrubber.saveMessage,
               sizeof( scrubber.saveMessage ),
               restored ? ( messageTrack == RunReplayTrack::Presentation ? "V2 FILE BRANCHED" : "SOLVER RESTORED" )
                        : "RESTORE FAILED" );
    scrubber.saveMessageUntil = now + 2.5;
    scrubber.visibleUntil = now + ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS;
    scrubber.visible = true;
}
} // namespace Runtime
} // namespace SkullbonezCore
