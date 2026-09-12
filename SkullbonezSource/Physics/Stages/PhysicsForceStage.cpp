// Owns exact mutual-gravity pair construction, canonical reduction, and
// per-body force/integration dispatch. Large parallel fields reuse bounded
// pair batches; workers never accumulate directly into body force arrays.

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
constexpr int MUTUAL_GRAVITY_MAX_CHUNKS = ( MUTUAL_GRAVITY_MAX_BODIES + MUTUAL_GRAVITY_ROWS_PER_CHUNK - 1 ) / MUTUAL_GRAVITY_ROWS_PER_CHUNK;
constexpr int MUTUAL_GRAVITY_PARALLEL_MIN_BODIES = 32;
constexpr uint16_t MUTUAL_GRAVITY_RECEIVER_BIT = 0x8000u;
constexpr uint16_t MUTUAL_GRAVITY_BODY_INDEX_MASK = 0x7fffu;
static_assert( Physics::PHYSICS_MAX_BODY_ROWS <= static_cast<std::size_t>( MUTUAL_GRAVITY_BODY_INDEX_MASK ) + 1u );
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

constexpr uint16_t PackMutualGravityBody( std::size_t bodyIndex, bool receivesForce )
{
    return static_cast<uint16_t>( bodyIndex ) | static_cast<uint16_t>( receivesForce ? MUTUAL_GRAVITY_RECEIVER_BIT : 0u );
}

constexpr std::size_t MutualGravityBodyIndex( uint16_t packedBody )
{
    return static_cast<std::size_t>( packedBody & MUTUAL_GRAVITY_BODY_INDEX_MASK );
}

constexpr bool MutualGravityBodyReceivesForce( uint16_t packedBody )
{
    return ( packedBody & MUTUAL_GRAVITY_RECEIVER_BIT ) != 0u;
}

template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}

bool IsSolverBodyFixed( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<size_t>( bodyIndex )] != 0u;
}

void ApplyForcesForSolverBody( Physics::PhysicsBodyStore& bodyStore,
                               const Physics::ColliderStore& colliderStore,
                               const Physics::PhysicsTerrainView& terrain,
                               const Physics::PhysicsWorldForces& worldForces,
                               std::span<const Physics::BuoyancyBodyFacts> buoyancyFacts,
                               const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                               std::span<const uint8_t> sleepState,
                               std::span<float> timeRemaining,
                               const Vector3* mutualGravityForces,
                               int bodyIndex,
                               float dt )
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
    (void)bodyStore.ApplyForces( worldForces, colliderStore, terrain, buoyancyFacts[static_cast<std::size_t>( bodyIndex )], bodyIndex, dt, mutualGravityForce );
}

