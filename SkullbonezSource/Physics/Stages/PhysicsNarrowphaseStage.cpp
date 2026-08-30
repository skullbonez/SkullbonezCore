/*
File: SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp
Purpose:
  Implements deterministic object/object CCD pair processing and island dispatch.

Summary:
  This owner performs the exact swept-contact and wake decisions formerly
  embedded in PhysicsWorld. It retains bounded island/event scratch while the
  PhysicsWorld sequencer commits value events in original pair order.

Glossary:
  CCD refinement: Exact-manifold search around a conservative swept hit time.
  Wake event: Value evidence that an energetic awake body contacted a sleeper.

Invariants:
  - Serial and parallel pair math, constants, and pair-slot order are unchanged.
  - A worker processes all pairs in one island, preventing shared-body races.
  - Full/count selection occurs before each pair loop; both lanes emit the same
    event kind while only the full lane materializes a pipeline payload.
  - Construction reserves bound every vector used during steady gameplay.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "PhysicsNarrowphaseStage.h"
#include "../PhysicsSpatialCellKey.h"
#include "PhysicsSleepController.h"

#include "../../Core/Profiler.h"
#include "../ColliderStore.h"
#include "../ObjectContactManifold.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsWorldForces.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
namespace Physics = SkullbonezCore::Physics;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
// Concept: an awake body is active wake authority once narrowphase proves an
// exact swept hit or persistent overlap. Sleep-speed thresholds do not veto
// island activation at that point.
bool IsAwakeContactAuthority( const PhysicsBodyHotFieldsConstView& hotFields, int awakeIndex )
{
    return hotFields.awake[static_cast<std::size_t>( awakeIndex )] != 0u;
}

ObjectContactBodyView ObjectContactBodyViewAtTime( const PhysicsBodyHotFieldsConstView& hotFields, int index, float time )
{
    const size_t bodyIndex = static_cast<size_t>( index );
    ObjectContactBodyView body;
    body.position = PhysicsBodyPosition( hotFields, bodyIndex ) + PhysicsBodyLinearVelocity( hotFields, bodyIndex ) * time;

    body.orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
    return body;
}

bool HasPersistentWakeContact( SkullbonezCore::Core::Profiler* profiler, const PhysicsBodyHotFieldsConstView& hotFields,
                               std::span<const ColliderRecord> colliderRecords, int awakeIndex, int sleepingIndex,
                               float contactEpsilon )
{
    PROFILE_SCOPED( "Frame/Physics/Narrowphase/WakePersistentContact" );

    // A swept test can miss a sleeper that is already overlapping after an
    // awake body's correction step. This fresh manifold test catches that
    // persistent contact so the sleeper cannot remain frozen inside the
    // awake body until a later frame happens to generate a swept hit.
    if ( awakeIndex < 0 || sleepingIndex < 0 || awakeIndex >= static_cast<int>( colliderRecords.size() ) ||
         sleepingIndex >= static_cast<int>( colliderRecords.size() ) )
    {
        return false;
    }

    ObjectContactManifold manifold;
    return BuildObjectContactManifold( profiler, ObjectContactBodyViewAtTime( hotFields, awakeIndex, 0.0f ),
                                       colliderRecords[static_cast<size_t>( awakeIndex )].shape,
                                       ObjectContactBodyViewAtTime( hotFields, sleepingIndex, 0.0f ),
                                       colliderRecords[static_cast<size_t>( sleepingIndex )].shape, awakeIndex,
                                       sleepingIndex, contactEpsilon, manifold );
}

bool HasObjectContactAtTime( SkullbonezCore::Core::Profiler* profiler, const PhysicsBodyHotFieldsConstView& hotFields,
                             std::span<const ColliderRecord> colliderRecords, int bodyA, int bodyB, float time,
                             float contactEpsilon )
{
    PROFILE_SCOPED( "Frame/Physics/Narrowphase/ExactContactAtTime" );

    if ( bodyA < 0 || bodyB < 0 || bodyA >= static_cast<int>( colliderRecords.size() ) ||
         bodyB >= static_cast<int>( colliderRecords.size() ) )
    {
        return false;
    }

    // Query at a candidate time without mutating PhysicsBodyStore or the
    // owner-side presentation rows. CCD refinement only needs temporary pose
    // views plus borrowed references to ColliderStore's per-kind shape payloads.
    ObjectContactManifold manifold;
    return BuildObjectContactManifold( profiler, ObjectContactBodyViewAtTime( hotFields, bodyA, time ),
                                       colliderRecords[static_cast<size_t>( bodyA )].shape,
                                       ObjectContactBodyViewAtTime( hotFields, bodyB, time ),
                                       colliderRecords[static_cast<size_t>( bodyB )].shape, bodyA, bodyB, contactEpsilon,
                                       manifold );
}

float RefineObjectSweepContactTime( SkullbonezCore::Core::Profiler* profiler, const PhysicsBodyHotFieldsConstView& hotFields,
                                    std::span<const ColliderRecord> colliderRecords, int bodyA, int bodyB, float coarseTime,
                                    float availableTime, float contactEpsilon )
{
    PROFILE_SCOPED( "Frame/Physics/Narrowphase/RefineContactTime" );

    // The broad sweep can give a conservative first time. Refinement walks
    // forward until exact manifold contact appears, then binary-searches the
    // edge of that contact window. This keeps fast objects from advancing
    // too far into each other before persistent rows solve the response.
    if ( coarseTime <= 0.0f || coarseTime >= availableTime )
    {
        return coarseTime;
    }

    if ( HasObjectContactAtTime( profiler, hotFields, colliderRecords, bodyA, bodyB, coarseTime, contactEpsilon ) )
    {
        return coarseTime;
    }

    float lo = coarseTime;
    float hi = coarseTime;
    bool foundContactWindow = false;

    for ( int step = 1; step <= 48; ++step )
    {
        const float t = coarseTime + ( availableTime - coarseTime ) * ( static_cast<float>( step ) / 48.0f );

        if ( HasObjectContactAtTime( profiler, hotFields, colliderRecords, bodyA, bodyB, t, contactEpsilon ) )
        {
            hi = t;
            foundContactWindow = true;
            break;
        }

        lo = t;
    }

    if ( !foundContactWindow )
    {
        return coarseTime;
    }

    for ( int iter = 0; iter < 12; ++iter )
    {
        const float mid = ( lo + hi ) * 0.5f;

        if ( HasObjectContactAtTime( profiler, hotFields, colliderRecords, bodyA, bodyB, mid, contactEpsilon ) )
        {
            hi = mid;
        }
        else
        {
            lo = mid;
        }
    }

    return hi;
}

ObjectContactSweepResult SweepObjectPair( SkullbonezCore::Core::Profiler*, const PhysicsBodyHotFieldsConstView& hotFields,
                                          std::span<const ColliderRecord> colliderRecords, int bodyA, int bodyB,
                                          float availableTime )
{
    PROFILE_SCOPED( "Frame/Physics/Narrowphase/SweepPairs" );
    ObjectContactSweepResult result;
    result.collisionTime = availableTime;

    if ( bodyA < 0 || bodyB < 0 || bodyA >= static_cast<int>( colliderRecords.size() ) ||
         bodyB >= static_cast<int>( colliderRecords.size() ) )
    {
        return result;
    }

    return SweepObjectContact( ObjectContactBodyViewAtTime( hotFields, bodyA, 0.0f ),
                               colliderRecords[static_cast<size_t>( bodyA )].shape,
                               PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( bodyA ) ),
                               ObjectContactBodyViewAtTime( hotFields, bodyB, 0.0f ),
                               colliderRecords[static_cast<size_t>( bodyB )].shape,
                               PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( bodyB ) ), availableTime );
}

bool BodyRequiresSweptTranslation( std::span<const uint8_t> motionEligibilityState, int bodyIndex )
{
    // Hazard: a short or invalid classification span cannot safely opt a body
    // out of CCD. The production owner publishes one row per body; direct tools
    // and future callers fail conservative until their contract is corrected.
    return bodyIndex < 0 || bodyIndex >= static_cast<int>( motionEligibilityState.size() ) ||
           ( motionEligibilityState[static_cast<std::size_t>( bodyIndex )] & PhysicsMotionEligibilityLinearPromoted ) != 0u;
}

bool ObjectPairNeedsSweptCcd( std::span<const uint8_t> motionEligibilityState, int bodyAIndex, int bodyBIndex,
                              float availableTime )
{
    if ( availableTime <= TOLERANCE )
    {
        return false;
    }

    if ( BodyRequiresSweptTranslation( motionEligibilityState, bodyAIndex ) ||
         BodyRequiresSweptTranslation( motionEligibilityState, bodyBIndex ) )
    {
        return true;
    }

    // Invariant: two bodies that each remain below their direction-valid radius
    // may overlap at the next boundary, but cannot cleanly exchange sides between
    // two discrete samples. Only promoted bodies pay swept narrowphase.
    return false;
}

} // namespace

void PhysicsNarrowphaseStage::ObserveObjectNarrowphaseEvent( ObjectNarrowphaseEvent& event, ObjectNarrowphaseEventKind kind )
{
    event.kind = kind;
    event.hasPipelineEvent = 1;
}

void PhysicsNarrowphaseStage::RecordObjectNarrowphaseEvent( ObjectNarrowphaseEvent& event, ObjectNarrowphaseEventKind kind,
                                                            const Physics::PhysicsPipelineRecord& record )
{
    ObserveObjectNarrowphaseEvent( event, kind );
    event.pipelineRecord = record;
}

void PhysicsNarrowphaseStage::EmitObjectCollisionTimeEvent( ObjectNarrowphaseEvent& event, int bodyA, int bodyB,
                                                            float collisionTime, float availableTime )
{
    event.emitCollisionTime = 1;
    event.collisionTimeBodyA = bodyA;
    event.collisionTimeBodyB = bodyB;
    event.collisionTime = collisionTime;
    event.availableTime = availableTime;
}

void PhysicsNarrowphaseStage::MarkObjectVisualEvent( ObjectNarrowphaseEvent& event, int bodyA, int bodyB )
{
    event.markVisualContact = 1;
    event.visualBodyA = bodyA;
    event.visualBodyB = bodyB;
}

void PhysicsNarrowphaseStage::WriteObjectCollisionCellEvent( ObjectNarrowphaseEvent& event,
                                                             const PhysicsBodyHotFieldsConstView& hotFields, int bodyA,
                                                             int bodyB, float invCellSize )
{
    const Vector3 midpoint = ( PhysicsBodyPosition( hotFields, static_cast<size_t>( bodyA ) ) +
                               PhysicsBodyPosition( hotFields, static_cast<size_t>( bodyB ) ) ) *
                             0.5f;

    // Invariant: diagnostics and Runtime visualization retain the same exact
    // supported cell identity as SpatialGrid. No publication or presentation
    // seam may narrow coordinates before or after this reversible encoding.
    const int cx = static_cast<int>( floorf( midpoint.x * invCellSize ) );
    const int cy = static_cast<int>( floorf( midpoint.y * invCellSize ) );
    const int cz = static_cast<int>( floorf( midpoint.z * invCellSize ) );
    event.collisionCellKey = EncodeExactSpatialCellKey( cx, cy, cz );

    event.hasCollisionCellKey = 1;
}

template <bool RetainPipelineRecords>
void PhysicsNarrowphaseStage::ProcessSleepingObjectPair( const ObjectNarrowphaseIslandStage& step, int bodyA, int bodyB,
                                                         int awakeIndex, int sleepingIndex, ObjectNarrowphaseEvent& event )
{
    const PhysicsBodyHotFieldsConstView hotFields = step.bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = step.colliderStore.Records();
    const bool sleepingLocked = step.wakeAccess.IsUnderwaterSleepLocked( sleepingIndex );
    bool wokeBySweptImpact = false;

    if ( step.timeRemaining[awakeIndex] > 0.0f &&
         ObjectPairNeedsSweptCcd( step.motionEligibilityState, awakeIndex, sleepingIndex, step.timeRemaining[awakeIndex] ) )
    {
        const ObjectContactSweepResult sweep = SweepObjectPair( step.profiler, hotFields, colliderRecords, awakeIndex,
                                                                sleepingIndex, step.timeRemaining[awakeIndex] );

        if ( sweep.hit )
        {
            const float availableTime = step.timeRemaining[awakeIndex];
            const float collisionTime = RefineObjectSweepContactTime( step.profiler, hotFields, colliderRecords, awakeIndex,
                                                                      sleepingIndex, sweep.collisionTime, availableTime,
                                                                      step.policy.contactEpsilon );

            if constexpr ( RetainPipelineRecords )
            {
                PhysicsPipelineRecord record;
                record.stage = PhysicsPipelineStage::SweptObjectHit;
                record.bodyA = awakeIndex;
                record.bodyB = sleepingIndex;
                record.point = ( PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyA ) ) +
                                 PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyB ) ) ) *
                               0.5f;
                record.scalarA = collisionTime;
                record.scalarB = availableTime;
                RecordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
            }
            else
            {
                ObserveObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit );
            }

            EmitObjectCollisionTimeEvent( event, awakeIndex, sleepingIndex, collisionTime, availableTime );
            (void)step.bodyStore.IntegrateBodyPose( step.profiler, step.colliderStore, step.terrain,
                                                    step.buoyancyFacts[static_cast<std::size_t>( awakeIndex )], awakeIndex,
                                                    collisionTime );
            step.timeRemaining[awakeIndex] = (std::max)( 0.0f, step.timeRemaining[awakeIndex] - collisionTime );

            if ( !sleepingLocked )
            {
                step.wakeAccess.WakeBody( sleepingIndex );
            }

            wokeBySweptImpact = true;
            MarkObjectVisualEvent( event, bodyA, bodyB );
            WriteObjectCollisionCellEvent( event, hotFields, bodyA, bodyB, step.policy.invCellSize );
        }
    }

    if ( wokeBySweptImpact || !HasPersistentWakeContact( step.profiler, hotFields, colliderRecords, awakeIndex,
                                                         sleepingIndex, step.policy.contactEpsilon ) )
    {
        return;
    }

    if constexpr ( RetainPipelineRecords )
    {
        PhysicsPipelineRecord record;
        record.stage = PhysicsPipelineStage::WakeDecision;
        record.bodyA = awakeIndex;
        record.bodyB = sleepingIndex;
        record.point = ( PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyA ) ) +
                         PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyB ) ) ) *
                       0.5f;
        record.scalarA = sleepingLocked ? 0.0f : 1.0f;
        RecordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::WakeDecision, record );
    }
    else
    {
        ObserveObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::WakeDecision );
    }

    if ( !sleepingLocked )
    {
        step.wakeAccess.WakeBody( sleepingIndex );
    }

    MarkObjectVisualEvent( event, bodyA, bodyB );
    WriteObjectCollisionCellEvent( event, hotFields, bodyA, bodyB, step.policy.invCellSize );
}

template <bool RetainPipelineRecords>
void PhysicsNarrowphaseStage::ProcessAwakeObjectPair( const ObjectNarrowphaseIslandStage& step, int bodyA, int bodyB,
                                                      ObjectNarrowphaseEvent& event )
{
    if ( step.timeRemaining[bodyA] <= 0.0f || step.timeRemaining[bodyB] <= 0.0f )
    {
        return;
    }

    const float availableTime = (std::min)( step.timeRemaining[bodyA], step.timeRemaining[bodyB] );

    if ( !ObjectPairNeedsSweptCcd( step.motionEligibilityState, bodyA, bodyB, availableTime ) )
    {
        return;
    }

    const PhysicsBodyHotFieldsConstView hotFields = step.bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = step.colliderStore.Records();
    const ObjectContactSweepResult sweep = SweepObjectPair( step.profiler, hotFields, colliderRecords, bodyA, bodyB,
                                                            availableTime );

    if ( !sweep.hit )
    {
        if constexpr ( RetainPipelineRecords )
        {
            PhysicsPipelineRecord record;
            record.stage = PhysicsPipelineStage::SweptObjectMiss;
            record.bodyA = bodyA;
            record.bodyB = bodyB;
            record.point = ( PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyA ) ) +
                             PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyB ) ) ) *
                           0.5f;
            record.scalarA = availableTime;
            RecordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectMiss, record );
        }
        else
        {
            ObserveObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectMiss );
        }
        return;
    }

    const float collisionTime = RefineObjectSweepContactTime( step.profiler, hotFields, colliderRecords, bodyA, bodyB,
                                                              sweep.collisionTime, availableTime,
                                                              step.policy.contactEpsilon );

    if constexpr ( RetainPipelineRecords )
    {
        PhysicsPipelineRecord record;
        record.stage = PhysicsPipelineStage::SweptObjectHit;
        record.bodyA = bodyA;
        record.bodyB = bodyB;
        record.point = ( PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyA ) ) +
                         PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyB ) ) ) *
                       0.5f;
        record.scalarA = collisionTime;
        record.scalarB = availableTime;
        RecordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
    }
    else
    {
        ObserveObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit );
    }

    EmitObjectCollisionTimeEvent( event, bodyA, bodyB, collisionTime, availableTime );
    (void)step.bodyStore.IntegrateBodyPose( step.profiler, step.colliderStore, step.terrain,
                                            step.buoyancyFacts[static_cast<std::size_t>( bodyA )], bodyA, collisionTime );
    (void)step.bodyStore.IntegrateBodyPose( step.profiler, step.colliderStore, step.terrain,
                                            step.buoyancyFacts[static_cast<std::size_t>( bodyB )], bodyB, collisionTime );
    step.timeRemaining[bodyA] = (std::max)( 0.0f, step.timeRemaining[bodyA] - collisionTime );
    step.timeRemaining[bodyB] = (std::max)( 0.0f, step.timeRemaining[bodyB] - collisionTime );
    MarkObjectVisualEvent( event, bodyA, bodyB );
    WriteObjectCollisionCellEvent( event, hotFields, bodyA, bodyB, step.policy.invCellSize );
}

template <bool RetainPipelineRecords>
void PhysicsNarrowphaseStage::ProcessObjectNarrowphasePair( const ObjectNarrowphaseIslandStage& step, int pairIndex,
                                                            ObjectNarrowphaseEvent& event )
{
    const auto& pair = step.candidatePairs[static_cast<std::size_t>( pairIndex )];
    const int bodyA = pair.first;
    const int bodyB = pair.second;
    const bool sleepingA = step.wakeAccess.IsSleeping( bodyA );
    const bool sleepingB = step.wakeAccess.IsSleeping( bodyB );

    if ( sleepingA || sleepingB )
    {
        if ( sleepingA != sleepingB )
        {
            const int awakeIndex = sleepingA ? bodyB : bodyA;
            const int sleepingIndex = sleepingA ? bodyA : bodyB;

            if ( IsAwakeContactAuthority( step.bodyStore.HotFields(), awakeIndex ) )
            {
                ProcessSleepingObjectPair<RetainPipelineRecords>( step, bodyA, bodyB, awakeIndex, sleepingIndex, event );
            }
        }
        return;
    }

    ProcessAwakeObjectPair<RetainPipelineRecords>( step, bodyA, bodyB, event );
}

template <bool RetainPipelineRecords>
void PhysicsNarrowphaseStage::ObjectNarrowphaseIslandStage::ProcessPair( int pairIndex, ObjectNarrowphaseEvent& event ) const
{
    stage.ProcessObjectNarrowphasePair<RetainPipelineRecords>( *this, pairIndex, event );
}

template void PhysicsNarrowphaseStage::ObjectNarrowphaseIslandStage::ProcessPair<true>( int, ObjectNarrowphaseEvent& ) const;
template void PhysicsNarrowphaseStage::ObjectNarrowphaseIslandStage::ProcessPair<false>( int,
                                                                                         ObjectNarrowphaseEvent& ) const;
