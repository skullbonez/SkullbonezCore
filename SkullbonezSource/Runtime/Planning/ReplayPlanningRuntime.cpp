/*
File : SkullbonezSource / Runtime / Planning /
       ReplayPlanningRuntime.cpp Purpose : Implements the planning sibling's input, update, and Physics-mutation sequence.

                                           Summary : Planning samples immutable replay /
       prediction publications,
    updates its four retained product owners,
    and applies planner velocity candidates through Physics.No borrowed sibling owner survives the frame
            call.

        Invariants : -Planning pointer geometry is shared with rendering.-
        Stable scene identity is authoritative;
dense rows are repairable hints.- Baseline capture precedes candidate mutation,
    and successful mutation commits prediction invalidation before the planner advances.

            Related : -SkullbonezSource /
            Runtime / Planning / ReplayPlanningRuntime.h -
        SkullbonezSource / Runtime / Planning / ReplayPlanningOverlayLayout.h - Agentic / Reference / engine -
        glossary.md */
#include "ReplayPlanningRuntime.h"

#include "ReplayPlanningOverlayLayout.h"
#include "../Interaction/RuntimePickService.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsWorldForces.h"

#include <cstdio>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
float ColliderRadius( const Physics::ColliderStore& colliderStore, Physics::PhysicsSceneObjectId sceneObjectId ) noexcept
{
    const Physics::PhysicsColliderHandle handle = colliderStore.HandleForSceneObjectId( sceneObjectId );
    const Physics::ColliderRecord* collider = colliderStore.RecordForHandle( handle );
    return collider ? collider->boundingRadius : 0.0f;
}

// Concept: Planning treats the heaviest fixed body as the central guide body.
// Stable scene identity is copied out; no body-store borrow survives this scan.
bool ReadGuideBodyState( const Physics::PhysicsBodyStore& bodyStore, Physics::PhysicsSceneObjectId id,
                         ReplayGuideBodyState& outState ) noexcept
{
    if ( !id.IsValid() )
    {
        return false;
    }

    const Physics::PhysicsBodyHandle handle = bodyStore.HandleForSceneObjectId( id );
    Physics::ModelRowHint row;
    const int resolvedRow = bodyStore.ResolveModelRow( handle, row );
    const std::span<const Physics::PhysicsBodyRecord> records = bodyStore.Records();
    const Physics::PhysicsBodyHotFieldsConstView hot = bodyStore.HotFields();

    if ( resolvedRow < 0 || static_cast<std::size_t>( resolvedRow ) >= records.size() ||
         static_cast<std::size_t>( resolvedRow ) >= hot.positionX.size() )
    {
        return false;
    }

    const std::size_t bodyIndex = static_cast<std::size_t>( resolvedRow );
    outState.id = records[bodyIndex].sceneObjectId;
    outState.position = Physics::PhysicsBodyPosition( hot, bodyIndex );
    outState.linearVelocity = Physics::PhysicsBodyLinearVelocity( hot, bodyIndex );
    outState.mass = records[bodyIndex].mass;
    outState.valid = true;
    return true;
}

bool ReadGuideSunState( const Physics::PhysicsBodyStore& bodyStore, ReplayGuideBodyState& outState ) noexcept
{
    const std::span<const Physics::PhysicsBodyRecord> records = bodyStore.Records();
    const Physics::PhysicsBodyHotFieldsConstView hot = bodyStore.HotFields();
    float heaviestFixedMass = 0.0f;

    for ( std::size_t bodyIndex = 0; bodyIndex < records.size() && bodyIndex < hot.fixed.size(); ++bodyIndex )
    {
        if ( hot.fixed[bodyIndex] == 0 || records[bodyIndex].mass <= heaviestFixedMass )
        {
            continue;
        }

        heaviestFixedMass = records[bodyIndex].mass;
        outState.id = records[bodyIndex].sceneObjectId;
        outState.position = Physics::PhysicsBodyPosition( hot, bodyIndex );
        outState.linearVelocity = Physics::PhysicsBodyLinearVelocity( hot, bodyIndex );
        outState.mass = heaviestFixedMass;
        outState.valid = true;
    }

    return outState.valid;
}

