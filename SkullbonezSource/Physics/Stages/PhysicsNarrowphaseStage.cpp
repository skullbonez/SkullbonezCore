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
  Pair island: Candidate pairs connected through shared body indices.
  Wake event: Value evidence that an energetic awake body contacted a sleeper.

Invariants:
  - Serial and parallel pair math, constants, and pair-slot order are unchanged.
  - A worker processes all pairs in one island, preventing shared-body races.
  - Construction reserves bound every vector used during steady gameplay.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reports/2026-07-15/physicsworld-ownership-map.md
*/
#include "PhysicsNarrowphaseStage.h"
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
constexpr float PHYSICS_OBJECT_CCD_RADIUS_FRACTION = 0.25f;
constexpr float PHYSICS_OBJECT_CCD_SKIN_SCALE = 4.0f;

bool IsSolverBodyFixed( const PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<size_t>( bodyIndex )] != 0u;
}

float SolverBodyRadius( std::span<const ColliderRecord> colliderRecords, int bodyIndex )
{
    return colliderRecords[static_cast<size_t>( bodyIndex )].boundingRadius;
}

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
    PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/WakePersistentContact" );

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
    PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/ExactContactAtTime" );

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
    PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/RefineContactTime" );

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

ObjectContactSweepResult SweepObjectPair( SkullbonezCore::Core::Profiler* profiler,
                                          const PhysicsBodyHotFieldsConstView& hotFields,
                                          std::span<const ColliderRecord> colliderRecords, int bodyA, int bodyB,
                                          float availableTime )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/SweepPairs" );
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

bool PersistentContactCacheEntryPrecedesKey( const PersistentContactCacheEntry& entry, int64_t lookupKey )
{
    return entry.key < lookupKey;
}

bool ObjectPairHasPersistentContactCache( std::span<const PersistentContactCacheEntry> persistentContactCache, int bodyA,
                                          int bodyB )
{
    constexpr uint64_t BODY_MASK = 0x7fffull;
    const int lo = ( bodyA < bodyB ) ? bodyA : bodyB;
    const int hi = ( bodyA < bodyB ) ? bodyB : bodyA;

    // Invariant: this mirrors the object/object prefix of the persistent solver
    // cache key. Feature ids occupy the low 32 bits, so masking those away
    // answers whether any cached contact row existed for this pair.
    const uint64_t pairPrefix = ( ( static_cast<uint64_t>( static_cast<uint32_t>( lo ) ) & BODY_MASK ) << 47 ) |
                                ( ( static_cast<uint64_t>( static_cast<uint32_t>( hi ) ) & BODY_MASK ) << 32 );

    const int64_t firstKey = static_cast<int64_t>( pairPrefix );
    auto cachedIt = std::lower_bound( persistentContactCache.begin(), persistentContactCache.end(), firstKey,
                                      PersistentContactCacheEntryPrecedesKey );

    return cachedIt != persistentContactCache.end() &&
           ( static_cast<uint64_t>( cachedIt->key ) & 0xffffffff00000000ull ) == pairPrefix;
}

bool ObjectPairNeedsSweptCcd( const PhysicsBodyHotFieldsConstView& hotFields,
                              std::span<const ColliderRecord> colliderRecords,
                              std::span<const PersistentContactCacheEntry> persistentContactCache, int bodyAIndex,
                              int bodyBIndex, float availableTime, float contactSkin )
{

    if ( availableTime <= TOLERANCE )
    {
        return false;
    }

    if ( !ObjectPairHasPersistentContactCache( persistentContactCache, bodyAIndex, bodyBIndex ) )
    {
        return true;
    }

    const float radiusA = SolverBodyRadius( colliderRecords, bodyAIndex );
    const float radiusB = SolverBodyRadius( colliderRecords, bodyBIndex );

    if ( !std::isfinite( radiusA ) || !std::isfinite( radiusB ) || radiusA <= TOLERANCE || radiusB <= TOLERANCE )
    {
        return true;
    }

    const Vector3 relativeLinearDisplacement = ( PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( bodyAIndex ) ) -
                                                 PhysicsBodyLinearVelocity( hotFields,
                                                                            static_cast<size_t>( bodyBIndex ) ) ) *
                                               availableTime;

    const float linearTravel = Vector::VectorMag( relativeLinearDisplacement );
    const float angularTravel = ( Vector::VectorMag( PhysicsBodyAngularVelocity( hotFields, static_cast<size_t>( bodyAIndex ) ) ) *
                                      radiusA +
                                  Vector::VectorMag( PhysicsBodyAngularVelocity( hotFields, static_cast<size_t>( bodyBIndex ) ) ) *
                                      radiusB ) *
                                availableTime;

    const float sweptTravel = linearTravel + angularTravel;
    const float smallerRadius = (std::min)( radiusA, radiusB );
    const float ccdThreshold = (std::max)( contactSkin * PHYSICS_OBJECT_CCD_SKIN_SCALE,
                                           smallerRadius * PHYSICS_OBJECT_CCD_RADIUS_FRACTION );

    // Why: only already-persistent pairs may bypass the swept front-end. New
    // contacts keep their old time-of-impact path; settled contacts rely on
    // persistent manifolds unless motion is large enough to tunnel.
    return sweptTravel > ccdThreshold;
}

} // namespace

