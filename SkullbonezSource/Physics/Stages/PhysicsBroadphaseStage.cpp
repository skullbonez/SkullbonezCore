/*
File: SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
Purpose:
  Implements deterministic broadphase candidate generation and retained output.

Summary:
  The stage rebuilds the spatial grid, preserves conservative fast-projectile
  augmentation, prunes fixed/joint/sleep-only pairs in their original order,
  and records the same bounded pipeline evidence as the certified facade code.

Glossary:
  Broadphase filter: Shape-aware cheap predicate applied while grid pairs form.
  Swept insertion: Grid coverage of a body's start-to-end fixed-step path.
  Sleep-pruned pair: Pair of dormant bodies with no awake energy to create work.

Invariants:
  - Float expressions and loop order are unchanged from the P0 implementation.
  - `remove_if` predicates preserve their original diagnostic side effects.
  - No hot-path vector operation may exceed construction-time capacity.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h
  - SkullbonezSource/Physics/SolverBroadphaseStage.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#include "PhysicsBroadphaseStage.h"

#include "../../Assets/AssetKeys.h"
#include "../../Core/Config.h"
#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../../Runtime/Scene/SceneCapacity.h"
#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"
#include "../SolverBroadphaseStage.h"

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
constexpr int PHYSICS_CANDIDATE_PAIR_RESERVE = SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS * 4;

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
void AppendCandidatePairIfMissing( std::vector<std::pair<int, int>>& candidatePairs,
                                   const Physics::BroadphaseCandidateFilterContext& filterContext,
                                   int a,
                                   int b )
{
    if ( a == b || a < 0 || b < 0 || a >= filterContext.modelCount || b >= filterContext.modelCount )
    {
        return;
    }

    if ( a > b )
    {
        std::swap( a, b );
    }

    if ( !Physics::BroadphaseCandidateCanTouch( &filterContext, a, b ) )
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

    candidatePairs.emplace_back( a, b );
}

bool IsFastSmallSweepBody( const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                           std::span<const Physics::ColliderRecord> colliderRecords,
                           int bodyIndex,
                           float dt )
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
                                      std::span<const Physics::ColliderRecord> colliderRecords,
                                      int movingIndex,
                                      int targetIndex,
                                      float dt,
                                      float contactEpsilon )
{
    const Vector3 relativeStart =
        SolverBodyPosition( hotFields, movingIndex ) - SolverBodyPosition( hotFields, targetIndex );
    const Vector3 relativeDisplacement =
        ( Physics::PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( movingIndex ) ) -
          Physics::PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( targetIndex ) ) ) *
        dt;
    const float relativeLengthSq = Vector::VectorMagSquared( relativeDisplacement );
    if ( relativeLengthSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    float t = -( relativeStart * relativeDisplacement ) / relativeLengthSq;
    t = (std::max)( 0.0f, (std::min)( 1.0f, t ) );
    const Vector3 closestRelative = relativeStart + relativeDisplacement * t;
    const float expandedRadius = SolverBodyRadius( colliderRecords, movingIndex ) +
                                 SolverBodyRadius( colliderRecords, targetIndex ) + contactEpsilon +
                                 PHYSICS_FAST_SWEEP_PAIR_SLOP;
    return Vector::VectorMagSquared( closestRelative ) <= expandedRadius * expandedRadius;
}

bool IsFixedSolverCandidatePair( const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                 int modelCount,
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
                                const std::vector<Physics::PointJointConstraint>& pointJointConstraints,
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
    const std::vector<Physics::PointJointConstraint>& pointJointConstraints;

    bool operator()( const std::pair<int, int>& pair ) const
    {
        return IsPointJointCandidatePair( bodyStore, pointJointConstraints, pair );
    }
};

bool IsSleepPrunedCandidatePair( std::span<const uint8_t> sleepState, const std::pair<int, int>& pair )
{
    const int a = pair.first;
    const int b = pair.second;
    return a >= 0 && b >= 0 && a < static_cast<int>( sleepState.size() ) && b < static_cast<int>( sleepState.size() ) &&
           sleepState[a] != 0 && sleepState[b] != 0;
}

void TryRecordSleepPrunedCandidatePair( std::vector<Physics::PhysicsPipelineRecord>& physicsPipelineTrace,
                                        const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                        const std::pair<int, int>& pair )
{
    if ( physicsPipelineTrace.size() >= MAX_PIPELINE_TRACE_RECORDS )
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
    physicsPipelineTrace.push_back( record );
}

// Invariant: trace emission remains part of the predicate contract and occurs
// only for a pair that remove_if erases.
struct SleepPrunedCandidatePairPredicate
{
    std::span<const uint8_t> sleepState;
    Physics::PhysicsBodyHotFieldsConstView hotFields;
    std::vector<Physics::PhysicsPipelineRecord>& physicsPipelineTrace;

    bool operator()( const std::pair<int, int>& pair ) const
    {
        const bool prune = IsSleepPrunedCandidatePair( sleepState, pair );
        if ( prune )
        {
            TryRecordSleepPrunedCandidatePair( physicsPipelineTrace, hotFields, pair );
        }
        return prune;
    }
};

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}
} // namespace

namespace SkullbonezCore
{
namespace Physics
{
PhysicsBroadphaseStage::PhysicsBroadphaseStage() : m_spatialGrid( DEFAULT_BROADPHASE_CELL )
{
    // Runtime allocation policy: both outputs are fully reserved before the
    // fixed-step pass and fail fatally rather than growing during gameplay.
    m_candidatePairs.reserve( PHYSICS_CANDIDATE_PAIR_RESERVE );
    m_collisionCellKeys.reserve( PHYSICS_CANDIDATE_PAIR_RESERVE );
}


void PhysicsBroadphaseStage::ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    const float configuredCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, config.broadphase.cellSize );
    m_spatialGrid.SetCellSize( configuredCell );
}


void PhysicsBroadphaseStage::Clear()
{
    m_candidatePairs.clear();
    m_collisionCellKeys.clear();
}


void PhysicsBroadphaseStage::ResetTransientAfterReplayRestore()
{
    // Invariant: replay restores collision-cell diagnostic keys from the
    // snapshot, while candidate pairs and grid buckets are rebuilt next tick.
    m_candidatePairs.clear();
    m_spatialGrid.Clear();
}


std::span<const std::pair<int, int>> PhysicsBroadphaseStage::Run( const PhysicsBroadphaseStageContext& context )
{
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    BroadphaseCandidateFilterContext broadphaseCandidateFilterContext{
        context.bodyRecords,
        context.hotFields,
        context.colliderRecords,
        context.modelCount,
        context.dt,
        context.contactSkin,
    };
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/GridBuild" );
        float largestBroadphaseRadius = 0.0f;
        for ( int i = 0; i < context.modelCount; ++i )
        {
            const float radius = SolverBodyRadius( context.colliderRecords, i );
            if ( std::isfinite( radius ) && radius > largestBroadphaseRadius )
            {
                largestBroadphaseRadius = radius;
            }
        }

        // Why: a fixed 24m cell made the 200-brick wall share huge buckets.
        // Deterministic scene inputs choose a cell no larger than the config cap.
        const float configuredCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, context.config.broadphase.cellSize );
        const float sceneCell =
            (std::max)( BROADPHASE_MIN_CELL_SIZE, ( largestBroadphaseRadius + context.contactSkin ) * 2.0f );
        m_spatialGrid.SetCellSize( (std::min)( configuredCell, sceneCell ) );
        m_spatialGrid.Clear();
        m_collisionCellKeys.clear();
        for ( int i = 0; i < context.modelCount; ++i )
        {
            const float radius = SolverBodyRadius( context.colliderRecords, i ) + context.contactSkin;
            const Vector3 displacement =
                PhysicsBodyLinearVelocity( context.hotFields, static_cast<size_t>( i ) ) * context.dt;
            const float displacementSq = Vector::VectorMagSquared( displacement );
            if ( !IsSolverBodyFixed( context.hotFields, i ) && displacementSq > radius * radius )
            {
                m_spatialGrid.InsertSwept( i, SolverBodyPosition( context.hotFields, i ), displacement, radius );
            }
            else
            {
                m_spatialGrid.Insert( i, SolverBodyPosition( context.hotFields, i ), radius );
            }
        }
        m_spatialGrid.GetCandidatePairs( m_candidatePairs, &broadphaseCandidateFilterContext );
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/FastSmallSweepAugment" );
        for ( int movingIndex = 0; movingIndex < context.modelCount; ++movingIndex )
        {
            if ( !IsFastSmallSweepBody( context.hotFields, context.colliderRecords, movingIndex, context.dt ) )
            {
                continue;
            }

            for ( int targetIndex = 0; targetIndex < context.modelCount; ++targetIndex )
            {
                if ( movingIndex == targetIndex )
                {
                    continue;
                }
                if ( SweptSegmentTouchesExpandedBody( context.hotFields,
                                                      context.colliderRecords,
                                                      movingIndex,
                                                      targetIndex,
                                                      context.dt,
                                                      context.config.bodySimulation.contactEpsilon ) )
                {
                    AppendCandidatePairIfMissing( m_candidatePairs,
                                                  broadphaseCandidateFilterContext,
                                                  movingIndex,
                                                  targetIndex );
                }
            }
        }
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneFixedPairs" );
        m_candidatePairs.erase(
            std::remove_if( m_candidatePairs.begin(),
                            m_candidatePairs.end(),
                            FixedSolverCandidatePairPredicate{ context.hotFields, context.modelCount } ),
            m_candidatePairs.end() );
    }

    if ( !context.pointJointConstraints.empty() )
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneJointPairs" );
        m_candidatePairs.erase(
            std::remove_if( m_candidatePairs.begin(),
                            m_candidatePairs.end(),
                            PointJointCandidatePairPredicate{ context.bodyStore, context.pointJointConstraints } ),
            m_candidatePairs.end() );
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/RecordCandidates" );
        for ( const auto& pair : m_candidatePairs )
        {
            if ( context.physicsPipelineTrace.size() >= MAX_PIPELINE_TRACE_RECORDS )
            {
                break;
            }

            if ( pair.first < 0 || pair.second < 0 || pair.first >= context.modelCount ||
                 pair.second >= context.modelCount )
            {
                continue;
            }

            PhysicsPipelineRecord record;
            record.stage = PhysicsPipelineStage::BroadphaseCandidate;
            record.bodyA = pair.first;
            record.bodyB = pair.second;
            record.point = ( PhysicsBodyPosition( context.hotFields, static_cast<size_t>( pair.first ) ) +
                             PhysicsBodyPosition( context.hotFields, static_cast<size_t>( pair.second ) ) ) *
                           0.5f;
            Vector3 delta = PhysicsBodyPosition( context.hotFields, static_cast<size_t>( pair.second ) ) -
                            PhysicsBodyPosition( context.hotFields, static_cast<size_t>( pair.first ) );
            float deltaMag = Vector::VectorMag( delta );
            record.normal = deltaMag > TOLERANCE ? delta / deltaMag : Vector3( 0.0f, 1.0f, 0.0f );
            record.scalarA = static_cast<float>( m_candidatePairs.size() );
            context.physicsPipelineTrace.push_back( record );
        }
    }
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneSleepPairs" );
        m_candidatePairs.erase( std::remove_if( m_candidatePairs.begin(),
                                                m_candidatePairs.end(),
                                                SleepPrunedCandidatePairPredicate{ context.sleepState,
                                                                                   context.hotFields,
                                                                                   context.physicsPipelineTrace } ),
                                m_candidatePairs.end() );
    }
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


const std::vector<int64_t>& PhysicsBroadphaseStage::GetCollisionCellKeys() const
{
    return m_collisionCellKeys;
}


const std::vector<int64_t>& PhysicsBroadphaseStage::CollisionCellKeysForReplay() const
{
    return m_collisionCellKeys;
}


std::vector<int64_t>& PhysicsBroadphaseStage::CollisionCellKeysForReplay()
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
    return VectorCapacityBytes( m_candidatePairs ) + VectorCapacityBytes( m_collisionCellKeys );
}


uint64_t PhysicsBroadphaseStage::CollectDebugAndBroadphaseMemoryBytes() const
{
    return static_cast<uint64_t>( sizeof( m_spatialGrid ) ) + CollectDynamicMemoryBytes();
}
} // namespace Physics
} // namespace SkullbonezCore
