/*
File: SkullbonezSource/SkullbonezPhysicsWorld.cpp
Purpose:
  Owns per-scene physics working state shared by broadphase, solver, and diagnostics.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  CPU (Central Processing Unit): Host processor running engine code and
  recording GPU commands.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/SkullbonezPhysicsWorld.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezPhysicsWorld.h"

#include "SkullbonezConfig.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezObjectContactManifold.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezWorkerPool.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
namespace Math = SkullbonezCore::Math;
namespace Physics = SkullbonezCore::Physics;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
constexpr size_t MAX_PIPELINE_TRACE_RECORDS = 4096;
constexpr int TERRAIN_BODY_INDEX = -1;
constexpr float TORNADO_EJECTION_PHASE_HZ = 10.0f;
constexpr float UNDERWATER_SLEEP_LOCK_SUBMERGED_PERCENT = 0.999f;
constexpr int PHYSICS_PARALLEL_MIN_BODIES = 512;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MIN_PAIRS = 256;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MIN_ISLANDS = 16;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MAX_AVG_PAIRS_PER_ISLAND = 4;

Vector3 ClampVectorMagnitude( const Vector3& value, float maxMagnitude )
{
    if ( maxMagnitude <= TOLERANCE )
    {
        return ZERO_VECTOR;
    }

    const float magSq = value * value;
    const float maxSq = maxMagnitude * maxMagnitude;
    if ( magSq <= maxSq || magSq <= TOLERANCE * TOLERANCE )
    {
        return value;
    }

    return value * ( maxMagnitude / sqrtf( magSq ) );
}
} // namespace


PhysicsWorld::PhysicsWorld()
    : m_spatialGrid( Cfg().broadphaseCell )
{
    m_timeRemaining.reserve( MAX_GAME_MODELS );
    m_sleepSupportedThisFrame.reserve( MAX_GAME_MODELS );
    m_sleepInhibitedThisFrame.reserve( MAX_GAME_MODELS );
    m_underwaterSleepLocked.reserve( MAX_GAME_MODELS );
    m_tornadoCaptureSeconds.reserve( MAX_GAME_MODELS );
    m_tornadoEjectCooldownSeconds.reserve( MAX_GAME_MODELS );
    m_collisionVisualContacts.reserve( MAX_GAME_MODELS );
    m_sleepIslandVisualId.reserve( MAX_GAME_MODELS );
    m_sleepIslandAssignedVisualId.reserve( MAX_GAME_MODELS );
    m_sleepSupportEdges.reserve( MAX_GAME_MODELS * 4 );
    m_sleepIslandParent.reserve( MAX_GAME_MODELS );
    m_sleepIslandRank.reserve( MAX_GAME_MODELS );
    m_sleepIslandHasAwake.reserve( MAX_GAME_MODELS );
    m_sleepIslandHasSupportAnchor.reserve( MAX_GAME_MODELS );
    m_sleepIslandEligible.reserve( MAX_GAME_MODELS );
    m_sleepIslandCanSleep.reserve( MAX_GAME_MODELS );
    m_persistentContacts.reserve( MAX_GAME_MODELS * 4 );
    m_persistentContactCache.reserve( MAX_GAME_MODELS * 4 );
    m_persistentContactCounts.reserve( MAX_GAME_MODELS );
    m_solverBodies.reserve( MAX_GAME_MODELS );
    m_physicsDebugContacts.reserve( MAX_GAME_MODELS * 4 );
    m_physicsPipelineTrace.reserve( MAX_PIPELINE_TRACE_RECORDS );
    m_terrainContactManifolds.reserve( MAX_GAME_MODELS );
    m_terrainDetectionCandidates.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseEvents.reserve( MAX_GAME_MODELS * 4 );
    m_objectNarrowphaseIslands.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseParent.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseRank.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseRootToIsland.reserve( MAX_GAME_MODELS );
}


void PhysicsWorld::Clear()
{
    m_timeRemaining.clear();
    m_candidatePairs.clear();
    m_sleepSupportedThisFrame.clear();
    m_sleepInhibitedThisFrame.clear();
    m_sleepState.clear();
    m_sleepCounter.clear();
    m_underwaterSleepLocked.clear();
    m_tornadoCaptureSeconds.clear();
    m_tornadoEjectCooldownSeconds.clear();
    m_collisionVisualContacts.clear();
    m_sleepIslandVisualId.clear();
    m_sleepIslandAssignedVisualId.clear();
    m_nextSleepIslandVisualId = 1;
    m_collisionVisualFrameActive = false;
    m_sleepSupportEdges.clear();
    m_sleepIslandParent.clear();
    m_sleepIslandRank.clear();
    m_sleepIslandHasAwake.clear();
    m_sleepIslandHasSupportAnchor.clear();
    m_sleepIslandEligible.clear();
    m_sleepIslandCanSleep.clear();
    m_persistentContacts.clear();
    m_persistentContactCache.clear();
    m_persistentContactSolverStats = PersistentContactSolverStats();
    m_persistentContactCounts.clear();
    m_solverBodies.clear();
    m_physicsDebugContacts.clear();
    m_physicsPipelineTrace.clear();
    m_terrainContactManifolds.clear();
    m_terrainDetectionCandidates.clear();
    m_objectNarrowphaseEvents.clear();
    m_objectNarrowphaseIslands.clear();
    m_objectNarrowphaseParent.clear();
    m_objectNarrowphaseRank.clear();
    m_objectNarrowphaseRootToIsland.clear();
    m_collisionCellKeys.clear();
}


void PhysicsWorld::EnsureCollisionVisualBuffers( int modelCount )
{
    if ( static_cast<int>( m_collisionVisualContacts.size() ) != modelCount )
    {
        m_collisionVisualContacts.assign( modelCount, 0 );
    }
    if ( static_cast<int>( m_sleepIslandVisualId.size() ) != modelCount )
    {
        m_sleepIslandVisualId.assign( modelCount, 0 );
    }
}


void PhysicsWorld::EnsureTornadoStateBuffers( int modelCount )
{
    if ( static_cast<int>( m_tornadoCaptureSeconds.size() ) != modelCount )
    {
        m_tornadoCaptureSeconds.assign( modelCount, 0.0f );
    }
    if ( static_cast<int>( m_tornadoEjectCooldownSeconds.size() ) != modelCount )
    {
        m_tornadoEjectCooldownSeconds.assign( modelCount, 0.0f );
    }
}


void PhysicsWorld::EnsureUnderwaterSleepLockBuffer( int modelCount )
{
    if ( modelCount < 0 )
    {
        return;
    }
    if ( static_cast<int>( m_underwaterSleepLocked.size() ) != modelCount )
    {
        m_underwaterSleepLocked.resize( static_cast<size_t>( modelCount ), 0 );
    }
}


bool PhysicsWorld::IsFullySubmergedBall( GameModelCollection& collection, const GameModelBodyStream& bodyStream, int index )
{
    auto& m_gameModels = collection.m_gameModels;
    if ( index < 0 ||
         index >= bodyStream.count ||
         index >= static_cast<int>( m_gameModels.size() ) ||
         bodyStream.isFixed[index] ||
         bodyStream.isBox[index] )
    {
        return false;
    }

    return m_gameModels[index].GetSubmergedVolumePercent() >= UNDERWATER_SLEEP_LOCK_SUBMERGED_PERCENT;
}


void PhysicsWorld::LockUnderwaterSleeperIfReady( GameModelCollection& collection, const GameModelBodyStream& bodyStream, int index )
{
    EnsureUnderwaterSleepLockBuffer( bodyStream.count );
    if ( index < 0 ||
         index >= bodyStream.count ||
         index >= static_cast<int>( m_sleepState.size() ) ||
         !m_sleepState[index] ||
         m_underwaterSleepLocked[index] ||
         !IsFullySubmergedBall( collection, bodyStream, index ) )
    {
        return;
    }

    m_underwaterSleepLocked[index] = 1;
    if ( index < static_cast<int>( m_timeRemaining.size() ) )
    {
        m_timeRemaining[index] = 0.0f;
    }
    collection.m_gameModels[index].SetLinearVelocity( ZERO_VECTOR );
    collection.m_gameModels[index].SetAngularVelocity( ZERO_VECTOR );
}


bool PhysicsWorld::IsUnderwaterSleepLocked( GameModelCollection& collection, const GameModelBodyStream& bodyStream, int index )
{
    EnsureUnderwaterSleepLockBuffer( bodyStream.count );
    if ( index < 0 || index >= bodyStream.count )
    {
        return false;
    }
    if ( m_underwaterSleepLocked[index] )
    {
        return true;
    }

    LockUnderwaterSleeperIfReady( collection, bodyStream, index );
    return m_underwaterSleepLocked[index] != 0;
}


void PhysicsWorld::MarkCollisionVisualContact( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_collisionVisualContacts.size() ) )
    {
        return;
    }
    m_collisionVisualContacts[index] = 1;
}


void PhysicsWorld::MarkFixedContact( GameModelCollection& collection, int index )
{
    auto& m_gameModels = collection.m_gameModels;
    if ( index < 0 || index >= static_cast<int>( m_gameModels.size() ) )
    {
        return;
    }
    if ( m_gameModels[index].IsFixed() )
    {
        m_gameModels[index].NotifyFixedContact( 0.5f );
    }
}


void PhysicsWorld::RecordPhysicsPipelineStage( const PhysicsPipelineRecord& record )
{
    if ( m_physicsPipelineTrace.size() < MAX_PIPELINE_TRACE_RECORDS )
    {
        m_physicsPipelineTrace.push_back( record );
    }
}


void PhysicsWorld::BeginCollisionVisualFrame( int modelCount )
{
    m_collisionVisualContacts.assign( modelCount, 0 );
    if ( static_cast<int>( m_sleepIslandVisualId.size() ) != modelCount )
    {
        m_sleepIslandVisualId.assign( modelCount, 0 );
    }
    m_collisionVisualFrameActive = true;
}


void PhysicsWorld::EndCollisionVisualFrame()
{
    m_collisionVisualFrameActive = false;
}


void PhysicsWorld::RunPhysics( GameModelCollection& collection, float fChangeInTime )
{
    // Concept: one fixed physics tick has a predictable data flow.
    //
    // 1. Resize/clear per-frame arrays so every model index has a slot.
    // 2. Reset debug, sleep-support, pipeline, and terrain-manifold output.
    // 3. Refresh the SoA cache from GameModel/RigidBody state for hot loops.
    // 4. Run broadphase, swept movement, terrain manifold generation, and the
    //    persistent Catto-style contact solver.
    // 5. Emit bounded Debug diagnostics, then invalidate cached render/physics
    //    SoA data because solver writeback may have changed body state.
    //
    // Determinism note: changing this ordering can change byte-exact physics
    // baselines even when the final scene "looks" similar.
    auto& m_gameModels = collection.m_gameModels;
    const int modelCount = static_cast<int>( m_gameModels.size() );
    EnsureCollisionVisualBuffers( modelCount );
    if ( !m_collisionVisualFrameActive )
    {
        m_collisionVisualContacts.assign( modelCount, 0 );
    }
    m_timeRemaining.assign( modelCount, fChangeInTime );
    m_sleepSupportedThisFrame.assign( modelCount, 0 );
    m_sleepInhibitedThisFrame.assign( modelCount, 0 );
    m_physicsDebugContacts.clear();
    m_physicsPipelineTrace.clear();
    m_terrainContactManifolds.clear();
    m_sleepSupportEdges.clear();

    for ( int i = 0; i < modelCount; ++i )
    {
        m_gameModels[i].TickFixedContactHighlight( fChangeInTime );
    }

    if ( static_cast<int>( m_sleepState.size() ) != modelCount )
    {
        m_sleepState.assign( modelCount, 0 );
        m_sleepCounter.assign( modelCount, 0 );
    }
    EnsureUnderwaterSleepLockBuffer( modelCount );
    if ( !m_sleepEnabled )
    {
        std::fill( m_sleepState.begin(), m_sleepState.end(), static_cast<uint8_t>( 0 ) );
        std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
        std::fill( m_underwaterSleepLocked.begin(), m_underwaterSleepLocked.end(), static_cast<uint8_t>( 0 ) );
        std::fill( m_sleepIslandVisualId.begin(), m_sleepIslandVisualId.end(), 0 );
    }
    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_gameModels[i].IsFixed() )
        {
            m_sleepState[i] = 0;
            m_sleepCounter[i] = 0;
            m_underwaterSleepLocked[i] = 0;
            m_sleepSupportedThisFrame[i] = 1;
            m_sleepIslandVisualId[i] = 0;
            continue;
        }
        if ( !m_sleepState[i] )
        {
            m_underwaterSleepLocked[i] = 0;
            m_sleepIslandVisualId[i] = 0;
        }
    }

    (void)collection.GetBodyStream();
    RunSolverPhysics( collection, fChangeInTime );

#ifdef _DEBUG
    m_diagnostics.EmitRegressionLog( *this, collection );
    m_diagnostics.IncrementCollisionTimeFrameIfEnabled();
    m_diagnostics.EmitFrame( collection, fChangeInTime );
#endif

    collection.InvalidateSoA();
}


void PhysicsWorld::WakeModel( GameModelCollection& collection, int index )
{
    auto& m_gameModels = collection.m_gameModels;
    if ( index >= 0 &&
         index < static_cast<int>( m_gameModels.size() ) &&
         m_gameModels[index].IsFixed() )
    {
        return;
    }

    if ( static_cast<int>( m_sleepState.size() ) != static_cast<int>( m_gameModels.size() ) )
    {
        m_sleepState.assign( m_gameModels.size(), 0 );
        m_sleepCounter.assign( m_gameModels.size(), 0 );
    }
    EnsureUnderwaterSleepLockBuffer( static_cast<int>( m_gameModels.size() ) );
    if ( index >= 0 && index < static_cast<int>( m_sleepState.size() ) )
    {
        GameModelBodyStream bodyStream = collection.GetBodyStream();
        if ( IsUnderwaterSleepLocked( collection, bodyStream, index ) )
        {
            return;
        }
    }
    if ( index >= 0 && index < static_cast<int>( m_sleepState.size() ) )
    {
        collection.InvalidateSoA();
        m_sleepState[index] = 0;
        m_sleepCounter[index] = 0;
        m_underwaterSleepLocked[index] = 0;
        if ( index < static_cast<int>( m_sleepIslandVisualId.size() ) )
        {
            m_sleepIslandVisualId[index] = 0;
        }
    }

    // Hazard: waking a body must also forget any cached contact impulses that
    // involve that body. Warm-start impulses are great for resting contact, but
    // stale impulses after a manual wake or external force can push the body as
    // if an old support contact still existed.
    const auto cacheEntryReferencesBody = []( const PersistentContactCacheEntry& entry, int bodyIndex ) -> bool
    {
        const uint64_t key = static_cast<uint64_t>( entry.key );
        const uint32_t highBody = static_cast<uint32_t>( ( key >> 48 ) & 0xffffu );
        if ( highBody == 0xffffu )
        {
            const uint32_t terrainBody = static_cast<uint32_t>( ( key >> 16 ) & 0xffffffffu );
            return terrainBody == static_cast<uint32_t>( bodyIndex );
        }

        const uint32_t lowBody = static_cast<uint32_t>( ( key >> 40 ) & 0xffffffu );
        const uint32_t objectHighBody = static_cast<uint32_t>( ( key >> 16 ) & 0xffffffu );
        return lowBody == static_cast<uint32_t>( bodyIndex ) ||
               objectHighBody == static_cast<uint32_t>( bodyIndex );
    };

    m_persistentContactCache.erase(
        std::remove_if( m_persistentContactCache.begin(),
                        m_persistentContactCache.end(),
                        [index, &cacheEntryReferencesBody]( const PersistentContactCacheEntry& entry )
                        {
                            return cacheEntryReferencesBody( entry, index );
                        } ),
        m_persistentContactCache.end() );
}


void PhysicsWorld::SetPhysicsSleepEnabled( bool enabled )
{
    m_sleepEnabled = enabled;
    if ( enabled )
    {
        return;
    }

    std::fill( m_sleepState.begin(), m_sleepState.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_underwaterSleepLocked.begin(), m_underwaterSleepLocked.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_sleepIslandVisualId.begin(), m_sleepIslandVisualId.end(), 0 );
    std::fill( m_sleepIslandAssignedVisualId.begin(), m_sleepIslandAssignedVisualId.end(), 0 );
}


void PhysicsWorld::ApplyTornadoField( GameModelCollection& collection, float dt )
{
    const TornadoFieldConfig& config = m_tornadoField.GetConfig();
    if ( !config.enabled )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Physics/TornadoField" );
    auto& m_gameModels = collection.m_gameModels;
    const GameModelBodyStream bodyStream = collection.GetBodyStream();
    const int modelCount = bodyStream.count;
    const float step = (std::max)( 0.0f, dt );
    const float height = (std::max)( config.height, 1.0f );
    const float ejectBand = std::clamp( config.ejectBand, 0.0f, 1.0f );
    const float minCaptureSeconds = (std::max)( 0.0f, config.minCaptureSeconds );
    const float cooldownSeconds = (std::max)( 0.0f, config.ejectCooldownSeconds );
    const float maxDeltaVelocity = (std::max)( 1.0f, config.maxDeltaVelocity );
    const float minTangentialSpeed = (std::max)( 18.0f, config.swirlAcceleration * 0.12f );
    EnsureTornadoStateBuffers( modelCount );

    auto applyTornadoAt = [&]( int i )
    {
        if ( bodyStream.isFixed[i] || bodyStream.isBox[i] || IsUnderwaterSleepLocked( collection, bodyStream, i ) )
        {
            m_tornadoCaptureSeconds[i] = 0.0f;
            m_tornadoEjectCooldownSeconds[i] = 0.0f;
            return;
        }

        const Vector3 position = m_gameModels[i].GetPosition();
        const float dx = position.x - config.center.x;
        const float dz = position.z - config.center.z;
        const float horizontalSq = dx * dx + dz * dz;
        const float horizontal = sqrtf( horizontalSq );
        const float height01 = ( position.y - config.center.y ) / height;
        Vector3 acceleration = m_tornadoField.SampleAcceleration( position );
        if ( ( acceleration * acceleration ) <= TOLERANCE * TOLERANCE )
        {
            m_tornadoCaptureSeconds[i] = 0.0f;
            m_tornadoEjectCooldownSeconds[i] = (std::max)( 0.0f, m_tornadoEjectCooldownSeconds[i] - step );
            return;
        }

        if ( m_sleepState[i] )
        {
            m_sleepState[i] = 0;
            m_sleepCounter[i] = 0;
            m_sleepIslandVisualId[i] = 0;
            m_timeRemaining[i] = dt;
            m_gameModels[i].ApplyForces( dt );
        }

        Vector3 velocity = m_gameModels[i].GetVelocity();
        m_tornadoCaptureSeconds[i] += step;
        m_tornadoEjectCooldownSeconds[i] = (std::max)( 0.0f, m_tornadoEjectCooldownSeconds[i] - step );

        Vector3 outward;
        if ( horizontal > TOLERANCE )
        {
            outward = Vector3( dx / horizontal, 0.0f, dz / horizontal );
        }
        else
        {
            switch ( i & 3 )
            {
            case 0:
                outward = Vector3( 1.0f, 0.0f, 0.0f );
                break;
            case 1:
                outward = Vector3( 0.0f, 0.0f, 1.0f );
                break;
            case 2:
                outward = Vector3( -1.0f, 0.0f, 0.0f );
                break;
            default:
                outward = Vector3( 0.0f, 0.0f, -1.0f );
                break;
            }
        }

        const Vector3 tangent( -outward.z, 0.0f, outward.x );
        const float tangentialSpeed = fabsf( velocity * tangent );
        const int captureBucket = static_cast<int>( m_tornadoCaptureSeconds[i] * TORNADO_EJECTION_PHASE_HZ );
        const bool deterministicSlot = ( ( i + captureBucket ) % 3 ) == 0;
        if ( height01 >= ejectBand &&
             m_tornadoCaptureSeconds[i] >= minCaptureSeconds &&
             m_tornadoEjectCooldownSeconds[i] <= 0.0f &&
             tangentialSpeed >= minTangentialSpeed &&
             deterministicSlot )
        {
            acceleration += outward * config.ejectAcceleration +
                            Vector3( 0.0f, config.ejectUpAcceleration, 0.0f );
            m_tornadoCaptureSeconds[i] = 0.0f;
            m_tornadoEjectCooldownSeconds[i] = cooldownSeconds;
        }

        velocity += ClampVectorMagnitude( acceleration * step, maxDeltaVelocity );
        m_gameModels[i].SetLinearVelocity( velocity );
    };

    if ( Cfg().physicsParallel )
    {
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor( 0, modelCount, applyTornadoAt, PHYSICS_PARALLEL_MIN_BODIES );
    }
    else
    {
        for ( int i = 0; i < modelCount; ++i )
        {
            applyTornadoAt( i );
        }
    }
}


void PhysicsWorld::SetTornadoFieldConfig( const TornadoFieldConfig& config )
{
    m_tornadoField.SetConfig( config );
    if ( !m_tornadoField.GetConfig().enabled )
    {
        m_tornadoCaptureSeconds.clear();
        m_tornadoEjectCooldownSeconds.clear();
    }
}


const TornadoFieldConfig& PhysicsWorld::GetTornadoFieldConfig() const
{
    return m_tornadoField.GetConfig();
}


void PhysicsWorld::RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj )
{
    m_tornadoField.RenderVectors( viewProj );
}


#ifdef _DEBUG
void PhysicsWorld::SetPhysicsRegressionLogPath( const char* path )
{
    m_diagnostics.SetPhysicsRegressionLogPath( path );
}


void PhysicsWorld::SetPhysicsCollisionTimeLogPath( const char* path )
{
    m_diagnostics.SetPhysicsCollisionTimeLogPath( path );
}


void PhysicsWorld::SetPhysicsDiagnosticsPath( const char* path )
{
    m_diagnostics.SetPhysicsDiagnosticsPath( path );
}


void PhysicsWorld::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_diagnostics.SetPhysicsDiagnosticsRunId( runId );
}


void PhysicsWorld::EmitPhysicsDiagnosticsFrame( GameModelCollection& collection, float dt )
{
    m_diagnostics.EmitFrame( collection, dt );
}
#endif


void PhysicsWorld::EmitPhysicsCollisionTime( GameModelCollection& collection, const char* type, int bodyA, int bodyB, float collisionTime, float availableTime )
{
    m_diagnostics.EmitCollisionTime( collection, type, bodyA, bodyB, collisionTime, availableTime );
}


void PhysicsWorld::PropagateSleepSupport( GameModelCollection& collection )
{
    m_sleepIslandSystem.PropagateSupport( *this, collection );
}

void PhysicsWorld::RunSolverPhysics( GameModelCollection& collection, float dt )
{
    auto& m_gameModels = collection.m_gameModels;
    const GameModelBodyStream bodyStream = collection.GetBodyStream();
    const int modelCount = bodyStream.count;

    // Sleep thresholds are config-backed because they directly trade CPU cost
    // against visible settling behavior. Higher thresholds keep bodies awake
    // longer, which is useful while validating the solver but expensive in
    // sleeping-heavy scenes. Lower thresholds save broadphase/narrowphase work
    // sooner, but if set too aggressively they can freeze objects before the
    // persistent contact solver has converged to a stable support impulse.
    //
    // The counter storage is still uint8_t, so physics_sleep_frames is clamped
    // to 1..255 here. Widening that storage is a separate data-layout change and
    // should be measured before doing it in a hot per-body array.
    const float sleepLinear = (std::max)( 0.0f, Cfg().physicsSleepLinearSpeed );
    const float sleepAngular = (std::max)( 0.0f, Cfg().physicsSleepAngularSpeed );
    const float SLEEP_LINEAR_SQ = sleepLinear * sleepLinear;
    const float SLEEP_ANGULAR_SQ = sleepAngular * sleepAngular;
    const uint8_t SLEEP_FRAMES = static_cast<uint8_t>( (std::max)( 1, (std::min)( Cfg().physicsSleepFrames, 255 ) ) );

    EnsureUnderwaterSleepLockBuffer( modelCount );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_sleepState[x] )
        {
            LockUnderwaterSleeperIfReady( collection, bodyStream, x );
        }
    }

    // Apply forces to awake models only
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    auto applyForcesAt = [&]( int x )
    {
        if ( bodyStream.isFixed[x] )
        {
            return;
        }
        if ( m_sleepState[x] )
        {
            m_timeRemaining[x] = 0.0f;
            return;
        }
        m_gameModels[x].ApplyForces( dt );
    };

    if ( Cfg().physicsParallel )
    {
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor( 0, modelCount, applyForcesAt, PHYSICS_PARALLEL_MIN_BODIES );
    }
    else
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            applyForcesAt( x );
        }
    }
    PROFILE_END( "Frame/Physics/ApplyForces" );

    ApplyTornadoField( collection, dt );

    // Broadphase: build spatial grid from all object positions (include sleeping for wake detection)
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    m_spatialGrid.Clear();
    m_collisionCellKeys.clear();
    for ( int i = 0; i < modelCount; ++i )
    {
        const float radius = bodyStream.boundingRadii[i];
        const Vector3 displacement = m_gameModels[i].GetVelocity() * dt;
        const float displacementSq = Vector::VectorMagSquared( displacement );
        if ( !bodyStream.isFixed[i] && displacementSq > radius * radius )
        {
            m_spatialGrid.InsertSwept( i, bodyStream.positions[i], displacement, radius );
        }
        else
        {
            m_spatialGrid.Insert( i, bodyStream.positions[i], radius );
        }
    }
    std::vector<std::pair<int, int>>& candidatePairs = m_candidatePairs;
    m_spatialGrid.GetCandidatePairs( candidatePairs );
    for ( const auto& pair : candidatePairs )
    {
        if ( pair.first < 0 || pair.second < 0 || pair.first >= modelCount || pair.second >= modelCount )
        {
            continue;
        }

        Physics::PhysicsPipelineRecord record;
        record.stage = Physics::PhysicsPipelineStage::BroadphaseCandidate;
        record.bodyA = pair.first;
        record.bodyB = pair.second;
        record.point = ( m_gameModels[pair.first].GetPosition() + m_gameModels[pair.second].GetPosition() ) * 0.5f;
        Vector3 delta = m_gameModels[pair.second].GetPosition() - m_gameModels[pair.first].GetPosition();
        float deltaMag = Vector::VectorMag( delta );
        record.normal = deltaMag > TOLERANCE ? delta / deltaMag : Vector3( 0.0f, 1.0f, 0.0f );
        record.scalarA = static_cast<float>( candidatePairs.size() );
        RecordPhysicsPipelineStage( record );
    }
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneSleepPairs" );
        // The spatial grid is still populated with sleeping bodies because an
        // awake body must be able to find and wake a sleeping neighbor. What we
        // do not need is sleep/sleep work: two sleeping dynamic bodies cannot
        // generate a new wake event because neither has wake energy, and their
        // previous support relationship is already represented by sleep state
        // and island visual ids. Pruning these pairs immediately keeps both the
        // swept narrowphase and the persistent contact manifold builder from
        // re-checking pairs that would only be skipped later.
        //
        // This is deliberately narrower than a separate awake/sleeping grid.
        // The full partition is still a valid future optimization, but this
        // single pass removes the common dead work without changing pair
        // generation order for any pair that can affect simulation behavior.
        candidatePairs.erase(
            std::remove_if( candidatePairs.begin(),
                            candidatePairs.end(),
                            [&]( const std::pair<int, int>& pair )
                            {
                                const int a = pair.first;
                                const int b = pair.second;
                                const bool prune = a >= 0 && b >= 0 &&
                                                   a < static_cast<int>( m_sleepState.size() ) &&
                                                   b < static_cast<int>( m_sleepState.size() ) &&
                                                   m_sleepState[a] != 0 &&
                                                   m_sleepState[b] != 0;
                                if ( prune )
                                {
                                    Physics::PhysicsPipelineRecord record;
                                    record.stage = Physics::PhysicsPipelineStage::SleepPrunedPair;
                                    record.bodyA = a;
                                    record.bodyB = b;
                                    record.point = ( m_gameModels[a].GetPosition() + m_gameModels[b].GetPosition() ) * 0.5f;
                                    record.scalarA = 1.0f;
                                    RecordPhysicsPipelineStage( record );
                                }
                                return prune;
                            } ),
            candidatePairs.end() );
    }
    PROFILE_END( "Frame/Physics/Broadphase" );

    auto hasWakeEnergy = [&]( int awakeIndex ) -> bool
    {
        const Vector3& vel = m_gameModels[awakeIndex].GetVelocity();
        const Vector3& omega = m_gameModels[awakeIndex].GetAngularVelocity();
        float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
        return speedSq >= SLEEP_LINEAR_SQ || omegaSq >= SLEEP_ANGULAR_SQ;
    };

    auto wakeSleepingModel = [&]( int sleepingIndex )
    {
        // Waking re-enters the body into this frame rather than waiting for the
        // next tick. Applying forces immediately keeps gravity and other forces
        // consistent with an awake body that was never asleep.
        if ( sleepingIndex < 0 ||
             sleepingIndex >= modelCount ||
             bodyStream.isFixed[sleepingIndex] ||
             !m_sleepState[sleepingIndex] ||
             IsUnderwaterSleepLocked( collection, bodyStream, sleepingIndex ) )
        {
            return;
        }

        m_sleepState[sleepingIndex] = 0;
        m_sleepCounter[sleepingIndex] = 0;
        m_sleepIslandVisualId[sleepingIndex] = 0;
        m_timeRemaining[sleepingIndex] = dt;
        m_gameModels[sleepingIndex].ApplyForces( dt );
    };

    auto hasPersistentWakeContact = [&]( int awakeIndex, int sleepingIndex ) -> bool
    {
        // A swept test can miss a sleeper that is already overlapping after an
        // awake body's correction step. This fresh manifold test catches that
        // persistent contact so the sleeper cannot remain frozen inside the
        // awake body until a later frame happens to generate a swept hit.
        ObjectContactManifold manifold;
        return BuildObjectContactManifold( m_gameModels[awakeIndex],
                                           m_gameModels[sleepingIndex],
                                           awakeIndex,
                                           sleepingIndex,
                                           Cfg().contactEpsilon,
                                           manifold );
    };

    auto hasObjectContactAtTime = [&]( int a, int b, float time ) -> bool
    {
        // Temporarily place both bodies at a candidate time, ask the exact
        // narrowphase whether they touch there, then restore positions. This is
        // a query only; it must leave the world exactly as it found it.
        const Vector3 startA = m_gameModels[a].GetPosition();
        const Vector3 startB = m_gameModels[b].GetPosition();
        m_gameModels[a].SetPosition( startA + m_gameModels[a].GetVelocity() * time );
        m_gameModels[b].SetPosition( startB + m_gameModels[b].GetVelocity() * time );

        ObjectContactManifold manifold;
        const bool hit = BuildObjectContactManifold( m_gameModels[a],
                                                     m_gameModels[b],
                                                     a,
                                                     b,
                                                     Cfg().contactEpsilon,
                                                     manifold );

        m_gameModels[a].SetPosition( startA );
        m_gameModels[b].SetPosition( startB );
        return hit;
    };

    auto refineObjectSweepContactTime = [&]( int a, int b, float coarseTime, float availableTime ) -> float
    {
        // The broad sweep can give a conservative first time. Refinement walks
        // forward until exact manifold contact appears, then binary-searches the
        // edge of that contact window. This keeps fast objects from advancing
        // too far into each other before persistent rows solve the response.
        if ( coarseTime <= 0.0f || coarseTime >= availableTime )
        {
            return coarseTime;
        }

        if ( hasObjectContactAtTime( a, b, coarseTime ) )
        {
            return coarseTime;
        }

        float lo = coarseTime;
        float hi = coarseTime;
        bool foundContactWindow = false;
        for ( int step = 1; step <= 48; ++step )
        {
            const float t = coarseTime + ( availableTime - coarseTime ) * ( static_cast<float>( step ) / 48.0f );
            if ( hasObjectContactAtTime( a, b, t ) )
            {
                hi = t;
                foundContactWindow = true;
                break;
            }
            lo = t;
        }

        if ( !foundContactWindow )
        {
            return coarseTime;
        }

        for ( int iter = 0; iter < 12; ++iter )
        {
            const float mid = ( lo + hi ) * 0.5f;
            if ( hasObjectContactAtTime( a, b, mid ) )
            {
                hi = mid;
            }
            else
            {
                lo = mid;
            }
        }
        return hi;
    };

    // Object/object CCD front-end: wake sleepers and advance swept hits to a
    // contact candidate, but leave velocity response to the persistent rows.
    PROFILE_BEGIN( "Frame/Physics/Narrowphase" );
    float invCellSize = 1.0f / m_spatialGrid.GetCellSize();
    const int candidatePairCount = static_cast<int>( candidatePairs.size() );
    m_objectNarrowphaseEvents.assign( candidatePairs.size(), ObjectNarrowphaseEvent() );
    m_objectNarrowphaseParent.resize( static_cast<size_t>( modelCount ) );
    m_objectNarrowphaseRank.assign( static_cast<size_t>( modelCount ), 0 );
    for ( int i = 0; i < modelCount; ++i )
    {
        m_objectNarrowphaseParent[static_cast<size_t>( i )] = i;
    }

    auto findObjectNarrowphaseRoot = [&]( int index ) -> int
    {
        int root = index;
        while ( m_objectNarrowphaseParent[static_cast<size_t>( root )] != root )
        {
            root = m_objectNarrowphaseParent[static_cast<size_t>( root )];
        }
        while ( m_objectNarrowphaseParent[static_cast<size_t>( index )] != index )
        {
            const int next = m_objectNarrowphaseParent[static_cast<size_t>( index )];
            m_objectNarrowphaseParent[static_cast<size_t>( index )] = root;
            index = next;
        }
        return root;
    };

    auto unionObjectNarrowphaseRoots = [&]( int a, int b )
    {
        int rootA = findObjectNarrowphaseRoot( a );
        int rootB = findObjectNarrowphaseRoot( b );
        if ( rootA == rootB )
        {
            return;
        }

        if ( m_objectNarrowphaseRank[static_cast<size_t>( rootA )] < m_objectNarrowphaseRank[static_cast<size_t>( rootB )] )
        {
            std::swap( rootA, rootB );
        }
        m_objectNarrowphaseParent[static_cast<size_t>( rootB )] = rootA;
        if ( m_objectNarrowphaseRank[static_cast<size_t>( rootA )] == m_objectNarrowphaseRank[static_cast<size_t>( rootB )] )
        {
            ++m_objectNarrowphaseRank[static_cast<size_t>( rootA )];
        }
    };

    for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
    {
        const int x = candidatePairs[static_cast<size_t>( pairIndex )].first;
        const int y = candidatePairs[static_cast<size_t>( pairIndex )].second;
        if ( x < 0 || y < 0 || x >= modelCount || y >= modelCount )
        {
            continue;
        }
        unionObjectNarrowphaseRoots( x, y );
    }

    m_objectNarrowphaseIslands.clear();
    m_objectNarrowphaseRootToIsland.assign( static_cast<size_t>( modelCount ), -1 );
    for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
    {
        const int x = candidatePairs[static_cast<size_t>( pairIndex )].first;
        const int y = candidatePairs[static_cast<size_t>( pairIndex )].second;
        if ( x < 0 || y < 0 || x >= modelCount || y >= modelCount )
        {
            continue;
        }

        const int root = findObjectNarrowphaseRoot( x );
        int islandIndex = m_objectNarrowphaseRootToIsland[static_cast<size_t>( root )];
        if ( islandIndex < 0 )
        {
            islandIndex = static_cast<int>( m_objectNarrowphaseIslands.size() );
            m_objectNarrowphaseRootToIsland[static_cast<size_t>( root )] = islandIndex;
            m_objectNarrowphaseIslands.push_back( ObjectNarrowphaseIsland() );
            m_objectNarrowphaseIslands.back().minPairIndex = INT_MAX;
        }

        ObjectNarrowphaseIsland& island = m_objectNarrowphaseIslands[static_cast<size_t>( islandIndex )];
        island.minPairIndex = (std::min)( island.minPairIndex, pairIndex );
        island.pairIndices.push_back( pairIndex );
    }
    std::sort( m_objectNarrowphaseIslands.begin(),
               m_objectNarrowphaseIslands.end(),
               []( const ObjectNarrowphaseIsland& a, const ObjectNarrowphaseIsland& b )
               { return a.minPairIndex < b.minPairIndex; } );

    auto recordObjectNarrowphaseEvent = []( ObjectNarrowphaseEvent& event,
                                            ObjectNarrowphaseEventKind kind,
                                            const Physics::PhysicsPipelineRecord& record )
    {
        event.kind = kind;
        event.pipelineRecord = record;
        event.hasPipelineRecord = 1;
    };

    auto emitObjectCollisionTimeEvent = []( ObjectNarrowphaseEvent& event,
                                            int bodyA,
                                            int bodyB,
                                            float collisionTime,
                                            float availableTime )
    {
        event.emitCollisionTime = 1;
        event.collisionTimeBodyA = bodyA;
        event.collisionTimeBodyB = bodyB;
        event.collisionTime = collisionTime;
        event.availableTime = availableTime;
    };

    auto markObjectVisualEvent = []( ObjectNarrowphaseEvent& event, int bodyA, int bodyB )
    {
        event.markVisualContact = 1;
        event.visualBodyA = bodyA;
        event.visualBodyB = bodyB;
    };

    auto writeObjectCollisionCellEvent = [&]( ObjectNarrowphaseEvent& event, int bodyA, int bodyB )
    {
        const Vector3 midpoint = ( m_gameModels[bodyA].GetPosition() + m_gameModels[bodyB].GetPosition() ) * 0.5f;
        const int16_t cx = static_cast<int16_t>( floorf( midpoint.x * invCellSize ) );
        const int16_t cy = static_cast<int16_t>( floorf( midpoint.y * invCellSize ) );
        const int16_t cz = static_cast<int16_t>( floorf( midpoint.z * invCellSize ) );
        event.collisionCellKey = ( int64_t( cx ) * 73856093 ) ^ ( int64_t( cy ) * 19349663 ) ^ ( int64_t( cz ) * 83492791 );
        event.hasCollisionCellKey = 1;
    };

    auto processObjectNarrowphasePair = [&]( int pairIndex )
    {
        const auto& cp = candidatePairs[static_cast<size_t>( pairIndex )];
        const int x = cp.first;
        const int y = cp.second;
        ObjectNarrowphaseEvent& event = m_objectNarrowphaseEvents[static_cast<size_t>( pairIndex )];

        // Wake a sleeping object only after an energetic awake neighbor proves
        // an actual swept hit or persistent overlap. Underwater-locked sleepers
        // still receive the swept hit timing, but remain static solver anchors.
        if ( m_sleepState[x] || m_sleepState[y] )
        {
            // Quiet awake bodies cannot wake sleepers just by sharing a broadphase cell.
            if ( m_sleepState[x] && !m_sleepState[y] )
            {
                const bool sleepingLocked = IsUnderwaterSleepLocked( collection, bodyStream, x );
                if ( !hasWakeEnergy( y ) )
                {
                    return;
                }
                // Swept impact wakes immediately when time remains; persistent
                // overlap wakes too so sleepers cannot stay frozen after a hit.
                bool wokeBySweptImpact = false;
                if ( m_timeRemaining[y] > 0.0f )
                {
                    GameModel::ObjectSweepResult sweep = m_gameModels[y].SweepGameModel( m_gameModels[x], m_timeRemaining[y] );
                    if ( sweep.hit )
                    {
                        const float availableTime = m_timeRemaining[y];
                        float colTime = refineObjectSweepContactTime( y, x, sweep.collisionTime, availableTime );
                        Physics::PhysicsPipelineRecord record;
                        record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
                        record.bodyA = y;
                        record.bodyB = x;
                        record.point = ( m_gameModels[y].GetPosition() + m_gameModels[x].GetPosition() ) * 0.5f;
                        record.scalarA = colTime;
                        record.scalarB = availableTime;
                        recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
                        emitObjectCollisionTimeEvent( event, y, x, colTime, availableTime );

                        m_gameModels[y].UpdatePosition( colTime );
                        m_timeRemaining[y] = (std::max)( 0.0f, m_timeRemaining[y] - colTime );
                        if ( !sleepingLocked )
                        {
                            wakeSleepingModel( x );
                        }
                        wokeBySweptImpact = true;
                        markObjectVisualEvent( event, x, y );
                    }
                }
                if ( !wokeBySweptImpact && hasPersistentWakeContact( y, x ) )
                {
                    Physics::PhysicsPipelineRecord record;
                    record.stage = Physics::PhysicsPipelineStage::WakeDecision;
                    record.bodyA = y;
                    record.bodyB = x;
                    record.point = ( m_gameModels[y].GetPosition() + m_gameModels[x].GetPosition() ) * 0.5f;
                    record.scalarA = sleepingLocked ? 0.0f : 1.0f;
                    recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::WakeDecision, record );

                    if ( !sleepingLocked )
                    {
                        wakeSleepingModel( x );
                    }
                    markObjectVisualEvent( event, x, y );
                }
                return;
            }
            else if ( m_sleepState[y] && !m_sleepState[x] )
            {
                const bool sleepingLocked = IsUnderwaterSleepLocked( collection, bodyStream, y );
                if ( !hasWakeEnergy( x ) )
                {
                    return;
                }
                bool wokeBySweptImpact = false;
                if ( m_timeRemaining[x] > 0.0f )
                {
                    GameModel::ObjectSweepResult sweep = m_gameModels[x].SweepGameModel( m_gameModels[y], m_timeRemaining[x] );
                    if ( sweep.hit )
                    {
                        const float availableTime = m_timeRemaining[x];
                        float colTime = refineObjectSweepContactTime( x, y, sweep.collisionTime, availableTime );
                        Physics::PhysicsPipelineRecord record;
                        record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
                        record.bodyA = x;
                        record.bodyB = y;
                        record.point = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
                        record.scalarA = colTime;
                        record.scalarB = availableTime;
                        recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
                        emitObjectCollisionTimeEvent( event, x, y, colTime, availableTime );

                        m_gameModels[x].UpdatePosition( colTime );
                        m_timeRemaining[x] = (std::max)( 0.0f, m_timeRemaining[x] - colTime );
                        if ( !sleepingLocked )
                        {
                            wakeSleepingModel( y );
                        }
                        wokeBySweptImpact = true;
                        markObjectVisualEvent( event, x, y );
                    }
                }
                if ( !wokeBySweptImpact && hasPersistentWakeContact( x, y ) )
                {
                    Physics::PhysicsPipelineRecord record;
                    record.stage = Physics::PhysicsPipelineStage::WakeDecision;
                    record.bodyA = x;
                    record.bodyB = y;
                    record.point = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
                    record.scalarA = sleepingLocked ? 0.0f : 1.0f;
                    recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::WakeDecision, record );

                    if ( !sleepingLocked )
                    {
                        wakeSleepingModel( y );
                    }
                    markObjectVisualEvent( event, x, y );
                }
                return;
            }
            else
            {
                // Both bodies are sleeping; there is no awake energy to produce a wake event.
                return;
            }
        }

        if ( m_timeRemaining[x] <= 0.0f || m_timeRemaining[y] <= 0.0f )
        {
            return;
        }

        float availableTime = (std::min)( m_timeRemaining[x], m_timeRemaining[y] );
        GameModel::ObjectSweepResult sweep = m_gameModels[x].SweepGameModel( m_gameModels[y], availableTime );

        if ( sweep.hit )
        {
            float colTime = refineObjectSweepContactTime( x, y, sweep.collisionTime, availableTime );
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
            record.bodyA = x;
            record.bodyB = y;
            record.point = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
            record.scalarA = colTime;
            record.scalarB = availableTime;
            recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
            emitObjectCollisionTimeEvent( event, x, y, colTime, availableTime );

            m_gameModels[x].UpdatePosition( colTime );
            m_gameModels[y].UpdatePosition( colTime );
            m_timeRemaining[x] = (std::max)( 0.0f, m_timeRemaining[x] - colTime );
            m_timeRemaining[y] = (std::max)( 0.0f, m_timeRemaining[y] - colTime );

            // Object/object CCD only advances to the contact candidate. The
            // persistent Catto rows below own velocity response and cache storage.
            markObjectVisualEvent( event, x, y );
            writeObjectCollisionCellEvent( event, x, y );
        }
        else
        {
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SweptObjectMiss;
            record.bodyA = x;
            record.bodyB = y;
            record.point = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
            record.scalarA = availableTime;
            recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectMiss, record );
        }
    };

    auto processObjectNarrowphaseIsland = [&]( int islandIndex )
    {
        const ObjectNarrowphaseIsland& island = m_objectNarrowphaseIslands[static_cast<size_t>( islandIndex )];
        for ( int pairIndex : island.pairIndices )
        {
            processObjectNarrowphasePair( pairIndex );
        }
    };

    const int islandCount = static_cast<int>( m_objectNarrowphaseIslands.size() );
    const bool hasSpreadOutNarrowphaseIslands =
        islandCount > 0 &&
        candidatePairCount <= islandCount * PHYSICS_NARROWPHASE_PARALLEL_MAX_AVG_PAIRS_PER_ISLAND;
    if ( Cfg().physicsParallel &&
         islandCount >= PHYSICS_NARROWPHASE_PARALLEL_MIN_ISLANDS &&
         candidatePairCount >= PHYSICS_NARROWPHASE_PARALLEL_MIN_PAIRS &&
         hasSpreadOutNarrowphaseIslands )
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/IslandWorkerDispatch" );
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor( 0,
                                                                       islandCount,
                                                                       processObjectNarrowphaseIsland,
                                                                       PHYSICS_NARROWPHASE_PARALLEL_MIN_ISLANDS );
    }
    else
    {
        for ( int islandIndex = 0; islandIndex < islandCount; ++islandIndex )
        {
            processObjectNarrowphaseIsland( islandIndex );
        }
    }

    for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
    {
        const ObjectNarrowphaseEvent& event = m_objectNarrowphaseEvents[static_cast<size_t>( pairIndex )];
        if ( event.hasPipelineRecord )
        {
            RecordPhysicsPipelineStage( event.pipelineRecord );
        }
        if ( event.emitCollisionTime )
        {
            EmitPhysicsCollisionTime( collection,
                                      "object",
                                      event.collisionTimeBodyA,
                                      event.collisionTimeBodyB,
                                      event.collisionTime,
                                      event.availableTime );
        }
        if ( event.markVisualContact )
        {
            MarkCollisionVisualContact( event.visualBodyA );
            MarkCollisionVisualContact( event.visualBodyB );
        }
        if ( event.hasCollisionCellKey )
        {
            m_collisionCellKeys.push_back( event.collisionCellKey );
        }
    }
    PROFILE_END( "Frame/Physics/Narrowphase" );

    // Terrain phase ownership:
    //   1. Keep swept terrain detection here so fast bodies still stop at the
    //      correct time of impact.
    //   2. Convert the hit into a terrain manifold only. Do not apply impulses
    //      or terrain-only velocity response in this phase.
    //   3. Leave remaining-time integration and all normal/friction response to
    //      the shared persistent contact rows below.
    PROFILE_BEGIN( "Frame/Physics/Terrain" );
    PROFILE_BEGIN( "Frame/Physics/Terrain/Detect" );
    auto detectTerrainAt = [&]( int x )
    {
        TerrainDetectionCandidate& candidate = m_terrainDetectionCandidates[static_cast<size_t>( x )];
        if ( bodyStream.isFixed[x] )
        {
            return;
        }
        if ( m_sleepState[x] || m_timeRemaining[x] <= 0.0f )
        {
            return;
        }

        candidate.availableTime = m_timeRemaining[x];
        candidate.collisionTime = m_gameModels[x].CollisionDetectTerrain( candidate.availableTime );
        candidate.tested = 1;
    };

    auto commitTerrainCandidate = [&]( int x, float availableTime, float colTime )
    {
        if ( m_gameModels[x].IsResponseRequired() )
        {
            m_gameModels[x].UpdatePosition( colTime );
            const float remainingTime = (std::max)( 0.0f, availableTime - colTime );
            // BuildTerrainContactManifold is the handoff from terrain-specific
            // collision data to solver-neutral contact geometry. The old
            // response-required flag is now just a detection latch; clear it
            // once the manifold is captured so no later path can replay terrain
            // response work.
            Physics::TerrainContactManifold manifold;
            const bool hasManifold = m_gameModels[x].BuildTerrainContactManifold( x, colTime, availableTime, manifold );
            m_gameModels[x].ClearResponseRequired();

            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::TerrainHit;
            record.bodyA = x;
            record.bodyB = TERRAIN_BODY_INDEX;
            record.point = hasManifold ? manifold.points[0].point : m_gameModels[x].GetPosition();
            record.normal = hasManifold ? manifold.normal : ZERO_VECTOR;
            record.scalarA = colTime;
            record.scalarB = hasManifold && manifold.supportsRestingPolicy ? 1.0f : 0.0f;
            record.scalarC = hasManifold ? static_cast<float>( manifold.pointCount ) : 0.0f;
            RecordPhysicsPipelineStage( record );
            EmitPhysicsCollisionTime( collection, "terrain", x, -1, colTime, availableTime );

            if ( hasManifold )
            {
                m_terrainContactManifolds.push_back( manifold );
                if ( manifold.supportsRestingPolicy )
                {
                    m_sleepSupportedThisFrame[x] = 1;
                }
                else
                {
                    m_sleepInhibitedThisFrame[x] = 1;
                }
            }
            else
            {
                m_sleepInhibitedThisFrame[x] = 1;
            }
            MarkCollisionVisualContact( x );
            m_timeRemaining[x] = remainingTime;
        }
    };

    m_terrainDetectionCandidates.assign( static_cast<size_t>( modelCount ), TerrainDetectionCandidate() );
    if ( Cfg().physicsParallel )
    {
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor( 0, modelCount, detectTerrainAt, PHYSICS_PARALLEL_MIN_BODIES );
    }
    else
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            detectTerrainAt( x );
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        const TerrainDetectionCandidate& candidate = m_terrainDetectionCandidates[static_cast<size_t>( x )];
        if ( candidate.tested )
        {
            commitTerrainCandidate( x, candidate.availableTime, candidate.collisionTime );
        }
    }
    PROFILE_END( "Frame/Physics/Terrain/Detect" );
    PROFILE_END( "Frame/Physics/Terrain" );

    m_contactSolver.Solve( *this, collection, dt );
    // Object contacts are converted into stack support only after terrain
    // response has had a chance to seed true support for this frame.
    PropagateSleepSupport( collection );

    // Integrate remaining time for awake models
    PROFILE_BEGIN( "Frame/Physics/Integrate" );
    auto integrateRemainingAt = [&]( int x )
    {
        if ( bodyStream.isFixed[x] )
        {
            return;
        }
        if ( m_sleepState[x] )
        {
            return;
        }

        if ( m_timeRemaining[x] > 0.0f )
        {
            m_gameModels[x].UpdatePosition( m_timeRemaining[x] );
        }
    };

    if ( Cfg().physicsParallel )
    {
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor( 0, modelCount, integrateRemainingAt, PHYSICS_PARALLEL_MIN_BODIES );
    }
    else
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            integrateRemainingAt( x );
        }
    }

    // Build sleep islands from the persistent contact graph. Sleep counters are
    // tracked per body, but the final transition is island-level: connected awake
    // bodies deactivate together only if the whole island is quiet and rooted in
    // credible support.
    //
    // Important nuance:
    //   "Supported" is an island property, not a demand that every body directly
    //   touch terrain. A box can be quiet and physically constrained by the side
    //   of a grounded pile. Requiring that specific box to also pass terrain
    //   support classification creates the bad varied-scene wedge: terrain says
    //   "not a stable footprint", object contacts keep the box from falling, and
    //   the sleep gate has no way out. The anchor pass below keeps the original
    //   safety rule for floating/mid-air islands: at least one member must still
    //   be terrain-supported, fixed, or already sleeping from a previous proven
    //   support state.
    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    m_sleepIslandHasAwake.assign( modelCount, 0 );
    m_sleepIslandHasSupportAnchor.assign( modelCount, 0 );
    m_sleepIslandEligible.assign( modelCount, 1 );
    m_sleepIslandCanSleep.assign( modelCount, 1 );
    for ( int i = 0; i < modelCount; ++i )
    {
        m_sleepIslandParent[i] = i;
    }

    auto findIsland = [&]( int index ) -> int
    {
        // Union-find lookup with path compression. In plain terms: every body in
        // a connected contact group points to the same representative root, so
        // the sleep system can make one decision for the whole group.
        int root = index;
        while ( m_sleepIslandParent[root] != root )
        {
            root = m_sleepIslandParent[root];
        }
        while ( m_sleepIslandParent[index] != index )
        {
            int parent = m_sleepIslandParent[index];
            m_sleepIslandParent[index] = root;
            index = parent;
        }
        return root;
    };

    auto unionIslands = [&]( int a, int b )
    {
        // Merge two contact groups. Rank keeps the tree shallow so repeated
        // findIsland calls stay cheap during large stacks.
        int rootA = findIsland( a );
        int rootB = findIsland( b );
        if ( rootA == rootB )
        {
            return;
        }

        if ( m_sleepIslandRank[rootA] < m_sleepIslandRank[rootB] )
        {
            std::swap( rootA, rootB );
        }
        m_sleepIslandParent[rootB] = rootA;
        if ( m_sleepIslandRank[rootA] == m_sleepIslandRank[rootB] )
        {
            ++m_sleepIslandRank[rootA];
        }
    };

    for ( const PersistentContact& c : m_persistentContacts )
    {
        // Persistent contacts are the solver's current dynamic contact graph, so
        // they are the natural edges for island sleep. Sleeping bodies still act
        // as graph anchors, but only awake bodies below participate in the current
        // eligibility and counter checks.
        if ( c.bodyA >= 0 && c.bodyA < modelCount && c.bodyB >= 0 && c.bodyB < modelCount )
        {
            unionIslands( c.bodyA, c.bodyB );
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        const int root = findIsland( x );

        // A support anchor is evidence that this island is not a free-floating
        // collection of bodies that merely became numerically quiet. Terrain
        // support remains the usual anchor. Fixed objects and sleeping bodies are
        // also valid anchors: fixed objects are immovable world geometry, and a
        // sleeping dynamic body could only have reached sleep after satisfying the
        // same support gate in an earlier frame.
        if ( bodyStream.isFixed[x] ||
             ( x < static_cast<int>( m_sleepState.size() ) && m_sleepState[x] != 0 ) ||
             ( x < static_cast<int>( m_sleepSupportedThisFrame.size() ) && m_sleepSupportedThisFrame[x] != 0 ) )
        {
            m_sleepIslandHasSupportAnchor[root] = 1;
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( bodyStream.isFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        m_sleepIslandHasAwake[root] = 1;

        const Vector3& vel = m_gameModels[x].GetVelocity();
        const Vector3& omega = m_gameModels[x].GetAngularVelocity();
        float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
        bool quiet = speedSq < SLEEP_LINEAR_SQ && omegaSq < SLEEP_ANGULAR_SQ;
        bool supported = x < static_cast<int>( m_sleepSupportedThisFrame.size() ) && m_sleepSupportedThisFrame[x] != 0;
        bool hasObjectContact = x < static_cast<int>( m_persistentContactCounts.size() ) && m_persistentContactCounts[x] > 0;
        bool islandHasSupportAnchor = m_sleepIslandHasSupportAnchor[root] != 0;

        // A quiet body in a grounded object-contact island is supported even if
        // the body itself is side-wedged or touching terrain on an edge/point.
        // This is deliberately narrower than "any contact means support":
        //
        //   * quiet keeps active impacts and real toppling awake;
        //   * hasObjectContact requires the body to be constrained by the island;
        //   * islandHasSupportAnchor keeps floating piles from becoming sleepers.
        //
        // Marking the body supported here also keeps SkullScope diagnostics honest:
        // the body is not terrain-supported, but it is supported for deactivation
        // by a contact island rooted in credible support.
        if ( !supported && quiet && hasObjectContact && islandHasSupportAnchor )
        {
            m_sleepSupportedThisFrame[x] = 1;
            supported = true;
        }

        // Terrain can still inhibit sleep for edge/point contacts when that
        // contact is the only apparent support. In a quiet anchored island,
        // though, the same terrain rejection must not be an infinite veto: the
        // object solver may have wedged the body against neighbors so it cannot
        // fall into a more stable footprint. The island anchor and object-contact
        // checks above are the escape hatch for that exact low-energy state.
        bool terrainInhibitBlocksSleep = m_sleepInhibitedThisFrame[x] != 0 &&
                                         !( quiet && hasObjectContact && islandHasSupportAnchor );

        // Modern sleep is still velocity based, but Skullbonez also requires
        // credible island support so unsupported gravity bodies cannot become
        // numerically quiet for a few frames while visibly floating.
        if ( !quiet || !supported || terrainInhibitBlocksSleep )
        {
            m_sleepIslandEligible[root] = 0;
        }

        Physics::PhysicsPipelineRecord record;
        record.stage = Physics::PhysicsPipelineStage::SleepIslandDecision;
        record.bodyA = x;
        record.bodyB = root;
        record.point = m_gameModels[x].GetPosition();
        record.scalarA = quiet ? 1.0f : 0.0f;
        record.scalarB = supported ? 1.0f : 0.0f;
        record.scalarC = terrainInhibitBlocksSleep ? 1.0f : 0.0f;
        RecordPhysicsPipelineStage( record );
    }

    if ( !m_sleepEnabled )
    {
        std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
        m_sleepIslandCanSleep.assign( modelCount, 0 );
        m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
        PROFILE_END( "Frame/Physics/Integrate" );
        return;
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( bodyStream.isFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepIslandHasAwake[root] && m_sleepIslandEligible[root] )
        {
            if ( m_sleepCounter[x] < SLEEP_FRAMES )
            {
                ++m_sleepCounter[x];
            }
        }
        else
        {
            m_sleepCounter[x] = 0;
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( bodyStream.isFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepCounter[x] < SLEEP_FRAMES )
        {
            // Every awake body in an eligible island must accumulate the full
            // quiet-frame count before any body in that island is deactivated.
            m_sleepIslandCanSleep[root] = 0;
        }
    }

    m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( bodyStream.isFixed[x] )
        {
            continue;
        }
        if ( !m_sleepState[x] || m_sleepIslandVisualId[x] == 0 )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepIslandAssignedVisualId[root] == 0 )
        {
            m_sleepIslandAssignedVisualId[root] = m_sleepIslandVisualId[x];
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( bodyStream.isFixed[x] )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepIslandHasAwake[root] && m_sleepIslandEligible[root] && m_sleepIslandCanSleep[root] )
        {
            if ( m_sleepIslandAssignedVisualId[root] == 0 )
            {
                m_sleepIslandAssignedVisualId[root] = m_nextSleepIslandVisualId++;
                if ( m_nextSleepIslandVisualId <= 0 )
                {
                    m_nextSleepIslandVisualId = 1;
                }
            }
            m_sleepState[x] = 1;
            m_sleepIslandVisualId[x] = m_sleepIslandAssignedVisualId[root];
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SleepIslandDecision;
            record.bodyA = x;
            record.bodyB = root;
            record.point = m_gameModels[x].GetPosition();
            record.scalarA = 1.0f;
            record.scalarB = static_cast<float>( m_sleepIslandAssignedVisualId[root] );
            record.scalarC = static_cast<float>( m_sleepCounter[x] );
            RecordPhysicsPipelineStage( record );
            // Zeroing velocities at the island sleep transition prevents tiny
            // residual solver drift from reappearing when the body later wakes.
            m_gameModels[x].SetLinearVelocity( Math::Vector::ZERO_VECTOR );
            m_gameModels[x].SetAngularVelocity( Math::Vector::ZERO_VECTOR );
            LockUnderwaterSleeperIfReady( collection, bodyStream, x );
        }
    }
    PROFILE_END( "Frame/Physics/Integrate" );
}


const Math::CollisionDetection::SpatialGrid& PhysicsWorld::GetSpatialGrid() const
{
    return m_spatialGrid;
}


const std::vector<int64_t>& PhysicsWorld::GetCollisionCellKeys() const
{
    return m_collisionCellKeys;
}


const std::vector<uint8_t>& PhysicsWorld::GetCollisionVisualContacts() const
{
    return m_collisionVisualContacts;
}


const std::vector<uint8_t>& PhysicsWorld::GetSleepStates() const
{
    return m_sleepState;
}


const std::vector<int>& PhysicsWorld::GetSleepIslandVisualIds() const
{
    return m_sleepIslandVisualId;
}


const std::vector<uint8_t>& PhysicsWorld::GetSleepSupportedStates() const
{
    return m_sleepSupportedThisFrame;
}


const std::vector<uint8_t>& PhysicsWorld::GetSleepInhibitedStates() const
{
    return m_sleepInhibitedThisFrame;
}


const std::vector<PhysicsDebugContact>& PhysicsWorld::GetPhysicsDebugContacts() const
{
    return m_physicsDebugContacts;
}


const std::vector<PhysicsPipelineRecord>& PhysicsWorld::GetPhysicsPipelineTrace() const
{
    return m_physicsPipelineTrace;
}
