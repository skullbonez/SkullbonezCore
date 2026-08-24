/*
File: SkullbonezSource/Runtime/App/ReplayPredictionComposition.cpp
Purpose:
  Builds detached Prediction inputs from sibling runtime owners.

Summary:
  Scene identity and ragdoll grouping are copied into caller-owned bounded
  storage so future-simulation publication can classify bodies without
  borrowing the mutable Scene owner.

Invariants:
  - Model-row order matches the source Scene store for this synchronous turn.
  - A short destination fails closed to the copied prefix; production supplies
    MAX_SCENE_OBJECTS storage.

Related:
  - SkullbonezSource/Runtime/App/ReplayPredictionComposition.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h
*/
#include "ReplayPredictionComposition.h"

#include "../Scene/SceneEntityStore.h"
#include "../Replay/ReplayAuthoring.h"
#include "../Replay/ReplayRecorder.h"

#include <algorithm>

namespace SkullbonezCore::Runtime
{
ReplayPredictionSceneView BuildReplayPredictionSceneView( const SceneEntityStore& entities,
                                                          std::span<ReplayPredictionSceneEntityFact> destination ) noexcept
{
    const std::size_t count = (std::min)( static_cast<std::size_t>( (std::max)( entities.Count(), 0 ) ),
                                          destination.size() );

    for ( std::size_t modelIndex = 0; modelIndex < count; ++modelIndex )
    {
        const SceneEntityRecord& source = entities.At( static_cast<int>( modelIndex ) );
        ReplayPredictionSceneEntityFact& target = destination[modelIndex];
        target.id = source.sceneObjectId;
        target.ragdollRootId = source.behaviorGroup.rootObjectId;
        target.simpleRagdollPart = source.behaviorGroup.kind == SceneBehaviorGroupKind::SimpleRagdoll;
    }

    return ReplayPredictionSceneView { destination.first( count ) };
}

ReplayPredictionAuthoringCommand
BuildReplayPredictionAuthoringCommand( const ReplayAuthoringPredictionRequest& request ) noexcept
{
    ReplayPredictionAuthoringCommand command;
    command.velocityPreviewTargetId = request.velocityPreviewTargetId;
    command.velocityPreviewDelta = request.velocityPreviewDelta;
    command.enablePrediction = request.enablePrediction;
    command.refreshPrediction = request.refreshPrediction;
    command.clearPredictionCache = request.clearPredictionCache;
    command.prepareVelocityMutationBaseline = request.prepareVelocityMutationBaseline;
    command.updateVelocityPreview = request.updateVelocityPreview;
    command.finishVelocityPreview = request.finishVelocityPreview;
    return command;
}

ReplayPastTrajectoryUpdate RefreshReplayPastTrajectory( ReplayPrediction& prediction, const ReplaySolverRecorder& solver,
                                                        const ReplayPastTrajectoryView& path )
{
    const ReplayRecorderStats stats = solver.GetStats();
    const ReplayPredictionRecorderWindow recorder { stats.sampleCount, stats.nextFrameIndex, stats.totalFramesEvicted,
                                                    stats.enabled };
    const ReplayPastTrajectoryRefreshPlan plan = prediction.BeginPastTrajectoryRefresh( recorder, path );

    if ( !plan.appendSamples )
    {
        return plan.update;
    }

    bool appendOk = true;
    bool hasSample = false;
    ReplayFrameIndex firstFrame = 0;
    Physics::ModelRowHint targetModelRow;
    const bool traversalOk = solver.ForEachBodyPositionChronological( path.targetId,
                                                                      [&]( ReplayFrameIndex frame, Physics::ModelRowHint modelRow, const Math::Vector::Vector3& position )
                                                                      {
                                                                      if ( !appendOk )
                                                                      {
                                                                      return;
                                                                      }

                                                                      appendOk = prediction.AppendPastTrajectoryRefreshPoint( path.targetId, frame, modelRow, position );

                                                                      if ( appendOk )
                                                                      {
                                                                      if ( !hasSample )
                                                                      {
                                                                      firstFrame = frame;
                                                                      hasSample = true;
                                                                      }

                                                                      targetModelRow = modelRow;
                                                                      }
                                                                      } );

                                                                      return prediction.CompletePastTrajectoryRefresh( plan, traversalOk && appendOk, hasSample, firstFrame, targetModelRow );
                                                                      }
                                                                      } // namespace SkullbonezCore::Runtime