bool ReadPlannerBodyState( const Physics::PhysicsBodyStore& bodyStore, Physics::PhysicsSceneObjectId id,
                           ReplayTripPlannerBodyState& outState ) noexcept
{
    const Physics::PhysicsBodyHandle handle = bodyStore.HandleForSceneObjectId( id );
    Physics::ModelRowHint row;
    const int bodyIndex = bodyStore.ResolveModelRow( handle, row );
    const std::span<const Physics::PhysicsBodyRecord> records = bodyStore.Records();
    const Physics::PhysicsBodyHotFieldsConstView hot = bodyStore.HotFields();

    if ( bodyIndex < 0 || static_cast<std::size_t>( bodyIndex ) >= records.size() ||
         static_cast<std::size_t>( bodyIndex ) >= hot.positionX.size() )
    {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>( bodyIndex );
    outState.id = records[index].sceneObjectId;
    outState.position = Physics::PhysicsBodyPosition( hot, index );
    outState.linearVelocity = Physics::PhysicsBodyLinearVelocity( hot, index );
    outState.mass = records[index].mass;
    outState.valid = true;
    return true;
}

bool ReadPlannerSunState( const Physics::PhysicsBodyStore& bodyStore, ReplayTripPlannerBodyState& outState ) noexcept
{
    ReplayGuideBodyState guideState;

    if ( !ReadGuideSunState( bodyStore, guideState ) )
    {
        return false;
    }

    outState.id = guideState.id;
    outState.position = guideState.position;
    outState.linearVelocity = guideState.linearVelocity;
    outState.mass = guideState.mass;
    outState.valid = true;
    return true;
}

ReplayPorkchopBodyState PorkchopBody( const ReplayGuideBodyState& state ) noexcept
{
    ReplayPorkchopBodyState result;
    result.id = state.id;
    result.position = state.position;
    result.linearVelocity = state.linearVelocity;
    result.mass = state.mass;
    result.valid = state.valid;
    return result;
}
} // namespace

void ReplayPlanningRuntime::ToggleGuideArcs() noexcept
{
    m_guideArcs.Toggle();
}

void ReplayPlanningRuntime::SetGuideArcsEnabled( bool enabled ) noexcept
{
    m_guideArcs.SetEnabled( enabled );
}

void ReplayPlanningRuntime::TogglePorkchopPanel() noexcept
{
    m_porkchopPanel.Toggle();
}

bool ReplayPlanningRuntime::QueueTripPlannerCommand( const ReplayTripPlannerCommand& command ) noexcept
{
    return m_tripPlanner.QueueCommand( command );
}

void ReplayPlanningRuntime::SetInterceptTarget( Physics::PhysicsSceneObjectId id, Physics::ModelRowHint modelRow ) noexcept
{
    m_interceptReadout.SetTarget( id, modelRow );
}

void ReplayPlanningRuntime::ClearInterceptTarget() noexcept
{
    m_interceptReadout.ClearTarget();
}

void ReplayPlanningRuntime::ClearState() noexcept
{
    m_interceptReadout.ClearTarget();
    m_guideArcs.Reset();
    ResetTransientPlanState();
}

void ReplayPlanningRuntime::ResetTransientPlanState() noexcept
{
    m_porkchopPanel.Reset();
    m_tripPlanner.ResetForSceneDiscard();
    m_causeInspection.Reset();
}

ReplayTripPlannerVelocityMutation ReplayPlanningRuntime::CancelActivePlan() noexcept
{
    return m_tripPlanner.CancelActivePlan();
}

void ReplayPlanningRuntime::AbortTripPlannerMutation() noexcept
{
    m_tripPlanner.Abort();
}

void ReplayPlanningRuntime::ConfirmTripPlannerVelocityApplied() noexcept
{
    m_tripPlanner.ConfirmVelocityApplied();
}

ReplayGuideArcsView ReplayPlanningRuntime::GuideArcsView() const noexcept
{
    return m_guideArcs.View();
}

ReplayInterceptView ReplayPlanningRuntime::InterceptView() const noexcept
{
    return m_interceptReadout.View();
}

const ReplayPorkchopPanelView& ReplayPlanningRuntime::PorkchopView() const noexcept
{
    return m_porkchopPanel.View();
}

const ReplayTripPlannerView& ReplayPlanningRuntime::TripPlannerView() const noexcept
{
    return m_tripPlanner.View();
}

ReplayCauseInspection& ReplayPlanningRuntime::CauseInspection() noexcept
{
    return m_causeInspection;
}

ReplayCauseInspectionView ReplayPlanningRuntime::CauseInspectionView() const noexcept
{
    return m_causeInspection.View();
}

