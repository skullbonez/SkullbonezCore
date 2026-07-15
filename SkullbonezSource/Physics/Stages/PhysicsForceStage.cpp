/*
File: SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
Purpose:
  Implements exact mutual gravity and deterministic per-body force dispatch.

Summary:
  Scenes through 512 bodies may build pair forces on bounded worker chunks,
  then always reduce those values in the original serial order. Larger scenes
  bypass pair storage and execute the original exact nested-loop accumulation.
  The same owner dispatches ordinary force application without retaining any
  store, sleep, or remaining-time reference.

Glossary:
  Pair-build worker: Worker that computes disjoint pair slots without reducing.
  Reduction: Model-order addition/subtraction of retained pair forces.
  Receive predicate: Dynamic, positive-inverse-mass, awake body eligibility.

Invariants:
  - Float expressions and loop order are unchanged from the P2 implementation.
  - The pair table never represents more than 512 bodies (130,816 rows).
  - Apply-forces worker dispatch uses the same threshold, label, and hash.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsForceStage.h
  - SkullbonezSource/Physics/PhysicsWorldForces.h
  - SkullbonezTests/TestDeterminism.cpp
*/
#include "PhysicsForceStage.h"

#include "PhysicsStageContexts.h"
#include "../../Assets/AssetKeys.h"
#include "../../Core/Config.h"
#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../../Core/WorkerPool.h"
#include "../../Runtime/Scene/SceneCapacity.h"
#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsWorldForces.h"

#include <algorithm>
#include <cmath>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
namespace Physics = SkullbonezCore::Physics;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
constexpr int PHYSICS_PARALLEL_MIN_BODIES = 512;
constexpr int MUTUAL_GRAVITY_MAX_BODIES = 512;
constexpr int MUTUAL_GRAVITY_ROWS_PER_CHUNK = 8;
constexpr int MUTUAL_GRAVITY_MAX_CHUNKS =
    ( MUTUAL_GRAVITY_MAX_BODIES + MUTUAL_GRAVITY_ROWS_PER_CHUNK - 1 ) / MUTUAL_GRAVITY_ROWS_PER_CHUNK;
constexpr int MUTUAL_GRAVITY_PARALLEL_MIN_BODIES = 32;
constexpr uint32_t PHYSICS_APPLY_FORCES_WORKER_HASH = HashStr( "Frame/Physics/ApplyForces/WorkerBodies" );

constexpr std::size_t MutualGravityPairCount( std::size_t bodyCount )
{
    return bodyCount > 1 ? bodyCount * ( bodyCount - 1 ) / 2 : 0;
}

constexpr std::size_t MutualGravityRowOffset( int row, int bodyCount )
{
    return static_cast<std::size_t>( row ) * static_cast<std::size_t>( 2 * bodyCount - row - 1 ) / 2;
}

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

bool IsSolverBodyFixed( std::span<const Physics::PhysicsBodyRecord> bodyRecords, int bodyIndex )
{
    return bodyRecords[static_cast<size_t>( bodyIndex )].isFixed;
}

void ApplyForcesForSolverBody( Physics::PhysicsBodyStore& bodyStore,
                               const Physics::ColliderStore& colliderStore,
                               const Physics::PhysicsWorldForces& worldForces,
                               std::span<const Physics::PhysicsBodyRecord> bodyRecords,
                               std::span<const uint8_t> sleepState,
                               std::vector<float>& timeRemaining,
                               const Vector3* mutualGravityForces,
                               int bodyIndex,
                               float dt )
{
    // Invariant: this is the extracted body of the former applyForcesAt lambda.
    // Sleeping rows must keep their cached pose and consume no remaining time;
    // awake dynamic rows still receive the same force application call.
    if ( IsSolverBodyFixed( bodyRecords, bodyIndex ) )
    {
        return;
    }
    if ( sleepState[bodyIndex] )
    {
        timeRemaining[bodyIndex] = 0.0f;
        return;
    }
    const Vector3* mutualGravityForce = mutualGravityForces ? &mutualGravityForces[bodyIndex] : nullptr;
    (void)bodyStore.ApplyForces( worldForces, colliderStore, bodyIndex, dt, mutualGravityForce );
}
} // namespace

