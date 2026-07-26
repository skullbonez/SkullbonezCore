/*
File: SkullbonezSource/Runtime/Planning/ReplayTripPlanner.cpp
Purpose:
  Implements bounded trip-planner command, shooting, and ghost-arc behavior.

Summary:
  A Lambert solve creates the first heliocentric candidate. Completed engine
  predictions then either prove the intercept or produce one first-order
  correction, with a strict four-generation cap and honest recoverable failure.

Glossary:
  Live input: One copied snapshot of the sun, ship, target, and scene policy.
  Published generation: Completed isolated-engine prediction inspected once.
  Candidate: Absolute live-world linear velocity requested for the ship.

Invariants:
  - Failed analytic solves and non-improving misses never mutate output values.
  - A prediction generation is observed at most once.
  - Ghost retention scans a completed frame span synchronously and retains only
    fixed downsampled positions.

Related:
  - SkullbonezSource/Runtime/Planning/ReplayTripPlanner.h
  - SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.cpp
*/
#include "ReplayTripPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::Runtime
{
namespace
{
using Math::Vector::Vector3;

bool ValidPlannerBody( const ReplayTripPlannerBodyState& body ) noexcept
{
    return body.valid && body.id.value != 0 && std::isfinite( body.position.x ) && std::isfinite( body.position.y ) &&
           std::isfinite( body.position.z ) && std::isfinite( body.linearVelocity.x ) &&
           std::isfinite( body.linearVelocity.y ) && std::isfinite( body.linearVelocity.z );
}

const RunReplayPredictionBodySample* FindPlannerPredictionBody( const RunReplayPredictionFrame& frame,
                                                                Physics::PhysicsSceneObjectId id ) noexcept
{
    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }

    return nullptr;
}

bool PlanningState( ReplayTripPlannerState state ) noexcept
{
    return state == ReplayTripPlannerState::Seeding || state == ReplayTripPlannerState::AwaitingPrediction ||
           state == ReplayTripPlannerState::Correcting || state == ReplayTripPlannerState::Converged ||
           state == ReplayTripPlannerState::Failed;
}
} // namespace

bool ReplayTripPlanner::QueueCommand( const ReplayTripPlannerCommand& command ) noexcept
{
    if ( command.kind == ReplayTripPlannerCommandKind::None || m_commandCount >= m_commands.size() )
    {
        return false;
    }

    m_commands[m_commandCount++] = command;
    return true;
}

ReplayTripPlannerVelocityMutation ReplayTripPlanner::BeginFrame( const ReplayTripPlannerLiveInput& input ) noexcept
{
    m_view.available = input.mutualGravityEnabled && input.targetSelected && ValidPlannerBody( input.sun ) &&
                       ValidPlannerBody( input.ship ) && ValidPlannerBody( input.target ) &&
                       input.ship.id.value != input.target.id.value && input.gravitationalConstant > 0.0f;

    if ( ( input.liveAdvanceHeld || !m_view.available ) && PlanningState( m_view.state ) )
    {
        // Invariant: losing the planning preconditions is cancellation, not an
        // implicit commit of the last candidate already applied to live Physics.
        m_commandCount = 0;
        return CancelActivePlan();
    }

    ReplayTripPlannerVelocityMutation mutation;
    for ( std::size_t commandIndex = 0; commandIndex < m_commandCount; ++commandIndex )
    {
        const ReplayTripPlannerCommand command = m_commands[commandIndex];
        switch ( command.kind )
        {
        case ReplayTripPlannerCommandKind::TogglePanel:
            m_view.visible = !m_view.visible;
            break;
        case ReplayTripPlannerCommandKind::DecreaseTimeOfFlight:
            if ( m_view.state == ReplayTripPlannerState::Idle )
            {
                m_view.timeOfFlightSeconds = (std::max)( REPLAY_TRIP_PLANNER_MIN_TOF_SECONDS,
                                                         m_view.timeOfFlightSeconds - 0.5f );
            }

            break;
        case ReplayTripPlannerCommandKind::IncreaseTimeOfFlight:
            if ( m_view.state == ReplayTripPlannerState::Idle )
            {
                m_view.timeOfFlightSeconds = (std::min)( (std::max)( REPLAY_TRIP_PLANNER_MIN_TOF_SECONDS,
                                                                     input.predictionHorizonSeconds ),
                                                         m_view.timeOfFlightSeconds + 0.5f );
            }

            break;
        case ReplayTripPlannerCommandKind::SetTimeOfFlight:
            if ( m_view.state == ReplayTripPlannerState::Idle && std::isfinite( command.timeOfFlightSeconds ) )
            {
                m_view.timeOfFlightSeconds = std::clamp(
                    command.timeOfFlightSeconds,
                    REPLAY_TRIP_PLANNER_MIN_TOF_SECONDS,
                    (std::max)( REPLAY_TRIP_PLANNER_MIN_TOF_SECONDS, input.predictionHorizonSeconds ) );
            }

            break;
        case ReplayTripPlannerCommandKind::Plan:
            if ( m_view.state == ReplayTripPlannerState::Idle && !mutation.requested )
            {
                mutation = BeginPlan( input );
            }

            break;
        case ReplayTripPlannerCommandKind::Commit:
            if ( m_view.state == ReplayTripPlannerState::Converged )
            {
                ClearPlanState();
            }

            break;
        case ReplayTripPlannerCommandKind::Cancel:
            if ( !mutation.requested )
            {
                mutation = CancelActivePlan();
            }

            break;
        case ReplayTripPlannerCommandKind::None:
            break;
        }
    }

    m_commandCount = 0;
    return mutation;
}