bool ReplayPlanningRuntime::HasActiveState() const noexcept
{
    return m_interceptReadout.HasTarget() || m_guideArcs.Enabled() || m_porkchopPanel.Visible() ||
           m_tripPlanner.RequiresLiveInput() || m_causeInspection.View().mode != ReplayCauseInspectionMode::Inactive;
}

bool ReplayPlanningRuntime::HasInterceptTarget() const noexcept
{
    return m_interceptReadout.HasTarget();
}

const UI::UIDrawList& ReplayPlanningRuntime::ComposeOverlayDrawList( const ReplayOverlay::ReplayOverlayStateView& replay,
                                                                     bool gameUiSurfaceActive, bool scenePhysicsEnabled,
                                                                     ReplayOverlay::ReplayOverlayGestureView gesture,
                                                                     ReplayOverlay::ReplayOverlayViewport viewport,
                                                                     double nowSeconds )
{
    return m_overlayDrawOwner.Compose( replay, gameUiSurfaceActive, scenePhysicsEnabled, gesture, viewport, nowSeconds );
}


bool ReplayPlanningRuntime::TickPointerSurface( bool uiBlocksMouse, int screenWidth, int clientX, int clientY,
                                                bool hasClientPosition, bool leftPressed )
{
    bool porkchopOwnsMouse = false;

    if ( m_porkchopPanel.Visible() && hasClientPosition )
    {
        const ReplayPorkchopPanelView& porkchop = m_porkchopPanel.View();
        const UI::UIRect panel = ReplayOverlay::ReplayPorkchopPanelRect( screenWidth );
        const float pointerX = static_cast<float>( clientX );
        const float pointerY = static_cast<float>( clientY );
        porkchopOwnsMouse = !uiBlocksMouse && pointerX >= panel.x && pointerY >= panel.y && pointerX < panel.x + panel.w &&
                            pointerY < panel.y + panel.h;

        std::size_t cellIndex = 0u;
        const bool hasCell = porkchopOwnsMouse &&
                             ReplayOverlay::ReplayPorkchopCellAtPointer( screenWidth, clientX, clientY, cellIndex ) &&
                             cellIndex < porkchop.completedCells;

        m_porkchopPanel.SetHoveredCell( hasCell ? static_cast<int>( cellIndex ) : -1 );

        if ( hasCell && leftPressed && m_porkchopPanel.SelectCell( cellIndex ) )
        {
            const ReplayPorkchopPanelView& selected = m_porkchopPanel.View();
            (void)m_tripPlanner.QueueCommand(
                { ReplayTripPlannerCommandKind::SetTimeOfFlight, selected.selectedTimeOfFlightSeconds } );
        }
    }
    else
    {
        m_porkchopPanel.SetHoveredCell( -1 );
    }

    bool tripPlannerOwnsMouse = false;
    const ReplayTripPlannerView& planner = m_tripPlanner.View();

    if ( planner.visible && planner.available && hasClientPosition )
    {
        ReplayOverlay::ReplayTripPlannerSurface surface;
        ReplayOverlay::BuildReplayTripPlannerSurface( planner, screenWidth, surface );
        surface.ResolvePointer( clientX, clientY, uiBlocksMouse || porkchopOwnsMouse );
        tripPlannerOwnsMouse = surface.consumesPointer;

        if ( leftPressed && surface.hasHotControl )
        {
            const ReplayOverlay::ReplayTripPlannerControlRow* control = surface.Find( surface.hotControl );

            if ( control && control->action != ReplayTripPlannerCommandKind::None )
            {
                (void)m_tripPlanner.QueueCommand( { control->action } );
            }
        }
    }

    return porkchopOwnsMouse || tripPlannerOwnsMouse;
}

