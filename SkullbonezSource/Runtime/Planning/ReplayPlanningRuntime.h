/*
File: SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.h
Purpose:
  Owns and sequences the operator-facing planning features built on replay and prediction.

Summary:
  Runtime/App composes this owner beside ReplayRuntime and the Prediction owner.
  Frame methods borrow lower Replay and Prediction values synchronously; the
  planning owner never stores either sibling or reaches back into Run.

Glossary:
  Planning target: Stable scene-object identity selected for intercept analysis.
  Planning surface: Pointer controls for the trip planner and porkchop heatmap.
  Candidate mutation: One planner-produced velocity value for Physics to apply.

Invariants:
  - Intercept, guide, porkchop, and trip-planner state lives only in this owner.
  - Replay and Prediction inputs expire when the consuming method returns.
  - A candidate velocity is baseline-gated before Physics mutation and prediction refresh.
  - Hidden planning surfaces do not scan scene or Physics stores.

Related:
  - SkullbonezSource/Runtime/App/Run.h
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.h
  - SkullbonezSource/Runtime/Replay/ReplayPathPackets.h
*/
#pragma once

#include "ReplayGuideArcs.h"
#include "ReplayInterceptReadout.h"
#include "ReplayPorkchopPanel.h"
#include "ReplayTripPlanner.h"
#include "../Prediction/ReplayPrediction.h"
#include "../Replay/ReplayPresentation.h"

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
class PhysicsEngine;
struct PhysicsWorldForces;
} // namespace Physics
namespace Runtime
{
class InputRouter;
class SceneEntityStore;

class ReplayPlanningRuntime
{
  public:
    void ToggleGuideArcs() noexcept;
    void SetGuideArcsEnabled( bool enabled ) noexcept;
    void TogglePorkchopPanel() noexcept;
    bool QueueTripPlannerCommand( const ReplayTripPlannerCommand& command ) noexcept;
    void SetInterceptTarget( Physics::PhysicsSceneObjectId id, Physics::ModelRowHint modelRow ) noexcept;
    void ClearInterceptTarget() noexcept;
    void ClearState() noexcept;
    void ResetTransientPlanState() noexcept;
    bool CancelActivePlan( Physics::PhysicsEngine& physics, ReplayPrediction& predictionOwner );

    ReplayGuideArcsView GuideArcsView() const noexcept;
    ReplayInterceptView InterceptView() const noexcept;
    const ReplayPorkchopPanelView& PorkchopView() const noexcept;
    const ReplayTripPlannerView& TripPlannerView() const noexcept;
    bool HasActiveState() const noexcept;
    bool HasInterceptTarget() const noexcept;
    Physics::PhysicsSceneObjectId InterceptTargetId() const noexcept;
    Physics::ModelRowHint InterceptTargetModelRow() const noexcept;

    // Returns whether either visible planning surface owns the pointer.
    bool TickPointerSurface( bool uiBlocksMouse, int screenWidth, InputRouter& inputRouter );
    ReplayPathPickResult TryPickInterceptTarget( const ReplayPathPickInput& input,
                                                 const Physics::PhysicsBodyStore& bodyStore,
                                                 const Physics::ColliderStore& colliderStore );

    void BeginFrameBeforePrediction( Physics::PhysicsEngine& physics,
                                     const SceneEntityStore& entities,
                                     const Physics::PhysicsWorldForces& worldForces,
                                     const RunReplayPathVisualizerState& path,
                                     const ReplayPredictionPresentationView& prediction,
                                     bool liveAdvanceHeld,
                                     ReplayPrediction& predictionOwner );
    void FinishFrameAfterPrediction( Physics::PhysicsEngine& physics,
                                     const SceneEntityStore& entities,
                                     const Physics::PhysicsWorldForces& worldForces,
                                     double nowSeconds,
                                     const RunReplayPathVisualizerState& path,
                                     const ReplayPredictionPresentationView& prediction,
                                     bool liveAdvanceHeld,
                                     ReplayPrediction& predictionOwner );

  private:
    void UpdateInterceptReadout( Physics::PhysicsEngine& physics,
                                 bool mutualGravityEnabled,
                                 const RunReplayPathVisualizerState& path,
                                 const ReplayPredictionPresentationView& prediction );
    void UpdateGuideArcs( Physics::PhysicsEngine& physics,
                          const SceneEntityStore& entities,
                          const Physics::PhysicsWorldForces& worldForces,
                          double nowSeconds );
    void UpdatePorkchopPanel( Physics::PhysicsEngine& physics,
                              const SceneEntityStore& entities,
                              const Physics::PhysicsWorldForces& worldForces,
                              double nowSeconds );
    void BeginTripPlannerFrame( Physics::PhysicsEngine& physics,
                                const SceneEntityStore& entities,
                                const Physics::PhysicsWorldForces& worldForces,
                                const RunReplayPathVisualizerState& path,
                                const ReplayPredictionPresentationView& prediction,
                                bool liveAdvanceHeld,
                                ReplayPrediction& predictionOwner );
    void ObserveTripPlannerPrediction( Physics::PhysicsEngine& physics,
                                       const RunReplayPathVisualizerState& path,
                                       const ReplayPredictionPresentationView& prediction,
                                       bool liveAdvanceHeld,
                                       ReplayPrediction& predictionOwner );
    bool ApplyTripPlannerMutation( Physics::PhysicsEngine& physics,
                                   const ReplayTripPlannerVelocityMutation& mutation,
                                   ReplayPrediction& predictionOwner );

    ReplayInterceptReadout m_interceptReadout;
    ReplayGuideArcs m_guideArcs;
    ReplayPorkchopPanel m_porkchopPanel;
    ReplayTripPlanner m_tripPlanner;
};
} // namespace Runtime
} // namespace SkullbonezCore
