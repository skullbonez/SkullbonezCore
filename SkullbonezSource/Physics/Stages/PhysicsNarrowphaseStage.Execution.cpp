/*
File: SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp
Purpose:
  Implements bounded narrowphase island construction and worker dispatch.

Summary:
  The narrowphase owner keeps pair math in its primary implementation unit and
  isolates deterministic island scheduling here. This is one cohesive owner;
  the file boundary only keeps each stage implementation reviewable.

Glossary:
  Pair island: Candidate pairs connected through shared body indices.
  Pair-order slot: Stable event index matching the broadphase candidate order.
  Bounded dispatch: Worker scheduling whose storage was reserved before play.

Invariants:
  - Union/find and final sort preserve ascending minimum candidate-pair order.
  - Worker islands never share a dynamic body.
  - Steady-play vectors cannot exceed their scene and candidate-pair reserves.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h
  - SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp
  - Agentic/Reports/2026-07-15/physicsworld-ownership-map.md
*/
#include "PhysicsNarrowphaseStage.h"

#include "../../Core/Config.h"
#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../../Core/WorkerPool.h"
#include "../../Runtime/Scene/SceneCapacity.h"
#include "../DisjointSet.h"

#include <algorithm>
#include <cassert>
#include <climits>

using namespace SkullbonezCore::Physics;

namespace
{
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MIN_PAIRS = 256;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MIN_ISLANDS = 16;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MAX_AVG_PAIRS_PER_ISLAND = 4;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MAX_PAIRS_PER_BODY = 2;
constexpr bool PHYSICS_NARROWPHASE_ISLAND_WORKER_ENABLED = true;
constexpr int PHYSICS_CANDIDATE_PAIR_RESERVE = SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS * 4;
constexpr uint32_t PHYSICS_NARROWPHASE_ISLAND_WORKER_HASH =
    HashStr( "Frame/Physics/Narrowphase/IslandWorkerDispatch/WorkerIslands" );

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}
} // namespace

void PhysicsNarrowphaseStage::ProcessObjectNarrowphaseIsland( const ObjectNarrowphasePairStageContext& context,
                                                              int islandIndex )
{
    const ObjectNarrowphaseIsland& island = m_objectNarrowphaseIslands[static_cast<size_t>( islandIndex )];
    const size_t pairEnd = island.firstPairOffset + island.pairCount;
    for ( size_t pairCursor = island.firstPairOffset; pairCursor < pairEnd; ++pairCursor )
    {
        const int pairIndex = m_objectNarrowphaseIslandPairIndices[pairCursor];
        ProcessObjectNarrowphasePair( context, pairIndex, m_objectNarrowphaseEvents[static_cast<size_t>( pairIndex )] );
    }
}

bool PhysicsNarrowphaseStage::ObjectNarrowphaseIslandPrecedesByMinPairIndex( const ObjectNarrowphaseIsland& a,
                                                                             const ObjectNarrowphaseIsland& b )
{
    return a.minPairIndex < b.minPairIndex;
}