// Invariant: pointer ownership uses the same ReplayOverlay geometry rendered
// later. UI blocking and porkchop capture are resolved before trip controls.
ReplayPathPickResult ReplayPlanningRuntime::TryPickInterceptTarget( const ReplayPathPickInput& input,
                                                                    const Physics::PhysicsBodyStore& bodyStore,
                                                                    const Physics::ColliderStore& colliderStore )
{
    ReplayPathPickResult result;

    if ( !input.hasWorldRay )
    {
        if ( input.clearOnMiss )
        {
            m_interceptReadout.ClearTarget();
        }

        return result;
    }

    RuntimePickRequest request;
    request.purpose = RuntimePickPurpose::ReplayPathTarget;
    request.bodyStore = &bodyStore;
    request.colliderStore = &colliderStore;
    request.rayOrigin = input.rayOrigin;
    request.rayDirection = input.rayDirection;
    RuntimePickResult pick;

    if ( RuntimePickService::TryPickModel( request, pick ) )
    {
        const Physics::PhysicsBodyRecord* body = bodyStore.RecordForHandle( pick.body );

        if ( body )
        {
            m_interceptReadout.SetTarget( body->sceneObjectId, pick.modelRow );
            result.picked = true;
        }
    }
    else if ( input.clearOnMiss )
    {
        m_interceptReadout.ClearTarget();
    }

    return result;
}

ReplayTripPlannerVelocityMutation ReplayPlanningRuntime::BeginFrameBeforePrediction(
    Physics::PhysicsEngine& physics, const ReplayPlanningSceneView& scene, const Physics::PhysicsWorldForces& worldForces,
    const RunReplayPathVisualizerState& path, const ReplayPredictionControlsView& predictionControls, bool liveAdvanceHeld )
{
    return BeginTripPlannerFrame( physics, scene, worldForces, path, predictionControls, liveAdvanceHeld );
}

ReplayTripPlannerVelocityMutation ReplayPlanningRuntime::FinishFrameAfterPrediction(
    Physics::PhysicsEngine& physics, const ReplayPlanningSceneView& scene, const Physics::PhysicsWorldForces& worldForces,
    double nowSeconds, const RunReplayPathVisualizerState& path, const ReplayPredictionTimelineView& predictionTimeline,
    const ReplayPredictionTopologyView& predictionTopology, const ReplayPredictionControlsView& predictionControls,
    bool liveAdvanceHeld )
{
    UpdateInterceptReadout( physics, worldForces.mutualGravity.enabled, path, predictionTimeline, predictionTopology,
                            predictionControls );
    const ReplayTripPlannerVelocityMutation mutation = ObserveTripPlannerPrediction( path, predictionTimeline,
                                                                                     predictionControls, liveAdvanceHeld );
    UpdateGuideArcs( physics, scene, worldForces, nowSeconds );
    UpdatePorkchopPanel( physics, scene, worldForces, nowSeconds );
    return mutation;
}

void ReplayPlanningRuntime::UpdateInterceptReadout( Physics::PhysicsEngine& physics, bool mutualGravityEnabled,
                                                    const RunReplayPathVisualizerState& path,
                                                    const ReplayPredictionTimelineView& timeline,
                                                    const ReplayPredictionTopologyView& topology,
                                                    const ReplayPredictionControlsView& controls )
{
    ReplayInterceptUpdateInput input;
    input.frames = timeline.frames;
    input.shipId = path.targetId;
    input.targetId = m_interceptReadout.TargetId();
    input.generation = timeline.generation;
    input.topologyVersion = topology.version;
    input.usingBuildFrames = timeline.usingBuildFrames;
    input.enabled = mutualGravityEnabled && controls.enabled && path.hasTarget && m_interceptReadout.HasTarget();

    if ( !input.enabled )
    {
        m_interceptReadout.Update( input );
        return;
    }

    const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
    const Physics::ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( physics );
    Physics::ModelRowHint targetRow = m_interceptReadout.TargetModelRow();
    const Physics::PhysicsBodyHandle targetHandle = bodyStore.HandleForSceneObjectId( m_interceptReadout.TargetId(),
                                                                                      targetRow.value );

    if ( bodyStore.ResolveModelRow( targetHandle, targetRow ) )
    {
        m_interceptReadout.SetTarget( m_interceptReadout.TargetId(), targetRow );
    }

    input.shipRadius = ColliderRadius( colliderStore, input.shipId );
    input.targetRadius = ColliderRadius( colliderStore, input.targetId );
    m_interceptReadout.Update( input );
}

void ReplayPlanningRuntime::UpdateGuideArcs( Physics::PhysicsEngine& physics, const ReplayPlanningSceneView& scene,
                                             const Physics::PhysicsWorldForces& worldForces, double nowSeconds )
{
    ReplayGuideArcsUpdateInput input;
    input.nowSeconds = nowSeconds;
    input.mutualGravityEnabled = worldForces.mutualGravity.enabled;
    input.gravitationalConstant = worldForces.mutualGravity.gravitationalConstant;

    if ( m_guideArcs.RefreshDue( nowSeconds ) && input.mutualGravityEnabled )
    {
        const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
        (void)ReadGuideSunState( bodyStore, input.sun );
        (void)ReadGuideBodyState( bodyStore, scene.earthId, input.earth );
        (void)ReadGuideBodyState( bodyStore, scene.marsId, input.mars );
    }

    m_guideArcs.Update( input );
}