ReplayTripPlannerVelocityMutation ReplayTripPlanner::BeginPlan( const ReplayTripPlannerLiveInput& input ) noexcept
{
    ReplayTripPlannerVelocityMutation mutation;
    if ( !m_view.available || input.predictionHorizonSeconds < REPLAY_TRIP_PLANNER_MIN_TOF_SECONDS )
    {
        Fail();
        return mutation;
    }

    m_view.timeOfFlightSeconds = std::clamp( m_view.timeOfFlightSeconds,
                                             REPLAY_TRIP_PLANNER_MIN_TOF_SECONDS,
                                             input.predictionHorizonSeconds );

    m_prePlanVelocity = input.ship.linearVelocity;
    m_prePlanVelocityValid = true;
    m_view.shipId = input.ship.id;
    m_view.targetId = input.target.id;
    m_view.iteration = 0;
    m_view.missDistance = 0.0f;
    m_view.ghostCount = 0;
    m_view.iterationMissCount = 0;
    m_view.noSolution = false;
    if ( input.targetName && input.targetName[0] != '\0' )
    {
        strncpy_s( m_view.targetName, input.targetName, _TRUNCATE );
    }
    else
    {
        strncpy_s( m_view.targetName, "TARGET", _TRUNCATE );
    }

    // Concept: analytic mechanics proposes only the first departure velocity.
    // The target is advanced to the requested arrival time in the sun-relative
    // frame, then Lambert supplies a matching sun-relative ship velocity. The
    // real isolated engine remains the authority on whether that proposal hits.
    const float mu = input.gravitationalConstant * input.sun.mass;
    Math::Orbital::OrbitalElements targetElements;
    Vector3 targetFuturePosition;
    Vector3 targetFutureVelocity;
    Math::Orbital::LambertSolution lambert;
    const Math::Orbital::OrbitalStatus elementsStatus = Math::Orbital::ElementsFromState(
        input.target.position - input.sun.position,
        input.target.linearVelocity - input.sun.linearVelocity,
        mu,
        targetElements );

    const Math::Orbital::OrbitalStatus propagationStatus = elementsStatus == Math::Orbital::OrbitalStatus::Ok
                                                               ? Math::Orbital::PropagateToTime(
                                                                     targetElements,
                                                                     m_view.timeOfFlightSeconds,
                                                                     targetFuturePosition,
                                                                     targetFutureVelocity )
                                                               : elementsStatus;

    const Math::Orbital::OrbitalStatus lambertStatus = propagationStatus == Math::Orbital::OrbitalStatus::Ok
                                                           ? Math::Orbital::SolveLambert(
                                                                 input.ship.position - input.sun.position,
                                                                 targetFuturePosition,
                                                                 m_view.timeOfFlightSeconds,
                                                                 mu,
                                                                 true,
                                                                 lambert )
                                                           : propagationStatus;

    if ( lambertStatus != Math::Orbital::OrbitalStatus::Ok )
    {
        Fail();
        return mutation;
    }

    m_view.candidateVelocity = lambert.v1 + input.sun.linearVelocity;
    m_view.iteration = 1;
    m_view.state = ReplayTripPlannerState::Seeding;
    m_previousMissDistance = 0.0f;
    m_hasPreviousMiss = false;
    m_lastObservedGeneration = 0;
    mutation.bodyId = input.ship.id;
    mutation.linearVelocity = m_view.candidateVelocity;
    mutation.requested = true;
    mutation.prepareBaseline = true;
    return mutation;
}

