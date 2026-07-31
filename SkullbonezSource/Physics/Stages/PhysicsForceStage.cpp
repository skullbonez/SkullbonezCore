/*
File: SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
Purpose:
  Implements exact mutual gravity and deterministic per-body force dispatch.

Summary:
  Scenes through 512 bodies may build pair forces on bounded worker chunks,
  then always reduce those values in the original serial order. Larger scenes
  bypass pair storage and execute the original exact nested-loop accumulation.
  The same owner dispatches ordinary force application and integration over the
  sleep owner's ascending awake list without retaining any borrowed reference.

Glossary:
  Pair-build worker: Worker that computes disjoint pair slots without reducing.
  Reduction: Model-order addition/subtraction of retained pair forces.
  Receive predicate: Dynamic, positive-inverse-mass, awake body eligibility.

Invariants:
  - Float expressions and loop order are unchanged from the P2 implementation.
  - The pair table never represents more than 512 bodies (130,816 rows).
  - Apply-forces worker dispatch uses the same threshold, label, and hash.
  - Worker slots map deterministically to ascending awake body indices; dormant
    bodies do not enter ordinary force or integration dispatch.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsForceStage.h
  - SkullbonezSource/Physics/PhysicsWorldForces.h
  - SkullbonezTests/TestDeterminism.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "PhysicsForceStage.h"

#include "../../Core/Common.h"
#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../../Core/WorkerPool.h"
#include "../../Core/SceneCapacity.h"
#include "../ColliderStore.h"
#include "../BuoyancySystem.h"
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
constexpr int MUTUAL_GRAVITY_MAX_BODIES = static_cast<int>( Physics::PHYSICS_MUTUAL_GRAVITY_MAX_BODIES );
constexpr int MUTUAL_GRAVITY_ROWS_PER_CHUNK = 8;
constexpr int MUTUAL_GRAVITY_MAX_CHUNKS = ( MUTUAL_GRAVITY_MAX_BODIES + MUTUAL_GRAVITY_ROWS_PER_CHUNK - 1 ) /
                                          MUTUAL_GRAVITY_ROWS_PER_CHUNK;
constexpr int MUTUAL_GRAVITY_PARALLEL_MIN_BODIES = 32;
constexpr uint32_t PHYSICS_APPLY_FORCES_WORKER_HASH = HashStr( "Frame/Physics/ApplyForces/WorkerBodies" );
constexpr uint32_t PHYSICS_INTEGRATE_WORKER_HASH = HashStr( "Frame/Physics/Integrate/WorkerBodies" );

constexpr std::size_t MutualGravityPairCount( std::size_t bodyCount )
{
    return bodyCount > 1 ? bodyCount * ( bodyCount - 1 ) / 2 : 0;
}

constexpr std::size_t MutualGravityRowOffset( int row, int bodyCount )
{
    return static_cast<std::size_t>( row ) * static_cast<std::size_t>( 2 * bodyCount - row - 1 ) / 2;
}

template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}

bool IsSolverBodyFixed( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<size_t>( bodyIndex )] != 0u;
}

void ApplyForcesForSolverBody( Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                               const Physics::PhysicsTerrainView& terrain, const Physics::PhysicsWorldForces& worldForces,
                               std::span<const Physics::BuoyancyBodyFacts> buoyancyFacts,
                               const Physics::PhysicsBodyHotFieldsConstView& hotFields, std::span<const uint8_t> sleepState,
                               std::span<float> timeRemaining, const Vector3* mutualGravityForces, int bodyIndex, float dt )
{

    // Invariant: this is the extracted body of the former applyForcesAt lambda.
    // Sleeping rows must keep their cached pose and consume no remaining time;
    // awake dynamic rows still receive the same force application call.

    if ( IsSolverBodyFixed( hotFields, bodyIndex ) )
    {
        return;
    }

    if ( sleepState[bodyIndex] )
    {
        timeRemaining[bodyIndex] = 0.0f;
        return;
    }

    const Vector3* mutualGravityForce = mutualGravityForces ? &mutualGravityForces[bodyIndex] : nullptr;
    (void)bodyStore.ApplyForces( worldForces, colliderStore, terrain, buoyancyFacts[static_cast<std::size_t>( bodyIndex )],
                                 bodyIndex, dt, mutualGravityForce );
}

void IntegrateRemainingSolverBody( Physics::PhysicsBodyStore& bodyStore, SkullbonezCore::Core::Profiler* profiler,
                                   const Physics::ColliderStore& colliderStore, const Physics::PhysicsTerrainView& terrain,
                                   std::span<Physics::BuoyancyBodyFacts> buoyancyFacts,
                                   const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                   std::span<const uint8_t> sleepState, std::span<const float> timeRemaining, int bodyIndex )
{

    if ( IsSolverBodyFixed( hotFields, bodyIndex ) || sleepState[bodyIndex] )
    {
        return;
    }

    if ( timeRemaining[bodyIndex] > 0.0f )
    {
        (void)bodyStore.IntegrateBodyPose( profiler, colliderStore, terrain,
                                           buoyancyFacts[static_cast<std::size_t>( bodyIndex )], bodyIndex,
                                           timeRemaining[bodyIndex] );
    }
}
} // namespace

namespace SkullbonezCore
{
namespace Physics
{

PhysicsForceStage::PhysicsForceStage() = default;

void PhysicsForceStage::Clear()
{
    m_mutualGravityForces.clear();
    m_mutualGravityPairForces.clear();
}

void PhysicsForceStage::ReserveBodyScratchCapacity( std::size_t capacity )
{
    m_mutualGravityForces.Reserve( capacity );
    m_mutualGravityPairForces.Reserve( PhysicsMutualGravityPairCapacity( capacity ) );
}

const Vector3* PhysicsForceStage::PrepareMutualGravityForces( Core::Profiler* profiler, std::span<const PhysicsBodyRecord> bodyRecords, const PhysicsBodyHotFieldsConstView& hotFields,
                                                              std::span<const uint8_t> sleepState, int modelCount, const PhysicsWorldForces& worldForces,
                                                              const PhysicsExecutionSettings& execution, Threading::WorkerPool& workerPool )
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
                  m_mutualGravityForces.capacity(), requiredBodyCapacity );
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

            const std::size_t bodyAIndex = static_cast<std::size_t>( i );
            const bool bodyAReceives = hotFields.fixed[bodyAIndex] == 0u && hotFields.inverseMass[bodyAIndex] > 0.0f &&
                                       ( i >= static_cast<int>( sleepState.size() ) || sleepState[i] == 0 );

            for ( int j = i + 1; j < modelCount; ++j )
            {
                const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<std::size_t>( j )];

                if ( bodyB.mass <= TOLERANCE )
                {
                    continue;
                }

                const std::size_t bodyBIndex = static_cast<std::size_t>( j );
                const bool bodyBReceives = hotFields.fixed[bodyBIndex] == 0u && hotFields.inverseMass[bodyBIndex] > 0.0f &&
                                           ( j >= static_cast<int>( sleepState.size() ) || sleepState[j] == 0 );

                if ( !bodyAReceives && !bodyBReceives )
                {
                    continue;
                }

                const Vector3 positionA( hotFields.positionX[bodyAIndex], hotFields.positionY[bodyAIndex],
                                         hotFields.positionZ[bodyAIndex] );
                const Vector3 positionB( hotFields.positionX[bodyBIndex], hotFields.positionY[bodyBIndex],
                                         hotFields.positionZ[bodyBIndex] );
                const Vector3 displacement = positionB - positionA;
                const float distanceSq = Vector::VectorMagSquared( displacement ) + softenedDistanceSq;
                const float invDistance = 1.0f / sqrtf( distanceSq );
                const float invDistanceCubed = invDistance * invDistance * invDistance;
                const Vector3 force = displacement * ( gravitationalConstant * bodyA.mass * bodyB.mass * invDistanceCubed );

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
                  m_mutualGravityPairForces.capacity(), requiredPairCapacity, MUTUAL_GRAVITY_MAX_BODIES,
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
        PROFILE_WORKER_SCOPED( profiler, "Frame/Physics/MutualGravity/PairBuildWorker" );

        for ( int i = rowBegin; i < rowEnd; ++i )
        {
            const PhysicsBodyRecord& bodyA = bodyRecords[static_cast<std::size_t>( i )];

            if ( bodyA.mass <= TOLERANCE )
            {
                continue;
            }

            const std::size_t bodyAIndex = static_cast<std::size_t>( i );
            const bool bodyAReceives = hotFields.fixed[bodyAIndex] == 0u && hotFields.inverseMass[bodyAIndex] > 0.0f &&
                                       ( i >= static_cast<int>( sleepState.size() ) || sleepState[i] == 0 );

            const std::size_t rowOffset = MutualGravityRowOffset( i, modelCount );

            for ( int j = i + 1; j < modelCount; ++j )
            {
                const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<std::size_t>( j )];

                if ( bodyB.mass <= TOLERANCE )
                {
                    continue;
                }

                const std::size_t bodyBIndex = static_cast<std::size_t>( j );
                const bool bodyBReceives = hotFields.fixed[bodyBIndex] == 0u && hotFields.inverseMass[bodyBIndex] > 0.0f &&
                                           ( j >= static_cast<int>( sleepState.size() ) || sleepState[j] == 0 );

                if ( !bodyAReceives && !bodyBReceives )
                {
                    continue;
                }

                const Vector3 positionA( hotFields.positionX[bodyAIndex], hotFields.positionY[bodyAIndex],
                                         hotFields.positionZ[bodyAIndex] );
                const Vector3 positionB( hotFields.positionX[bodyBIndex], hotFields.positionY[bodyBIndex],
                                         hotFields.positionZ[bodyBIndex] );
                const Vector3 displacement = positionB - positionA;
                const float distanceSq = Vector::VectorMagSquared( displacement ) + softenedDistanceSq;
                const float invDistance = 1.0f / sqrtf( distanceSq );
                const float invDistanceCubed = invDistance * invDistance * invDistance;
                m_mutualGravityPairForces[rowOffset + static_cast<std::size_t>( j - i - 1 )] = displacement *
                                                                                               ( gravitationalConstant *
                                                                                                 bodyA.mass * bodyB.mass *
                                                                                                 invDistanceCubed );
            }
        }
    };

    const bool runParallel = execution.parallel && execution.parallelMutualGravity &&
                             modelCount >= MUTUAL_GRAVITY_PARALLEL_MIN_BODIES && workerPool.GetThreadCount() > 0;

    PROFILE_BEGIN( profiler, "Frame/Physics/MutualGravity/PairBuild" );

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

    PROFILE_END( profiler, "Frame/Physics/MutualGravity/PairBuild" );

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

        const std::size_t bodyAIndex = static_cast<std::size_t>( i );
        const bool bodyAReceives = hotFields.fixed[bodyAIndex] == 0u && hotFields.inverseMass[bodyAIndex] > 0.0f &&
                                   ( i >= static_cast<int>( sleepState.size() ) || sleepState[i] == 0 );

        const std::size_t rowOffset = MutualGravityRowOffset( i, modelCount );

        for ( int j = i + 1; j < modelCount; ++j )
        {
            const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<std::size_t>( j )];

            if ( bodyB.mass <= TOLERANCE )
            {
                continue;
            }

            const std::size_t bodyBIndex = static_cast<std::size_t>( j );
            const bool bodyBReceives = hotFields.fixed[bodyBIndex] == 0u && hotFields.inverseMass[bodyBIndex] > 0.0f &&
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

void PhysicsForceStage::ApplyForces( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                     PhysicsTerrainView terrain, const PhysicsWorldForces& worldForces,
                                     std::span<const BuoyancyBodyFacts> buoyancyFacts, std::span<const uint8_t> sleepState,
                                     std::span<float> timeRemaining, const Vector3* mutualGravityForces, float dt,
                                     std::span<const int> awakeBodyIndices, Threading::WorkerPool& workerPool,
                                     const PhysicsExecutionSettings& execution ) const
{
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const auto applyAwakeBody = [&]( int awakeSlot )
    {
        ApplyForcesForSolverBody( bodyStore, colliderStore, terrain, worldForces, buoyancyFacts, hotFields, sleepState,
                                  timeRemaining, mutualGravityForces,
                                  awakeBodyIndices[static_cast<std::size_t>( awakeSlot )], dt );
    };

    const int awakeBodyCount = static_cast<int>( awakeBodyIndices.size() );

    if ( execution.parallel && execution.parallelApplyForces )
    {
        workerPool.ParallelForNoAlloc( 0, awakeBodyCount, applyAwakeBody, PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/ApplyForces/WorkerBodies", PHYSICS_APPLY_FORCES_WORKER_HASH );
    }
    else
    {

        for ( int awakeSlot = 0; awakeSlot < awakeBodyCount; ++awakeSlot )
        {
            applyAwakeBody( awakeSlot );
        }
    }
}

void PhysicsForceStage::IntegrateRemaining( PhysicsBodyStore& bodyStore, Core::Profiler* profiler,
                                            const ColliderStore& colliderStore, PhysicsTerrainView terrain,
                                            std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<const uint8_t> sleepState,
                                            std::span<const float> timeRemaining, std::span<const int> awakeBodyIndices,
                                            Threading::WorkerPool& workerPool,
                                            const PhysicsExecutionSettings& execution ) const
{
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const auto integrateAwakeBody = [&]( int awakeSlot )
    {
        IntegrateRemainingSolverBody( bodyStore, profiler, colliderStore, terrain, buoyancyFacts, hotFields, sleepState,
                                      timeRemaining, awakeBodyIndices[static_cast<std::size_t>( awakeSlot )] );
    };

    const int awakeBodyCount = static_cast<int>( awakeBodyIndices.size() );

    if ( execution.parallel && execution.parallelIntegrate )
    {
        workerPool.ParallelForNoAlloc( 0, awakeBodyCount, integrateAwakeBody, PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/Integrate/WorkerBodies", PHYSICS_INTEGRATE_WORKER_HASH );
    }
    else
    {

        for ( int awakeSlot = 0; awakeSlot < awakeBodyCount; ++awakeSlot )
        {
            integrateAwakeBody( awakeSlot );
        }
    }
}

uint64_t PhysicsForceStage::CollectDynamicMemoryBytes() const
{
    return ListCapacityBytes( m_mutualGravityForces ) + ListCapacityBytes( m_mutualGravityPairForces );
}
} // namespace Physics
} // namespace SkullbonezCore