void IntegrateRemainingSolverBody( Physics::PhysicsBodyStore& bodyStore,
                                   SkullbonezCore::Core::Profiler* profiler,
                                   const Physics::ColliderStore& colliderStore,
                                   const Physics::PhysicsTerrainView& terrain,
                                   std::span<Physics::BuoyancyBodyFacts> buoyancyFacts,
                                   const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                   std::span<const uint8_t> sleepState,
                                   std::span<const float> timeRemaining,
                                   int bodyIndex )
{
    if ( IsSolverBodyFixed( hotFields, bodyIndex ) || sleepState[bodyIndex] )
    {
        return;
    }

    if ( timeRemaining[bodyIndex] > 0.0f )
    {
        (void)bodyStore.IntegrateBodyPose( profiler, colliderStore, terrain, buoyancyFacts[static_cast<std::size_t>( bodyIndex )], bodyIndex, timeRemaining[bodyIndex] );
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
    m_workStats = {};
}

void PhysicsForceStage::ReserveBodyScratchCapacity( std::size_t capacity )
{
    m_mutualGravityForces.Reserve( capacity );
    m_mutualGravityPairForces.Reserve( PhysicsMutualGravityPairCapacity( capacity ) );
}

std::size_t PhysicsForceStage::BuildMutualGravityPairChunk( std::span<const PhysicsBodyRecord> bodyRecords,
                                                            const PhysicsBodyHotFieldsConstView& hotFields,
                                                            std::span<const uint8_t> sleepState,
                                                            std::span<MutualGravityPairForce> output,
                                                            float softenedDistanceSq,
                                                            float gravitationalConstant,
                                                            int modelCount,
                                                            int rowBegin,
                                                            int rowEnd )
{
    std::size_t outputCount = 0u;
    for ( int i = rowBegin; i < rowEnd; ++i )
    {
        const PhysicsBodyRecord& bodyA = bodyRecords[static_cast<std::size_t>( i )];

        if ( bodyA.mass <= TOLERANCE )
        {
            continue;
        }

        const std::size_t bodyAIndex = static_cast<std::size_t>( i );
        const bool bodyAReceives = hotFields.fixed[bodyAIndex] == 0u && hotFields.inverseMass[bodyAIndex] > 0.0f && ( i >= static_cast<int>( sleepState.size() ) || sleepState[i] == 0 );

        for ( int j = i + 1; j < modelCount; ++j )
        {
            const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<std::size_t>( j )];

            if ( bodyB.mass <= TOLERANCE )
            {
                continue;
            }

            const std::size_t bodyBIndex = static_cast<std::size_t>( j );
            const bool bodyBReceives = hotFields.fixed[bodyBIndex] == 0u && hotFields.inverseMass[bodyBIndex] > 0.0f && ( j >= static_cast<int>( sleepState.size() ) || sleepState[j] == 0 );

            if ( !bodyAReceives && !bodyBReceives )
            {
                continue;
            }

            const Vector3 positionA( hotFields.positionX[bodyAIndex], hotFields.positionY[bodyAIndex], hotFields.positionZ[bodyAIndex] );
            const Vector3 positionB( hotFields.positionX[bodyBIndex], hotFields.positionY[bodyBIndex], hotFields.positionZ[bodyBIndex] );
            const Vector3 displacement = positionB - positionA;
            const float distanceSq = Vector::VectorMagSquared( displacement ) + softenedDistanceSq;
            const float invDistance = 1.0f / sqrtf( distanceSq );
            const float invDistanceCubed = invDistance * invDistance * invDistance;
            MutualGravityPairForce& pair = output[outputCount];
            pair.force = displacement * ( gravitationalConstant * bodyA.mass * bodyB.mass * invDistanceCubed );

            // Invariant: the supported body ceiling is below 32,768, so
            // the high bit can carry the immutable receiver decision while
            // the low bits retain the exact model index.
            pair.bodyAAndReceiver = PackMutualGravityBody( bodyAIndex, bodyAReceives );
            pair.bodyBAndReceiver = PackMutualGravityBody( bodyBIndex, bodyBReceives );
            ++outputCount;
        }
    }

    return outputCount;
}

