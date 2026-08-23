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
  Manifold commit: Append of solver-ready terrain contact points and sleep policy.
  Sequencer gap: Typed boundary where cross-domain diagnostics are emitted.

Invariants:
  - Worker scheduling never changes candidate slot identity.
  - Body integration and manifold construction occur before diagnostics.
  - Manifold/sleep writes occur after diagnostics and before clock completion.
  - Full and count-only lanes observe the same hit; only the full lane samples
    the diagnostic position/manifold payload.
  - Dormant and fixed bodies never enter terrain-detection dispatch.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Physics/TerrainContactManifold.cpp
*/
#include "PhysicsTerrainStage.h"

#include "../../Core/Common.h"
#include "../../Core/Profiler.h"
#include "../../Core/WorkerPool.h"
#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsMotionEligibility.h"

#include <algorithm>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
namespace Physics = SkullbonezCore::Physics;

namespace
{
constexpr int TERRAIN_BODY_INDEX = -1;
constexpr int PHYSICS_PARALLEL_MIN_BODIES = 512;
constexpr uint32_t PHYSICS_TERRAIN_DETECT_WORKER_HASH = HashStr( "Frame/Physics/Terrain/Detect/WorkerBodies" );

template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}

bool IsSolverBodyFixed( const PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<size_t>( bodyIndex )] != 0u;
}

TerrainContactBodyView TerrainContactBodyViewForIndex( std::span<const BuoyancyBodyFacts> buoyancyFacts,
                                                       const PhysicsBodyHotFieldsConstView& hotFields,
                                                       const PhysicsTerrainView& terrain,
                                                       const PhysicsRuntimeSettings& settings, int index )
{
    const size_t bodyIndex = static_cast<size_t>( index );
    TerrainContactBodyView body;
    body.position = PhysicsBodyPosition( hotFields, bodyIndex );
    body.orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
    body.linearVelocity = PhysicsBodyLinearVelocity( hotFields, bodyIndex );
    body.terrain = terrain;
    body.boundingRadius = hotFields.boundingRadius[bodyIndex];
    body.contactEpsilon = buoyancyFacts[bodyIndex].contactEpsilon;
    body.terrainContactThreshold = settings.terrain.threshold;
    body.restitutionThreshold = settings.body.contactRestitutionThreshold;
    body.isFixed = hotFields.fixed[bodyIndex] != 0u;
    return body;
}
} // namespace

PhysicsTerrainStage::PhysicsTerrainStage() = default;

