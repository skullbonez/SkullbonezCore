/*
File: SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
Purpose:
  Implements deterministic force dispatch plus dark AVX2/FMA integration,
  universal-gravity, and mutual-gravity pair kernels.

Summary:
  Scenes through 512 bodies may build pair forces on bounded worker chunks,
  then always reduce those values in the original serial order. Larger scenes
  bypass pair storage and execute the original exact nested-loop accumulation.
  The same owner dispatches ordinary force application without retaining any
  store, sleep, or remaining-time reference. When explicitly enabled, it sends
  eight-row blocks to integration and gravity kernels, vector-builds pair-table
  rows for worlds through 512 bodies, and leaves store-owned force completion
  and the certified serial pair reduction in their original model order.

Glossary:
  Pair-build worker: Worker that computes disjoint pair slots without reducing.
  Reduction: Model-order addition/subtraction of retained pair forces.
  Receive predicate: Dynamic, positive-inverse-mass, awake body eligibility.
  Dark kernel: An AVX2/FMA path selected only by the default-OFF v3 execution
    toggle until the S7 cutover ceremony.

Invariants:
  - Toggle OFF retains the original float expressions, loop order, and
    byte-exact physics gate behavior.
  - The pair table never represents more than 512 bodies (130,816 rows).
  - SIMD pair construction writes independent rows; model-order reduction is
    still scalar, and the >512 fallback never enters the AVX2 kernel.
  - SIMD gravity runs before store completion so gravity is applied exactly
    once while drag, buoyancy, torque, and pending impulses retain one owner.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsForceStage.h
  - SkullbonezSource/Physics/Stages/Kernels/ForceKernel.h
  - SkullbonezSource/Physics/PhysicsWorldForces.h
  - SkullbonezTests/TestDeterminism.cpp
*/
#include "PhysicsForceStage.h"

#include "Kernels/IntegrationKernel.h"
#include "Kernels/ForceKernel.h"
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
constexpr uint32_t PHYSICS_INTEGRATE_WORKER_HASH = HashStr( "Frame/Physics/Integrate/WorkerBodies" );

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

bool IsSolverBodyFixed( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<size_t>( bodyIndex )] != 0u;
}

void ApplyForcesForSolverBody( Physics::PhysicsBodyStore& bodyStore,
                               const Physics::ColliderStore& colliderStore,
                               const Physics::PhysicsWorldForces& worldForces,
                               const Physics::PhysicsBodyHotFieldsView& hotFields,
                               std::span<const uint8_t> sleepState,
                               std::vector<float>& timeRemaining,
                               const Vector3* mutualGravityForces,
                               int bodyIndex,
                               float dt )
{
    // Invariant: this is the extracted body of the former applyForcesAt lambda.
    // Sleeping rows must keep their cached pose and consume no remaining time;
    // awake dynamic rows still receive the same force application call.
    if ( hotFields.fixed[static_cast<std::size_t>( bodyIndex )] != 0u )
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

void ApplyForcesSimdBlock( const Physics::ApplyForcesStageContext& context, int modelCount, int blockIndex )
{
    const int bodyBegin = blockIndex * Physics::Kernels::FORCE_LANE_COUNT;
    const int laneCount = (std::min)( Physics::Kernels::FORCE_LANE_COUNT, modelCount - bodyBegin );
    for ( int lane = 0; lane < laneCount; ++lane )
    {
        const int bodyIndex = bodyBegin + lane;
        if ( context.sleepState[static_cast<std::size_t>( bodyIndex )] != 0u )
        {
            context.timeRemaining[static_cast<std::size_t>( bodyIndex )] = 0.0f;
        }
    }

    const uint32_t completionMask = Physics::Kernels::ApplyGravityAvx2( context.hotFields,
                                                                        context.sleepState,
                                                                        bodyBegin,
                                                                        modelCount,
                                                                        context.worldForces.gravity,
                                                                        context.dt );
    for ( int lane = 0; lane < laneCount; ++lane )
    {
        if ( ( completionMask & ( 1u << lane ) ) == 0u )
        {
            continue;
        }
        const int bodyIndex = bodyBegin + lane;
        const Vector3* mutualGravityForce =
            context.mutualGravityForces ? &context.mutualGravityForces[bodyIndex] : nullptr;
        (void)context.bodyStore.CompleteForcesAfterSimdGravity( context.worldForces,
                                                                context.colliderStore,
                                                                bodyIndex,
                                                                context.dt,
                                                                mutualGravityForce );
    }
}

struct ApplyForcesSimdBlockContext
{
    const Physics::ApplyForcesStageContext& context;
    int modelCount = 0;

    void operator()( int blockIndex ) const
    {
        ApplyForcesSimdBlock( context, modelCount, blockIndex );
    }
};

void IntegrateRemainingSolverBody( Physics::PhysicsBodyStore& bodyStore,
                                   const Physics::ColliderStore& colliderStore,
                                   const Physics::PhysicsBodyHotFieldsView& hotFields,
                                   std::span<const uint8_t> sleepState,
                                   std::span<const float> timeRemaining,
                                   int bodyIndex )
{
    if ( hotFields.fixed[static_cast<size_t>( bodyIndex )] != 0u || sleepState[bodyIndex] )
    {
        return;
    }
    if ( timeRemaining[bodyIndex] > 0.0f )
    {
        (void)bodyStore.IntegrateBodyPose( colliderStore, bodyIndex, timeRemaining[bodyIndex] );
    }
}

// Invariant: the kernel owns position/velocity arithmetic only. The stage uses
// its returned mask to preserve the scalar owner and order for orientation,
// terrain clamping, and water-sample invalidation.
void IntegrateRemainingSimdBlock( const Physics::IntegrateRemainingStageContext& context,
                                  int modelCount,
                                  int blockIndex )
{
    const int bodyBegin = blockIndex * Physics::Kernels::INTEGRATION_LANE_COUNT;
    const uint32_t activeMask = Physics::Kernels::IntegratePositionAvx2( context.hotFields,
                                                                         context.sleepState,
                                                                         context.timeRemaining,
                                                                         bodyBegin,
                                                                         modelCount );
    for ( int lane = 0; lane < Physics::Kernels::INTEGRATION_LANE_COUNT; ++lane )
    {
        if ( ( activeMask & ( 1u << lane ) ) == 0u )
        {
            continue;
        }
        const int bodyIndex = bodyBegin + lane;
        (void)context.bodyStore.CompleteBodyPoseIntegration( context.colliderStore,
                                                             bodyIndex,
                                                             context.timeRemaining[bodyIndex] );
    }
}

struct IntegrateRemainingSimdBlockContext
{
    const Physics::IntegrateRemainingStageContext& context;
    int modelCount = 0;

    void operator()( int blockIndex ) const
    {
        IntegrateRemainingSimdBlock( context, modelCount, blockIndex );
    }
};
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
                              hotFields,
                              sleepState,
                              timeRemaining,
                              mutualGravityForces,
                              bodyIndex,
                              dt );
}