namespace SkullbonezCore
{
namespace Physics
{
void ApplyForcesStageContext::operator()( int bodyIndex ) const
{
    ApplyForcesForSolverBody( bodyStore,
                              colliderStore,
                              worldForces,
                              bodyRecords,
                              sleepState,
                              timeRemaining,
                              mutualGravityForces,
                              bodyIndex,
                              dt );
}

PhysicsForceStage::PhysicsForceStage()
{
    m_mutualGravityForces.reserve( Scene::Capacity::MAX_GAME_MODELS );
}

void PhysicsForceStage::Clear()
{
    m_mutualGravityForces.clear();
    m_mutualGravityPairForces.clear();
}

void PhysicsForceStage::ReserveBodyScratchCapacity( std::size_t capacity )
{
    m_mutualGravityForces.reserve( capacity );
    const std::size_t pairBodyCapacity = (std::min)( capacity, static_cast<std::size_t>( MUTUAL_GRAVITY_MAX_BODIES ) );
    m_mutualGravityPairForces.reserve( MutualGravityPairCount( pairBodyCapacity ) );
}

const Vector3* PhysicsForceStage::PrepareMutualGravityForces( std::span<const PhysicsBodyRecord> bodyRecords,
                                                              std::span<const uint8_t> sleepState,
                                                              int modelCount,
                                                              const PhysicsWorldForces& worldForces,
                                                              const Core::PhysicsExecutionConfig& execution,
                                                              Threading::WorkerPool& workerPool )
{
    const MutualGravitySettings& settings = worldForces.mutualGravity;
    if ( !settings.enabled || settings.gravitationalConstant <= 0.0f || modelCount <= 0 )
    {
        return nullptr;
    }

    const std::size_t requiredBodyCapacity = static_cast<std::size_t>( modelCount );
    if ( m_mutualGravityForces.capacity() < requiredBodyCapacity )
    {
        SB_FATAL( "Physics/MutualGravity",
                  "Mutual gravity body scratch capacity exhausted: owner=Physics/MutualGravity "
                  "phase=steady_gameplay body_capacity=%zu required_bodies=%zu.",
                  m_mutualGravityForces.capacity(),
                  requiredBodyCapacity );
    }

    m_mutualGravityForces.assign( requiredBodyCapacity, ZERO_VECTOR );
    const float softeningLength = (std::max)( settings.softeningLength, TOLERANCE );
    const float softenedDistanceSq = softeningLength * softeningLength;
    const float gravitationalConstant = settings.gravitationalConstant;

    if ( modelCount > MUTUAL_GRAVITY_MAX_BODIES )
    {
        // Why: mutual-gravity-large-scene-fallback keeps the triangular pair
        // table capped at 512 bodies (about 1.5 MiB) without shrinking the
        // engine's 8,192-body capability. Larger fields use the original exact
        // serial order and only the body-count scratch reserved at scene load;
        // no approximation or baseline change is permitted.
        for ( int i = 0; i < modelCount; ++i )
        {
            const PhysicsBodyRecord& bodyA = bodyRecords[static_cast<std::size_t>( i )];
            if ( bodyA.mass <= TOLERANCE )
            {
                continue;
            }

            const bool bodyAReceives = !bodyA.isFixed && bodyA.invMass > 0.0f &&
                                       ( i >= static_cast<int>( sleepState.size() ) || sleepState[i] == 0 );
            for ( int j = i + 1; j < modelCount; ++j )
            {
                const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<std::size_t>( j )];
                if ( bodyB.mass <= TOLERANCE )
                {
                    continue;
                }

                const bool bodyBReceives = !bodyB.isFixed && bodyB.invMass > 0.0f &&
                                           ( j >= static_cast<int>( sleepState.size() ) || sleepState[j] == 0 );
                if ( !bodyAReceives && !bodyBReceives )
                {
                    continue;
                }

                const Vector3 displacement = bodyB.position - bodyA.position;
                const float distanceSq = Vector::VectorMagSquared( displacement ) + softenedDistanceSq;
                const float invDistance = 1.0f / sqrtf( distanceSq );
                const float invDistanceCubed = invDistance * invDistance * invDistance;
                const Vector3 force =
                    displacement * ( gravitationalConstant * bodyA.mass * bodyB.mass * invDistanceCubed );

                if ( bodyAReceives )
                {
                    m_mutualGravityForces[static_cast<std::size_t>( i )] += force;
                }
                if ( bodyBReceives )
                {
                    m_mutualGravityForces[static_cast<std::size_t>( j )] -= force;
                }
            }
        }

        return m_mutualGravityForces.data();
    }

    const std::size_t requiredPairCapacity = MutualGravityPairCount( requiredBodyCapacity );
    if ( m_mutualGravityPairForces.capacity() < requiredPairCapacity )
    {
        SB_FATAL( "Physics/MutualGravity",
                  "Mutual gravity pair scratch capacity exhausted: owner=Physics/MutualGravity "
                  "phase=steady_gameplay pair_capacity=%zu required_pairs=%zu max_parallel_bodies=%d "
                  "pair_high_water=%zu.",
                  m_mutualGravityPairForces.capacity(),
                  requiredPairCapacity,
                  MUTUAL_GRAVITY_MAX_BODIES,
                  m_mutualGravityPairHighWater );
    }

    m_mutualGravityPairHighWater = (std::max)( m_mutualGravityPairHighWater, requiredPairCapacity );
    m_mutualGravityPairForces.assign( requiredPairCapacity, ZERO_VECTOR );

    Threading::WorkerChunkRange chunks[MUTUAL_GRAVITY_MAX_CHUNKS] = {};
    int chunkCount = 0;
    for ( int rowBegin = 0; rowBegin < modelCount; rowBegin += MUTUAL_GRAVITY_ROWS_PER_CHUNK )
    {
        const int rowEnd = (std::min)( modelCount, rowBegin + MUTUAL_GRAVITY_ROWS_PER_CHUNK );
        chunks[chunkCount] = { chunkCount, rowBegin, rowEnd };
        ++chunkCount;
    }