void ReplayPlanningRuntime::UpdatePorkchopPanel( Physics::PhysicsEngine& physics, const ReplayPlanningSceneView& scene,
                                                 const Physics::PhysicsWorldForces& worldForces, double nowSeconds )
{
    if ( !m_porkchopPanel.Visible() )
    {
        return;
    }

    const Physics::PhysicsSceneObjectId targetId = m_interceptReadout.TargetId();

    if ( m_porkchopPanel.NeedsRefresh( targetId, worldForces.mutualGravity.enabled ) )
    {
        ReplayPorkchopSweepInput input;
        input.gravitationalConstant = worldForces.mutualGravity.gravitationalConstant;
        input.epochSeconds = nowSeconds;
        input.mutualGravityEnabled = worldForces.mutualGravity.enabled;
        input.target.id = targetId;

        if ( input.mutualGravityEnabled && targetId.IsValid() )
        {
            const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
            ReplayGuideBodyState sun;
            ReplayGuideBodyState earth;
            ReplayGuideBodyState target;
            (void)ReadGuideSunState( bodyStore, sun );
            (void)ReadGuideBodyState( bodyStore, scene.earthId, earth );
            (void)ReadGuideBodyState( bodyStore, scene.targetId, target );
            input.sun = PorkchopBody( sun );
            input.departure = PorkchopBody( earth );
            input.target = PorkchopBody( target );
        }

        m_porkchopPanel.BeginSweep( input );
    }

    m_porkchopPanel.AdvanceSweep( nowSeconds );
}

ReplayTripPlannerVelocityMutation ReplayPlanningRuntime::BeginTripPlannerFrame(
    Physics::PhysicsEngine& physics, const ReplayPlanningSceneView& scene, const Physics::PhysicsWorldForces& worldForces,
    const RunReplayPathVisualizerState& path, const ReplayPredictionControlsView& controls, bool liveAdvanceHeld )
{
    if ( !m_tripPlanner.RequiresLiveInput() )
    {
        return {};
    }

    const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
    ReplayTripPlannerLiveInput input;
    input.gravitationalConstant = worldForces.mutualGravity.gravitationalConstant;
    input.predictionHorizonSeconds = controls.horizonSeconds;
    input.mutualGravityEnabled = worldForces.mutualGravity.enabled;
    input.targetSelected = path.hasTarget && m_interceptReadout.HasTarget();
    input.liveAdvanceHeld = liveAdvanceHeld;
    (void)ReadPlannerSunState( bodyStore, input.sun );
    (void)ReadPlannerBodyState( bodyStore, path.targetId, input.ship );
    (void)ReadPlannerBodyState( bodyStore, m_interceptReadout.TargetId(), input.target );
    input.targetName = scene.targetName[0] != '\0' ? scene.targetName : nullptr;
    return m_tripPlanner.BeginFrame( input );
}

// Invariant: baseline preparation precedes the first candidate write. A failed
// candidate attempts rollback through the same Physics velocity seam before aborting.
ReplayTripPlannerVelocityMutation
ReplayPlanningRuntime::ObserveTripPlannerPrediction( const RunReplayPathVisualizerState& path,
                                                     const ReplayPredictionTimelineView& timeline,
                                                     const ReplayPredictionControlsView& controls, bool liveAdvanceHeld )
{
    if ( !m_tripPlanner.AwaitingPrediction() )
    {
        return {};
    }

    ReplayTripPlannerPredictionInput input;
    input.frames = timeline.frames;
    input.intercept = m_interceptReadout.View();
    input.shipId = path.targetId;
    input.targetId = m_interceptReadout.TargetId();
    input.generation = timeline.generation;
    input.complete = timeline.complete;
    input.cancelled = !controls.enabled;
    input.liveAdvanceHeld = liveAdvanceHeld;
    input.targetAvailable = path.hasTarget && m_interceptReadout.HasTarget();
    return m_tripPlanner.ObservePrediction( input );
}
} // namespace Runtime
} // namespace SkullbonezCore
