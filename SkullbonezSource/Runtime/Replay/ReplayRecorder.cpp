/*
File: SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
Purpose:
  Captures bounded replay presentation and solver-state samples.

Mental model:
  These recorders observe committed simulation state. They must not mutate
  bodies, physics caches, renderer resources, or UI state; capture enabled
  should only add bounded CPU memory use and optional hash-log writes.

Glossary:
  Presentation sample: Render-facing pose/state captured from a frame.
  Solver sample: Physics-facing state retained for rollback and diagnostics.
  Hash log: Deterministic per-sample digest stream used to compare replay output.
  Retention window: Maximum in-memory duration retained by the ring buffers.

Invariants:
  - Recording observes committed state and never advances simulation.
  - Hash packing must stay deterministic across machines and configurations.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h
*/
#include "ReplayRecorder.h"

#include "../CameraCollection.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Basics;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Environment::WorldEnvironment;
using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::GameObjects::GameModelCollection;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::PhysicsDebugContact;
namespace Physics = SkullbonezCore::Physics;

namespace
{
constexpr int REPLAY_TICKS_PER_SECOND = 120;
constexpr int REPLAY_MIN_SECONDS = 1;
constexpr int REPLAY_MAX_SECONDS = 600;
constexpr uint64_t FNV64_OFFSET = 14695981039346656037ull;
constexpr uint64_t FNV64_PRIME = 1099511628211ull;

uint64_t HashByte( uint64_t hash, uint8_t value )
{
    hash ^= static_cast<uint64_t>( value );
    hash *= FNV64_PRIME;
    return hash;
}

uint64_t HashBytes( uint64_t hash, const void* bytes, std::size_t byteCount )
{
    const uint8_t* cursor = static_cast<const uint8_t*>( bytes );
    for ( std::size_t i = 0; i < byteCount; ++i )
    {
        hash = HashByte( hash, cursor[i] );
    }
    return hash;
}

uint64_t HashUint32( uint64_t hash, uint32_t value )
{
    return HashBytes( hash, &value, sizeof( value ) );
}

uint64_t HashUint64( uint64_t hash, uint64_t value )
{
    return HashBytes( hash, &value, sizeof( value ) );
}

uint64_t HashInt64( uint64_t hash, int64_t value )
{
    return HashBytes( hash, &value, sizeof( value ) );
}

uint64_t HashSize( uint64_t hash, std::size_t value )
{
    return HashUint64( hash, static_cast<uint64_t>( value ) );
}

uint64_t HashInt( uint64_t hash, int value )
{
    const int32_t packed = static_cast<int32_t>( value );
    return HashBytes( hash, &packed, sizeof( packed ) );
}

uint64_t HashBool( uint64_t hash, bool value )
{
    return HashByte( hash, value ? static_cast<uint8_t>( 1 ) : static_cast<uint8_t>( 0 ) );
}

uint64_t HashFloat( uint64_t hash, float value )
{
    // Invariant: hash the exact IEEE bytes, not formatted text or rounded
    // values. Replay validation expects byte-exact drift detection.
    uint32_t packed = 0;
    std::memcpy( &packed, &value, sizeof( packed ) );
    return HashUint32( hash, packed );
}

uint64_t HashVector( uint64_t hash, const Vector3& value )
{
    hash = HashFloat( hash, value.x );
    hash = HashFloat( hash, value.y );
    hash = HashFloat( hash, value.z );
    return hash;
}

uint64_t HashOrientation( uint64_t hash, const float orientation[4] )
{
    hash = HashFloat( hash, orientation[0] );
    hash = HashFloat( hash, orientation[1] );
    hash = HashFloat( hash, orientation[2] );
    hash = HashFloat( hash, orientation[3] );
    return hash;
}

uint64_t HashFloatVector( uint64_t hash, const std::vector<float>& values )
{
    hash = HashSize( hash, values.size() );
    for ( float value : values )
    {
        hash = HashFloat( hash, value );
    }
    return hash;
}

uint64_t HashUint8Vector( uint64_t hash, const std::vector<uint8_t>& values )
{
    hash = HashSize( hash, values.size() );
    for ( uint8_t value : values )
    {
        hash = HashByte( hash, value );
    }
    return hash;
}

uint64_t HashUint16Vector( uint64_t hash, const std::vector<uint16_t>& values )
{
    hash = HashSize( hash, values.size() );
    for ( uint16_t value : values )
    {
        hash = HashBytes( hash, &value, sizeof( value ) );
    }
    return hash;
}

uint64_t HashIntVector( uint64_t hash, const std::vector<int>& values )
{
    hash = HashSize( hash, values.size() );
    for ( int value : values )
    {
        hash = HashInt( hash, value );
    }
    return hash;
}

uint64_t HashInt64Vector( uint64_t hash, const std::vector<int64_t>& values )
{
    hash = HashSize( hash, values.size() );
    for ( int64_t value : values )
    {
        hash = HashInt64( hash, value );
    }
    return hash;
}

uint64_t HashPairVector( uint64_t hash, const std::vector<std::pair<int, int>>& values )
{
    hash = HashSize( hash, values.size() );
    for ( const std::pair<int, int>& value : values )
    {
        hash = HashInt( hash, value.first );
        hash = HashInt( hash, value.second );
    }
    return hash;
}

uint64_t HashWorld( uint64_t hash, const ReplayWorldPresentationSample& world )
{
    hash = HashFloat( hash, world.gravity );
    hash = HashFloat( hash, world.fluidHeight );
    hash = HashFloat( hash, world.fluidDensity );
    hash = HashBool( hash, world.waterHidden );
    hash = HashBool( hash, world.terrainHidden );
    hash = HashBool( hash, world.fixedStep );
    hash = HashBool( hash, world.scenePhysicsEnabled );
    hash = HashBool( hash, world.sceneTextEnabled );
    return hash;
}

uint64_t HashTornadoConfig( uint64_t hash, const Physics::TornadoFieldConfig& config )
{
    hash = HashBool( hash, config.enabled );
    hash = HashBool( hash, config.visualizeVelocityField );
    hash = HashVector( hash, config.center );
    hash = HashFloat( hash, config.radius );
    hash = HashFloat( hash, config.height );
    hash = HashFloat( hash, config.inwardAcceleration );
    hash = HashFloat( hash, config.swirlAcceleration );
    hash = HashFloat( hash, config.liftAcceleration );
    hash = HashFloat( hash, config.ejectAcceleration );
    hash = HashFloat( hash, config.ejectUpAcceleration );
    hash = HashFloat( hash, config.ejectBand );
    hash = HashFloat( hash, config.minCaptureSeconds );
    hash = HashFloat( hash, config.ejectCooldownSeconds );
    hash = HashFloat( hash, config.maxDeltaVelocity );
    return hash;
}


uint64_t HashTornadoSystemConfig( uint64_t hash, const Physics::TornadoSystemConfig& config )
{
    hash = HashBool( hash, config.enabled );
    hash = HashBool( hash, config.visualizeVelocityField );
    hash = HashSize( hash, config.vortices.size() );
    for ( const Physics::TornadoVortexConfig& vortex : config.vortices )
    {
        hash = HashTornadoConfig( hash, vortex.field );
        hash = HashFloat( hash, vortex.spawnSeconds );
        hash = HashFloat( hash, vortex.timeToLiveSeconds );
        hash = HashFloat( hash, vortex.growSeconds );
        hash = HashFloat( hash, vortex.shrinkSeconds );
        hash = HashFloat( hash, vortex.driftRadius );
        hash = HashFloat( hash, vortex.driftSpeed );
        hash = HashFloat( hash, vortex.driftPhase );
        hash = HashFloat( hash, vortex.repulsionRadius );
        hash = HashFloat( hash, vortex.repulsionStrength );
    }
    return hash;
}


uint64_t HashLauncherVisual( uint64_t hash, const ReplayLauncherVisualSample& visual )
{
    hash = HashInt( hash, visual.nextRayLine );
    hash = HashInt( hash, visual.nextLaserShot );
    hash = HashInt( hash, static_cast<int>( visual.fireMode ) );
    hash = HashBool( hash, visual.visualizeRays );
    hash = HashFloat( hash, visual.impulseStrength );
    hash = HashFloat( hash, visual.projectileSpeed );

    hash = HashSize( hash, visual.rayLines.size() );
    for ( const ReplayRayCastLineSample& line : visual.rayLines )
    {
        hash = HashVector( hash, line.start );
        hash = HashVector( hash, line.end );
        hash = HashFloat( hash, line.ageSeconds );
        hash = HashBool( hash, line.active );
        hash = HashBool( hash, line.hit );
    }

    hash = HashSize( hash, visual.laserShots.size() );
    for ( const LauncherLaserShotSnapshot& shot : visual.laserShots )
    {
        hash = HashVector( hash, shot.start );
        hash = HashVector( hash, shot.end );
        hash = HashVector( hash, shot.cameraRight );
        hash = HashVector( hash, shot.cameraUp );
        hash = HashFloat( hash, shot.ageSeconds );
        hash = HashFloat( hash, shot.lifetimeSeconds );
        hash = HashBool( hash, shot.active );
        hash = HashBool( hash, shot.hit );
    }
    return hash;
}

uint64_t HashLauncherControlState( uint64_t hash, const ReplayLauncherVisualSample& visual )
{
    hash = HashInt( hash, static_cast<int>( visual.fireMode ) );
    hash = HashFloat( hash, visual.impulseStrength );
    hash = HashFloat( hash, visual.projectileSpeed );
    return hash;
}

ReplayBodyShapeKind ShapeKindForModel( const GameModel& model )
{
    if ( model.IsSphere() )
    {
        return ReplayBodyShapeKind::Sphere;
    }
    if ( model.IsBox() )
    {
        return ReplayBodyShapeKind::Box;
    }
    if ( model.IsConvexHull() )
    {
        return ReplayBodyShapeKind::ConvexHull;
    }
    return ReplayBodyShapeKind::Unknown;
}

uint16_t SaturatingUint16( std::size_t value )
{
    return value > 0xffffu ? 0xffffu : static_cast<uint16_t>( value );
}

ReplayBranchInfo NormalizeBranchInfo( const ReplayBranchInfo& branch )
{
    ReplayBranchInfo normalized = branch;
    if ( normalized.branchId == 0 )
    {
        normalized.branchId = 1;
    }
    return normalized;
}

void IncrementBodyContactSummary( int bodyIndex,
                                  float penetration,
                                  float normalImpulse,
                                  std::vector<uint16_t>& contactCounts,
                                  std::vector<float>& maxPenetrations,
                                  std::vector<float>& normalImpulseSums )
{
    if ( bodyIndex < 0 || bodyIndex >= static_cast<int>( contactCounts.size() ) )
    {
        return;
    }

    const std::size_t index = static_cast<std::size_t>( bodyIndex );
    if ( contactCounts[index] < 0xffffu )
    {
        ++contactCounts[index];
    }
    maxPenetrations[index] = (std::max)( maxPenetrations[index], penetration );
    normalImpulseSums[index] += normalImpulse;
}

uint64_t HashBodySample( uint64_t hash, const ReplayBodyPresentationSample& body )
{
    hash = HashUint32( hash, body.id.value );
    hash = HashInt( hash, body.modelIndex );
    hash = HashInt( hash, static_cast<int>( body.shapeKind ) );
    hash = HashVector( hash, body.position );
    hash = HashOrientation( hash, body.orientation );
    hash = HashVector( hash, body.linearVelocity );
    hash = HashVector( hash, body.angularVelocity );
    hash = HashFloat( hash, body.mass );
    hash = HashBool( hash, body.fixed );
    hash = HashBool( hash, body.sleeping );
    hash = HashBool( hash, body.sleepSupported );
    hash = HashBool( hash, body.sleepInhibited );
    hash = HashBool( hash, body.collisionContact );
    hash = HashInt( hash, body.sleepIslandVisualId );
    hash = HashInt( hash, static_cast<int>( body.contactCount ) );
    hash = HashFloat( hash, body.maxPenetration );
    hash = HashFloat( hash, body.normalImpulseSum );
    return hash;
}

uint64_t HashSolverBodyPresentationFields( uint64_t hash, const ReplaySolverBodySample& body )
{
    hash = HashUint32( hash, body.id.value );
    hash = HashInt( hash, body.modelIndex );
    hash = HashInt( hash, static_cast<int>( body.shapeKind ) );
    hash = HashVector( hash, body.position );
    hash = HashOrientation( hash, body.orientation );
    hash = HashVector( hash, body.linearVelocity );
    hash = HashVector( hash, body.angularVelocity );
    hash = HashFloat( hash, body.mass );
    hash = HashBool( hash, body.fixed );
    hash = HashBool( hash, body.sleeping );
    hash = HashBool( hash, body.sleepSupported );
    hash = HashBool( hash, body.sleepInhibited );
    hash = HashBool( hash, body.collisionContact );
    hash = HashInt( hash, body.sleepIslandVisualId );
    hash = HashInt( hash, static_cast<int>( body.contactCount ) );
    hash = HashFloat( hash, body.maxPenetration );
    hash = HashFloat( hash, body.normalImpulseSum );
    return hash;
}

uint64_t HashSolverBodySample( uint64_t hash, const ReplaySolverBodySample& body )
{
    hash = HashSolverBodyPresentationFields( hash, body );
    hash = HashFloat( hash, body.inverseMass );
    hash = HashVector( hash, body.rotationalInertia );
    hash = HashVector( hash, body.inverseRotationalInertia );
    return hash;
}

uint64_t HashPersistentContact( uint64_t hash, const ReplaySolverPersistentContactSample& contact )
{
    hash = HashInt( hash, contact.bodyA );
    hash = HashInt( hash, contact.bodyB );
    hash = HashUint32( hash, contact.featureId );
    hash = HashInt64( hash, contact.key );
    hash = HashVector( hash, contact.normal );
    hash = HashVector( hash, contact.tangent1 );
    hash = HashVector( hash, contact.tangent2 );
    hash = HashVector( hash, contact.rA );
    hash = HashVector( hash, contact.rB );
    hash = HashFloat( hash, contact.penetration );
    hash = HashFloat( hash, contact.normalMass );
    hash = HashFloat( hash, contact.tangentMass1 );
    hash = HashFloat( hash, contact.tangentMass2 );
    hash = HashFloat( hash, contact.bias );
    hash = HashFloat( hash, contact.frictionLimit );
    hash = HashFloat( hash, contact.accN );
    hash = HashFloat( hash, contact.accT1 );
    hash = HashFloat( hash, contact.accT2 );
    hash = HashBool( hash, contact.warmStarted );
    hash = HashBool( hash, contact.isTerrain );
    hash = HashBool( hash, contact.supportsRestingPolicy );
    hash = HashBool( hash, contact.allowsTangentFriction );
    hash = HashBool( hash, contact.normalCoupledFriction );
    hash = HashBool( hash, contact.inhibitsSleep );
    hash = HashByte( hash, contact.manifoldPointCount );
    hash = HashVector( hash, contact.terrainNormal );
    hash = HashFloat( hash, contact.terrainWarmStart );
    return hash;
}

uint64_t HashContactCache( uint64_t hash, const ReplaySolverContactCacheSample& cache )
{
    hash = HashInt64( hash, cache.key );
    hash = HashFloat( hash, cache.accN );
    hash = HashFloat( hash, cache.accT1 );
    hash = HashFloat( hash, cache.accT2 );
    return hash;
}

uint64_t HashSolverStats( uint64_t hash, const ReplaySolverStatsSample& stats )
{
    hash = HashInt( hash, stats.rowCount );
    hash = HashInt( hash, stats.cachePreviousRows );
    hash = HashInt( hash, stats.cacheHits );
    hash = HashInt( hash, stats.cacheMisses );
    hash = HashInt( hash, stats.warmStartedRows );
    hash = HashInt( hash, stats.positionCorrectionRows );
    hash = HashInt( hash, stats.solverIterations );
    hash = HashFloat( hash, stats.positionCorrectionTotal );
    hash = HashFloat( hash, stats.positionCorrectionMax );
    return hash;
}

uint64_t HashPhysicsDebugContact( uint64_t hash, const PhysicsDebugContact& contact )
{
    hash = HashInt( hash, contact.bodyA );
    hash = HashInt( hash, contact.bodyB );
    hash = HashUint32( hash, contact.featureId );
    hash = HashVector( hash, contact.point );
    hash = HashVector( hash, contact.normal );
    hash = HashVector( hash, contact.tangent1 );
    hash = HashVector( hash, contact.tangent2 );
    hash = HashFloat( hash, contact.penetration );
    hash = HashFloat( hash, contact.normalImpulse );
    return hash;
}

uint64_t HashPhysicsPipelineRecord( uint64_t hash, const Physics::PhysicsPipelineRecord& record )
{
    hash = HashInt( hash, static_cast<int>( record.stage ) );
    hash = HashInt( hash, record.bodyA );
    hash = HashInt( hash, record.bodyB );
    hash = HashInt( hash, record.iteration );
    hash = HashUint32( hash, record.featureId );
    hash = HashVector( hash, record.point );
    hash = HashVector( hash, record.normal );
    hash = HashFloat( hash, record.scalarA );
    hash = HashFloat( hash, record.scalarB );
    hash = HashFloat( hash, record.scalarC );
    return hash;
}

uint64_t HashSolverWorldSnapshot( uint64_t hash, const ReplaySolverWorldSnapshot& snapshot )
{
    hash = HashUint32( hash, snapshot.version );
    hash = HashInt( hash, snapshot.modelCount );
    hash = HashInt( hash, snapshot.nextSleepIslandVisualId );
    hash = HashBool( hash, snapshot.sleepEnabled );
    hash = HashBool( hash, snapshot.collisionVisualFrameActive );
    hash = HashTornadoConfig( hash, snapshot.tornadoConfig );
    if ( snapshot.version >= 2 )
    {
        hash = HashTornadoSystemConfig( hash, snapshot.tornadoSystemConfig );
        hash = HashFloat( hash, snapshot.tornadoSystemElapsedSeconds );
    }
    hash = HashFloatVector( hash, snapshot.timeRemaining );
    hash = HashUint8Vector( hash, snapshot.sleepSupportedThisFrame );
    hash = HashUint8Vector( hash, snapshot.sleepInhibitedThisFrame );
    hash = HashUint8Vector( hash, snapshot.sleepState );
    hash = HashUint8Vector( hash, snapshot.sleepCounter );
    hash = HashUint8Vector( hash, snapshot.underwaterSleepLocked );
    hash = HashFloatVector( hash, snapshot.tornadoCaptureSeconds );
    hash = HashFloatVector( hash, snapshot.tornadoEjectCooldownSeconds );
    hash = HashUint8Vector( hash, snapshot.collisionVisualContacts );
    hash = HashIntVector( hash, snapshot.sleepIslandVisualId );
    hash = HashIntVector( hash, snapshot.sleepIslandAssignedVisualId );
    hash = HashPairVector( hash, snapshot.sleepSupportEdges );
    hash = HashIntVector( hash, snapshot.sleepIslandParent );
    hash = HashUint8Vector( hash, snapshot.sleepIslandRank );
    hash = HashUint8Vector( hash, snapshot.sleepIslandHasAwake );
    hash = HashUint8Vector( hash, snapshot.sleepIslandHasSupportAnchor );
    hash = HashUint8Vector( hash, snapshot.sleepIslandEligible );
    hash = HashUint8Vector( hash, snapshot.sleepIslandCanSleep );

    hash = HashSize( hash, snapshot.persistentContacts.size() );
    for ( const ReplaySolverPersistentContactSample& contact : snapshot.persistentContacts )
    {
        hash = HashPersistentContact( hash, contact );
    }

    hash = HashSize( hash, snapshot.persistentContactCache.size() );
    for ( const ReplaySolverContactCacheSample& cache : snapshot.persistentContactCache )
    {
        hash = HashContactCache( hash, cache );
    }

    hash = HashSolverStats( hash, snapshot.solverStats );
    hash = HashUint16Vector( hash, snapshot.persistentContactCounts );
    hash = HashUint16Vector( hash, snapshot.persistentRestingContactCounts );

    hash = HashSize( hash, snapshot.debugContacts.size() );
    for ( const PhysicsDebugContact& contact : snapshot.debugContacts )
    {
        hash = HashPhysicsDebugContact( hash, contact );
    }

    hash = HashSize( hash, snapshot.pipelineTrace.size() );
    for ( const Physics::PhysicsPipelineRecord& record : snapshot.pipelineTrace )
    {
        hash = HashPhysicsPipelineRecord( hash, record );
    }

    hash = HashInt64Vector( hash, snapshot.collisionCellKeys );
    return hash;
}
} // namespace