void IntegrateRemainingStageContext::operator()( int bodyIndex ) const
{
    IntegrateRemainingSolverBody( bodyStore, colliderStore, hotFields, sleepState, timeRemaining, bodyIndex );
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
                                                              const PhysicsBodyHotFieldsConstView& hotFields,
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
                const bool bodyBReceives = hotFields.fixed[bodyBIndex] == 0u &&
                                           hotFields.inverseMass[bodyBIndex] > 0.0f &&
                                           ( j >= static_cast<int>( sleepState.size() ) || sleepState[j] == 0 );
                if ( !bodyAReceives && !bodyBReceives )
                {
                    continue;
                }

                const Vector3 positionA( hotFields.positionX[bodyAIndex],
                                         hotFields.positionY[bodyAIndex],
                                         hotFields.positionZ[bodyAIndex] );
                const Vector3 positionB( hotFields.positionX[bodyBIndex],
                                         hotFields.positionY[bodyBIndex],
                                         hotFields.positionZ[bodyBIndex] );
                const Vector3 displacement = positionB - positionA;
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

            const std::size_t bodyAIndex = static_cast<std::size_t>( i );
            const bool bodyAReceives = hotFields.fixed[bodyAIndex] == 0u && hotFields.inverseMass[bodyAIndex] > 0.0f &&
                                       ( i >= static_cast<int>( sleepState.size() ) || sleepState[i] == 0 );
            const std::size_t rowOffset = MutualGravityRowOffset( i, modelCount );
            if ( execution.simdKernels )
            {
                for ( int bodyBBegin = i + 1; bodyBBegin < modelCount; bodyBBegin += Kernels::FORCE_LANE_COUNT )
                {
                    Kernels::BuildMutualGravityPairsAvx2(
                        bodyRecords,
                        hotFields,
                        sleepState,
                        i,
                        bodyBBegin,
                        modelCount,
                        softenedDistanceSq,
                        gravitationalConstant,
                        m_mutualGravityPairForces.data() + rowOffset + static_cast<std::size_t>( bodyBBegin - i - 1 ) );
                }
                continue;
            }
            for ( int j = i + 1; j < modelCount; ++j )
            {
                const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<std::size_t>( j )];
                if ( bodyB.mass <= TOLERANCE )
                {
                    continue;
                }

                const std::size_t bodyBIndex = static_cast<std::size_t>( j );
                const bool bodyBReceives = hotFields.fixed[bodyBIndex] == 0u &&
                                           hotFields.inverseMass[bodyBIndex] > 0.0f &&
                                           ( j >= static_cast<int>( sleepState.size() ) || sleepState[j] == 0 );
                if ( !bodyAReceives && !bodyBReceives )
                {
                    continue;
                }

                const Vector3 positionA( hotFields.positionX[bodyAIndex],
                                         hotFields.positionY[bodyAIndex],
                                         hotFields.positionZ[bodyAIndex] );
                const Vector3 positionB( hotFields.positionX[bodyBIndex],
                                         hotFields.positionY[bodyBIndex],
                                         hotFields.positionZ[bodyBIndex] );
                const Vector3 displacement = positionB - positionA;
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

void PhysicsForceStage::ApplyForces( const ApplyForcesStageContext& context,
                                     int modelCount,
                                     Threading::WorkerPool& workerPool,
                                     const Core::PhysicsExecutionConfig& execution ) const
{
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    if ( execution.simdKernels )
    {
        const int blockCount = ( modelCount + Kernels::FORCE_LANE_COUNT - 1 ) / Kernels::FORCE_LANE_COUNT;
        const ApplyForcesSimdBlockContext simdBlocks{ context, modelCount };
        PROFILE_BEGIN( "Frame/Physics/ApplyForces/SimdKernel" );
        if ( execution.parallel && execution.parallelApplyForces )
        {
            workerPool.ParallelForNoAlloc(
                0,
                blockCount,
                simdBlocks,
                ( PHYSICS_PARALLEL_MIN_BODIES + Kernels::FORCE_LANE_COUNT - 1 ) / Kernels::FORCE_LANE_COUNT,
                "Frame/Physics/ApplyForces/WorkerBodies",
                PHYSICS_APPLY_FORCES_WORKER_HASH );
        }
        else
        {
            for ( int blockIndex = 0; blockIndex < blockCount; ++blockIndex )
            {
                simdBlocks( blockIndex );
            }
        }
        PROFILE_END( "Frame/Physics/ApplyForces/SimdKernel" );
        PROFILE_END( "Frame/Physics/ApplyForces" );
        return;
    }

    PROFILE_BEGIN( "Frame/Physics/ApplyForces/ScalarBodyLoop" );
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
    PROFILE_END( "Frame/Physics/ApplyForces/ScalarBodyLoop" );
    PROFILE_END( "Frame/Physics/ApplyForces" );
}

void PhysicsForceStage::IntegrateRemaining( const IntegrateRemainingStageContext& context,
                                            int modelCount,
                                            Threading::WorkerPool& workerPool,
                                            const Core::PhysicsExecutionConfig& execution ) const
{
    if ( execution.simdKernels )
    {
        const int blockCount = ( modelCount + Kernels::INTEGRATION_LANE_COUNT - 1 ) / Kernels::INTEGRATION_LANE_COUNT;
        const IntegrateRemainingSimdBlockContext simdBlocks{ context, modelCount };
        PROFILE_BEGIN( "Frame/Physics/Integrate/SimdPilot" );
        if ( execution.parallel && execution.parallelIntegrate )
        {
            workerPool.ParallelForNoAlloc(
                0,
                blockCount,
                simdBlocks,
                ( PHYSICS_PARALLEL_MIN_BODIES + Kernels::INTEGRATION_LANE_COUNT - 1 ) / Kernels::INTEGRATION_LANE_COUNT,
                "Frame/Physics/Integrate/WorkerBodies",
                PHYSICS_INTEGRATE_WORKER_HASH );
        }
        else
        {
            for ( int blockIndex = 0; blockIndex < blockCount; ++blockIndex )
            {
                simdBlocks( blockIndex );
            }
        }
        PROFILE_END( "Frame/Physics/Integrate/SimdPilot" );
        return;
    }

    PROFILE_BEGIN( "Frame/Physics/Integrate/ScalarBodyLoop" );
    if ( execution.parallel && execution.parallelIntegrate )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       context,
                                       PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/Integrate/WorkerBodies",
                                       PHYSICS_INTEGRATE_WORKER_HASH );
    }
    else
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            context( x );
        }
    }
    PROFILE_END( "Frame/Physics/Integrate/ScalarBodyLoop" );
}

uint64_t PhysicsForceStage::CollectDynamicMemoryBytes() const
{
    return VectorCapacityBytes( m_mutualGravityForces ) + VectorCapacityBytes( m_mutualGravityPairForces );
}
} // namespace Physics
} // namespace SkullbonezCore
