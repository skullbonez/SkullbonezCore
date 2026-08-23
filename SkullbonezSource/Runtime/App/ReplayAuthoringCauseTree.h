/*
File: SkullbonezSource/Runtime/App/ReplayAuthoringCauseTree.h
Purpose:
  Declares synchronous App composition for replay cause rows and focus selection.

Summary:
  App combines immutable Prediction evidence with Replay-owned authoring,
  presentation, and scrub state. Neither sibling retains another owner or an
  App callback after either operation returns.

Invariants:
  - Cause rows are committed through ReplayAuthoring's bounded storage.
  - Focus application finishes lower-owner mutations synchronously.
  - Prediction is observed only through const publication and evidence views.

Related:
  - SkullbonezSource/Runtime/App/ReplayAuthoringCauseTree.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.h
  - SkullbonezSource/Runtime/Replay/ReplayAuthoring.h
*/
#pragma once

#include "../Prediction/ReplayPrediction.h"

#include <span>

namespace SkullbonezCore::Physics
{
class ColliderStore;
class PhysicsBodyStore;
} // namespace SkullbonezCore::Physics

namespace SkullbonezCore::Rendering
{
struct RenderInstancePresentationRecord;
}

namespace SkullbonezCore::Runtime
{
class ReplayAuthoring;
class ReplayPresentation;
class ReplayScrubber;
class RuntimeInteractionController;
struct RunReplayCameraState;
struct RunReplayPathVisualizerState;

bool BuildReplayCauseTreeRows( const ReplayPrediction& predictionOwner, ReplayAuthoring& authoring,
                               const RunReplayPathVisualizerState& path, const ReplaySolverFrameSample* solverSample,
                               std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                               const Physics::PhysicsBodyStore& bodyStore, const RunReplayCameraState& camera,
                               int& outCameraFocusedRow );

bool ActivateReplayCauseTreeRow( const ReplayPrediction& predictionOwner, ReplayAuthoring& authoring, int rowIndex,
                                 ReplayPresentation& presentationOwner, ReplayScrubber& scrubberOwner,
                                 const ReplaySolverFrameSample* currentSolverSample,
                                 const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                                 RuntimeInteractionController& interaction, Math::Vector::Vector3& outTargetPosition,
                                 float& outTargetRadius );
} // namespace SkullbonezCore::Runtime
