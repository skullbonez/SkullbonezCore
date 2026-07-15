/*
File: SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp
Purpose:
  Implements deterministic swept-terrain detection and manifold commit.

Summary:
  Detection preserves the original per-body worker dispatch and thresholds.
  Prepared commits preserve the original float expressions and model order,
  while a narrow sequencer gap retains diagnostics and visual side effects at
  their certified positions.

Glossary:
  Terrain sweep: Continuous collision query over one body's remaining substep.
  Manifold commit: Append of solver-ready terrain contact points and sleep policy.
  Sequencer gap: Typed boundary where cross-domain diagnostics are emitted.

Invariants:
  - Worker scheduling never changes candidate slot identity.
  - Body integration and manifold construction occur before diagnostics.
  - Manifold/sleep writes occur after diagnostics and before clock completion.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Physics/TerrainContactManifold.cpp
*/
#include "PhysicsTerrainStage.h"

#include "../../Assets/AssetKeys.h"
#include "../../Core/Config.h"
#include "../../Core/Profiler.h"
#include "../../Core/WorkerPool.h"
#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"

#include <algorithm>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
namespace Physics = SkullbonezCore::Physics;

namespace
{
constexpr int TERRAIN_BODY_INDEX = -1;
constexpr int PHYSICS_PARALLEL_MIN_BODIES = 512;
constexpr uint32_t PHYSICS_TERRAIN_DETECT_WORKER_HASH =
    HashStr( "Frame/Physics/Terrain/Detect/WorkerBodies" );

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

bool IsSolverBodyFixed( std::span<const PhysicsBodyRecord> bodyRecords, int bodyIndex )
{
    return bodyRecords[static_cast<size_t>( bodyIndex )].isFixed;
}

TerrainContactBodyView TerrainContactBodyViewForIndex( std::span<const PhysicsBodyRecord> bodyRecords,
                                                       const SkullbonezCore::Core::EngineConfig& config,
                                                       int index )
{
    const PhysicsBodyRecord& record = bodyRecords[static_cast<size_t>( index )];
    TerrainContactBodyView body;
    body.position = record.position;
    body.orientation = record.orientation;
    body.linearVelocity = record.linearVelocity;
    body.terrain = record.terrain;
    body.boundingRadius = record.boundingRadius;
    body.contactEpsilon = record.contactEpsilon;
    body.terrainContactThreshold = config.terrainContact.threshold;
    body.restitutionThreshold = config.bodySimulation.contactRestitutionThreshold;
    body.isFixed = record.isFixed;
    return body;
}
} // namespace

PhysicsTerrainStage::PhysicsTerrainStage()
{
    m_detectionCandidates.reserve( Scene::Capacity::MAX_GAME_MODELS );
    m_contactManifolds.reserve( Scene::Capacity::MAX_GAME_MODELS );
}

void PhysicsTerrainStage::Clear()
{
    m_detectionCandidates.clear();
    m_contactManifolds.clear();
}

void PhysicsTerrainStage::BeginFrame()
{
    m_contactManifolds.clear();
}

void PhysicsTerrainStage::DetectTerrainAt( const TerrainDetectionStageContext& context, int bodyIndex )
{
    TerrainDetectionCandidate& candidate = m_detectionCandidates[static_cast<size_t>( bodyIndex )];
    if ( IsSolverBodyFixed( context.bodyRecords, bodyIndex ) )
    {
        return;
    }
    if ( context.sleepState[bodyIndex] || context.timeRemaining[bodyIndex] <= 0.0f )
    {
        return;
    }
    if ( bodyIndex >= static_cast<int>( context.bodyRecords.size() ) ||
         bodyIndex >= static_cast<int>( context.colliderRecords.size() ) )
    {
        return;
    }

    candidate.availableTime = context.timeRemaining[bodyIndex];
    candidate.sweep =
        SweepTerrainContact( TerrainContactBodyViewForIndex( context.bodyRecords, context.config, bodyIndex ),
                             context.colliderRecords[static_cast<size_t>( bodyIndex )].shape,
                             candidate.availableTime );
    candidate.tested = 1;
}