    // Invariant: row boundaries are a pure function of modelCount and the
    // compile-time row size. Worker count changes scheduling only; every pair
    // writes one unique flat slot and cannot race with another chunk.
    const auto buildPairForces = [&]( int, int rowBegin, int rowEnd )
    {
        PROFILE_WORKER_SCOPED( "Frame/Physics/MutualGravity/PairBuildWorker" );
        for ( int i = rowBegin; i < rowEnd; ++i )
        {
            const PhysicsBodyRecord& bodyA = bodyRecords[static_cast<std::size_t>( i )];
            if ( bodyA.mass <= TOLERANCE )
            {
                continue;
            }

            const bool bodyAReceives = !bodyA.isFixed && bodyA.invMass > 0.0f &&
                                       ( i >= static_cast<int>( sleepState.size() ) || sleepState[i] == 0 );
            const std::size_t rowOffset = MutualGravityRowOffset( i, modelCount );
            for ( int j = i + 1; j < modelCount; ++j )
            {
                const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<std::size_t>( j )];
                if ( bodyB.mass <= TOLERANCE )
                {
                    continue;
                }

                const bool bodyBReceives = !bodyB.isFixed && bodyB.invMass > 0.0f &&
                                           ( j >= static_cast<int>( sleepState.size() ) || sleepState[j] == 0 );
                if ( !bodyAReceives && !bodyBReceives )
                {
                    continue;
                }

                const Vector3 displacement = bodyB.position - bodyA.position;
                const float distanceSq = Vector::VectorMagSquared( displacement ) + softenedDistanceSq;
                const float invDistance = 1.0f / sqrtf( distanceSq );
                const float invDistanceCubed = invDistance * invDistance * invDistance;
                m_mutualGravityPairForces[rowOffset + static_cast<std::size_t>( j - i - 1 )] =
                    displacement * ( gravitationalConstant * bodyA.mass * bodyB.mass * invDistanceCubed );
            }
        }
    };

    const bool runParallel = execution.parallel && execution.parallelMutualGravity &&
                             modelCount >= MUTUAL_GRAVITY_PARALLEL_MIN_BODIES && workerPool.GetThreadCount() > 0;
    PROFILE_BEGIN( "Frame/Physics/MutualGravity/PairBuild" );
    if ( runParallel )
    {
        workerPool.ParallelForChunksNoAlloc( chunks, chunkCount, buildPairForces );
    }
    else
    {
        for ( int chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            const Threading::WorkerChunkRange& chunk = chunks[chunkIndex];
            buildPairForces( chunk.chunkIndex, chunk.begin, chunk.end );
        }
    }
    PROFILE_END( "Frame/Physics/MutualGravity/PairBuild" );

    // Invariant: replay the original triangular pair order exactly. Chunk
    // partial-body reduction would regroup additions and change float bits;
    // storing pair forces makes scheduling invisible to accumulation order.
    for ( int i = 0; i < modelCount; ++i )
    {
        const PhysicsBodyRecord& bodyA = bodyRecords[static_cast<std::size_t>( i )];
        if ( bodyA.mass <= TOLERANCE )
        {
            continue;
        }

        const bool bodyAReceives = !bodyA.isFixed && bodyA.invMass > 0.0f &&
                                   ( i >= static_cast<int>( sleepState.size() ) || sleepState[i] == 0 );
        const std::size_t rowOffset = MutualGravityRowOffset( i, modelCount );
        for ( int j = i + 1; j < modelCount; ++j )
        {
            const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<std::size_t>( j )];
            if ( bodyB.mass <= TOLERANCE )
            {
                continue;
            }
            const bool bodyBReceives = !bodyB.isFixed && bodyB.invMass > 0.0f &&
                                       ( j >= static_cast<int>( sleepState.size() ) || sleepState[j] == 0 );
            if ( !bodyAReceives && !bodyBReceives )
            {
                continue;
            }
            const Vector3& force = m_mutualGravityPairForces[rowOffset + static_cast<std::size_t>( j - i - 1 )];

            if ( bodyAReceives )
            {
                m_mutualGravityForces[static_cast<std::size_t>( i )] += force;
            }
            if ( bodyBReceives )
            {
                m_mutualGravityForces[static_cast<std::size_t>( j )] -= force;
            }
        }
    }

    return m_mutualGravityForces.data();
}

void PhysicsForceStage::ApplyForces( const ApplyForcesStageContext& context,
                                     int modelCount,
                                     Threading::WorkerPool& workerPool,
                                     const Core::PhysicsExecutionConfig& execution ) const
{
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    if ( execution.parallel && execution.parallelApplyForces )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       context,
                                       PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/ApplyForces/WorkerBodies",
                                       PHYSICS_APPLY_FORCES_WORKER_HASH );
    }
    else
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            context( x );
        }
    }
    PROFILE_END( "Frame/Physics/ApplyForces" );
}

uint64_t PhysicsForceStage::CollectDynamicMemoryBytes() const
{
    return VectorCapacityBytes( m_mutualGravityForces ) + VectorCapacityBytes( m_mutualGravityPairForces );
}
} // namespace Physics
} // namespace SkullbonezCore