void PhysicsNarrowphaseStage::BuildObjectNarrowphaseIslands( Core::Profiler* profiler,
                                                             std::span<const std::pair<int, int>> candidatePairs,
                                                             int candidatePairCount,
                                                             int modelCount )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Narrowphase/BuildIslands" );
    m_objectNarrowphaseParent.resize( static_cast<size_t>( modelCount ) );
    m_objectNarrowphaseRank.assign( static_cast<size_t>( modelCount ), 0 );
    for ( int i = 0; i < modelCount; ++i )
    {
        m_objectNarrowphaseParent[static_cast<size_t>( i )] = i;
    }

    DisjointSet objectNarrowphaseSets( m_objectNarrowphaseParent, m_objectNarrowphaseRank, modelCount );

    for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
    {
        const int x = candidatePairs[static_cast<size_t>( pairIndex )].first;
        const int y = candidatePairs[static_cast<size_t>( pairIndex )].second;
        if ( x < 0 || y < 0 || x >= modelCount || y >= modelCount )
        {
            continue;
        }
        objectNarrowphaseSets.Unite( x, y );
    }

    m_objectNarrowphaseIslands.clear();
    m_objectNarrowphaseIslandPairIndices.clear();
    m_objectNarrowphaseIslandWriteOffsets.clear();
    m_objectNarrowphaseRootToIsland.assign( static_cast<size_t>( modelCount ), -1 );
    for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
    {
        const int x = candidatePairs[static_cast<size_t>( pairIndex )].first;
        const int y = candidatePairs[static_cast<size_t>( pairIndex )].second;
        if ( x < 0 || y < 0 || x >= modelCount || y >= modelCount )
        {
            continue;
        }

        const int root = objectNarrowphaseSets.Find( x );
        int islandIndex = m_objectNarrowphaseRootToIsland[static_cast<size_t>( root )];
        if ( islandIndex < 0 )
        {
            islandIndex = static_cast<int>( m_objectNarrowphaseIslands.size() );
            m_objectNarrowphaseRootToIsland[static_cast<size_t>( root )] = islandIndex;
            if ( m_objectNarrowphaseIslands.size() >= m_objectNarrowphaseIslands.capacity() )
            {
                assert( false && "Physics object narrowphase island capacity exceeded" );
                // Invariant: object narrowphase island storage is bounded by the
                // precomputed pair/model limits for this frame. Overflow would
                // reorder or drop pair work.
                SB_FATAL( "Physics/PhysicsWorld", "Physics object narrowphase island capacity exceeded" );
            }
            m_objectNarrowphaseIslands.push_back( ObjectNarrowphaseIsland() );
            m_objectNarrowphaseIslands.back().minPairIndex = INT_MAX;
        }

        ObjectNarrowphaseIsland& island = m_objectNarrowphaseIslands[static_cast<size_t>( islandIndex )];
        island.minPairIndex = (std::min)( island.minPairIndex, pairIndex );
        ++island.pairCount;
    }
    if ( m_objectNarrowphaseIslandWriteOffsets.capacity() < m_objectNarrowphaseIslands.size() )
    {
        assert( false && "Physics object narrowphase island write-offset capacity exceeded" );
        // Invariant: write offsets are one row per island. A short reserve would
        // make worker writes overlap or depend on allocation order.
        SB_FATAL( "Physics/PhysicsWorld", "Physics object narrowphase island write-offset capacity exceeded" );
    }
    m_objectNarrowphaseIslandWriteOffsets.assign( m_objectNarrowphaseIslands.size(), 0 );
    size_t pairOffset = 0;
    for ( size_t islandIndex = 0; islandIndex < m_objectNarrowphaseIslands.size(); ++islandIndex )
    {
        ObjectNarrowphaseIsland& island = m_objectNarrowphaseIslands[islandIndex];
        island.firstPairOffset = pairOffset;
        m_objectNarrowphaseIslandWriteOffsets[islandIndex] = pairOffset;
        pairOffset += island.pairCount;
    }
    if ( pairOffset > m_objectNarrowphaseIslandPairIndices.capacity() )
    {
        assert( false && "Physics object narrowphase island pair capacity exceeded" );
        // Invariant: pair-index staging owns the exact compacted pair set for the
        // worker pass. Overflow would drop pairs from narrowphase.
        SB_FATAL( "Physics/PhysicsWorld", "Physics object narrowphase island pair capacity exceeded" );
    }
    m_objectNarrowphaseIslandPairIndices.resize( pairOffset, 0 );
    for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
    {
        const int x = candidatePairs[static_cast<size_t>( pairIndex )].first;
        const int y = candidatePairs[static_cast<size_t>( pairIndex )].second;
        if ( x < 0 || y < 0 || x >= modelCount || y >= modelCount )
        {
            continue;
        }

        const int root = objectNarrowphaseSets.Find( x );
        const int islandIndex = m_objectNarrowphaseRootToIsland[static_cast<size_t>( root )];
        if ( islandIndex < 0 )
        {
            continue;
        }
        size_t& writeOffset = m_objectNarrowphaseIslandWriteOffsets[static_cast<size_t>( islandIndex )];
        m_objectNarrowphaseIslandPairIndices[writeOffset++] = pairIndex;
    }
    std::sort( m_objectNarrowphaseIslands.begin(),
               m_objectNarrowphaseIslands.end(),
               ObjectNarrowphaseIslandPrecedesByMinPairIndex );
}

