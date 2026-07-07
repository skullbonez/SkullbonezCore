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
#include "../../Core/Profiler.h"
#include "../../GameObjects/GameModelCollection.h"

#include <algorithm>
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


ReplayVelocityEditInputFrame ReplayInteractionController::BeginVelocityEditInputFrame( ReplayRuntime& replayRuntime,
                                                                                       bool leftDown )
{
    RunReplayVelocityEditState& velocityEdit = replayRuntime.VelocityEdit();
    ReplayVelocityEditInputFrame frame;
    frame.leftDown = leftDown;
    frame.leftPressed = leftDown && !velocityEdit.leftWasDown;
    frame.leftReleased = !leftDown && velocityEdit.leftWasDown;
    velocityEdit.leftWasDown = leftDown;
    return frame;
}


void ReplayInteractionController::SetVelocityEditHoverAxes( ReplayRuntime& replayRuntime,
                                                            int linearAxis,
                                                            int angularAxis )
{
    replayRuntime.VelocityEdit().hotLinearAxis = linearAxis;
    replayRuntime.VelocityEdit().hotAngularAxis = angularAxis;
}


ReplayVelocityEditResetResult ReplayInteractionController::ResetVelocityEditInteraction( ReplayRuntime& replayRuntime,
                                                                                         bool clearHoverAxes )
{
    RunReplayVelocityEditState& velocityEdit = replayRuntime.VelocityEdit();
    ReplayVelocityEditResetResult result;
    if ( clearHoverAxes )
    {
        velocityEdit.hotLinearAxis = -1;
        velocityEdit.hotAngularAxis = -1;
    }
    result.endDragGesture = velocityEdit.dragging;
    velocityEdit.dragging = false;
    velocityEdit.draggingAngular = false;
    velocityEdit.activeAxis = -1;
    result.releaseMouseCapture = velocityEdit.mouseCaptured;
    velocityEdit.mouseCaptured = false;
    return result;
}


ReplayVelocityEditResetResult ReplayInteractionController::EndVelocityEditDrag( ReplayRuntime& replayRuntime )
{
    return ResetVelocityEditInteraction( replayRuntime, false );
}


void ReplayInteractionController::BeginVelocityEditDrag( ReplayRuntime& replayRuntime,
                                                         const ReplayVelocityEditDragStart& start )
{
    // Invariant: replay velocity edit drag state stores the body handle's model
    // slot only as gesture metadata. Live velocity mutation resolves the handle
    // again before touching PhysicsBodyStore.
    replayRuntime.Prediction().enabled = true;
    RunReplayVelocityEditState& velocityEdit = replayRuntime.VelocityEdit();
    velocityEdit.dragging = true;
    velocityEdit.draggingAngular = start.angular;
    velocityEdit.activeAxis = start.axis;
    velocityEdit.dragStartAxisT = start.axisT;
    velocityEdit.dragStartAngle = start.angle;
    velocityEdit.dragStartLinearVelocity = start.linearVelocity;
    velocityEdit.dragStartAngularVelocity = start.angularVelocity;
}


void ReplayInteractionController::SelectVelocityEditTarget( ReplayRuntime& replayRuntime, double visibleUntil )
{
    replayRuntime.Prediction().enabled = true;
    replayRuntime.Scrubber().visibleUntil = visibleUntil;
    replayRuntime.Scrubber().visible = true;
}


bool ReplayInteractionController::ApplyVelocityEditToBody( const ReplayVelocityEditApplyContext& context )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Apply" );
    // Invariant: replay velocity edit mutates live physics state deliberately,
    // then marks prediction dirty so retained and predicted overlays do not
    // present stale paths for the edited body.
    if ( !context.body.IsValid() )
    {
        return false;
    }

    Math::Vector::Vector3 clampedLinear = context.linearVelocity;
    Math::Vector::Vector3 clampedAngular = context.angularVelocity;
    clampedLinear.x = std::clamp( clampedLinear.x, -context.linearVelocityLimit, context.linearVelocityLimit );
    clampedLinear.y = std::clamp( clampedLinear.y, -context.linearVelocityLimit, context.linearVelocityLimit );
    clampedLinear.z = std::clamp( clampedLinear.z, -context.linearVelocityLimit, context.linearVelocityLimit );
    clampedAngular.x = std::clamp( clampedAngular.x, -context.angularVelocityLimit, context.angularVelocityLimit );
    clampedAngular.y = std::clamp( clampedAngular.y, -context.angularVelocityLimit, context.angularVelocityLimit );
    clampedAngular.z = std::clamp( clampedAngular.z, -context.angularVelocityLimit, context.angularVelocityLimit );

    if ( !context.models.GetPhysicsEngine().SetBodyVelocity( context.body, clampedLinear, clampedAngular, true ) )
    {
        return false;
    }
    context.replayRuntime.MarkPredictionDirty();
    context.replayRuntime.Scrubber().visibleUntil = context.visibleUntil;
    context.replayRuntime.Scrubber().visible = true;
    return true;
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
