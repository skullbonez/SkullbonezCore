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
// Concept: wake energy uses the same quietness thresholds as sleep eligibility.
// A body with enough linear or angular motion can wake a sleeping neighbor
// during persistent-contact handling.
bool HasWakeEnergy( const PhysicsBodyHotFieldsConstView& hotFields, int awakeIndex, float sleepLinearSq,
                    float sleepAngularSq )
{
    const Vector3 vel = PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( awakeIndex ) );
    const Vector3 omega = PhysicsBodyAngularVelocity( hotFields, static_cast<size_t>( awakeIndex ) );
    float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
    float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
    return speedSq >= sleepLinearSq || omegaSq >= sleepAngularSq;
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

bool ObjectPairNeedsSweptCcd( SkullbonezCore::Core::Profiler* profiler, const PhysicsBodyHotFieldsConstView& hotFields,
                              std::span<const ColliderRecord> colliderRecords,
                              std::span<const uint8_t> motionEligibilityState, int bodyAIndex, int bodyBIndex,
                              float availableTime, float contactEpsilon )
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

    const Vector3 relativeLinearDisplacement = ( PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( bodyAIndex ) ) -
                                                 PhysicsBodyLinearVelocity( hotFields,
                                                                            static_cast<size_t>( bodyBIndex ) ) ) *
                                               availableTime;
    const float relativeTravelSquared = Vector::VectorMagSquared( relativeLinearDisplacement );
    constexpr float RELATIVE_PROMOTION_THRESHOLD_SQUARED = PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK *
                                                           PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK;

    // Invariant: per-body promotion remains an absolute, thickness-independent
    // travel policy. Pair geometry is only a narrowphase fallback for two bodies
    // that each stayed Discrete but can still close a smaller gap together.
    if ( !std::isfinite( relativeTravelSquared ) || relativeTravelSquared >= RELATIVE_PROMOTION_THRESHOLD_SQUARED )
    {
        return true;
    }

    if ( relativeTravelSquared <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    const std::size_t bodyA = static_cast<std::size_t>( bodyAIndex );
    const std::size_t bodyB = static_cast<std::size_t>( bodyBIndex );
    const Vector3 relativeCenterStart = PhysicsBodyPosition( hotFields, bodyA ) - PhysicsBodyPosition( hotFields, bodyB );
    const float combinedRadius = (std::max)( 0.0f, colliderRecords[bodyA].boundingRadius ) +
                                 (std::max)( 0.0f, colliderRecords[bodyB].boundingRadius ) +
                                 (std::max)( 0.0f, contactEpsilon );
    const float closestTimeFraction = std::clamp( -Vector::Dot( relativeCenterStart, relativeLinearDisplacement ) /
                                                      relativeTravelSquared,
                                                  0.0f, 1.0f );
    const Vector3 closestCenterDelta = relativeCenterStart + relativeLinearDisplacement * closestTimeFraction;
    const float closestCenterDistanceSquared = Vector::VectorMagSquared( closestCenterDelta );
    const float combinedRadiusSquared = combinedRadius * combinedRadius;

    if ( !std::isfinite( closestCenterDistanceSquared ) || !std::isfinite( combinedRadiusSquared ) )
    {
        return true;
    }

    if ( closestCenterDistanceSquared > combinedRadiusSquared )
    {
        return false;
    }

    // Why: a current exact manifold belongs to the persistent solver. Keeping
    // that pair Discrete preserves resting/static-friction response while the
    // conservative body-origin spheres route only a possible future contact.
    return !HasObjectContactAtTime( profiler, hotFields, colliderRecords, bodyAIndex, bodyBIndex, 0.0f, contactEpsilon );
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
void PhysicsNarrowphaseStage::ProcessObjectNarrowphasePair( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, PhysicsTerrainView terrain,
                                                            std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<const std::pair<int, int>> candidatePairs,
                                                            PhysicsNarrowphaseWakeAccess wakeAccess, std::span<float> timeRemaining, std::span<const uint8_t> motionEligibilityState,
                                                            const ObjectNarrowphaseStepPolicy& policy, Core::Profiler* profiler, int pairIndex, ObjectNarrowphaseEvent& event )
{
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    const auto& cp = candidatePairs[static_cast<size_t>( pairIndex )];
    const int x = cp.first;
    const int y = cp.second;

    // Wake a sleeping object only after an energetic awake neighbor proves
    // an actual swept hit or persistent overlap. Underwater-locked sleepers
    // still receive the swept hit timing, but remain static solver anchors.
    if ( wakeAccess.IsSleeping( x ) || wakeAccess.IsSleeping( y ) )
    {
        // Quiet awake bodies cannot wake sleepers just by sharing a broadphase cell.
        if ( wakeAccess.IsSleeping( x ) && !wakeAccess.IsSleeping( y ) )
        {
            const bool sleepingLocked = wakeAccess.IsUnderwaterSleepLocked( x );

            if ( !HasWakeEnergy( hotFields, y, policy.sleepLinearSq, policy.sleepAngularSq ) )
            {
                return;
            }

            // Swept impact wakes immediately when time remains; persistent
            // overlap wakes too so sleepers cannot stay frozen after a hit.
            bool wokeBySweptImpact = false;

            if ( timeRemaining[y] > 0.0f &&
                 ObjectPairNeedsSweptCcd( profiler, hotFields, colliderRecords, motionEligibilityState, y, x,
                                          timeRemaining[y], policy.contactEpsilon ) )
            {
                ObjectContactSweepResult sweep = SweepObjectPair( profiler, hotFields, colliderRecords, y, x,
                                                                  timeRemaining[y] );

                if ( sweep.hit )
                {
                    const float availableTime = timeRemaining[y];
                    float colTime = RefineObjectSweepContactTime( profiler, hotFields, colliderRecords, y, x,
                                                                  sweep.collisionTime, availableTime,
                                                                  policy.contactEpsilon );

                    if constexpr ( RetainPipelineRecords )
                    {
                        Physics::PhysicsPipelineRecord record;
                        record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
                        record.bodyA = y;
                        record.bodyB = x;
                        record.point = ( PhysicsBodyPosition( hotFields, static_cast<size_t>( y ) ) +
                                         PhysicsBodyPosition( hotFields, static_cast<size_t>( x ) ) ) *
                                       0.5f;

                        record.scalarA = colTime;
                        record.scalarB = availableTime;
                        RecordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
                    }
                    else
                    {
                        ObserveObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit );
                    }

                    EmitObjectCollisionTimeEvent( event, y, x, colTime, availableTime );

                    (void)bodyStore.IntegrateBodyPose( profiler, colliderStore, terrain,
                                                       buoyancyFacts[static_cast<std::size_t>( y )], y, colTime );
                    timeRemaining[y] = (std::max)( 0.0f, timeRemaining[y] - colTime );

                    if ( !sleepingLocked )
                    {
                        wakeAccess.WakeBody( x );
                    }

                    wokeBySweptImpact = true;
                    MarkObjectVisualEvent( event, x, y );
                    WriteObjectCollisionCellEvent( event, hotFields, x, y, policy.invCellSize );
                }
            }

            if ( !wokeBySweptImpact &&
                 HasPersistentWakeContact( profiler, hotFields, colliderRecords, y, x, policy.contactEpsilon ) )
            {
                if constexpr ( RetainPipelineRecords )
                {
                    Physics::PhysicsPipelineRecord record;

                    record.stage = Physics::PhysicsPipelineStage::WakeDecision;
                    record.bodyA = y;
                    record.bodyB = x;
                    record.point = ( PhysicsBodyPosition( hotFields, static_cast<size_t>( y ) ) +
                                     PhysicsBodyPosition( hotFields, static_cast<size_t>( x ) ) ) *
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
                    wakeAccess.WakeBody( x );
                }

                MarkObjectVisualEvent( event, x, y );
                WriteObjectCollisionCellEvent( event, hotFields, x, y, policy.invCellSize );
            }

            return;
        }
        else if ( wakeAccess.IsSleeping( y ) && !wakeAccess.IsSleeping( x ) )
        {
            const bool sleepingLocked = wakeAccess.IsUnderwaterSleepLocked( y );

            if ( !HasWakeEnergy( hotFields, x, policy.sleepLinearSq, policy.sleepAngularSq ) )
            {
                return;
            }

            bool wokeBySweptImpact = false;

            if ( timeRemaining[x] > 0.0f &&
                 ObjectPairNeedsSweptCcd( profiler, hotFields, colliderRecords, motionEligibilityState, x, y,
                                          timeRemaining[x], policy.contactEpsilon ) )
            {
                ObjectContactSweepResult sweep = SweepObjectPair( profiler, hotFields, colliderRecords, x, y,
                                                                  timeRemaining[x] );

                if ( sweep.hit )
                {
                    const float availableTime = timeRemaining[x];
                    float colTime = RefineObjectSweepContactTime( profiler, hotFields, colliderRecords, x, y,
                                                                  sweep.collisionTime, availableTime,
                                                                  policy.contactEpsilon );

                    if constexpr ( RetainPipelineRecords )
                    {
                        Physics::PhysicsPipelineRecord record;
                        record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
                        record.bodyA = x;
                        record.bodyB = y;
                        record.point = ( PhysicsBodyPosition( hotFields, static_cast<size_t>( x ) ) +
                                         PhysicsBodyPosition( hotFields, static_cast<size_t>( y ) ) ) *
                                       0.5f;

                        record.scalarA = colTime;
                        record.scalarB = availableTime;
                        RecordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
                    }
                    else
                    {
                        ObserveObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit );
                    }

                    EmitObjectCollisionTimeEvent( event, x, y, colTime, availableTime );

                    (void)bodyStore.IntegrateBodyPose( profiler, colliderStore, terrain,
                                                       buoyancyFacts[static_cast<std::size_t>( x )], x, colTime );
                    timeRemaining[x] = (std::max)( 0.0f, timeRemaining[x] - colTime );

                    if ( !sleepingLocked )
                    {
                        wakeAccess.WakeBody( y );
                    }

                    wokeBySweptImpact = true;
                    MarkObjectVisualEvent( event, x, y );
                    WriteObjectCollisionCellEvent( event, hotFields, x, y, policy.invCellSize );
                }
            }

            if ( !wokeBySweptImpact &&
                 HasPersistentWakeContact( profiler, hotFields, colliderRecords, x, y, policy.contactEpsilon ) )
            {
                if constexpr ( RetainPipelineRecords )
                {
                    Physics::PhysicsPipelineRecord record;

                    record.stage = Physics::PhysicsPipelineStage::WakeDecision;
                    record.bodyA = x;
                    record.bodyB = y;
                    record.point = ( PhysicsBodyPosition( hotFields, static_cast<size_t>( x ) ) +
                                     PhysicsBodyPosition( hotFields, static_cast<size_t>( y ) ) ) *
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
                    wakeAccess.WakeBody( y );
                }

                MarkObjectVisualEvent( event, x, y );
                WriteObjectCollisionCellEvent( event, hotFields, x, y, policy.invCellSize );
            }

            return;
        }
        else
        {
            // Both bodies are sleeping; there is no awake energy to produce a wake event.
            return;
        }
    }

    if ( timeRemaining[x] <= 0.0f || timeRemaining[y] <= 0.0f )
    {
        return;
    }

    float availableTime = (std::min)( timeRemaining[x], timeRemaining[y] );

    if ( !ObjectPairNeedsSweptCcd( profiler, hotFields, colliderRecords, motionEligibilityState, x, y, availableTime,
                                   policy.contactEpsilon ) )
    {
        // Invariant: a Discrete pair performs no swept query and therefore emits
        // neither a SweptObjectHit nor a misleading SweptObjectMiss event.
        return;
    }

    ObjectContactSweepResult sweep = SweepObjectPair( profiler, hotFields, colliderRecords, x, y, availableTime );

    if ( sweep.hit )
    {
        float colTime = RefineObjectSweepContactTime( profiler, hotFields, colliderRecords, x, y, sweep.collisionTime,
                                                      availableTime, policy.contactEpsilon );

        if constexpr ( RetainPipelineRecords )
        {
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
            record.bodyA = x;
            record.bodyB = y;
            record.point = ( PhysicsBodyPosition( hotFields, static_cast<size_t>( x ) ) +
                             PhysicsBodyPosition( hotFields, static_cast<size_t>( y ) ) ) *
                           0.5f;

            record.scalarA = colTime;
            record.scalarB = availableTime;
            RecordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
        }
        else
        {
            ObserveObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit );
        }

        EmitObjectCollisionTimeEvent( event, x, y, colTime, availableTime );

        (void)bodyStore.IntegrateBodyPose( profiler, colliderStore, terrain, buoyancyFacts[static_cast<std::size_t>( x )], x,
                                           colTime );
        (void)bodyStore.IntegrateBodyPose( profiler, colliderStore, terrain, buoyancyFacts[static_cast<std::size_t>( y )], y,
                                           colTime );
        timeRemaining[x] = (std::max)( 0.0f, timeRemaining[x] - colTime );
        timeRemaining[y] = (std::max)( 0.0f, timeRemaining[y] - colTime );

        // Object/object CCD only advances to the contact candidate. The
        // persistent Catto rows below own velocity response and cache storage.
        MarkObjectVisualEvent( event, x, y );
        WriteObjectCollisionCellEvent( event, hotFields, x, y, policy.invCellSize );
    }
    else
    {
        if constexpr ( RetainPipelineRecords )
        {
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SweptObjectMiss;
            record.bodyA = x;
            record.bodyB = y;
            record.point = ( PhysicsBodyPosition( hotFields, static_cast<size_t>( x ) ) +
                             PhysicsBodyPosition( hotFields, static_cast<size_t>( y ) ) ) *
                           0.5f;

            record.scalarA = availableTime;
            RecordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectMiss, record );
        }
        else
        {
            ObserveObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectMiss );
        }
    }
}

template void PhysicsNarrowphaseStage::ProcessObjectNarrowphasePair<true>( PhysicsBodyStore&, const ColliderStore&, PhysicsTerrainView, std::span<BuoyancyBodyFacts>,
                                                                           std::span<const std::pair<int, int>>, PhysicsNarrowphaseWakeAccess, std::span<float>, std::span<const uint8_t>,
                                                                           const ObjectNarrowphaseStepPolicy&, Core::Profiler*, int, ObjectNarrowphaseEvent& );
template void PhysicsNarrowphaseStage::ProcessObjectNarrowphasePair<false>( PhysicsBodyStore&, const ColliderStore&, PhysicsTerrainView, std::span<BuoyancyBodyFacts>,
                                                                            std::span<const std::pair<int, int>>, PhysicsNarrowphaseWakeAccess, std::span<float>, std::span<const uint8_t>,
                                                                            const ObjectNarrowphaseStepPolicy&, Core::Profiler*, int, ObjectNarrowphaseEvent& );
