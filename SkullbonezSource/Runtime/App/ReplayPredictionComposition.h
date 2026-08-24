/*
File: SkullbonezSource/Runtime/App/ReplayPredictionComposition.h
Purpose:
  Declares App-built detached inputs for Prediction.

Summary:
  App samples sibling owners into bounded Prediction values immediately before
  each synchronous prediction operation. Prediction never receives or retains
  a Scene, Replay, Tools, or Editor owner.

Invariants:
  - Destination storage is caller-owned and bounded by scene capacity.
  - Returned spans expire before Scene mutation or the next composition turn.

Related:
  - SkullbonezSource/Runtime/App/ReplayPredictionComposition.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h
  - SkullbonezSource/Runtime/Scene/SceneEntityStore.h
*/
#pragma once

#include "../Prediction/ReplayPrediction.h"

#include <span>

namespace SkullbonezCore::Runtime
{
class SceneEntityStore;
struct ReplayAuthoringPredictionRequest;
class ReplaySolverRecorder;

ReplayPredictionSceneView BuildReplayPredictionSceneView( const SceneEntityStore& entities,
                                                          std::span<ReplayPredictionSceneEntityFact> destination ) noexcept;
ReplayPredictionAuthoringCommand
BuildReplayPredictionAuthoringCommand( const ReplayAuthoringPredictionRequest& request ) noexcept;
ReplayPastTrajectoryUpdate RefreshReplayPastTrajectory( ReplayPrediction& prediction, const ReplaySolverRecorder& solver,
                                                        const ReplayPastTrajectoryView& path );
} // namespace SkullbonezCore::Runtime