bool ReplayRecorder::Configure( const ReplayRecorderConfig& config )
{
    m_config = config;
    m_config.retentionSeconds = std::clamp( m_config.retentionSeconds, REPLAY_MIN_SECONDS, REPLAY_MAX_SECONDS );
    m_config.checkpointIntervalFrames = (std::max)( 1, m_config.checkpointIntervalFrames );
    m_config.enabled = m_config.enabled || !m_config.hashLogPath.empty();

    m_hashLog.close();
    m_samples.clear();
    m_checkpoints.clear();
    m_contactCountScratch.clear();
    m_maxPenetrationScratch.clear();
    m_normalImpulseSumScratch.clear();
    m_sampleHead = 0;
    m_sampleCount = 0;
    m_checkpointHead = 0;
    m_checkpointCount = 0;
    m_nextFrameIndex = 0;
    m_totalFramesCaptured = 0;
    m_totalFramesEvicted = 0;
    m_latestStateHash = 0;

    if ( !m_config.enabled )
    {
        return true;
    }

    m_samples.resize( SampleCapacityFromConfig() );
    m_checkpoints.resize( CheckpointCapacityFromConfig() );

    if ( !m_config.hashLogPath.empty() )
    {
        m_hashLog.open( m_config.hashLogPath, std::ios::out | std::ios::trunc );
        if ( !m_hashLog.is_open() )
        {
            fprintf( stderr, "[replay] Failed to open hash log: %s\n", m_config.hashLogPath.c_str() );
            m_config.hashLogPath.clear();
        }
    }

    return true;
}