void PhysicsTerrainStage::ReserveSceneCapacity( std::size_t bodyCapacity )
{
    m_detectionCandidates.Reserve( bodyCapacity );
    m_contactManifolds.Reserve( bodyCapacity );
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

void PhysicsTerrainStage::DetectTerrainAt( std::span<const PhysicsBodyRecord> bodyRecords,
                                           std::span<const BuoyancyBodyFacts> buoyancyFacts,
                                           const PhysicsBodyHotFieldsConstView& hotFields,
                                           std::span<const ColliderRecord> colliderRecords, PhysicsTerrainView terrain,
                                           const PhysicsRuntimeSettings& settings, std::span<const uint8_t> sleepState,
                                           std::span<const uint8_t> motionEligibilityState,
                                           std::span<const float> timeRemaining, Core::Profiler* profiler, int bodyIndex )
{
    TerrainDetectionCandidate& candidate = m_detectionCandidates[static_cast<size_t>( bodyIndex )];

    if ( IsSolverBodyFixed( hotFields, bodyIndex ) )
    {
        return;
    }

    if ( sleepState[bodyIndex] || timeRemaining[bodyIndex] <= 0.0f )
    {
        return;
    }

    if ( bodyIndex >= static_cast<int>( bodyRecords.size() ) || bodyIndex >= static_cast<int>( colliderRecords.size() ) )
    {
        return;
    }

    candidate.availableTime = timeRemaining[bodyIndex];

    // Hazard: a short classification span cannot safely opt a body out of CCD.
    // Production supplies one row per body; direct callers fail conservative.
    const bool linearPromoted = bodyIndex >= static_cast<int>( motionEligibilityState.size() ) ||
                                ( motionEligibilityState[static_cast<std::size_t>( bodyIndex )] &
                                  PhysicsMotionEligibilityLinearPromoted ) != 0u;
    const float detectionHorizon = linearPromoted ? candidate.availableTime : 0.0f;
    candidate.sweep = SweepTerrainContact( profiler,
                                           TerrainContactBodyViewForIndex( buoyancyFacts, hotFields, terrain, settings,
                                                                           bodyIndex ),
                                           colliderRecords[static_cast<size_t>( bodyIndex )].shape, detectionHorizon );

    candidate.tested = 1;
}

void PhysicsTerrainStage::Detect( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                  std::span<const BuoyancyBodyFacts> buoyancyFacts, PhysicsTerrainView terrain,
                                  const PhysicsRuntimeSettings& settings, std::span<const uint8_t> sleepState,
                                  std::span<const uint8_t> motionEligibilityState, std::span<const float> timeRemaining,
                                  Core::Profiler* profiler, std::span<const int> awakeBodyIndices,
                                  const PhysicsExecutionSettings& execution, Threading::WorkerPool& workerPool )
{
    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    const int modelCount = (std::min)( { bodyStore.Count(), static_cast<int>( bodyRecords.size() ), colliderStore.Count(),
                                         static_cast<int>( colliderRecords.size() ),
                                         static_cast<int>( buoyancyFacts.size() ) } );

    m_detectionCandidates.assign( static_cast<size_t>( modelCount ), TerrainDetectionCandidate() );
    const auto detectAwakeBody = [&]( int bodySlot )
    {
        DetectTerrainAt( bodyRecords, buoyancyFacts, hotFields, colliderRecords, terrain, settings, sleepState,
                         motionEligibilityState, timeRemaining, profiler,
                         awakeBodyIndices[static_cast<std::size_t>( bodySlot )] );
    };

    const int awakeBodyCount = static_cast<int>( awakeBodyIndices.size() );

    if ( execution.parallel && execution.parallelTerrainDetect )
    {
        workerPool.ParallelForNoAlloc( 0, awakeBodyCount, detectAwakeBody, PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/Terrain/Detect/WorkerBodies", PHYSICS_TERRAIN_DETECT_WORKER_HASH );
    }
    else
    {
        for ( int awakeSlot = 0; awakeSlot < awakeBodyCount; ++awakeSlot )
        {
            detectAwakeBody( awakeSlot );
        }
    }
}

template <bool RetainPipelineRecords>
PreparedTerrainCandidateCommit
PhysicsTerrainStage::PrepareCandidateCommit( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                             PhysicsTerrainView terrain, std::span<BuoyancyBodyFacts> buoyancyFacts,
                                             const PhysicsRuntimeSettings& settings, Core::Profiler* profiler, int bodyIndex,
                                             float availableTime, const TerrainContactSweepResult& sweep )
{
    PreparedTerrainCandidateCommit commit;
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();

    if ( sweep.hit )
    {
        const float colTime = sweep.collisionTime;
        (void)bodyStore.IntegrateBodyPose( profiler, colliderStore, terrain,
                                           buoyancyFacts[static_cast<std::size_t>( bodyIndex )], bodyIndex, colTime );
        const float remainingTime = (std::max)( 0.0f, availableTime - colTime );
        const bool hasManifold = Physics::BuildTerrainContactManifold( profiler,
                                                                       TerrainContactBodyViewForIndex( buoyancyFacts,
                                                                                                       hotFields, terrain,
                                                                                                       settings, bodyIndex ),
                                                                       colliderRecords[static_cast<size_t>( bodyIndex )]
                                                                           .shape,
                                                                       bodyIndex, sweep, availableTime, commit.manifold );

        if constexpr ( RetainPipelineRecords )
        {
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::TerrainHit;
            record.bodyA = bodyIndex;
            record.bodyB = TERRAIN_BODY_INDEX;
            record.point = hasManifold ? commit.manifold.points[0].point
                                       : PhysicsBodyPosition( hotFields, static_cast<size_t>( bodyIndex ) );

            record.normal = hasManifold ? commit.manifold.normal : ZERO_VECTOR;
            record.scalarA = colTime;
            record.scalarB = hasManifold && commit.manifold.supportsRestingPolicy ? 1.0f : 0.0f;
            record.scalarC = hasManifold ? static_cast<float>( commit.manifold.pointCount ) : 0.0f;

            commit.pipelineRecord = record;
        }

        commit.collisionTime = colTime;
        commit.availableTime = availableTime;
        commit.remainingTime = remainingTime;
        commit.bodyIndex = bodyIndex;
        commit.hit = 1;
        commit.hasManifold = hasManifold ? 1 : 0;
    }

    return commit;
}

template PreparedTerrainCandidateCommit
PhysicsTerrainStage::PrepareCandidateCommit<true>( PhysicsBodyStore&, const ColliderStore&, PhysicsTerrainView,
                                                   std::span<BuoyancyBodyFacts>, const PhysicsRuntimeSettings&,
                                                   Core::Profiler*, int, float, const TerrainContactSweepResult& );
template PreparedTerrainCandidateCommit
PhysicsTerrainStage::PrepareCandidateCommit<false>( PhysicsBodyStore&, const ColliderStore&, PhysicsTerrainView,
                                                    std::span<BuoyancyBodyFacts>, const PhysicsRuntimeSettings&,
                                                    Core::Profiler*, int, float, const TerrainContactSweepResult& );

void PhysicsTerrainStage::CommitCandidate( const PreparedTerrainCandidateCommit& commit,
                                           std::span<uint8_t> sleepSupportedThisFrame,
                                           std::span<uint8_t> sleepInhibitedThisFrame )
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
            sleepSupportedThisFrame[commit.bodyIndex] = 1;
        }
        else
        {
            sleepInhibitedThisFrame[commit.bodyIndex] = 1;
        }
    }
    else
    {
        sleepInhibitedThisFrame[commit.bodyIndex] = 1;
    }
}

std::span<const TerrainDetectionCandidate> PhysicsTerrainStage::GetDetectionCandidates() const
{
    return m_detectionCandidates;
}

PhysicsBodyRowList<TerrainContactManifold>& PhysicsTerrainStage::GetContactManifolds()
{
    return m_contactManifolds;
}

std::span<const TerrainContactManifold> PhysicsTerrainStage::GetContactManifolds() const
{
    return m_contactManifolds;
}

std::span<uint8_t> PhysicsTerrainStage::GetRestApplied()
{
    return m_restApplied;
}

uint64_t PhysicsTerrainStage::CollectDynamicMemoryBytes() const
{
    return ListCapacityBytes( m_detectionCandidates ) + ListCapacityBytes( m_contactManifolds );
}