const Vector3* PhysicsForceStage::PrepareMutualGravityForces( Core::Profiler* profiler,
                                                              std::span<const PhysicsBodyRecord> bodyRecords,
                                                              const PhysicsBodyHotFieldsConstView& hotFields,
                                                              std::span<const uint8_t> sleepState,
                                                              int modelCount,
                                                              const PhysicsWorldForces& worldForces,
                                                              const PhysicsExecutionSettings& execution,
                                                              Threading::WorkerPool& workerPool )
{
    m_workStats = {};
    m_workStats.scratchBytes = CollectDynamicMemoryBytes();
    const MutualGravitySettings& settings = worldForces.mutualGravity;

    if ( !settings.enabled || settings.gravitationalConstant <= 0.0f || modelCount <= 0 )
    {
        m_mutualGravityForces.clear();
        return nullptr;
    }

    const std::size_t requiredBodyCapacity = static_cast<std::size_t>( modelCount );

    if ( m_mutualGravityForces.capacity() < requiredBodyCapacity )
    {
        SB_FATAL( "Physics/MutualGravity",
                  "Mutual gravity body scratch capacity exhausted: owner=Physics/MutualGravity " "phase=steady_gameplay body_capacity=%zu required_bodies=%zu.",
                  m_mutualGravityForces.capacity(),
                  requiredBodyCapacity );
    }

    m_mutualGravityForces.assign( requiredBodyCapacity, ZERO_VECTOR );
    const float softeningLength = (std::max)( settings.softeningLength, TOLERANCE );
    const float softenedDistanceSq = softeningLength * softeningLength;
    const float gravitationalConstant = settings.gravitationalConstant;

    const bool runParallel = execution.parallel && execution.parallelMutualGravity && modelCount >= MUTUAL_GRAVITY_PARALLEL_MIN_BODIES && workerPool.GetThreadCount() > 0;

    if ( modelCount > MUTUAL_GRAVITY_MAX_BODIES && !runParallel )
    {
        // Without worker dispatch, retain the measured direct serial path.
        // Its arithmetic and receiver decisions remain the reference order.
        for ( int i = 0; i < modelCount; ++i )
        {
            const PhysicsBodyRecord& bodyA = bodyRecords[static_cast<std::size_t>( i )];

            if ( bodyA.mass <= TOLERANCE )
            {
                continue;
            }

            const std::size_t bodyAIndex = static_cast<std::size_t>( i );
            const bool bodyAReceives = hotFields.fixed[bodyAIndex] == 0u && hotFields.inverseMass[bodyAIndex] > 0.0f && ( i >= static_cast<int>( sleepState.size() ) || sleepState[i] == 0 );

            for ( int j = i + 1; j < modelCount; ++j )
            {
                const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<std::size_t>( j )];

                if ( bodyB.mass <= TOLERANCE )
                {
                    continue;
                }

                const std::size_t bodyBIndex = static_cast<std::size_t>( j );
                const bool bodyBReceives = hotFields.fixed[bodyBIndex] == 0u && hotFields.inverseMass[bodyBIndex] > 0.0f && ( j >= static_cast<int>( sleepState.size() ) || sleepState[j] == 0 );

                if ( !bodyAReceives && !bodyBReceives )
                {
                    continue;
                }

                const Vector3 positionA( hotFields.positionX[bodyAIndex], hotFields.positionY[bodyAIndex], hotFields.positionZ[bodyAIndex] );
                const Vector3 positionB( hotFields.positionX[bodyBIndex], hotFields.positionY[bodyBIndex], hotFields.positionZ[bodyBIndex] );
                const Vector3 displacement = positionB - positionA;
                const float distanceSq = Vector::VectorMagSquared( displacement ) + softenedDistanceSq;
                const float invDistance = 1.0f / sqrtf( distanceSq );
                const float invDistanceCubed = invDistance * invDistance * invDistance;
                const Vector3 force = displacement * ( gravitationalConstant * bodyA.mass * bodyB.mass * invDistanceCubed );

                ++m_workStats.pairContributions;

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

    const std::size_t requiredPairCapacity = (std::min)( MutualGravityPairCount( requiredBodyCapacity ), PHYSICS_MAX_MUTUAL_GRAVITY_PAIRS );

    if ( m_mutualGravityPairForces.capacity() < requiredPairCapacity )
    {
        SB_FATAL( "Physics/MutualGravity",
                  "Mutual gravity pair scratch capacity exhausted: owner=Physics/MutualGravity " "phase=steady_gameplay pair_capacity=%zu required_pairs=%zu max_parallel_bodies=%d " "pair_high_water=%zu.",
                  m_mutualGravityPairForces.capacity(),
                  requiredPairCapacity,
                  MUTUAL_GRAVITY_MAX_BODIES,
                  m_mutualGravityPairHighWater );
    }

    m_mutualGravityPairHighWater = (std::max)( m_mutualGravityPairHighWater, requiredPairCapacity );

    // Why: active workers overwrite only their compact contribution prefixes.
    // Retaining the live scratch extent avoids clearing every triangular slot
    // before a sparse build; stale suffixes are never read.
    m_mutualGravityPairForces.ExtendDefaultTo( requiredPairCapacity );

    // A batch spans whole canonical rows, has worker-count-independent bounds,
    // and fits the existing 130,816-record (about 2 MiB) pair reservation.
    // Each eight-row chunk fits even at the supported 8,192-body ceiling.
    static_assert( PHYSICS_MAX_BODY_ROWS * MUTUAL_GRAVITY_ROWS_PER_CHUNK <= PHYSICS_MAX_MUTUAL_GRAVITY_PAIRS );
    int batchRowBegin = 0;
    while ( batchRowBegin < modelCount )
    {
        const std::size_t batchPairBegin = MutualGravityRowOffset( batchRowBegin, modelCount );
        Threading::WorkerChunkRange chunks[MUTUAL_GRAVITY_MAX_CHUNKS] = {};
        std::size_t chunkPairCounts[MUTUAL_GRAVITY_MAX_CHUNKS] = {};
        int chunkCount = 0;
        int rowBegin = batchRowBegin;
        for ( ; rowBegin < modelCount && chunkCount < MUTUAL_GRAVITY_MAX_CHUNKS; rowBegin += MUTUAL_GRAVITY_ROWS_PER_CHUNK )
        {
            const int rowEnd = (std::min)( modelCount, rowBegin + MUTUAL_GRAVITY_ROWS_PER_CHUNK );
            if ( MutualGravityRowOffset( rowEnd, modelCount ) - batchPairBegin > requiredPairCapacity )
            {
                break;
            }
            chunks[chunkCount] = { chunkCount, rowBegin, rowEnd };
            ++chunkCount;
        }
        if ( chunkCount == 0 )
        {
            SB_FATAL( "Physics/MutualGravity", "A complete gravity row chunk exceeds the reserved pair batch: bodies=%d rows=%d capacity=%zu.", modelCount, batchRowBegin, requiredPairCapacity );
        }
        // Invariant: row boundaries are a pure function of modelCount and the
        // compile-time row size. Worker count changes scheduling only; every pair
        // writes one unique flat slot and cannot race with another chunk.
        const auto buildPairForces = [&]( int chunkIndex, int rowBegin, int rowEnd )
        {
            PROFILE_WORKER_SCOPED( profiler, "Frame/Physics/MutualGravity/PairBuildWorker" );

            const std::size_t outputBegin = MutualGravityRowOffset( rowBegin, modelCount ) - batchPairBegin;
            const std::size_t outputCapacity = MutualGravityRowOffset( rowEnd, modelCount ) - MutualGravityRowOffset( rowBegin, modelCount );
            chunkPairCounts[chunkIndex] = BuildMutualGravityPairChunk( bodyRecords,
                                                                       hotFields,
                                                                       sleepState,
                                                                       std::span<MutualGravityPairForce>( m_mutualGravityPairForces.data() + outputBegin, outputCapacity ),
                                                                       softenedDistanceSq,
                                                                       gravitationalConstant,
                                                                       modelCount,
                                                                       rowBegin,
                                                                       rowEnd );
        };


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

        PROFILE_BEGIN( "Frame/Physics/MutualGravity/Reduce" );

        std::size_t builtPairCount = 0u;

        // Invariant: chunks and their written prefixes follow ascending (i,j)
        // order. Reducing each prefix directly preserves every body's addition
        // sequence without copying contributions or reading unwritten suffixes.
        for ( int chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            const std::size_t sourceBegin = MutualGravityRowOffset( chunks[chunkIndex].begin, modelCount ) - batchPairBegin;
            const std::size_t sourceCount = chunkPairCounts[chunkIndex];

            for ( std::size_t sourceOffset = 0u; sourceOffset < sourceCount; ++sourceOffset )
            {
                const MutualGravityPairForce& pair = m_mutualGravityPairForces[sourceBegin + sourceOffset];

                if ( MutualGravityBodyReceivesForce( pair.bodyAAndReceiver ) )
                {
                    m_mutualGravityForces[MutualGravityBodyIndex( pair.bodyAAndReceiver )] += pair.force;
                }

                if ( MutualGravityBodyReceivesForce( pair.bodyBAndReceiver ) )
                {
                    m_mutualGravityForces[MutualGravityBodyIndex( pair.bodyBAndReceiver )] -= pair.force;
                }
            }
            builtPairCount += sourceCount;
        }

        PROFILE_END( "Frame/Physics/MutualGravity/Reduce" );
        m_workStats.pairContributions += builtPairCount;
        ++m_workStats.pairBatches;
        batchRowBegin = rowBegin;
    }

    return m_mutualGravityForces.data();
}

void PhysicsForceStage::ApplyForces( PhysicsBodyStore& bodyStore,
                                     const ColliderStore& colliderStore,
                                     PhysicsTerrainView terrain,
                                     const PhysicsWorldForces& worldForces,
                                     std::span<const BuoyancyBodyFacts> buoyancyFacts,
                                     std::span<const uint8_t> sleepState,
                                     std::span<float> timeRemaining,
                                     float dt,
                                     std::span<const int> awakeBodyIndices,
                                     Threading::WorkerPool& workerPool,
                                     const PhysicsExecutionSettings& execution ) const
{
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    // Prepare owns this scratch, including clearing its live extent when
    // gravity is disabled. Apply consumes that result without a round-trip
    // borrowed pointer through PhysicsWorld.
    const Vector3* mutualGravityForces = m_mutualGravityForces.empty() ? nullptr : m_mutualGravityForces.data();
    const auto applyAwakeBody = [&]( int awakeSlot )
    {
        ApplyForcesForSolverBody( bodyStore,
                                  colliderStore,
                                  terrain,
                                  worldForces,
                                  buoyancyFacts,
                                  hotFields,
                                  sleepState,
                                  timeRemaining,
                                  mutualGravityForces,
                                  awakeBodyIndices[static_cast<std::size_t>( awakeSlot )],
                                  dt );
    };

    const int awakeBodyCount = static_cast<int>( awakeBodyIndices.size() );

    if ( execution.parallel && execution.parallelApplyForces )
    {
        workerPool.ParallelForNoAlloc( 0, awakeBodyCount, applyAwakeBody, PHYSICS_PARALLEL_MIN_BODIES, "Frame/Physics/ApplyForces/WorkerBodies", PHYSICS_APPLY_FORCES_WORKER_HASH );
    }
    else
    {
        for ( int awakeSlot = 0; awakeSlot < awakeBodyCount; ++awakeSlot )
        {
            applyAwakeBody( awakeSlot );
        }
    }
}

void PhysicsForceStage::IntegrateRemaining( PhysicsBodyStore& bodyStore,
                                            Core::Profiler* profiler,
                                            const ColliderStore& colliderStore,
                                            PhysicsTerrainView terrain,
                                            std::span<BuoyancyBodyFacts> buoyancyFacts,
                                            std::span<const uint8_t> sleepState,
                                            std::span<const float> timeRemaining,
                                            std::span<const int> awakeBodyIndices,
                                            Threading::WorkerPool& workerPool,
                                            const PhysicsExecutionSettings& execution ) const
{
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const auto integrateAwakeBody = [&]( int awakeSlot )
    { IntegrateRemainingSolverBody( bodyStore, profiler, colliderStore, terrain, buoyancyFacts, hotFields, sleepState, timeRemaining, awakeBodyIndices[static_cast<std::size_t>( awakeSlot )] ); };

    const int awakeBodyCount = static_cast<int>( awakeBodyIndices.size() );

    if ( execution.parallel && execution.parallelIntegrate )
    {
        workerPool.ParallelForNoAlloc( 0, awakeBodyCount, integrateAwakeBody, PHYSICS_PARALLEL_MIN_BODIES, "Frame/Physics/Integrate/WorkerBodies", PHYSICS_INTEGRATE_WORKER_HASH );
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