PhysicsNarrowphaseStage::PhysicsNarrowphaseStage()
{
    m_objectNarrowphaseEvents.reserve( PHYSICS_CANDIDATE_PAIR_RESERVE );
    m_objectNarrowphaseIslands.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_objectNarrowphaseIslandPairIndices.reserve( PHYSICS_CANDIDATE_PAIR_RESERVE );
    m_objectNarrowphaseIslandWriteOffsets.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_objectNarrowphaseParent.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_objectNarrowphaseRank.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_objectNarrowphaseRootToIsland.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
}

void PhysicsNarrowphaseStage::Clear()
{
    m_objectNarrowphaseEvents.clear();
    m_objectNarrowphaseIslands.clear();
    m_objectNarrowphaseIslandPairIndices.clear();
    m_objectNarrowphaseIslandWriteOffsets.clear();
    m_objectNarrowphaseParent.clear();
    m_objectNarrowphaseRank.clear();
    m_objectNarrowphaseRootToIsland.clear();
}

void PhysicsNarrowphaseStage::ObjectNarrowphaseIslandStage::operator()( int islandIndex ) const
{
    stage.ProcessObjectNarrowphaseIsland( pairContext, islandIndex );
}

bool PhysicsNarrowphaseStage::TryRunParallel( const ObjectNarrowphasePairStageContext& context,
                                              int candidatePairCount,
                                              int modelCount,
                                              const Core::PhysicsExecutionConfig& execution,
                                              Threading::WorkerPool& workerPool )
{
    m_objectNarrowphaseIslands.clear();
    m_objectNarrowphaseIslandPairIndices.clear();
    m_objectNarrowphaseIslandWriteOffsets.clear();

    const bool mayBenefitFromIslandDispatch =
        PHYSICS_NARROWPHASE_ISLAND_WORKER_ENABLED && execution.parallel && execution.parallelNarrowphase &&
        candidatePairCount >= PHYSICS_NARROWPHASE_PARALLEL_MIN_PAIRS &&
        candidatePairCount <= modelCount * PHYSICS_NARROWPHASE_PARALLEL_MAX_PAIRS_PER_BODY &&
        workerPool.GetThreadCount() > 0;
    if ( !mayBenefitFromIslandDispatch )
    {
        return false;
    }

    BuildObjectNarrowphaseIslands( context.profiler, context.candidatePairs, candidatePairCount, modelCount );

    const int islandCount = static_cast<int>( m_objectNarrowphaseIslands.size() );
    const bool hasSpreadOutNarrowphaseIslands =
        islandCount > 0 && candidatePairCount <= islandCount * PHYSICS_NARROWPHASE_PARALLEL_MAX_AVG_PAIRS_PER_ISLAND;
    if ( islandCount < PHYSICS_NARROWPHASE_PARALLEL_MIN_ISLANDS || !hasSpreadOutNarrowphaseIslands )
    {
        return false;
    }

    m_objectNarrowphaseEvents.assign( context.candidatePairs.size(), ObjectNarrowphaseEvent() );
    ObjectNarrowphaseIslandStage islandStage{ *this, context };
    {
        PROFILE_SCOPED( context.profiler, "Frame/Physics/Narrowphase/IslandWorkerDispatch" );
        workerPool.ParallelForNoAlloc( 0,
                                       islandCount,
                                       islandStage,
                                       PHYSICS_NARROWPHASE_PARALLEL_MIN_ISLANDS,
                                       "Frame/Physics/Narrowphase/IslandWorkerDispatch/WorkerIslands",
                                       PHYSICS_NARROWPHASE_ISLAND_WORKER_HASH );
    }
    return true;
}

std::span<const ObjectNarrowphaseEvent> PhysicsNarrowphaseStage::GetEvents() const
{
    return m_objectNarrowphaseEvents;
}

uint64_t PhysicsNarrowphaseStage::CollectDynamicMemoryBytes() const
{
    uint64_t bytes = 0;
    bytes += VectorCapacityBytes( m_objectNarrowphaseEvents );
    bytes += VectorCapacityBytes( m_objectNarrowphaseIslands );
    bytes += VectorCapacityBytes( m_objectNarrowphaseIslandPairIndices );
    bytes += VectorCapacityBytes( m_objectNarrowphaseIslandWriteOffsets );
    bytes += VectorCapacityBytes( m_objectNarrowphaseParent );
    bytes += VectorCapacityBytes( m_objectNarrowphaseRank );
    bytes += VectorCapacityBytes( m_objectNarrowphaseRootToIsland );
    return bytes;
}