void ReplayRecorder::ResetTimeline( const char* sceneLabel )
{
    if ( !m_config.enabled )
    {
        return;
    }

    m_sampleHead = 0;
    m_sampleCount = 0;
    m_checkpointHead = 0;
    m_checkpointCount = 0;
    m_nextFrameIndex = 0;
    m_latestStateHash = 0;
    WriteHashLogHeader( sceneLabel );
}

void ReplayRecorder::CaptureFrame( const ReplayCaptureInput& input )
{
    if ( !m_config.enabled || !input.models )
    {
        return;
    }

    ReplayPresentationSample& sample = AcquireSampleSlot();
    sample.frameIndex = m_nextFrameIndex++;
    sample.branch = NormalizeBranchInfo( input.branch );
    sample.eventCursor = input.eventCursor;
    sample.sceneFrame = input.sceneFrame;
    sample.physicsDt = input.physicsDt;
    sample.simulationSeconds = input.physicsDt > 0.0f
                                   ? static_cast<double>( sample.frameIndex ) * static_cast<double>( input.physicsDt )
                                   : input.simulationSeconds;
    sample.world.fixedStep = input.fixedStep;
    sample.world.scenePhysicsEnabled = input.scenePhysicsEnabled;
    sample.world.sceneTextEnabled = input.sceneTextEnabled;
    sample.world.waterHidden = input.waterHidden;
    sample.world.terrainHidden = input.terrainHidden;
    sample.contactCount = 0;
    sample.pipelineRecordCount = 0;
    sample.checkpointBoundary =
        ( sample.frameIndex == 0 ) ||
        ( sample.frameIndex % static_cast<ReplayFrameIndex>( m_config.checkpointIntervalFrames ) == 0 );

    if ( input.world )
    {
        sample.world.gravity = input.world->GetGravity();
        sample.world.fluidHeight = input.world->GetFluidSurfaceHeight();
        sample.world.fluidDensity = input.world->GetFluidDensity();
    }

    if ( input.cameras )
    {
        sample.camera.eye = input.cameras->GetCameraTranslation();
        sample.camera.view = input.cameras->GetCameraView();
        sample.camera.up = input.cameras->GetCameraUp();
    }

    GameModelCollection& models = *input.models;
    std::vector<GameModel>& physicsModels = models.PhysicsModels();
    const std::size_t modelCount = physicsModels.size();
    sample.bodies.clear();
    sample.bodies.reserve( modelCount );

    m_contactCountScratch.assign( modelCount, 0 );
    m_maxPenetrationScratch.assign( modelCount, 0.0f );
    m_normalImpulseSumScratch.assign( modelCount, 0.0f );

    const std::vector<PhysicsDebugContact>& contacts = models.GetPhysicsDebugContacts();
    sample.contactCount = SaturatingUint16( contacts.size() );
    for ( const PhysicsDebugContact& contact : contacts )
    {
        IncrementBodyContactSummary( contact.bodyA,
                                     contact.penetration,
                                     contact.normalImpulse,
                                     m_contactCountScratch,
                                     m_maxPenetrationScratch,
                                     m_normalImpulseSumScratch );
        IncrementBodyContactSummary( contact.bodyB,
                                     contact.penetration,
                                     contact.normalImpulse,
                                     m_contactCountScratch,
                                     m_maxPenetrationScratch,
                                     m_normalImpulseSumScratch );
    }

    sample.pipelineRecordCount = SaturatingUint16( models.GetPhysicsPipelineTrace().size() );

    const std::vector<uint8_t>& sleepStates = models.GetSleepStates();
    const std::vector<uint8_t>& sleepSupportedStates = models.GetSleepSupportedStates();
    const std::vector<uint8_t>& sleepInhibitedStates = models.GetSleepInhibitedStates();
    const std::vector<uint8_t>& collisionContacts = models.GetCollisionVisualContacts();
    const std::vector<int>& sleepIslandIds = models.GetSleepIslandVisualIds();

    uint64_t hash = FNV64_OFFSET;
    hash = HashWorld( hash, sample.world );
    hash = HashInt( hash, static_cast<int>( modelCount ) );
    hash = HashInt( hash, static_cast<int>( sample.contactCount ) );
    hash = HashInt( hash, static_cast<int>( sample.pipelineRecordCount ) );

    for ( std::size_t i = 0; i < modelCount; ++i )
    {
        GameModel& model = physicsModels[i];
        ReplayBodyPresentationSample body;
        body.id.value = model.GetReplayBodyId();
        body.modelIndex = static_cast<int>( i );
        const char* modelName = model.GetName();
        if ( modelName && modelName[0] != '\0' )
        {
            strncpy_s( body.name, sizeof( body.name ), modelName, _TRUNCATE );
        }
        body.shapeKind = ShapeKindForModel( model );
        body.position = model.GetPosition();
        body.linearVelocity = model.GetVelocity();
        body.angularVelocity = model.GetAngularVelocity();
        const Quaternion& orientation = model.GetOrientation();
        orientation.GetComponents( body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
        body.mass = model.GetMass();
        body.fixed = model.IsFixed();
        body.sleeping = i < sleepStates.size() && sleepStates[i] != 0;
        body.sleepSupported = i < sleepSupportedStates.size() && sleepSupportedStates[i] != 0;
        body.sleepInhibited = i < sleepInhibitedStates.size() && sleepInhibitedStates[i] != 0;
        body.collisionContact = i < collisionContacts.size() && collisionContacts[i] != 0;
        body.sleepIslandVisualId = i < sleepIslandIds.size() ? sleepIslandIds[i] : 0;
        body.contactCount = i < m_contactCountScratch.size() ? m_contactCountScratch[i] : 0;
        body.maxPenetration = i < m_maxPenetrationScratch.size() ? m_maxPenetrationScratch[i] : 0.0f;
        body.normalImpulseSum = i < m_normalImpulseSumScratch.size() ? m_normalImpulseSumScratch[i] : 0.0f;

        hash = HashBodySample( hash, body );
        sample.bodies.push_back( body );
    }

    sample.stateHash = hash;
    m_latestStateHash = hash;
    ++m_totalFramesCaptured;

    if ( sample.checkpointBoundary )
    {
        StoreCheckpointSummary( sample );
    }
    WriteHashLogRow( sample );
}

void ReplayRecorder::FlushHashLog()
{
    if ( m_hashLog.is_open() )
    {
        m_hashLog.flush();
    }
}

bool ReplayRecorder::IsEnabled() const
{
    return m_config.enabled;
}

ReplayRecorderStats ReplayRecorder::GetStats() const
{
    ReplayRecorderStats stats;
    stats.enabled = m_config.enabled;
    stats.totalFramesCaptured = m_totalFramesCaptured;
    stats.totalFramesEvicted = m_totalFramesEvicted;
    stats.nextFrameIndex = m_nextFrameIndex;
    stats.sampleCapacity = m_samples.size();
    stats.sampleCount = m_sampleCount;
    stats.checkpointCapacity = m_checkpoints.size();
    stats.checkpointCount = m_checkpointCount;
    stats.latestStateHash = m_latestStateHash;
    return stats;
}

void ReplayRecorder::CopySamplesChronological( std::vector<ReplayPresentationSample>& outSamples ) const
{
    outSamples.clear();
    outSamples.reserve( m_sampleCount );
    if ( m_sampleCount == 0 || m_samples.empty() )
    {
        return;
    }

    for ( std::size_t i = 0; i < m_sampleCount; ++i )
    {
        const std::size_t index = ( m_sampleHead + i ) % m_samples.size();
        outSamples.push_back( m_samples[index] );
    }
}

const ReplayPresentationSample* ReplayRecorder::LatestSample() const
{
    if ( m_sampleCount == 0 || m_samples.empty() )
    {
        return nullptr;
    }

    const std::size_t index = ( m_sampleHead + m_sampleCount - 1 ) % m_samples.size();
    return &m_samples[index];
}


const ReplayPresentationSample* ReplayRecorder::SampleAtNormalized( float normalized ) const
{
    if ( m_sampleCount == 0 || m_samples.empty() )
    {
        return nullptr;
    }

    const float t = std::clamp( normalized, 0.0f, 1.0f );
    const std::size_t maxOffset = m_sampleCount - 1;
    const std::size_t offset = static_cast<std::size_t>( static_cast<float>( maxOffset ) * t + 0.5f );
    const std::size_t index = ( m_sampleHead + (std::min)( offset, maxOffset ) ) % m_samples.size();
    return &m_samples[index];
}


ReplayPresentationSample& ReplayRecorder::AcquireSampleSlot()
{
    if ( m_sampleCount < m_samples.size() )
    {
        const std::size_t index = ( m_sampleHead + m_sampleCount ) % m_samples.size();
        ++m_sampleCount;
        return m_samples[index];
    }

    ReplayPresentationSample& sample = m_samples[m_sampleHead];
    m_sampleHead = ( m_sampleHead + 1 ) % m_samples.size();
    ++m_totalFramesEvicted;
    return sample;
}

void ReplayRecorder::StoreCheckpointSummary( const ReplayPresentationSample& sample )
{
    if ( m_checkpoints.empty() )
    {
        return;
    }

    std::size_t index = 0;
    if ( m_checkpointCount < m_checkpoints.size() )
    {
        index = ( m_checkpointHead + m_checkpointCount ) % m_checkpoints.size();
        ++m_checkpointCount;
    }
    else
    {
        index = m_checkpointHead;
        m_checkpointHead = ( m_checkpointHead + 1 ) % m_checkpoints.size();
    }

    ReplayCheckpointSummary& checkpoint = m_checkpoints[index];
    checkpoint.frameIndex = sample.frameIndex;
    checkpoint.eventCursor = sample.eventCursor;
    checkpoint.simulationSeconds = sample.simulationSeconds;
    checkpoint.stateHash = sample.stateHash;
    checkpoint.bodyCount =
        static_cast<uint32_t>( (std::min)( sample.bodies.size(), static_cast<std::size_t>( 0xffffffffu ) ) );
    checkpoint.contactCount = sample.contactCount;
    checkpoint.pipelineRecordCount = sample.pipelineRecordCount;
}

void ReplayRecorder::WriteHashLogHeader( const char* sceneLabel )
{
    if ( !m_hashLog.is_open() )
    {
        return;
    }

    m_hashLog << "# replay_scene scene=\"" << ( sceneLabel && sceneLabel[0] != '\0' ? sceneLabel : "generated" )
              << "\" retention_seconds=" << m_config.retentionSeconds << " retention_frames=" << m_samples.size()
              << " checkpoint_interval_frames=" << m_config.checkpointIntervalFrames << "\n";
    m_hashLog << "frame,scene_frame,simulation_seconds,body_count,contact_count,pipeline_record_count,checkpoint,state_"
                 "hash\n";
}

void ReplayRecorder::WriteHashLogRow( const ReplayPresentationSample& sample )
{
    if ( !m_hashLog.is_open() )
    {
        return;
    }

    char line[256] = {};
    sprintf_s( line,
               sizeof( line ),
               "%llu,%d,%.6f,%llu,%u,%u,%u,0x%016llX\n",
               static_cast<unsigned long long>( sample.frameIndex ),
               sample.sceneFrame,
               sample.simulationSeconds,
               static_cast<unsigned long long>( sample.bodies.size() ),
               static_cast<unsigned>( sample.contactCount ),
               static_cast<unsigned>( sample.pipelineRecordCount ),
               sample.checkpointBoundary ? 1u : 0u,
               static_cast<unsigned long long>( sample.stateHash ) );
    m_hashLog << line;
}

std::size_t ReplayRecorder::SampleCapacityFromConfig() const
{
    const int seconds = std::clamp( m_config.retentionSeconds, REPLAY_MIN_SECONDS, REPLAY_MAX_SECONDS );
    return static_cast<std::size_t>( seconds ) * static_cast<std::size_t>( REPLAY_TICKS_PER_SECOND );
}

std::size_t ReplayRecorder::CheckpointCapacityFromConfig() const
{
    const std::size_t sampleCapacity = SampleCapacityFromConfig();
    const std::size_t interval = static_cast<std::size_t>( (std::max)( 1, m_config.checkpointIntervalFrames ) );
    return (std::max)( static_cast<std::size_t>( 2 ), sampleCapacity / interval + 2 );
}

bool ReplaySolverRecorder::Configure( const ReplayRecorderConfig& config )
{
    m_config = config;
    m_config.retentionSeconds = std::clamp( m_config.retentionSeconds, REPLAY_MIN_SECONDS, REPLAY_MAX_SECONDS );
    m_config.checkpointIntervalFrames = (std::max)( 1, m_config.checkpointIntervalFrames );
    m_config.enabled = m_config.enabled || !m_config.hashLogPath.empty();

    m_hashLog.close();
    m_samples.clear();
    m_checkpoints.clear();
    m_contactCountScratch.clear();
    m_maxPenetrationScratch.clear();
    m_normalImpulseSumScratch.clear();
    m_sampleHead = 0;
    m_sampleCount = 0;
    m_checkpointHead = 0;
    m_checkpointCount = 0;
    m_nextFrameIndex = 0;
    m_totalFramesCaptured = 0;
    m_totalFramesEvicted = 0;
    m_latestSolverHash = 0;

    if ( !m_config.enabled )
    {
        return true;
    }

    m_samples.resize( SampleCapacityFromConfig() );
    m_checkpoints.resize( CheckpointCapacityFromConfig() );

    if ( !m_config.hashLogPath.empty() )
    {
        m_hashLog.open( m_config.hashLogPath, std::ios::out | std::ios::trunc );
        if ( !m_hashLog.is_open() )
        {
            fprintf( stderr, "[replay] Failed to open solver hash log: %s\n", m_config.hashLogPath.c_str() );
            m_config.hashLogPath.clear();
        }
    }

    return true;
}

void ReplaySolverRecorder::ResetTimeline( const char* sceneLabel )
{
    if ( !m_config.enabled )
    {
        return;
    }

    m_sampleHead = 0;
    m_sampleCount = 0;
    m_checkpointHead = 0;
    m_checkpointCount = 0;
    m_nextFrameIndex = 0;
    m_latestSolverHash = 0;
    WriteHashLogHeader( sceneLabel );
}

void ReplaySolverRecorder::CaptureFrame( const ReplayCaptureInput& input )
{
    if ( !m_config.enabled || !input.models )
    {
        return;
    }

    ReplaySolverFrameSample& sample = AcquireSampleSlot();
    sample.frameIndex = m_nextFrameIndex++;
    sample.branch = NormalizeBranchInfo( input.branch );
    sample.eventCursor = input.eventCursor;
    sample.sceneFrame = input.sceneFrame;
    sample.physicsDt = input.physicsDt;
    sample.simulationSeconds = input.physicsDt > 0.0f
                                   ? static_cast<double>( sample.frameIndex ) * static_cast<double>( input.physicsDt )
                                   : input.simulationSeconds;
    sample.world.fixedStep = input.fixedStep;
    sample.world.scenePhysicsEnabled = input.scenePhysicsEnabled;
    sample.world.sceneTextEnabled = input.sceneTextEnabled;
    sample.world.waterHidden = input.waterHidden;
    sample.world.terrainHidden = input.terrainHidden;
    sample.contactCount = 0;
    sample.pipelineRecordCount = 0;
    sample.launcherVisual = input.launcherVisual ? *input.launcherVisual : ReplayLauncherVisualSample();
    sample.checkpointBoundary =
        ( sample.frameIndex == 0 ) ||
        ( sample.frameIndex % static_cast<ReplayFrameIndex>( m_config.checkpointIntervalFrames ) == 0 );

    if ( input.world )
    {
        sample.world.gravity = input.world->GetGravity();
        sample.world.fluidHeight = input.world->GetFluidSurfaceHeight();
        sample.world.fluidDensity = input.world->GetFluidDensity();
    }

    if ( input.cameras )
    {
        sample.camera.eye = input.cameras->GetCameraTranslation();
        sample.camera.view = input.cameras->GetCameraView();
        sample.camera.up = input.cameras->GetCameraUp();
    }

    GameModelCollection& models = *input.models;
    std::vector<GameModel>& physicsModels = models.PhysicsModels();
    const std::size_t modelCount = physicsModels.size();
    sample.bodies.clear();
    sample.bodies.reserve( modelCount );

    m_contactCountScratch.assign( modelCount, 0 );
    m_maxPenetrationScratch.assign( modelCount, 0.0f );
    m_normalImpulseSumScratch.assign( modelCount, 0.0f );

    const std::vector<PhysicsDebugContact>& contacts = models.GetPhysicsDebugContacts();
    sample.contactCount = SaturatingUint16( contacts.size() );
    for ( const PhysicsDebugContact& contact : contacts )
    {
        IncrementBodyContactSummary( contact.bodyA,
                                     contact.penetration,
                                     contact.normalImpulse,
                                     m_contactCountScratch,
                                     m_maxPenetrationScratch,
                                     m_normalImpulseSumScratch );
        IncrementBodyContactSummary( contact.bodyB,
                                     contact.penetration,
                                     contact.normalImpulse,
                                     m_contactCountScratch,
                                     m_maxPenetrationScratch,
                                     m_normalImpulseSumScratch );
    }

    sample.pipelineRecordCount = SaturatingUint16( models.GetPhysicsPipelineTrace().size() );
    models.GetPhysicsEngine().CaptureReplaySolverSnapshot( sample.worldSnapshot, static_cast<int>( modelCount ) );

    const std::vector<uint8_t>& sleepStates = models.GetSleepStates();
    const std::vector<uint8_t>& sleepSupportedStates = models.GetSleepSupportedStates();
    const std::vector<uint8_t>& sleepInhibitedStates = models.GetSleepInhibitedStates();
    const std::vector<uint8_t>& collisionContacts = models.GetCollisionVisualContacts();
    const std::vector<int>& sleepIslandIds = models.GetSleepIslandVisualIds();

    uint64_t presentationHash = FNV64_OFFSET;
    presentationHash = HashWorld( presentationHash, sample.world );
    presentationHash = HashInt( presentationHash, static_cast<int>( modelCount ) );
    presentationHash = HashInt( presentationHash, static_cast<int>( sample.contactCount ) );
    presentationHash = HashInt( presentationHash, static_cast<int>( sample.pipelineRecordCount ) );

    uint64_t solverHash = FNV64_OFFSET;
    solverHash = HashWorld( solverHash, sample.world );
    solverHash = HashInt( solverHash, static_cast<int>( modelCount ) );
    solverHash = HashInt( solverHash, static_cast<int>( sample.contactCount ) );
    solverHash = HashInt( solverHash, static_cast<int>( sample.pipelineRecordCount ) );
    solverHash = HashLauncherControlState( solverHash, sample.launcherVisual );
    solverHash = HashSolverWorldSnapshot( solverHash, sample.worldSnapshot );

    for ( std::size_t i = 0; i < modelCount; ++i )
    {
        GameModel& model = physicsModels[i];
        ReplaySolverBodySample body;
        body.id.value = model.GetReplayBodyId();
        body.modelIndex = static_cast<int>( i );
        const char* modelName = model.GetName();
        if ( modelName && modelName[0] != '\0' )
        {
            strncpy_s( body.name, sizeof( body.name ), modelName, _TRUNCATE );
        }
        body.shapeKind = ShapeKindForModel( model );
        body.position = model.GetPosition();
        body.linearVelocity = model.GetVelocity();
        body.angularVelocity = model.GetAngularVelocity();
        const Quaternion& orientation = model.GetOrientation();
        orientation.GetComponents( body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
        body.mass = model.GetMass();
        body.inverseMass = model.GetInvertedMass();
        body.rotationalInertia = model.GetRotationalInertia();
        body.inverseRotationalInertia = model.GetInvertedRotationalInertia();
        body.fixed = model.IsFixed();
        body.sleeping = i < sleepStates.size() && sleepStates[i] != 0;
        body.sleepSupported = i < sleepSupportedStates.size() && sleepSupportedStates[i] != 0;
        body.sleepInhibited = i < sleepInhibitedStates.size() && sleepInhibitedStates[i] != 0;
        body.collisionContact = i < collisionContacts.size() && collisionContacts[i] != 0;
        body.sleepIslandVisualId = i < sleepIslandIds.size() ? sleepIslandIds[i] : 0;
        body.contactCount = i < m_contactCountScratch.size() ? m_contactCountScratch[i] : 0;
        body.maxPenetration = i < m_maxPenetrationScratch.size() ? m_maxPenetrationScratch[i] : 0.0f;
        body.normalImpulseSum = i < m_normalImpulseSumScratch.size() ? m_normalImpulseSumScratch[i] : 0.0f;

        presentationHash = HashSolverBodyPresentationFields( presentationHash, body );
        solverHash = HashSolverBodySample( solverHash, body );
        sample.bodies.push_back( body );
    }

    sample.presentationHash = presentationHash;
    sample.solverHash = solverHash;
    m_latestSolverHash = solverHash;
    ++m_totalFramesCaptured;

    if ( sample.checkpointBoundary )
    {
        StoreCheckpointSummary( sample );
    }
    WriteHashLogRow( sample );
}

void ReplaySolverRecorder::FlushHashLog()
{
    if ( m_hashLog.is_open() )
    {
        m_hashLog.flush();
    }
}

bool ReplaySolverRecorder::IsEnabled() const
{
    return m_config.enabled;
}

ReplayRecorderStats ReplaySolverRecorder::GetStats() const
{
    ReplayRecorderStats stats;
    stats.enabled = m_config.enabled;
    stats.totalFramesCaptured = m_totalFramesCaptured;
    stats.totalFramesEvicted = m_totalFramesEvicted;
    stats.nextFrameIndex = m_nextFrameIndex;
    stats.sampleCapacity = m_samples.size();
    stats.sampleCount = m_sampleCount;
    stats.checkpointCapacity = m_checkpoints.size();
    stats.checkpointCount = m_checkpointCount;
    stats.latestStateHash = m_latestSolverHash;
    return stats;
}

void ReplaySolverRecorder::CopySamplesChronological( std::vector<ReplaySolverFrameSample>& outSamples ) const
{
    outSamples.clear();
    outSamples.reserve( m_sampleCount );
    if ( m_sampleCount == 0 || m_samples.empty() )
    {
        return;
    }

    for ( std::size_t i = 0; i < m_sampleCount; ++i )
    {
        const std::size_t index = ( m_sampleHead + i ) % m_samples.size();
        outSamples.push_back( m_samples[index] );
    }
}

void ReplaySolverRecorder::ForEachSampleChronological( ReplaySolverSampleVisitor visitor, void* userData ) const
{
    if ( !visitor || m_sampleCount == 0 || m_samples.empty() )
    {
        return;
    }

    for ( std::size_t i = 0; i < m_sampleCount; ++i )
    {
        const std::size_t index = ( m_sampleHead + i ) % m_samples.size();
        visitor( m_samples[index], userData );
    }
}

const ReplaySolverFrameSample* ReplaySolverRecorder::LatestSample() const
{
    if ( m_sampleCount == 0 || m_samples.empty() )
    {
        return nullptr;
    }

    const std::size_t index = ( m_sampleHead + m_sampleCount - 1 ) % m_samples.size();
    return &m_samples[index];
}

const ReplaySolverFrameSample* ReplaySolverRecorder::SampleAtNormalized( float normalized ) const
{
    if ( m_sampleCount == 0 || m_samples.empty() )
    {
        return nullptr;
    }

    const float t = std::clamp( normalized, 0.0f, 1.0f );
    const std::size_t maxOffset = m_sampleCount - 1;
    const std::size_t offset = static_cast<std::size_t>( static_cast<float>( maxOffset ) * t + 0.5f );
    const std::size_t index = ( m_sampleHead + (std::min)( offset, maxOffset ) ) % m_samples.size();
    return &m_samples[index];
}

ReplaySolverFrameSample& ReplaySolverRecorder::AcquireSampleSlot()
{
    if ( m_sampleCount < m_samples.size() )
    {
        const std::size_t index = ( m_sampleHead + m_sampleCount ) % m_samples.size();
        ++m_sampleCount;
        return m_samples[index];
    }

    ReplaySolverFrameSample& sample = m_samples[m_sampleHead];
    m_sampleHead = ( m_sampleHead + 1 ) % m_samples.size();
    ++m_totalFramesEvicted;
    return sample;
}

void ReplaySolverRecorder::StoreCheckpointSummary( const ReplaySolverFrameSample& sample )
{
    if ( m_checkpoints.empty() )
    {
        return;
    }

    std::size_t index = 0;
    if ( m_checkpointCount < m_checkpoints.size() )
    {
        index = ( m_checkpointHead + m_checkpointCount ) % m_checkpoints.size();
        ++m_checkpointCount;
    }
    else
    {
        index = m_checkpointHead;
        m_checkpointHead = ( m_checkpointHead + 1 ) % m_checkpoints.size();
    }

    ReplayCheckpointSummary& checkpoint = m_checkpoints[index];
    checkpoint.frameIndex = sample.frameIndex;
    checkpoint.eventCursor = sample.eventCursor;
    checkpoint.simulationSeconds = sample.simulationSeconds;
    checkpoint.stateHash = sample.solverHash;
    checkpoint.bodyCount =
        static_cast<uint32_t>( (std::min)( sample.bodies.size(), static_cast<std::size_t>( 0xffffffffu ) ) );
    checkpoint.contactCount = sample.contactCount;
    checkpoint.pipelineRecordCount = sample.pipelineRecordCount;
}

void ReplaySolverRecorder::WriteHashLogHeader( const char* sceneLabel )
{
    if ( !m_hashLog.is_open() )
    {
        return;
    }

    m_hashLog << "# solver_replay_scene scene=\"" << ( sceneLabel && sceneLabel[0] != '\0' ? sceneLabel : "generated" )
              << "\" retention_seconds=" << m_config.retentionSeconds << " retention_frames=" << m_samples.size()
              << " checkpoint_interval_frames=" << m_config.checkpointIntervalFrames << "\n";
    m_hashLog << "frame,scene_frame,simulation_seconds,body_count,contact_count,pipeline_record_count,checkpoint,"
                 "presentation_hash,solver_hash\n";
}

void ReplaySolverRecorder::WriteHashLogRow( const ReplaySolverFrameSample& sample )
{
    if ( !m_hashLog.is_open() )
    {
        return;
    }

    char line[288] = {};
    sprintf_s( line,
               sizeof( line ),
               "%llu,%d,%.6f,%llu,%u,%u,%u,0x%016llX,0x%016llX\n",
               static_cast<unsigned long long>( sample.frameIndex ),
               sample.sceneFrame,
               sample.simulationSeconds,
               static_cast<unsigned long long>( sample.bodies.size() ),
               static_cast<unsigned>( sample.contactCount ),
               static_cast<unsigned>( sample.pipelineRecordCount ),
               sample.checkpointBoundary ? 1u : 0u,
               static_cast<unsigned long long>( sample.presentationHash ),
               static_cast<unsigned long long>( sample.solverHash ) );
    m_hashLog << line;
}

std::size_t ReplaySolverRecorder::SampleCapacityFromConfig() const
{
    const int seconds = std::clamp( m_config.retentionSeconds, REPLAY_MIN_SECONDS, REPLAY_MAX_SECONDS );
    return static_cast<std::size_t>( seconds ) * static_cast<std::size_t>( REPLAY_TICKS_PER_SECOND );
}

std::size_t ReplaySolverRecorder::CheckpointCapacityFromConfig() const
{
    const std::size_t sampleCapacity = SampleCapacityFromConfig();
    const std::size_t interval = static_cast<std::size_t>( (std::max)( 1, m_config.checkpointIntervalFrames ) );
    return (std::max)( static_cast<std::size_t>( 2 ), sampleCapacity / interval + 2 );
}

bool ReplayEventRecorder::Configure( const ReplayRecorderConfig& config )
{
    m_config = config;
    m_config.retentionSeconds = std::clamp( m_config.retentionSeconds, REPLAY_MIN_SECONDS, REPLAY_MAX_SECONDS );

    m_events.clear();
    m_eventHead = 0;
    m_eventCount = 0;
    m_nextSequence = 0;
    m_totalEventsCaptured = 0;
    m_totalEventsEvicted = 0;

    if ( !m_config.enabled )
    {
        return true;
    }

    m_events.resize( EventCapacityFromConfig() );
    return true;
}

void ReplayEventRecorder::ResetTimeline( const char* )
{
    if ( !m_config.enabled )
    {
        return;
    }

    m_eventHead = 0;
    m_eventCount = 0;
    m_nextSequence = 0;
}

void ReplayEventRecorder::RecordEvent( const ReplayEventInput& input )
{
    if ( !m_config.enabled || m_events.empty() )
    {
        return;
    }

    ReplayEventSample& sample = AcquireEventSlot();
    sample = ReplayEventSample();
    sample.frameIndex = input.frameIndex;
    sample.sequence = m_nextSequence++;
    sample.branch = NormalizeBranchInfo( input.branch );
    sample.kind = input.kind;
    sample.payloadVersion = 1;
    sample.flags = input.flags;
    sample.value0 = input.value0;
    sample.value1 = input.value1;
    sample.value2 = input.value2;
    sample.value3 = input.value3;
    sample.data0 = input.data0;
    if ( input.text && input.text[0] != '\0' )
    {
        strncpy_s( sample.text, sizeof( sample.text ), input.text, _TRUNCATE );
    }

    ++m_totalEventsCaptured;
}

bool ReplayEventRecorder::IsEnabled() const
{
    return m_config.enabled;
}

ReplayEventRecorderStats ReplayEventRecorder::GetStats() const
{
    ReplayEventRecorderStats stats;
    stats.enabled = m_config.enabled;
    stats.totalEventsCaptured = m_totalEventsCaptured;
    stats.totalEventsEvicted = m_totalEventsEvicted;
    stats.nextSequence = m_nextSequence;
    stats.eventCapacity = m_events.size();
    stats.eventCount = m_eventCount;
    return stats;
}

void ReplayEventRecorder::CopyEventsChronological( std::vector<ReplayEventSample>& outEvents ) const
{
    outEvents.clear();
    outEvents.reserve( m_eventCount );
    if ( m_eventCount == 0 || m_events.empty() )
    {
        return;
    }

    for ( std::size_t i = 0; i < m_eventCount; ++i )
    {
        const std::size_t index = ( m_eventHead + i ) % m_events.size();
        outEvents.push_back( m_events[index] );
    }
}

ReplayEventSample& ReplayEventRecorder::AcquireEventSlot()
{
    if ( m_eventCount < m_events.size() )
    {
        const std::size_t index = ( m_eventHead + m_eventCount ) % m_events.size();
        ++m_eventCount;
        return m_events[index];
    }

    ReplayEventSample& sample = m_events[m_eventHead];
    m_eventHead = ( m_eventHead + 1 ) % m_events.size();
    ++m_totalEventsEvicted;
    return sample;
}

std::size_t ReplayEventRecorder::EventCapacityFromConfig() const
{
    const int seconds = std::clamp( m_config.retentionSeconds, REPLAY_MIN_SECONDS, REPLAY_MAX_SECONDS );
    return (std::max)( static_cast<std::size_t>( 64 ), static_cast<std::size_t>( seconds ) * 64u );
}
