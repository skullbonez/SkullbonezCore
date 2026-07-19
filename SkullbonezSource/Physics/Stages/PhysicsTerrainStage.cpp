/*
File: SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp
Purpose:
  Implements deterministic swept-terrain detection and manifold commit.

Summary:
  Detection maps worker slots through the ascending awake-body list while
  preserving the original thresholds. Prepared commits preserve the original
  float expressions and body-index order, while a narrow sequencer gap retains
  diagnostics and visual side effects at their certified positions.

Glossary:
  Terrain sweep: Continuous collision query over one body's remaining substep.
  Manifold commit: Append of solver-ready terrain contact points and sleep policy.
  Sequencer gap: Typed boundary where cross-domain diagnostics are emitted.
  Awake slot: Dispatch position mapped to one ascending dynamic body index.

Invariants:
  - Worker scheduling never changes candidate slot identity.
  - Body integration and manifold construction occur before diagnostics.
  - Manifold/sleep writes occur after diagnostics and before clock completion.
  - Dormant and fixed bodies never enter terrain-detection dispatch.

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
constexpr uint32_t PHYSICS_TERRAIN_DETECT_WORKER_HASH = HashStr( "Frame/Physics/Terrain/Detect/WorkerBodies" );

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

bool IsSolverBodyFixed( const PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<size_t>( bodyIndex )] != 0u;
}

TerrainContactBodyView TerrainContactBodyViewForIndex( std::span<const PhysicsBodyRecord> bodyRecords,
                                                       const PhysicsBodyHotFieldsConstView& hotFields,
                                                       const SkullbonezCore::Core::EngineConfig& config,
                                                       int index )
{
    const PhysicsBodyRecord& record = bodyRecords[static_cast<size_t>( index )];
    const size_t bodyIndex = static_cast<size_t>( index );
    TerrainContactBodyView body;
    body.position = PhysicsBodyPosition( hotFields, bodyIndex );
    body.orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
    body.linearVelocity = PhysicsBodyLinearVelocity( hotFields, bodyIndex );
    body.terrain = record.terrain;
    body.boundingRadius = hotFields.boundingRadius[bodyIndex];
    body.contactEpsilon = record.contactEpsilon;
    body.terrainContactThreshold = config.terrainContact.threshold;
    body.restitutionThreshold = config.bodySimulation.contactRestitutionThreshold;
    body.isFixed = hotFields.fixed[bodyIndex] != 0u;
    return body;
}
} // namespace

PhysicsTerrainStage::PhysicsTerrainStage()
{
    m_detectionCandidates.reserve( Scene::Capacity::MAX_SCENE_OBJECTS );
    m_contactManifolds.reserve( Scene::Capacity::MAX_SCENE_OBJECTS );
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
    if ( IsSolverBodyFixed( context.hotFields, bodyIndex ) )
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
    candidate.sweep = SweepTerrainContact(
        context.profiler,
        TerrainContactBodyViewForIndex( context.bodyRecords, context.hotFields, context.config, bodyIndex ),
        context.colliderRecords[static_cast<size_t>( bodyIndex )].shape,
        candidate.availableTime );
    candidate.tested = 1;
}

void PhysicsTerrainStage::TerrainDetectionStage::operator()( int bodySlot ) const
{
    stage.DetectTerrainAt( context, bodyIndices[static_cast<std::size_t>( bodySlot )] );
}

void PhysicsTerrainStage::Detect( const TerrainDetectionStageContext& context,
                                  int modelCount,
                                  std::span<const int> awakeBodyIndices,
                                  const Core::PhysicsExecutionConfig& execution,
                                  Threading::WorkerPool& workerPool )
{
    m_detectionCandidates.assign( static_cast<size_t>( modelCount ), TerrainDetectionCandidate() );
    TerrainDetectionStage detectionStage{ *this, context, awakeBodyIndices };
    const int awakeBodyCount = static_cast<int>( awakeBodyIndices.size() );
    if ( execution.parallel && execution.parallelTerrainDetect )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       awakeBodyCount,
                                       detectionStage,
                                       PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/Terrain/Detect/WorkerBodies",
                                       PHYSICS_TERRAIN_DETECT_WORKER_HASH );
    }
    else
    {
        for ( int awakeSlot = 0; awakeSlot < awakeBodyCount; ++awakeSlot )
        {
            detectionStage( awakeSlot );
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
        (void)context.bodyStore.IntegrateBodyPose( context.profiler, context.colliderStore, bodyIndex, colTime );
        const float remainingTime = (std::max)( 0.0f, availableTime - colTime );
        const bool hasManifold = Physics::BuildTerrainContactManifold(
            context.profiler,
            TerrainContactBodyViewForIndex( context.bodyRecords, context.hotFields, context.config, bodyIndex ),
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
                                   : PhysicsBodyPosition( context.hotFields, static_cast<size_t>( bodyIndex ) );
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