ReplayTripPlannerVelocityMutation
ReplayTripPlanner::ObservePrediction( const ReplayTripPlannerPredictionInput& input ) noexcept
{
    ReplayTripPlannerVelocityMutation mutation;
    if ( input.cancelled || input.liveAdvanceHeld || !input.targetAvailable ||
         input.shipId.value != m_view.shipId.value || input.targetId.value != m_view.targetId.value )
    {
        if ( PlanningState( m_view.state ) )
        {
            return CancelActivePlan();
        }

        return mutation;
    }

    if ( m_view.state != ReplayTripPlannerState::AwaitingPrediction || !input.complete || !input.intercept.valid ||
         input.generation == 0 || input.generation == m_lastObservedGeneration )
    {
        return mutation;
    }

    m_lastObservedGeneration = input.generation;
    RetainGhost( input.frames, input.shipId );
    m_view.missDistance = input.intercept.missDistance;
    if ( m_view.iterationMissCount < m_view.iterationMissDistances.size() )
    {
        m_view.iterationMissDistances[m_view.iterationMissCount++] = input.intercept.missDistance;
    }

    if ( input.intercept.intercept )
    {
        m_view.state = ReplayTripPlannerState::Converged;
        return mutation;
    }

    // Invariant: one completed generation supplies at most one correction.
    // A flat or worsening miss terminates the bounded shooting loop instead of
    // spending more prediction generations on an unstable local direction.
    const bool improving = !m_hasPreviousMiss || input.intercept.missDistance < m_previousMissDistance - 0.001f;
    if ( !improving || m_view.iteration >= REPLAY_TRIP_PLANNER_MAX_ITERATIONS || input.intercept.etaSeconds <= 0.0f )
    {
        Fail();
        return mutation;
    }

    m_previousMissDistance = input.intercept.missDistance;
    m_hasPreviousMiss = true;
    m_view.candidateVelocity = FirstOrderCorrection( m_view.candidateVelocity,
                                                     input.intercept.shipPosition,
                                                     input.intercept.targetPosition,
                                                     input.intercept.etaSeconds );

    ++m_view.iteration;
    m_view.state = ReplayTripPlannerState::Correcting;
    mutation.bodyId = m_view.shipId;
    mutation.linearVelocity = m_view.candidateVelocity;
    mutation.requested = true;
    return mutation;
}

void ReplayTripPlanner::ConfirmVelocityApplied() noexcept
{
    if ( m_view.state == ReplayTripPlannerState::Seeding || m_view.state == ReplayTripPlannerState::Correcting )
    {
        m_view.state = ReplayTripPlannerState::AwaitingPrediction;
    }
}

Math::Vector::Vector3 ReplayTripPlanner::FirstOrderCorrection( const Math::Vector::Vector3& velocity,
                                                               const Math::Vector::Vector3& shipAtClosest,
                                                               const Math::Vector::Vector3& targetAtClosest,
                                                               float closestTimeSeconds ) noexcept
{
    if ( !std::isfinite( closestTimeSeconds ) || closestTimeSeconds <= 0.0f )
    {
        return velocity;
    }

    // Concept: first-order shooting treats the observed position error as the
    // displacement a small departure-velocity change should cover by t*.
    return velocity +
           ( targetAtClosest - shipAtClosest ) * ( REPLAY_TRIP_PLANNER_CORRECTION_GAIN / closestTimeSeconds );
}

