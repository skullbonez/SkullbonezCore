/*
File: SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.h
Purpose:
  Owns and sequences the operator-facing planning features built on replay and prediction.

Summary:
  Runtime/App composes this owner beside ReplayRuntime and the Prediction owner.
  Frame methods borrow lower Replay and Prediction values synchronously and
  emit velocity commands for App to apply. The planning owner never stores a
  sibling, reaches back into Run, or mutates the Prediction owner.

Glossary:
  Planning target: Stable scene-object identity selected for intercept analysis.
  Planning surface: Pointer controls for the trip planner and porkchop heatmap.
  Candidate mutation: One planner-produced velocity value for Physics to apply.

Invariants:
  - Cause inspection, intercept, guide, porkchop, and trip-planner state lives only in this owner.
  - Replay and Prediction inputs expire when the consuming method returns.
  - App baseline-gates and applies each candidate velocity before returning the receipt to Planning.
  - Hidden planning surfaces do not scan scene or Physics stores.

Related:
  - SkullbonezSource/Runtime/App/Run.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h
  - SkullbonezSource/Runtime/Replay/ReplayPathPackets.h
*/
#pragma once

#include "ReplayGuideArcs.h"
#include "ReplayCauseInspection.h"
#include "ReplayInterceptReadout.h"
#include "ReplayPorkchopPanel.h"
#include "ReplayTripPlanner.h"
#include "../Prediction/ReplayPredictionView.h"
#include "../Replay/ReplayPathPackets.h"
#include "ReplayOverlayRenderer.h"

#include <cstddef>

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
struct ReplayPlanningPointerInput
{
    // Lifetime: App copies one published pointer snapshot; Planning retains none of it.
    int clientX = 0;
    int clientY = 0;
    bool hasClientPosition = false;
    bool leftPressed = false;
};

struct ReplayPlanningSceneView
{
    // Stable identities and bounded display text replace a mutable Scene-store borrow.
    Physics::PhysicsSceneObjectId earthId;
    Physics::PhysicsSceneObjectId marsId;
    Physics::PhysicsSceneObjectId targetId;
    char targetName[32] = {};
};

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
    ReplayTripPlannerVelocityMutation CancelActivePlan() noexcept;
    void AbortTripPlannerMutation() noexcept;
    void ConfirmTripPlannerVelocityApplied() noexcept;

    ReplayGuideArcsView GuideArcsView() const noexcept;
    ReplayInterceptView InterceptView() const noexcept;
    const ReplayPorkchopPanelView& PorkchopView() const noexcept;
    const ReplayTripPlannerView& TripPlannerView() const noexcept;
    ReplayCauseInspection& CauseInspection() noexcept;
    ReplayCauseInspectionView CauseInspectionView() const noexcept;
    bool HasActiveState() const noexcept;
    bool HasInterceptTarget() const noexcept;
    const UI::UIDrawList& ComposeOverlayDrawList( const ReplayOverlay::ReplayOverlayStateView& replay,
                                                  bool gameUiSurfaceActive, bool scenePhysicsEnabled,
                                                  ReplayOverlay::ReplayOverlayGestureView gesture,
                                                  ReplayOverlay::ReplayOverlayViewport viewport, double nowSeconds );

    // Returns whether either visible planning surface owns the pointer.
    bool TickPointerSurface( bool uiBlocksMouse, int screenWidth, const ReplayPlanningPointerInput& pointer );
    ReplayPathPickResult TryPickInterceptTarget( const ReplayPathPickInput& input,
                                                 const Physics::PhysicsBodyStore& bodyStore,
                                                 const Physics::ColliderStore& colliderStore );

    ReplayTripPlannerVelocityMutation BeginFrameBeforePrediction( Physics::PhysicsEngine& physics,
                                                                  const ReplayPlanningSceneView& scene,
                                                                  const Physics::PhysicsWorldForces& worldForces,
                                                                  const RunReplayPathVisualizerState& path,
                                                                  const ReplayPredictionPresentationView& prediction,
                                                                  bool liveAdvanceHeld );
    ReplayTripPlannerVelocityMutation FinishFrameAfterPrediction( Physics::PhysicsEngine& physics,
                                                                  const ReplayPlanningSceneView& scene,
                                                                  const Physics::PhysicsWorldForces& worldForces,
                                                                  double nowSeconds,
                                                                  const RunReplayPathVisualizerState& path,
                                                                  const ReplayPredictionPresentationView& prediction,
                                                                  bool liveAdvanceHeld );

  private:
    void UpdateInterceptReadout( Physics::PhysicsEngine& physics, bool mutualGravityEnabled,
                                 const RunReplayPathVisualizerState& path,
                                 const ReplayPredictionPresentationView& prediction );
    void UpdateGuideArcs( Physics::PhysicsEngine& physics, const ReplayPlanningSceneView& scene,
                          const Physics::PhysicsWorldForces& worldForces, double nowSeconds );
    void UpdatePorkchopPanel( Physics::PhysicsEngine& physics, const ReplayPlanningSceneView& scene,
                              const Physics::PhysicsWorldForces& worldForces, double nowSeconds );
    ReplayTripPlannerVelocityMutation BeginTripPlannerFrame( Physics::PhysicsEngine& physics,
                                                              const ReplayPlanningSceneView& scene,
                                                              const Physics::PhysicsWorldForces& worldForces,
                                                              const RunReplayPathVisualizerState& path,
                                                              const ReplayPredictionPresentationView& prediction,
                                                              bool liveAdvanceHeld );
    ReplayTripPlannerVelocityMutation ObserveTripPlannerPrediction( const RunReplayPathVisualizerState& path,
                                                                    const ReplayPredictionPresentationView& prediction,
                                                                    bool liveAdvanceHeld );

    ReplayInterceptReadout m_interceptReadout;
    ReplayGuideArcs m_guideArcs;
    ReplayPorkchopPanel m_porkchopPanel;
    ReplayTripPlanner m_tripPlanner;
    ReplayCauseInspection m_causeInspection;
    ReplayOverlay::ReplayOverlayDrawOwner m_overlayDrawOwner;
};
} // namespace Runtime
} // namespace SkullbonezCore
