/*
File: SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
Purpose:
  Implements deterministic broadphase candidate generation and retained output.

Summary:
  The stage maintains persistent integer-range membership, adds a one-step
  swept overlay for fast projectiles, canonicalizes solver-visible pair order,
  stamps cells reached by awake bodies, suppresses sleep-only work at emission,
  prunes fixed/joint pairs, and records bounded pipeline evidence.

Glossary:
  Broadphase filter: Shape-aware cheap predicate applied while grid pairs form.
  Sleep-pruned pair: Pair of dormant bodies with no awake energy to create work.

Invariants:
  - `remove_if` predicates preserve their diagnostic side effects in canonical
    solver-visible order.
  - Sleep-only pairs never enter the production candidate vector; Debug records
    the old geometric-admission evidence at the emission skip.
  - Count-only tracing batches admitted pair cardinality without loading body
    positions; full tracing preserves the canonical sorted payload order.
  - No hot-path list operation may exceed its scene-load reservation.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h
  - SkullbonezSource/Physics/SolverBroadphaseStage.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "PhysicsBroadphaseStage.h"

#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"
#include "../SolverBroadphaseStage.h"
#include "PhysicsStepDiagnostics.h"

#include <algorithm>
#include <cassert>
#include <cmath>

using SkullbonezCore::Math::Vector::Vector3;
namespace Physics = SkullbonezCore::Physics;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
constexpr size_t MAX_PIPELINE_TRACE_RECORDS = 4096;
constexpr float PHYSICS_FAST_SWEEP_MAX_RADIUS = 1.0f;
constexpr float PHYSICS_FAST_SWEEP_MIN_DISTANCE = 1.0f;
constexpr float PHYSICS_FAST_SWEEP_PAIR_SLOP = 1.0f;
constexpr float BROADPHASE_MIN_CELL_SIZE = 0.5f;
constexpr float DEFAULT_BROADPHASE_CELL = 24.0f;
bool IsSolverBodyFixed( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<size_t>( bodyIndex )] != 0u;
}

Vector3 SolverBodyPosition( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( bodyIndex ) );
}

float SolverBodyRadius( std::span<const Physics::ColliderRecord> colliderRecords, int bodyIndex )
{
    return colliderRecords[static_cast<size_t>( bodyIndex )].boundingRadius;
}

// Invariant: conservative augmentation appends only normalized pairs not
// already emitted by the grid. The linear scan preserves first-seen order.
void AppendCandidatePairIfMissing( Physics::PhysicsCandidatePairList& candidatePairs,
                                   const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                                   std::span<const uint8_t> sleepState, float dt, float contactSkin, int a, int b )
{
    const int modelCount = (std::min)( bodyStore.Count(), colliderStore.Count() );

    if ( a == b || a < 0 || b < 0 || a >= modelCount || b >= modelCount )
    {
        return;
    }

    if ( a > b )
    {
        std::swap( a, b );
    }

    if ( !Physics::BroadphaseCandidateCanTouch( bodyStore, colliderStore, sleepState, dt, contactSkin, a, b ) )
    {
        return;
    }

    for ( const std::pair<int, int>& pair : candidatePairs )
    {

        if ( pair.first == a && pair.second == b )
        {
            return;
        }
    }

    if ( !Physics::BroadphaseCandidateAppendHasCapacity( candidatePairs.size(), candidatePairs.capacity() ) )
    {

        // Lane F: growing here would violate the zero-allocation fixed-step
        // contract; dropping the conservative pair could miss a collision.
        SB_FATAL( "Physics/PhysicsBroadphaseStage",
                  "Candidate pair reserve exhausted: size=%zu capacity=%zu phase=steady_gameplay.", candidatePairs.size(),
                  candidatePairs.capacity() );
    }

    candidatePairs.emplace_back( a, b );
}

bool IsFastSmallSweepBody( const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                           std::span<const Physics::ColliderRecord> colliderRecords, int bodyIndex, float dt )
{

    if ( IsSolverBodyFixed( hotFields, bodyIndex ) )
    {
        return false;
    }

    const float radius = SolverBodyRadius( colliderRecords, bodyIndex );

    if ( radius > PHYSICS_FAST_SWEEP_MAX_RADIUS )
    {
        return false;
    }

    const Vector3 displacement = Physics::PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( bodyIndex ) ) * dt;
    const float displacementSq = Vector::VectorMagSquared( displacement );
    const float minSweepDistance = (std::max)( radius * 2.0f, PHYSICS_FAST_SWEEP_MIN_DISTANCE );
    return displacementSq > minSweepDistance * minSweepDistance;
}

// Invariant: contactEpsilon is the raw config value, not the clamped
// broadphase contact skin. It controls only conservative pair admission.
bool SweptSegmentTouchesExpandedBody( const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                      std::span<const Physics::ColliderRecord> colliderRecords, int movingIndex,
                                      int targetIndex, float dt, float contactEpsilon )
{
    const Vector3 relativeStart = SolverBodyPosition( hotFields, movingIndex ) -
                                  SolverBodyPosition( hotFields, targetIndex );

    const Vector3 relativeDisplacement = ( Physics::PhysicsBodyLinearVelocity( hotFields,
                                                                               static_cast<size_t>( movingIndex ) ) -
                                           Physics::PhysicsBodyLinearVelocity( hotFields,
                                                                               static_cast<size_t>( targetIndex ) ) ) *
                                         dt;

    const float relativeLengthSq = Vector::VectorMagSquared( relativeDisplacement );

    if ( relativeLengthSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    float t = -( Dot( relativeStart, relativeDisplacement ) ) / relativeLengthSq;
    t = (std::max)( 0.0f, (std::min)( 1.0f, t ) );
    const Vector3 closestRelative = relativeStart + relativeDisplacement * t;
    const float expandedRadius = SolverBodyRadius( colliderRecords, movingIndex ) +
                                 SolverBodyRadius( colliderRecords, targetIndex ) + contactEpsilon +
                                 PHYSICS_FAST_SWEEP_PAIR_SLOP;

    return Vector::VectorMagSquared( closestRelative ) <= expandedRadius * expandedRadius;
}

bool AppendFastSmallSweepPairs( Physics::PhysicsCandidatePairList& candidatePairs,
                                const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                                std::span<const uint8_t> sleepState, const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                std::span<const Physics::ColliderRecord> colliderRecords,
                                std::span<const int> awakeBodyIndices, float dt, float contactSkin, float contactEpsilon )
{
    const size_t pairCountBeforeSweep = candidatePairs.size();

    for ( int movingIndex : awakeBodyIndices )
    {

        if ( !IsFastSmallSweepBody( hotFields, colliderRecords, movingIndex, dt ) )
        {
            continue;
        }

        const int modelCount = (std::min)( bodyStore.Count(), colliderStore.Count() );

        for ( int targetIndex = 0; targetIndex < modelCount; ++targetIndex )
        {

            if ( movingIndex != targetIndex && SweptSegmentTouchesExpandedBody( hotFields, colliderRecords, movingIndex,
                                                                                targetIndex, dt, contactEpsilon ) )
            {
                AppendCandidatePairIfMissing( candidatePairs, bodyStore, colliderStore, sleepState, dt, contactSkin,
                                              movingIndex, targetIndex );
            }
        }
    }

    return candidatePairs.size() != pairCountBeforeSweep;
}

void CanonicalizeCandidatePairs( Physics::PhysicsCandidatePairList& candidatePairs )
{

    // Why: grid output is already canonical, but rare fast-sweep augmentation
    // appends pairs after it. Sorting once before pruning keeps the complete
    // solver-visible order independent of which conservative path found a pair.
    std::sort( candidatePairs.begin(), candidatePairs.end() );
}

bool IsFixedSolverCandidatePair( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int modelCount,
                                 const std::pair<int, int>& pair )
{
    const int a = pair.first;
    const int b = pair.second;
    return a >= 0 && b >= 0 && a < modelCount && b < modelCount && IsSolverBodyFixed( hotFields, a ) &&
           IsSolverBodyFixed( hotFields, b );
}

struct FixedSolverCandidatePairPredicate
{
    Physics::PhysicsBodyHotFieldsConstView hotFields;
    int modelCount = 0;

    bool operator()( const std::pair<int, int>& pair ) const
    {
        return IsFixedSolverCandidatePair( hotFields, modelCount, pair );
    }
};

bool IsPointJointCandidatePair( const Physics::PhysicsBodyStore& bodyStore,
                                std::span<const Physics::PointJointConstraint> pointJointConstraints,
                                const std::pair<int, int>& pair )
{
    int bodyA = pair.first;
    int bodyB = pair.second;

    if ( bodyA < 0 || bodyB < 0 || bodyA == bodyB )
    {
        return false;
    }

    if ( bodyA > bodyB )
    {
        std::swap( bodyA, bodyB );
    }

    for ( const Physics::PointJointConstraint& constraint : pointJointConstraints )
    {
        int jointA = constraint.BodyAIndex( bodyStore );
        int jointB = constraint.BodyBIndex( bodyStore );

        if ( jointA < 0 || jointB < 0 )
        {
            continue;
        }

        if ( jointA > jointB )
        {
            std::swap( jointA, jointB );
        }

        if ( jointA == bodyA && jointB == bodyB )
        {
            return true;
        }
    }

    return false;
}

struct PointJointCandidatePairPredicate
{
    const Physics::PhysicsBodyStore& bodyStore;
    std::span<const Physics::PointJointConstraint> pointJointConstraints;

    bool operator()( const std::pair<int, int>& pair ) const
    {
        return IsPointJointCandidatePair( bodyStore, pointJointConstraints, pair );
    }
};

void TryRecordSleepPrunedCandidatePair( Physics::PhysicsPipelineTraceRecorder& physicsPipelineTrace,
                                        const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                        const std::pair<int, int>& pair )
{

    if ( !physicsPipelineTrace.CanRecord() )
    {
        return;
    }

    const int a = pair.first;
    const int b = pair.second;
    Physics::PhysicsPipelineRecord record;
    record.stage = Physics::PhysicsPipelineStage::SleepPrunedPair;
    record.bodyA = a;
    record.bodyB = b;
    record.point = ( Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( a ) ) +
                     Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( b ) ) ) *
                   0.5f;

    record.scalarA = 1.0f;
    physicsPipelineTrace.Record( record );
}

bool TryRecordBroadphaseCandidatePair( Physics::PhysicsPipelineTraceRecorder& physicsPipelineTrace,
                                       const Physics::PhysicsBodyHotFieldsConstView& hotFields, int modelCount,
                                       const std::pair<int, int>& pair, size_t candidateCount )
{

    if ( !physicsPipelineTrace.CanRecord() )
    {
        return false;
    }

    if ( pair.first < 0 || pair.second < 0 || pair.first >= modelCount || pair.second >= modelCount )
    {
        return true;
    }

    Physics::PhysicsPipelineRecord record;
    record.stage = Physics::PhysicsPipelineStage::BroadphaseCandidate;
    record.bodyA = pair.first;
    record.bodyB = pair.second;
    record.point = ( Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.first ) ) +
                     Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.second ) ) ) *
                   0.5f;

    const Vector3 delta = Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.second ) ) -
                          Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.first ) );

    const float deltaMag = Vector::VectorMag( delta );
    record.normal = deltaMag > TOLERANCE ? delta / deltaMag : Vector3( 0.0f, 1.0f, 0.0f );
    record.scalarA = static_cast<float>( candidateCount );
    physicsPipelineTrace.Record( record );
    return true;
}

template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}
} // namespace

namespace SkullbonezCore
{
namespace Physics
{
PhysicsBroadphaseStage::PhysicsBroadphaseStage() : m_spatialGrid( DEFAULT_BROADPHASE_CELL )
{
}


void PhysicsBroadphaseStage::ReserveSceneCapacity( std::size_t bodyCapacity )
{
    m_spatialGrid.ReserveSceneCapacity( bodyCapacity );
    const std::size_t pairCapacity = PhysicsCandidatePairCapacity( bodyCapacity );
    m_candidatePairs.Reserve( pairCapacity );
    m_collisionCellKeys.Reserve( pairCapacity );
#if defined( _DEBUG )
    m_sleepPrunedPairs.Reserve( pairCapacity );
#endif
}


void PhysicsBroadphaseStage::ApplyRuntimeSettings( const BroadphaseSettings& settings )
{
    const float configuredCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, settings.cellSize );

    if ( configuredCell != m_spatialGrid.GetCellSize() )
    {
        m_gridMembershipSeeded = false;
    }

    m_spatialGrid.SetCellSize( configuredCell );
}


void PhysicsBroadphaseStage::Clear()
{
    m_candidatePairs.clear();
    m_collisionCellKeys.clear();
    m_spatialGrid.Clear();
    m_gridMembershipSeeded = false;
    m_gridMembershipBodyCount = 0;
    m_largestBroadphaseRadius = 0.0f;
    m_largestBroadphaseRadiusValid = false;
#if defined( _DEBUG )
    m_sleepPrunedPairs.clear();
#endif
}


void PhysicsBroadphaseStage::InvalidateBodyTopology()
{

    // Cold authored mutations may preserve body count while replacing a dense
    // row. The next Run refreshes every range in-place; retaining the fixed grid
    // avoids an O(table capacity) clear for each body in a replay restore batch.
    m_candidatePairs.clear();
    m_collisionCellKeys.clear();
    m_gridMembershipSeeded = false;
    m_gridMembershipBodyCount = 0;
    m_largestBroadphaseRadius = 0.0f;
    m_largestBroadphaseRadiusValid = false;
#if defined( _DEBUG )
    m_sleepPrunedPairs.clear();
#endif
}


void PhysicsBroadphaseStage::ResetTransientAfterReplayRestore()
{

    // Invariant: replay restores collision-cell diagnostic keys from the
    // snapshot, while candidate pairs and grid buckets are rebuilt next tick.
    m_candidatePairs.clear();
    m_spatialGrid.Clear();
    m_gridMembershipSeeded = false;
    m_gridMembershipBodyCount = 0;
    m_largestBroadphaseRadius = 0.0f;
    m_largestBroadphaseRadiusValid = false;
#if defined( _DEBUG )
    m_sleepPrunedPairs.clear();
#endif
}


std::span<const std::pair<int, int>> PhysicsBroadphaseStage::Run( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, const BroadphaseSettings& broadphaseSettings,
                                                                  std::span<const PointJointConstraint> pointJointConstraints, std::span<const uint8_t> sleepState,
                                                                  std::span<const int> awakeBodyIndices, PhysicsStepDiagnostics& stepDiagnostics, float dt, float contactSkin,
                                                                  float contactEpsilon, Core::Profiler* )
{
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    const int modelCount = (std::min)( { bodyStore.Count(), static_cast<int>( bodyRecords.size() ),
                                         static_cast<int>( colliderRecords.size() ) } );

    auto& physicsPipelineTrace = stepDiagnostics.MutablePipelineTraceRecorder();

    {

        // Invariant: Broadphase is the inclusive owner marker. Every direct
        // child below is mutually exclusive so reports can sum children once
        // without adding a nested interval a second time.
        PROFILE_SCOPED( "Frame/Physics/Broadphase/GridSetup" );

        if ( !m_largestBroadphaseRadiusValid )
        {

            // Cold topology boundary: collider radii do not change during a
            // fixed step, so the scene-wide maximum is not an all-body hot pass.
            m_largestBroadphaseRadius = 0.0f;

            for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
            {
                const float radius = SolverBodyRadius( colliderRecords, bodyIndex );

                if ( std::isfinite( radius ) && radius > m_largestBroadphaseRadius )
                {
                    m_largestBroadphaseRadius = radius;
                }
            }

            m_largestBroadphaseRadiusValid = true;
        }

        // Why: a fixed 24m cell made the 200-brick wall share huge buckets.
        // Deterministic scene inputs choose a cell no larger than the config cap.
        const float configuredCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, broadphaseSettings.cellSize );
        const float sceneCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, ( m_largestBroadphaseRadius + contactSkin ) * 2.0f );

        const float selectedCellSize = (std::min)( configuredCell, sceneCell );

        if ( selectedCellSize != m_spatialGrid.GetCellSize() )
        {
            m_gridMembershipSeeded = false;
        }

        m_spatialGrid.SetCellSize( selectedCellSize );
        m_spatialGrid.BeginFrame( modelCount );
        m_collisionCellKeys.clear();
    }
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/GridMaintain" );
        const bool fullSeed = !m_gridMembershipSeeded || m_gridMembershipBodyCount != modelCount;
        auto maintainBody = [&]( int bodyIndex, bool isAwakeSource )
        {
            const float radius = SolverBodyRadius( colliderRecords, bodyIndex ) + contactSkin;

            const Vector3 displacement = PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( bodyIndex ) ) * dt;

            const float displacementSq = Vector::VectorMagSquared( displacement );

            if ( isAwakeSource && displacementSq > radius * radius )
            {
                m_spatialGrid.InsertSwept( bodyIndex, SolverBodyPosition( hotFields, bodyIndex ), displacement, radius );
            }
            else
            {
                m_spatialGrid.Insert( bodyIndex, SolverBodyPosition( hotFields, bodyIndex ), radius );
            }

            if ( isAwakeSource )
            {
                m_spatialGrid.MarkPairSourceCells( bodyIndex );
            }
        };

        if ( fullSeed )
        {

            // Cold boundary: seed every persistent membership once, but stamp
            // only awake dynamic bodies as this frame's pair-work sources.

            for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
            {
                const bool isAwakeSource = !IsSolverBodyFixed( hotFields, bodyIndex ) &&
                                           sleepState[static_cast<size_t>( bodyIndex )] == 0u;

                maintainBody( bodyIndex, isAwakeSource );
            }

            m_gridMembershipSeeded = true;
            m_gridMembershipBodyCount = modelCount;
        }
        else
        {

            // P3 invariant: sleepers keep their last persistent range. Only
            // awake bodies can move, sweep, or source new narrowphase work.

            for ( int bodyIndex : awakeBodyIndices )
            {
                maintainBody( bodyIndex, true );
            }
        }
    }
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/CandidatePairs" );
#if defined( _DEBUG )
        m_sleepPrunedPairs.clear();

        // Debug walks the full retained grid to preserve one bounded
        // SleepPrunedPair breadcrumb per old sleep-only pair.
        m_spatialGrid.GetFilteredCandidatePairs( m_candidatePairs, bodyStore, colliderStore, sleepState, dt, contactSkin,
                                                 m_sleepPrunedPairs, false );
#else

        // Production visits only cells reached by an awake body this step;
        // sleep-only cells retain membership but emit no candidate work.
        m_spatialGrid.GetFilteredCandidatePairs( m_candidatePairs, bodyStore, colliderStore, sleepState, dt, contactSkin,
                                                 true );
#endif
    }

    bool fastSmallSweepAppendedPairs = false;
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/FastSmallSweepAugment" );
        fastSmallSweepAppendedPairs = AppendFastSmallSweepPairs( m_candidatePairs, bodyStore, colliderStore, sleepState,
                                                                 hotFields, colliderRecords, awakeBodyIndices, dt,
                                                                 contactSkin, contactEpsilon );
    }

    if ( fastSmallSweepAppendedPairs )
    {
        CanonicalizeCandidatePairs( m_candidatePairs );
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneFixedPairs" );
        m_candidatePairs.erase( std::remove_if( m_candidatePairs.begin(), m_candidatePairs.end(),
                                                FixedSolverCandidatePairPredicate { hotFields, modelCount } ),
                                m_candidatePairs.end() );

#if defined( _DEBUG )
        m_sleepPrunedPairs.erase( std::remove_if( m_sleepPrunedPairs.begin(), m_sleepPrunedPairs.end(),
                                                  FixedSolverCandidatePairPredicate { hotFields, modelCount } ),
                                  m_sleepPrunedPairs.end() );
#endif
    }

    if ( !pointJointConstraints.empty() )
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneJointPairs" );
        m_candidatePairs.erase( std::remove_if( m_candidatePairs.begin(), m_candidatePairs.end(),
                                                PointJointCandidatePairPredicate { bodyStore, pointJointConstraints } ),
                                m_candidatePairs.end() );

#if defined( _DEBUG )
        m_sleepPrunedPairs.erase( std::remove_if( m_sleepPrunedPairs.begin(), m_sleepPrunedPairs.end(),
                                                  PointJointCandidatePairPredicate { bodyStore, pointJointConstraints } ),
                                  m_sleepPrunedPairs.end() );
#endif
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/RecordCandidates" );

        // Why: every retained candidate already passed the pair-validity gate,
        // so count-only mode can batch the canonical event cardinality without
        // loading either body's position or comparing capacity per pair.

        if ( !physicsPipelineTrace.RetainsFullRecords() )
        {
            std::size_t pipelineEventCount = m_candidatePairs.size();
#if defined( _DEBUG )
            pipelineEventCount += m_sleepPrunedPairs.size();
#endif
            physicsPipelineTrace.RecordEvents( pipelineEventCount );
        }
        else
        {
#if defined( _DEBUG )

            // Compatibility invariant: P2 recorded the canonical geometrically
            // admitted stream before removing sleep-only pairs. Reconstruct that
            // Debug trace by merging the two retained sorted lists; this does not
            // restore dormant solver work to the production candidate vector.
            std::sort( m_sleepPrunedPairs.begin(), m_sleepPrunedPairs.end() );
            const size_t diagnosticCandidateCount = m_candidatePairs.size() + m_sleepPrunedPairs.size();
            auto visible = m_candidatePairs.begin();
            auto pruned = m_sleepPrunedPairs.begin();

            while ( visible != m_candidatePairs.end() || pruned != m_sleepPrunedPairs.end() )
            {
                const bool takePruned = visible == m_candidatePairs.end() ||
                                        ( pruned != m_sleepPrunedPairs.end() && *pruned < *visible );

                const std::pair<int, int>& pair = takePruned ? *pruned++ : *visible++;

                if ( !TryRecordBroadphaseCandidatePair( physicsPipelineTrace, hotFields, modelCount, pair,
                                                        diagnosticCandidateCount ) )
                {
                    break;
                }
            }
#else

            for ( const auto& pair : m_candidatePairs )
            {

                if ( !TryRecordBroadphaseCandidatePair( physicsPipelineTrace, hotFields, modelCount, pair,
                                                        m_candidatePairs.size() ) )
                {
                    break;
                }
            }
#endif
        }
    }
#if defined( _DEBUG )
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/RecordSleepPrunedPairs" );

        // The production path never walks sleep-only cells. Debug retains the
        // old diagnostic evidence at the earlier emission skip instead of
        // paying for a solver-visible list followed by a prune pass.

        if ( !physicsPipelineTrace.RetainsFullRecords() )
        {
            physicsPipelineTrace.RecordEvents( m_sleepPrunedPairs.size() );
        }
        else
        {

            for ( const std::pair<int, int>& pair : m_sleepPrunedPairs )
            {
                TryRecordSleepPrunedCandidatePair( physicsPipelineTrace, hotFields, pair );
            }
        }
    }
#endif
    PROFILE_END( "Frame/Physics/Broadphase" );
    return m_candidatePairs;
}


const Math::CollisionDetection::SpatialGrid& PhysicsBroadphaseStage::GetSpatialGrid() const
{
    return m_spatialGrid;
}


float PhysicsBroadphaseStage::GetCellSize() const
{
    return m_spatialGrid.GetCellSize();
}


std::span<const std::pair<int, int>> PhysicsBroadphaseStage::GetCandidatePairs() const
{
    return m_candidatePairs;
}


std::span<const int64_t> PhysicsBroadphaseStage::GetCollisionCellKeys() const
{
    return m_collisionCellKeys;
}


std::span<const int64_t> PhysicsBroadphaseStage::CollisionCellKeysForReplay() const
{
    return m_collisionCellKeys;
}


PhysicsCollisionCellKeyList& PhysicsBroadphaseStage::CollisionCellKeysForReplay()
{
    return m_collisionCellKeys;
}


void PhysicsBroadphaseStage::AppendCollisionCellKey( int64_t collisionCellKey )
{

    if ( m_collisionCellKeys.size() >= m_collisionCellKeys.capacity() )
    {
        assert( false && "Physics collision-cell key capacity exceeded" );

        // Invariant: collision-cell diagnostics share the fixed candidate-pair
        // event budget; overflow would lose deterministic evidence.
        SB_FATAL( "Physics/PhysicsWorld", "Physics collision-cell key capacity exceeded" );
    }

    m_collisionCellKeys.push_back( collisionCellKey );
}


uint64_t PhysicsBroadphaseStage::CollectDynamicMemoryBytes() const
{

    // Invariant: this is the owning contribution used by PhysicsWorld's total.
    // SpatialGrid's inline control/topology is already inside sizeof(PhysicsWorld);
    // its registered backing must be added here exactly once.
    uint64_t bytes = m_spatialGrid.CollectDynamicMemoryBytes() + ListCapacityBytes( m_candidatePairs ) +
                     ListCapacityBytes( m_collisionCellKeys );

#if defined( _DEBUG )
    bytes += ListCapacityBytes( m_sleepPrunedPairs );
#endif
    return bytes;
}


uint64_t PhysicsBroadphaseStage::CollectDebugAndBroadphaseMemoryBytes() const
{

    // Historical diagnostic subset: include the grid's inline bytes plus the
    // same owning dynamic contribution, but do not add this subset to totals.
    return static_cast<uint64_t>( sizeof( m_spatialGrid ) ) + CollectDynamicMemoryBytes();
}
} // namespace Physics
} // namespace SkullbonezCore