void PhysicsNarrowphaseStage::RecordObjectNarrowphaseEvent( ObjectNarrowphaseEvent& event, ObjectNarrowphaseEventKind kind,
                                                            const Physics::PhysicsPipelineRecord& record )
{
    event.kind = kind;
    event.pipelineRecord = record;
    event.hasPipelineRecord = 1;
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

    const int16_t cx = static_cast<int16_t>( floorf( midpoint.x * invCellSize ) );
    const int16_t cy = static_cast<int16_t>( floorf( midpoint.y * invCellSize ) );
    const int16_t cz = static_cast<int16_t>( floorf( midpoint.z * invCellSize ) );
    event.collisionCellKey = ( int64_t( cx ) * 73856093 ) ^ ( int64_t( cy ) * 19349663 ) ^ ( int64_t( cz ) * 83492791 );
    event.hasCollisionCellKey = 1;
}

void PhysicsNarrowphaseStage::ProcessObjectNarrowphasePair( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, PhysicsTerrainView terrain,
                                                            std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<const std::pair<int, int>> candidatePairs,
                                                            PhysicsNarrowphaseWakeAccess wakeAccess, std::span<float> timeRemaining,
                                                            std::span<const PersistentContactCacheEntry> persistentContactCache, const ObjectNarrowphaseStepPolicy& policy,
                                                            Core::Profiler* profiler, int pairIndex, ObjectNarrowphaseEvent& event )
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

            if ( timeRemaining[y] > 0.0f && ObjectPairNeedsSweptCcd( hotFields, colliderRecords, persistentContactCache, y,
                                                                     x, timeRemaining[y], policy.contactSkin ) )
            {
                ObjectContactSweepResult sweep = SweepObjectPair( profiler, hotFields, colliderRecords, y, x,
                                                                  timeRemaining[y] );

                if ( sweep.hit )
                {
                    const float availableTime = timeRemaining[y];
                    float colTime = RefineObjectSweepContactTime( profiler, hotFields, colliderRecords, y, x,
                                                                  sweep.collisionTime, availableTime,
                                                                  policy.contactEpsilon );

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
                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::WakeDecision;
                record.bodyA = y;
                record.bodyB = x;
                record.point = ( PhysicsBodyPosition( hotFields, static_cast<size_t>( y ) ) +
                                 PhysicsBodyPosition( hotFields, static_cast<size_t>( x ) ) ) *
                               0.5f;

                record.scalarA = sleepingLocked ? 0.0f : 1.0f;
                RecordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::WakeDecision, record );

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

            if ( timeRemaining[x] > 0.0f && ObjectPairNeedsSweptCcd( hotFields, colliderRecords, persistentContactCache, x,
                                                                     y, timeRemaining[x], policy.contactSkin ) )
            {
                ObjectContactSweepResult sweep = SweepObjectPair( profiler, hotFields, colliderRecords, x, y,
                                                                  timeRemaining[x] );

                if ( sweep.hit )
                {
                    const float availableTime = timeRemaining[x];
                    float colTime = RefineObjectSweepContactTime( profiler, hotFields, colliderRecords, x, y,
                                                                  sweep.collisionTime, availableTime,
                                                                  policy.contactEpsilon );

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
                Physics::PhysicsPipelineRecord record;
                record.stage = Physics::PhysicsPipelineStage::WakeDecision;
                record.bodyA = x;
                record.bodyB = y;
                record.point = ( PhysicsBodyPosition( hotFields, static_cast<size_t>( x ) ) +
                                 PhysicsBodyPosition( hotFields, static_cast<size_t>( y ) ) ) *
                               0.5f;

                record.scalarA = sleepingLocked ? 0.0f : 1.0f;
                RecordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::WakeDecision, record );

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

    if ( !ObjectPairNeedsSweptCcd( hotFields, colliderRecords, persistentContactCache, x, y, availableTime,
                                   policy.contactSkin ) )
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
        return;
    }

    ObjectContactSweepResult sweep = SweepObjectPair( profiler, hotFields, colliderRecords, x, y, availableTime );

    if ( sweep.hit )
    {
        float colTime = RefineObjectSweepContactTime( profiler, hotFields, colliderRecords, x, y, sweep.collisionTime,
                                                      availableTime, policy.contactEpsilon );

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
}
