/*
File: SkullbonezSource/Runtime/Replay/ReplayInteractionController.cpp
Purpose:
  Implements replay interaction commands that update replay-owned UI state.

Mental model:
  This controller handles cold replay commands. It decides which selected
  timeline track should become live, asks Run-provided restore APIs to mutate
  the active world, then publishes the scrubber status consistently.

Glossary:
  Presentation restore: V2 artifact target restore that can create a live
    branch from a saved replay file.
  Solver restore: In-memory retained solver sample restored into the live scene.
  Scrubber status: Short UI message and visibility timer shown after commands.

Invariants:
  - Successful restores always return the scrubber to the solver live edge.
  - Restore input is consumed whether the selected target succeeds or fails.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayInteractionController.h
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
#include "ReplayInteractionController.h"

#include "ReplayOverlayLayout.h"

#include <cstdio>
#include <cstring>

namespace SkullbonezCore
{
namespace Basics
{
bool ReplayInteractionController::RestoreScrubberSelectionAsLive( const ReplayLiveRestoreContext& context )
{
    if ( context.outV2Result )
    {
        *context.outV2Result = RunReplayV2TargetRestoreResult();
    }

    char reason[160] = {};
    bool restored = false;
    RunReplayTrack messageTrack = context.replayRuntime.Scrubber().activeTrack;
    if ( context.replayRuntime.HasLoadedPresentation() && context.replayRuntime.Scrubber().historicalSamplePaused &&
         context.replayRuntime.Scrubber().activeTrack == RunReplayTrack::Presentation )
    {
        if ( context.api.enterInteractiveSceneRun )
        {
            context.api.enterInteractiveSceneRun( context.api.user );
        }
        RunReplayV2TargetRestoreResult result;
        const ReplayPresentationSample* selected = context.replayRuntime.CurrentScrubSample();
        const ReplayFrameIndex selectedFrame = selected ? selected->frameIndex : 0;
        restored = selected && context.api.restoreV2ArtifactTargetState &&
                   context.api.restoreV2ArtifactTargetState( context.api.user,
                                                             context.replayRuntime.LoadedPresentation().path,
                                                             selectedFrame,
                                                             true,
                                                             result,
                                                             reason,
                                                             sizeof( reason ) );
        if ( context.outV2Result )
        {
            *context.outV2Result = result;
        }
        messageTrack = RunReplayTrack::Presentation;
        fprintf( stderr,
                 "[replay] V2 file restore %s target_frame=%llu branch_id=%u%s%s\n",
                 restored ? "applied" : "failed",
                 static_cast<unsigned long long>( selectedFrame ),
                 restored ? result.branchId : 0,
                 reason[0] != '\0' ? ": " : "",
                 reason );
    }
    else if ( context.replayRuntime.Scrubber().historicalSamplePaused &&
              context.replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver )
    {
        if ( context.api.enterInteractiveSceneRun )
        {
            context.api.enterInteractiveSceneRun( context.api.user );
        }
        const ReplaySolverFrameSample* sample = context.replayRuntime.CurrentSolverScrubSample();
        restored = sample && context.api.restoreSolverSampleAsLive &&
                   context.api.restoreSolverSampleAsLive( context.api.user, *sample, reason, sizeof( reason ) );
        messageTrack = RunReplayTrack::Solver;
        fprintf( stderr,
                 "[replay] Solver restore %s%s%s\n",
                 restored ? "applied" : "failed",
                 reason[0] != '\0' ? ": " : "",
                 reason );
    }
    else
    {
        sprintf_s( reason, sizeof( reason ), "no historical replay branch target selected" );
        fprintf( stderr, "[replay] Branch restore failed: %s\n", reason );
    }

    if ( restored )
    {
        // Why: a branch restore makes the selected historical frame the new live
        // timeline. Keep the visible scrubber at the live edge instead of
        // leaving it on the parent timeline's old historical position.
        context.replayRuntime.Scrubber().activeTrack = RunReplayTrack::Solver;
        context.replayRuntime.Scrubber().historicalSamplePaused = false;
        context.replayRuntime.Scrubber().branchHovered = false;
        context.replayRuntime.SetAllTrackPositions( 1.0f );
    }

    PublishScrubberRestoreResult( context.replayRuntime.Scrubber(), context.now, restored, messageTrack );
    WriteReason( context.outReason, context.reasonSize, reason );
    return restored;
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
} // namespace Basics
} // namespace SkullbonezCore
