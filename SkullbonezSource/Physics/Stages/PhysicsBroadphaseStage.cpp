/*
File: SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
Purpose:
  Implements deterministic broadphase candidate generation and retained output.

Summary:
  The stage maintains persistent integer-range membership, adds a one-step
  motion overlay for policy-selected translation and angular shape reach,
  canonicalizes solver-visible pair order, stamps cells reached by awake bodies,
  suppresses sleep-only work at emission, prunes fixed/joint pairs, and records
  bounded pipeline evidence.

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
  - Every maintained persistent range is committed before any transient motion
    overlay consumes shared SpatialGrid bucket rows.
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
#include <bit>
#include <cassert>
#include <cmath>
#include <limits>

using SkullbonezCore::Math::Vector::Vector3;
namespace Physics = SkullbonezCore::Physics;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
constexpr float PHYSICS_FAST_SWEEP_MAX_RADIUS = 1.0f;
constexpr float PHYSICS_FAST_SWEEP_MIN_DISTANCE = 1.0f;
constexpr float PHYSICS_FAST_SWEEP_PAIR_SLOP = 1.0f;
constexpr float BROADPHASE_MIN_CELL_SIZE = 0.5f;
constexpr float DEFAULT_BROADPHASE_CELL = 24.0f;
bool IsSolverBodyFixed( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<size_t>( bodyIndex )] != 0u;
}

float SolverBodyRadius( std::span<const Physics::ColliderRecord> colliderRecords, int bodyIndex )
{
    return colliderRecords[static_cast<size_t>( bodyIndex )].boundingRadius;
}

float SolverShapeRadius( std::span<const Physics::ColliderRecord> colliderRecords, int bodyIndex )
{
    return SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius( colliderRecords[static_cast<size_t>( bodyIndex )].shape );
}

Vector3 SolverColliderCenter( const Physics::PhysicsBodyHotFieldsConstView& hotFields, std::span<const Physics::ColliderRecord> colliderRecords, int bodyIndex )
{
    const size_t index = static_cast<size_t>( bodyIndex );
    const auto orientation = Physics::PhysicsBodyOrientation( hotFields, index ).GetOrientationMatrix();
    return SkullbonezCore::Math::CollisionDetection::GetWorldShapeCenter( colliderRecords[index].shape, Physics::PhysicsBodyPosition( hotFields, index ), orientation );
}

bool IsFastSmallSweepBody( const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                           std::span<const Physics::ColliderRecord> colliderRecords,
                           int bodyIndex,
                           Physics::BroadphaseSweepContactEnvelope envelope )
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

    const Vector3 displacement = Physics::PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( bodyIndex ) ) * envelope.DeltaTime();
    const float displacementSq = Vector::VectorMagSquared( displacement );
    const float minSweepDistance = (std::max)( radius * 2.0f, PHYSICS_FAST_SWEEP_MIN_DISTANCE );
    return displacementSq > minSweepDistance * minSweepDistance;
}

void CanonicalizeCandidatePairs( Physics::PhysicsCandidatePairList& candidatePairs )
{
    // Why: grid output is already canonical, but rare fast-sweep augmentation
    // appends pairs after it. Sorting once before pruning keeps the complete
    // solver-visible order independent of which conservative path found a pair.
    std::sort( candidatePairs.begin(), candidatePairs.end() );
}

bool IsFixedSolverCandidatePair( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int modelCount, const std::pair<int, int>& pair )
{
    const int a = pair.first;
    const int b = pair.second;
    return a >= 0 && b >= 0 && a < modelCount && b < modelCount && IsSolverBodyFixed( hotFields, a ) && IsSolverBodyFixed( hotFields, b );
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

#if defined( _DEBUG )
void TryRecordSleepPrunedCandidatePair( Physics::PhysicsPipelineTraceRecorder& physicsPipelineTrace, const Physics::PhysicsBodyHotFieldsConstView& hotFields, const std::pair<int, int>& pair )
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
    record.point = ( Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( a ) ) + Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( b ) ) ) * 0.5f;

    record.scalarA = 1.0f;
    physicsPipelineTrace.Record( record );
}
#endif

bool TryRecordBroadphaseCandidatePair( Physics::PhysicsPipelineTraceRecorder& physicsPipelineTrace,
                                       const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                       int modelCount,
                                       const std::pair<int, int>& pair,
                                       size_t candidateCount )
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
    record.point = ( Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.first ) ) + Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.second ) ) ) * 0.5f;

    const Vector3 delta = Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.second ) ) - Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.first ) );

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
PhysicsBroadphaseStage::PhysicsBroadphaseStage() : m_spatialGrid( DEFAULT_BROADPHASE_CELL ), m_configuredCellSize( DEFAULT_BROADPHASE_CELL )
{
}

bool PhysicsBroadphaseStage::MarkSweepPairFirstSeen( int a, int b )
{
    if ( a > b )
    {
        std::swap( a, b );
    }
    const uint64_t key = ( static_cast<uint64_t>( a ) << 32u ) | static_cast<uint32_t>( b );
    const std::size_t mask = m_sweepPairs.size() - 1u;
    std::size_t slot = static_cast<std::size_t>( ( ( key + 1u ) * 11400714819323198485ull ) >> 32u ) & mask;
    // Capacity is at least twice the accepted pair ceiling. Only admitted
    // pairs occupy slots, so probing always reaches an empty slot before a
    // candidate-capacity failure; duplicate discovery consumes no output slot.
    for ( std::size_t probe = 0; probe < m_sweepPairs.size(); ++probe )
    {
        ++m_workStats.sweepPairProbes;
        if ( m_sweepPairs[slot] == key + 1u )
        {
            return false;
        }
        if ( m_sweepPairs[slot] == 0u )
        {
            m_sweepPairs[slot] = key + 1u;
            return true;
        }
        slot = ( slot + 1u ) & mask;
    }
    SB_FATAL( "Physics/Broadphase", "Sweep pair membership exhausted: capacity=%zu.", m_sweepPairs.size() );
}

bool PhysicsBroadphaseStage::SweepTouches( const PhysicsBodyHotFieldsConstView& hotFields, int movingIndex, int targetIndex, BroadphaseSweepContactEnvelope envelope ) const
{
    const Vector3 relativeStart = m_sweepGeometry[movingIndex].center - m_sweepGeometry[targetIndex].center;
    // Preserve subtraction before scaling: separately scaled velocities can
    // change the last bits and therefore the admitted conservative pair set.
    const Vector3 relativeDisplacement = ( PhysicsBodyLinearVelocity( hotFields, movingIndex ) - PhysicsBodyLinearVelocity( hotFields, targetIndex ) ) * envelope.DeltaTime();
    const float relativeLengthSq = Vector::VectorMagSquared( relativeDisplacement );
    if ( relativeLengthSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }
    float t = -Dot( relativeStart, relativeDisplacement ) / relativeLengthSq;
    t = (std::max)( 0.0f, (std::min)( 1.0f, t ) );
    const Vector3 closestRelative = relativeStart + relativeDisplacement * t;
    const float expandedRadius = m_sweepGeometry[movingIndex].radius + m_sweepGeometry[targetIndex].radius + envelope.ContactEpsilon() + PHYSICS_FAST_SWEEP_PAIR_SLOP;
    return Vector::VectorMagSquared( closestRelative ) <= expandedRadius * expandedRadius;
}

bool PhysicsBroadphaseStage::SweepBoundsForBody( const PhysicsBodyHotFieldsConstView& hotFields, BroadphaseSweepContactEnvelope envelope, int body, SweepBounds& bounds ) const
{
    const auto& geometry = m_sweepGeometry[body];
    const Vector3 velocity = PhysicsBodyLinearVelocity( hotFields, body );
    const double positions[] = { geometry.center.x, geometry.center.y, geometry.center.z };
    const double velocities[] = { velocity.x, velocity.y, velocity.z };
    const double radius = geometry.radius;
    if ( !std::isfinite( radius ) || radius < 0.0 || radius > 1.0e15 )
    {
        return false;
    }
    for ( int axis = 0; axis < 3; ++axis )
    {
        const double travel = velocities[axis] * static_cast<double>( envelope.DeltaTime() );
        const double scale = std::abs( positions[axis] ) + std::abs( travel ) + radius + envelope.ContactEpsilon() + 1.0;
        if ( !std::isfinite( scale ) || scale > 1.0e15 )
        {
            return false;
        }
        // The exact predicate subtracts binary32 positions/velocities before
        // scaling and evaluating a clamped segment. Double bounds enclose both
        // separately represented sweeps, expanded by 64 float ulps of their
        // absolute input scale. This exceeds the accumulated rounding error
        // of subtraction, scaling, closest-point evaluation and radius sums.
        // Extreme/non-finite inputs use the complete original scan instead;
        // within this range none of the predicate's squared sums can overflow.
        const double padding = radius + envelope.ContactEpsilon() + PHYSICS_FAST_SWEEP_PAIR_SLOP + scale * ( 64.0 * std::numeric_limits<float>::epsilon() );
        bounds.low[axis] = (std::min)( positions[axis], positions[axis] + travel ) - padding;
        bounds.high[axis] = (std::max)( positions[axis], positions[axis] + travel ) + padding;
    }
    return true;
}

bool PhysicsBroadphaseStage::PrepareSweepQuery( const PhysicsBodyHotFieldsConstView& hotFields, BroadphaseSweepContactEnvelope envelope )
{
    const std::size_t count = m_sweepGeometry.size();
    m_sweepOrder.ResetDefault( count );
    for ( std::size_t body = 0; body < count; ++body )
    {
        SweepBounds bounds;
        if ( !SweepBoundsForBody( hotFields, envelope, static_cast<int>( body ), bounds ) )
        {
            return false;
        }
        m_sweepOrder[body] = static_cast<int>( body );
    }
    std::sort( m_sweepOrder.begin(), m_sweepOrder.end(), [&]( int a, int b )
               {
                   const float left = m_sweepGeometry[a].center.x;
                   const float right = m_sweepGeometry[b].center.x;
                   return left < right || ( left == right && a < b );
               } );
    m_sweepLeafBase = std::bit_ceil( count );
    m_sweepTree.ResetDefault( m_sweepLeafBase * 2u );
    for ( std::size_t leaf = 0; leaf < m_sweepLeafBase; ++leaf )
    {
        auto& bounds = m_sweepTree[m_sweepLeafBase + leaf];
        if ( leaf < count )
        {
            (void)SweepBoundsForBody( hotFields, envelope, m_sweepOrder[leaf], bounds );
        }
        else
        {
            for ( int axis = 0; axis < 3; ++axis )
            {
                bounds.low[axis] = std::numeric_limits<double>::infinity();
                bounds.high[axis] = -std::numeric_limits<double>::infinity();
            }
        }
    }
    for ( std::size_t node = m_sweepLeafBase - 1u; node > 0u; --node )
    {
        for ( int axis = 0; axis < 3; ++axis )
        {
            m_sweepTree[node].low[axis] = (std::min)( m_sweepTree[node * 2u].low[axis], m_sweepTree[node * 2u + 1u].low[axis] );
            m_sweepTree[node].high[axis] = (std::max)( m_sweepTree[node * 2u].high[axis], m_sweepTree[node * 2u + 1u].high[axis] );
        }
    }
    return true;
}

void PhysicsBroadphaseStage::AppendSweepTarget( const BroadphasePairFilter& filter, const PhysicsBodyHotFieldsConstView& hotFields, BroadphaseSweepContactEnvelope envelope, int moving, int target )
{
    if ( moving == target )
    {
        return;
    }
    ++m_workStats.sweepTargets;
    const int a = (std::min)( moving, target );
    const int b = (std::max)( moving, target );
    if ( !SweepTouches( hotFields, moving, target, envelope ) || !filter.CanTouch( a, b ) || !MarkSweepPairFirstSeen( a, b ) )
    {
        return;
    }
    if ( !BroadphaseCandidateAppendHasCapacity( m_candidatePairs.size(), m_candidatePairs.capacity() ) )
    {
        SB_FATAL( "Physics/Broadphase", "Candidate pair reserve exhausted: size=%zu capacity=%zu phase=steady_gameplay.", m_candidatePairs.size(), m_candidatePairs.capacity() );
    }
    m_candidatePairs.emplace_back( a, b );
}

void PhysicsBroadphaseStage::QuerySweepTargets( const BroadphasePairFilter& filter, const PhysicsBodyHotFieldsConstView& hotFields, BroadphaseSweepContactEnvelope envelope, int moving )
{
    SweepBounds query;
    (void)SweepBoundsForBody( hotFields, envelope, moving, query );
    // Depth-first traversal needs one sibling per level plus the current node.
    // The fixed tree's 8,192-body ceiling needs at most fourteen stack entries.
    static_assert( std::bit_width( PHYSICS_MAX_BODY_ROWS ) < 32u );
    std::size_t stack[32] = { 1u };
    std::size_t pending = 1u;
    while ( pending > 0u )
    {
        const std::size_t node = stack[--pending];
        const auto& bounds = m_sweepTree[node];
        ++m_workStats.sweepQueryNodes;
        bool separated = false;
        for ( int axis = 0; axis < 3; ++axis )
        {
            separated |= bounds.high[axis] < query.low[axis] || query.high[axis] < bounds.low[axis];
        }
        if ( separated )
        {
            continue;
        }
        if ( node >= m_sweepLeafBase )
        {
            const std::size_t leaf = node - m_sweepLeafBase;
            if ( leaf < m_sweepOrder.size() )
            {
                AppendSweepTarget( filter, hotFields, envelope, moving, m_sweepOrder[leaf] );
            }
        }
        else
        {
            stack[pending++] = node * 2u + 1u;
            stack[pending++] = node * 2u;
        }
    }
}

bool PhysicsBroadphaseStage::AppendFastSmallSweepPairs( const BroadphasePairFilter& pairFilter,
                                                        const PhysicsBodyHotFieldsConstView& hotFields,
                                                        std::span<const ColliderRecord> colliders,
                                                        BroadphaseBodyActivityView activity,
                                                        BroadphaseSweepContactEnvelope envelope )
{
    const std::size_t initialPairs = m_candidatePairs.size();
    bool prepared = false;
    bool useQuery = false;
    for ( int moving : activity.AwakeBodyIndices() )
    {
        if ( !IsFastSmallSweepBody( hotFields, colliders, moving, envelope ) )
        {
            continue;
        }
        ++m_workStats.sweepMovers;
        if ( !prepared )
        {
            m_sweepGeometry.ResetDefault( pairFilter.BodyCount() );
            for ( int index = 0; index < pairFilter.BodyCount(); ++index )
            {
                m_sweepGeometry[index] = { SolverColliderCenter( hotFields, colliders, index ), SolverShapeRadius( colliders, index ) };
            }
            m_workStats.sweepGeometryBodies = pairFilter.BodyCount();
            // Sorting/tree construction costs more than a short direct scan.
            // This threshold is a performance choice, never a coverage rule.
            const auto movers = std::count_if( activity.AwakeBodyIndices().begin(), activity.AwakeBodyIndices().end(), [&]( int index ) { return IsFastSmallSweepBody( hotFields, colliders, index, envelope ); } );
            useQuery = pairFilter.BodyCount() >= 512 && movers >= 8 && PrepareSweepQuery( hotFields, envelope );
            m_sweepPairs.ResetFill( m_sweepPairs.capacity(), 0u );
            for ( const auto& pair : m_candidatePairs )
            {
                (void)MarkSweepPairFirstSeen( pair.first, pair.second );
            }
            prepared = true;
        }
        if ( useQuery )
        {
            QuerySweepTargets( pairFilter, hotFields, envelope, moving );
        }
        else
        {
            ++m_workStats.sweepFullScanMovers;
            for ( int target = 0; target < pairFilter.BodyCount(); ++target )
            {
                AppendSweepTarget( pairFilter, hotFields, envelope, moving, target );
            }
        }
    }
    return m_candidatePairs.size() != initialPairs;
}


void PhysicsBroadphaseStage::PrepareJointExclusions( const PhysicsBodyStore& bodies, std::span<const PointJointConstraint> joints )
{
    m_jointPairs.clear();
    m_workStats.jointEndpointResolutions += joints.size() * 2u;
    // Dense rows may change after destruction or replay restore. Resolve the
    // stable handles once for this pass, never once for every candidate pair.
    for ( const auto& joint : joints )
    {
        int a = joint.BodyAIndex( bodies );
        int b = joint.BodyBIndex( bodies );
        if ( a < 0 || b < 0 || a == b )
        {
            continue;
        }
        if ( a > b )
        {
            std::swap( a, b );
        }
        m_jointPairs.emplace_back( a, b );
    }
    std::sort( m_jointPairs.begin(), m_jointPairs.end() );
    m_jointPairs.erase( std::unique( m_jointPairs.begin(), m_jointPairs.end() ), m_jointPairs.end() );
    m_workStats.jointExclusionKeys = m_jointPairs.size();
}

void PhysicsBroadphaseStage::PruneJointPairs( PhysicsCandidatePairList& pairs ) const
{
    if ( m_jointPairs.empty() )
    {
        return;
    }
    // Debug's sleep-pruned evidence is not sorted yet. Binary search of the
    // sorted exclusion keys preserves both input orders without a second policy.
    pairs.erase( std::remove_if( pairs.begin(), pairs.end(), [&]( const auto& pair ) { return std::binary_search( m_jointPairs.begin(), m_jointPairs.end(), pair ); } ), pairs.end() );
}

void PhysicsBroadphaseStage::ReserveSceneCapacity( std::size_t bodyCapacity, std::size_t pointJointCapacity )
{
    m_spatialGrid.ReserveSceneCapacity( bodyCapacity );
    m_jointPairs.Reserve( pointJointCapacity );
    m_sweepGeometry.Reserve( bodyCapacity );
    m_sweepOrder.Reserve( bodyCapacity );
    m_sweepTree.Reserve( std::bit_ceil( bodyCapacity ) * 2u );
    m_sweepPairs.Reserve( std::bit_ceil( PhysicsCandidatePairCapacity( bodyCapacity ) * 2u ) );
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

    m_configuredCellSize = configuredCell;
    m_spatialGrid.SetCellSize( configuredCell );
}


void PhysicsBroadphaseStage::Clear()
{
    m_sweepGeometry.clear();
    m_sweepPairs.clear();
    m_sweepOrder.clear();
    m_sweepTree.clear();
    m_jointPairs.clear();
    m_sweepLeafBase = 0u;
    m_workStats = {};
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
    m_sweepGeometry.clear();
    m_sweepPairs.clear();
    m_sweepOrder.clear();
    m_sweepTree.clear();
    m_jointPairs.clear();
    m_sweepLeafBase = 0u;
    m_workStats = {};
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
    m_sweepGeometry.clear();
    m_sweepPairs.clear();
    m_sweepOrder.clear();
    m_sweepTree.clear();
    m_jointPairs.clear();
    m_sweepLeafBase = 0u;
    m_workStats = {};
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


std::span<const std::pair<int, int>> PhysicsBroadphaseStage::Run( const PhysicsBodyStore& bodyStore,
                                                                  const ColliderStore& colliderStore,
                                                                  std::span<const PointJointConstraint> pointJointConstraints,
                                                                  BroadphaseBodyActivityView activity,
                                                                  BroadphaseSweepContactEnvelope envelope,
                                                                  PhysicsPipelineTraceRecorder& physicsPipelineTrace )
{
    m_workStats = {};
    m_workStats.sweepScratchBytes = ListCapacityBytes( m_sweepGeometry ) + ListCapacityBytes( m_sweepPairs ) + ListCapacityBytes( m_sweepOrder ) + ListCapacityBytes( m_sweepTree );
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    const int modelCount = (std::min)( { bodyStore.Count(), static_cast<int>( bodyRecords.size() ), static_cast<int>( colliderRecords.size() ) } );

    const BroadphasePairFilter pairFilter( bodyStore, colliderStore, activity, envelope );

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
        const float sceneCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, ( m_largestBroadphaseRadius + envelope.ContactSkin() ) * 2.0f );

        const float selectedCellSize = (std::min)( m_configuredCellSize, sceneCell );

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
        auto maintainPersistentBody = [&]( int bodyIndex )
        {
            const float baseRadius = SolverShapeRadius( colliderRecords, bodyIndex ) + envelope.ContactSkin();
            const Vector3 colliderCenter = SolverColliderCenter( hotFields, colliderRecords, bodyIndex );
            m_spatialGrid.Insert( bodyIndex, colliderCenter, baseRadius );
        };
        auto admitMotionOverlayAndMarkSource = [&]( int bodyIndex )
        {
            const float baseRadius = SolverShapeRadius( colliderRecords, bodyIndex ) + envelope.ContactSkin();
            const float angularExpansion = activity.AngularExpansion( bodyIndex );
            const float radius = std::isfinite( angularExpansion ) ? baseRadius + (std::max)( 0.0f, angularExpansion ) : ( std::numeric_limits<float>::quiet_NaN )();
            const Vector3 displacement = PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( bodyIndex ) ) * envelope.DeltaTime();
            const float displacementSq = Vector::VectorMagSquared( displacement );
            const bool hasLinearTravel = !std::isfinite( displacementSq ) || displacementSq > TOLERANCE * TOLERANCE;
            const bool linearPromoted = activity.IsLinearPromoted( bodyIndex );
            const bool publishLinearOverlay = hasLinearTravel && linearPromoted;

            if ( publishLinearOverlay || !std::isfinite( radius ) || radius > baseRadius )
            {
                const Vector3 colliderCenter = SolverColliderCenter( hotFields, colliderRecords, bodyIndex );
                const float conservativeRadius = std::isfinite( displacementSq ) ? radius : ( std::numeric_limits<float>::quiet_NaN )();

                // Invariant: translation is published only for linear-promoted
                // or angular-expanded bodies; a fully Discrete body is detected
                // at a later fixed-step boundary.
                m_spatialGrid.InsertSweptOverlayAfterPersistent( bodyIndex, colliderCenter, displacement, baseRadius, conservativeRadius );
            }

            m_spatialGrid.MarkPairSourceCells( bodyIndex );
        };

        if ( fullSeed )
        {
            // Invariant: every authoritative current-position cell is admitted
            // before any transient sweep can consume the shared bucket pool.
            for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
            {
                maintainPersistentBody( bodyIndex );
            }

            // Cold boundary stamps only awake dynamic bodies as this frame's
            // pair-work sources after persistent ownership is complete.
            for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
            {
                if ( !IsSolverBodyFixed( hotFields, bodyIndex ) && !activity.IsSleeping( bodyIndex ) )
                {
                    admitMotionOverlayAndMarkSource( bodyIndex );
                }
            }

            m_gridMembershipSeeded = true;
            m_gridMembershipBodyCount = modelCount;
        }
        else
        {
            // Invariant: sleepers keep their last persistent range. Only
            // awake bodies can move, sweep, or source new narrowphase work.
            for ( int bodyIndex : activity.AwakeBodyIndices() )
            {
                maintainPersistentBody( bodyIndex );
            }

            for ( int bodyIndex : activity.AwakeBodyIndices() )
            {
                admitMotionOverlayAndMarkSource( bodyIndex );
            }
        }
    }
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/CandidatePairs" );
#if defined( _DEBUG )
        m_sleepPrunedPairs.clear();

        // Debug walks the full retained grid to preserve one bounded
        // SleepPrunedPair breadcrumb per old sleep-only pair.
        m_spatialGrid.GetFilteredCandidatePairs( m_candidatePairs, pairFilter, m_sleepPrunedPairs, false );
#else
        // Production visits only cells reached by an awake body this step;
        // sleep-only cells retain membership but emit no candidate work.
        m_spatialGrid.GetFilteredCandidatePairs( m_candidatePairs, pairFilter, true );
#endif
    }

    bool fastSmallSweepAppendedPairs = false;
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/FastSmallSweepAugment" );
        fastSmallSweepAppendedPairs = AppendFastSmallSweepPairs( pairFilter, hotFields, colliderRecords, activity, envelope );
    }

    if ( fastSmallSweepAppendedPairs )
    {
        CanonicalizeCandidatePairs( m_candidatePairs );
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneFixedPairs" );
        m_candidatePairs.erase( std::remove_if( m_candidatePairs.begin(), m_candidatePairs.end(), FixedSolverCandidatePairPredicate { hotFields, modelCount } ), m_candidatePairs.end() );

#if defined( _DEBUG )
        m_sleepPrunedPairs.erase( std::remove_if( m_sleepPrunedPairs.begin(), m_sleepPrunedPairs.end(), FixedSolverCandidatePairPredicate { hotFields, modelCount } ), m_sleepPrunedPairs.end() );
#endif
    }

    if ( !pointJointConstraints.empty() )
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneJointPairs" );
        PrepareJointExclusions( bodyStore, pointJointConstraints );
        PruneJointPairs( m_candidatePairs );
#if defined( _DEBUG )
        PruneJointPairs( m_sleepPrunedPairs );
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
            // Compatibility invariant: Debug diagnostics record the canonical
            // geometrically admitted stream before removing sleep-only pairs. Reconstruct that
            // Debug trace by merging the two retained sorted lists; this does not
            // restore dormant solver work to the production candidate vector.
            std::sort( m_sleepPrunedPairs.begin(), m_sleepPrunedPairs.end() );
            const size_t diagnosticCandidateCount = m_candidatePairs.size() + m_sleepPrunedPairs.size();
            auto visible = m_candidatePairs.begin();
            auto pruned = m_sleepPrunedPairs.begin();

            while ( visible != m_candidatePairs.end() || pruned != m_sleepPrunedPairs.end() )
            {
                const bool takePruned = visible == m_candidatePairs.end() || ( pruned != m_sleepPrunedPairs.end() && *pruned < *visible );

                const std::pair<int, int>& pair = takePruned ? *pruned++ : *visible++;

                if ( !TryRecordBroadphaseCandidatePair( physicsPipelineTrace, hotFields, modelCount, pair, diagnosticCandidateCount ) )
                {
                    break;
                }
            }
#else

            for ( const auto& pair : m_candidatePairs )
            {
                if ( !TryRecordBroadphaseCandidatePair( physicsPipelineTrace, hotFields, modelCount, pair, m_candidatePairs.size() ) )
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

std::size_t PhysicsBroadphaseStage::CollisionCellKeyCapacityForReplay() const noexcept
{
    return m_collisionCellKeys.capacity();
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
    uint64_t bytes = m_spatialGrid.CollectDynamicMemoryBytes() + ListCapacityBytes( m_candidatePairs ) + ListCapacityBytes( m_collisionCellKeys ) + ListCapacityBytes( m_sweepGeometry ) +
                     ListCapacityBytes( m_sweepPairs ) + ListCapacityBytes( m_sweepOrder ) + ListCapacityBytes( m_sweepTree ) + ListCapacityBytes( m_jointPairs );

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

namespace SkullbonezCore::Physics
{
std::span<const std::pair<int, int>> PhysicsBroadphaseStage::RefreshCurrentContacts( const PhysicsBodyStore& bodies,
                                                                                     const ColliderStore& colliders,
                                                                                     std::span<const PointJointConstraint> joints,
                                                                                     BroadphaseBodyActivityView activity,
                                                                                     float contactEpsilon )
{
    const BroadphaseSweepContactEnvelope envelope( 0.0f, (std::max)( 0.0f, contactEpsilon ), contactEpsilon );
    const BroadphasePairFilter filter( bodies, colliders, activity, envelope );
    m_spatialGrid.GetFilteredCandidatePairs( m_candidatePairs, filter, false );
    m_candidatePairs.erase( std::remove_if( m_candidatePairs.begin(), m_candidatePairs.end(), FixedSolverCandidatePairPredicate { bodies.HotFields(), bodies.Count() } ), m_candidatePairs.end() );
    PrepareJointExclusions( bodies, joints );
    PruneJointPairs( m_candidatePairs );
    return m_candidatePairs;
}

} // namespace SkullbonezCore::Physics