ReplayTripPlannerVelocityMutation ReplayTripPlanner::CancelActivePlan() noexcept
{
    ReplayTripPlannerVelocityMutation mutation;
    if ( m_prePlanVelocityValid && m_view.shipId.value != 0 )
    {
        mutation.bodyId = m_view.shipId;
        mutation.linearVelocity = m_prePlanVelocity;
        mutation.requested = true;
        mutation.restoresPrePlanVelocity = true;
    }

    ClearPlanState();
    return mutation;
}

void ReplayTripPlanner::RetainGhost( std::span<const RunReplayPredictionFrame> frames,
                                     Physics::PhysicsSceneObjectId shipId ) noexcept
{
    if ( frames.empty() )
    {
        return;
    }

    // Invariant: once four slots are full the oldest, dimmest witness is
    // discarded and the three newer candidates retain their visual ordering.
    if ( m_view.ghostCount == m_view.ghosts.size() )
    {
        for ( std::size_t index = 1; index < m_view.ghosts.size(); ++index )
        {
            m_view.ghosts[index - 1] = m_view.ghosts[index];
        }

        --m_view.ghostCount;
    }

    ReplayTripPlannerGhostArc& ghost = m_view.ghosts[m_view.ghostCount];
    ghost.pointCount = 0;
    const std::size_t desiredCount = (std::min)( frames.size(), ghost.points.size() );
    for ( std::size_t pointIndex = 0; pointIndex < desiredCount; ++pointIndex )
    {
        const std::size_t frameIndex = desiredCount > 1 ? pointIndex * ( frames.size() - 1u ) / ( desiredCount - 1u )
                                                        : 0u;

        const RunReplayPredictionBodySample* body = FindPlannerPredictionBody( frames[frameIndex], shipId );
        if ( body )
        {
            ghost.points[ghost.pointCount++] = body->position;
        }
    }

    if ( ghost.pointCount >= 2u )
    {
        ++m_view.ghostCount;
    }
}

void ReplayTripPlanner::Fail() noexcept
{
    m_view.state = ReplayTripPlannerState::Failed;
    m_view.noSolution = true;
}

void ReplayTripPlanner::Abort() noexcept
{
    ClearPlanState();
}

void ReplayTripPlanner::ClearPlanState() noexcept
{
    const bool visible = m_view.visible;
    const bool available = m_view.available;
    const float timeOfFlightSeconds = m_view.timeOfFlightSeconds;
    m_view = ReplayTripPlannerView {};

    m_view.visible = visible;
    m_view.available = available;
    m_view.timeOfFlightSeconds = timeOfFlightSeconds;
    m_prePlanVelocity = Math::Vector::ZERO_VECTOR;
    m_previousMissDistance = 0.0f;
    m_lastObservedGeneration = 0;
    m_prePlanVelocityValid = false;
    m_hasPreviousMiss = false;
}

void ReplayTripPlanner::ResetForSceneDiscard() noexcept
{
    m_commands = {};
    m_commandCount = 0;
    m_view = ReplayTripPlannerView {};
    m_prePlanVelocity = Math::Vector::ZERO_VECTOR;
    m_previousMissDistance = 0.0f;
    m_lastObservedGeneration = 0;
    m_prePlanVelocityValid = false;
    m_hasPreviousMiss = false;
}

const ReplayTripPlannerView& ReplayTripPlanner::View() const noexcept
{
    return m_view;
}

bool ReplayTripPlanner::RequiresLiveInput() const noexcept
{
    // Why: the default-hidden planner must impose no body-store scan or large
    // view copy in steady gameplay. A queued command or non-idle plan wakes it.
    return m_commandCount > 0 || m_view.visible || m_view.state != ReplayTripPlannerState::Idle;
}

bool ReplayTripPlanner::AwaitingPrediction() const noexcept
{
    return m_view.state == ReplayTripPlannerState::AwaitingPrediction;
}
} // namespace SkullbonezCore::Runtime