void PhysicsTerrainStage::TerrainDetectionStage::operator()( int bodyIndex ) const
{
    stage.DetectTerrainAt( context, bodyIndex );
}

void PhysicsTerrainStage::Detect( const TerrainDetectionStageContext& context,
                                  int modelCount,
                                  const Core::PhysicsExecutionConfig& execution,
                                  Threading::WorkerPool& workerPool )
{
    m_detectionCandidates.assign( static_cast<size_t>( modelCount ), TerrainDetectionCandidate() );
    TerrainDetectionStage detectionStage{ *this, context };
    if ( execution.parallel && execution.parallelTerrainDetect )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       detectionStage,
                                       PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/Terrain/Detect/WorkerBodies",
                                       PHYSICS_TERRAIN_DETECT_WORKER_HASH );
    }
    else
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            detectionStage( x );
        }
    }
}

PreparedTerrainCandidateCommit
PhysicsTerrainStage::PrepareCandidateCommit( const TerrainCandidateCommitContext& context,
                                             int bodyIndex,
                                             float availableTime,
                                             const TerrainContactSweepResult& sweep )
{
    PreparedTerrainCandidateCommit commit;
    if ( sweep.hit )
    {
        const float colTime = sweep.collisionTime;
        (void)context.bodyStore.IntegrateBodyPose( context.colliderStore, bodyIndex, colTime );
        const float remainingTime = (std::max)( 0.0f, availableTime - colTime );
        const bool hasManifold = Physics::BuildTerrainContactManifold(
            TerrainContactBodyViewForIndex( context.bodyRecords, context.config, bodyIndex ),
            context.colliderRecords[static_cast<size_t>( bodyIndex )].shape,
            bodyIndex,
            sweep,
            availableTime,
            commit.manifold );

        Physics::PhysicsPipelineRecord record;
        record.stage = Physics::PhysicsPipelineStage::TerrainHit;
        record.bodyA = bodyIndex;
        record.bodyB = TERRAIN_BODY_INDEX;
        record.point = hasManifold ? commit.manifold.points[0].point
                                   : context.bodyRecords[static_cast<size_t>( bodyIndex )].position;
        record.normal = hasManifold ? commit.manifold.normal : ZERO_VECTOR;
        record.scalarA = colTime;
        record.scalarB = hasManifold && commit.manifold.supportsRestingPolicy ? 1.0f : 0.0f;
        record.scalarC = hasManifold ? static_cast<float>( commit.manifold.pointCount ) : 0.0f;

        commit.pipelineRecord = record;
        commit.collisionTime = colTime;
        commit.availableTime = availableTime;
        commit.remainingTime = remainingTime;
        commit.bodyIndex = bodyIndex;
        commit.hit = 1;
        commit.hasManifold = hasManifold ? 1 : 0;
    }
    return commit;
}

void PhysicsTerrainStage::CommitCandidate( const TerrainCandidateCommitContext& context,
                                           const PreparedTerrainCandidateCommit& commit )
{
    if ( !commit.hit )
    {
        return;
    }

    if ( commit.hasManifold )
    {
        m_contactManifolds.push_back( commit.manifold );
        if ( commit.manifold.supportsRestingPolicy )
        {
            context.sleepSupportedThisFrame[commit.bodyIndex] = 1;
        }
        else
        {
            context.sleepInhibitedThisFrame[commit.bodyIndex] = 1;
        }
    }
    else
    {
        context.sleepInhibitedThisFrame[commit.bodyIndex] = 1;
    }
}

std::span<const TerrainDetectionCandidate> PhysicsTerrainStage::GetDetectionCandidates() const
{
    return m_detectionCandidates;
}

std::vector<TerrainContactManifold>& PhysicsTerrainStage::GetContactManifolds()
{
    return m_contactManifolds;
}

const std::vector<TerrainContactManifold>& PhysicsTerrainStage::GetContactManifolds() const
{
    return m_contactManifolds;
}

std::span<uint8_t> PhysicsTerrainStage::GetRestApplied()
{
    return m_restApplied;
}

uint64_t PhysicsTerrainStage::CollectDynamicMemoryBytes() const
{
    return VectorCapacityBytes( m_detectionCandidates ) + VectorCapacityBytes( m_contactManifolds );
}
