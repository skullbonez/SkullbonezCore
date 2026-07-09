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
  Replay reserve owner: Runtime allocation-policy owner that permits replay-only
    vector growth when captured samples outgrow their current payload capacity.
  UI (User Interface): Runtime controls and overlays; recorders observe state
    but never mutate UI state.

Invariants:
  - Recording observes committed state and never advances simulation.
  - Hash packing must stay deterministic across machines and configurations.
  - Configure must not multiply startup body capacity across every future sample;
    retained body payloads grow only for captured frames under replay reserve gates.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h
*/
#include "ReplayRecorder.h"

#include "../CameraCollection.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../Allocation/RuntimeReserveAllocator.h"
#include "../../Core/Common.h"
#include "../../Core/FatalError.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace SkullbonezCore::Basics;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Environment::WorldEnvironment;
using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::GameObjects::GameModelCollection;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::PhysicsDebugContact;
namespace Physics = SkullbonezCore::Physics;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
constexpr int REPLAY_TICKS_PER_SECOND = 120;
constexpr int REPLAY_MIN_SECONDS = 1;
constexpr int REPLAY_MAX_SECONDS = 600;
constexpr std::size_t REPLAY_LAUNCHER_RAY_LINE_CAPACITY = 64;
constexpr std::size_t REPLAY_LAUNCHER_LASER_SHOT_CAPACITY = 32;
constexpr const char* REPLAY_RECORDER_SAMPLE_RESERVE_OWNER = "replay_recorder_samples";
constexpr int REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES = 64 * 1024 * 1024;
constexpr std::size_t REPLAY_RECORDER_SAMPLE_INITIAL_CAPACITY = 128u;
constexpr std::size_t REPLAY_RECORDER_SAMPLE_GROWTH_CHUNK = 256u;
// Runtime allocation policy: retained replay body payloads now grow per active
// scene size instead of preallocating every future slot at game_model_capacity.
// The hard byte cap is per vector reserve request and growth count is telemetry.
constexpr int REPLAY_RECORDER_SAMPLE_RESERVE_GROWTH_LIMIT =
    RuntimeAllocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;
constexpr uint64_t FNV64_OFFSET = 14695981039346656037ull;
constexpr uint64_t FNV64_PRIME = 1099511628211ull;

int ReplayRuntimeBodyCapacity( const ReplayRecorderConfig& config )
{
    return std::clamp( config.runtimeBodyCapacity, 1, MAX_GAME_MODELS );
}

RuntimeAllocation::RuntimeReserveOwnerHandle ReplayRecorderSampleReserveOwner()
{
    static const RuntimeAllocation::RuntimeReserveOwnerHandle owner =
        RuntimeAllocation::RuntimeReserveAllocator::RegisterOwner(
            { REPLAY_RECORDER_SAMPLE_RESERVE_OWNER,
              RuntimeAllocation::RuntimeReserveSubsystem::Replay,
              RuntimeAllocation::RuntimeReservePhase::Replay,
              0,
              REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES,
              REPLAY_RECORDER_SAMPLE_RESERVE_GROWTH_LIMIT,
              true,
              "replay recorder sample body vectors grow under the active scene size instead of startup capacity" } );
    return owner;
}

int ReplayRecorderGrowthFrameNumber( ReplayFrameIndex frameIndex )
{
    constexpr ReplayFrameIndex maxFrame = static_cast<ReplayFrameIndex>( ( std::numeric_limits<int>::max )() );
    return frameIndex > maxFrame ? ( std::numeric_limits<int>::max )() : static_cast<int>( frameIndex );
}

std::size_t ReplayRecorderReserveCapacity( std::size_t currentCapacity, std::size_t requestedCapacity )
{
    if ( requestedCapacity <= currentCapacity )
    {
        return currentCapacity;
    }
    if ( requestedCapacity > static_cast<std::size_t>( MAX_GAME_MODELS ) )
    {
        return requestedCapacity;
    }

    const std::size_t doubled = currentCapacity > 0u ? currentCapacity * 2u : REPLAY_RECORDER_SAMPLE_INITIAL_CAPACITY;
    const std::size_t remainder = requestedCapacity % REPLAY_RECORDER_SAMPLE_GROWTH_CHUNK;
    const std::size_t chunked =
        remainder == 0u ? requestedCapacity : requestedCapacity + ( REPLAY_RECORDER_SAMPLE_GROWTH_CHUNK - remainder );
    const std::size_t reserveCapacity = (std::max)( doubled, chunked );
    return (std::min)( reserveCapacity, static_cast<std::size_t>( MAX_GAME_MODELS ) );
}

template <typename T> uint64_t ReplayRecorderVectorBytes( std::size_t capacity )
{
    constexpr uint64_t elementBytes = static_cast<uint64_t>( sizeof( T ) );
    const uint64_t maxCapacity = ( std::numeric_limits<uint64_t>::max )() / elementBytes;
    if ( capacity > maxCapacity )
    {
        // Lane F: a capacity arithmetic overflow means the replay retention
        // contract can no longer bound its sample storage.
        SB_FATAL( "Runtime/Replay",
                  "Replay sample reserve byte overflow. capacity=%llu element_bytes=%llu",
                  static_cast<unsigned long long>( capacity ),
                  static_cast<unsigned long long>( elementBytes ) );
    }
    return static_cast<uint64_t>( capacity ) * elementBytes;
}

void ReportReplayRecorderReserveFailure( const char* targetName,
                                         std::size_t requestedCapacity,
                                         uint64_t requestedBytes )
{
    // Lane F: if a retained sample cannot fit inside the replay reserve budget,
    // continuing would make scrub/restore state partial and nondeterministic.
    SB_FATAL( "Runtime/Replay",
              "Replay recorder reserve denied. target=%s requested_capacity=%llu requested_bytes=%llu hard_bytes=%d",
              targetName ? targetName : "unknown",
              static_cast<unsigned long long>( requestedCapacity ),
              static_cast<unsigned long long>( requestedBytes ),
              REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES );
}

template <typename T>
void ReserveReplayRecorderSampleVector( std::vector<T>& values,
                                        std::size_t requestedCapacity,
                                        ReplayFrameIndex frameIndex,
                                        const char* targetName )
{
    if ( requestedCapacity <= values.capacity() )
    {
        return;
    }
    if ( requestedCapacity > static_cast<std::size_t>( MAX_GAME_MODELS ) )
    {
        ReportReplayRecorderReserveFailure( targetName, requestedCapacity, 0u );
    }

    const std::size_t reserveCapacity = ReplayRecorderReserveCapacity( values.capacity(), requestedCapacity );
    const uint64_t oldBytes = ReplayRecorderVectorBytes<T>( values.capacity() );
    const uint64_t requestedBytes = ReplayRecorderVectorBytes<T>( reserveCapacity );
    if ( requestedBytes > static_cast<uint64_t>( REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES ) ||
         oldBytes > static_cast<uint64_t>( ( std::numeric_limits<int>::max )() ) ||
         requestedBytes > static_cast<uint64_t>( ( std::numeric_limits<int>::max )() ) )
    {
        ReportReplayRecorderReserveFailure( targetName, reserveCapacity, requestedBytes );
    }

    if ( RuntimeAllocation::RuntimeAllocationGuardEnabled() )
    {
        const RuntimeAllocation::RuntimeReserveOwnerHandle owner = ReplayRecorderSampleReserveOwner();
        const RuntimeAllocation::RuntimeReserveGrowthRequest request = { REPLAY_RECORDER_SAMPLE_RESERVE_OWNER,
                                                                         targetName,
                                                                         RuntimeAllocation::RuntimeReservePhase::Replay,
                                                                         ReplayRecorderGrowthFrameNumber( frameIndex ),
                                                                         static_cast<int>( oldBytes ),
                                                                         static_cast<int>( requestedBytes ),
                                                                         1 };
        const RuntimeAllocation::RuntimeReserveGrowthResult result =
            RuntimeAllocation::RuntimeReserveAllocator::RequestGrowth( owner, request );
        if ( !result.granted )
        {
            ReportReplayRecorderReserveFailure( targetName, reserveCapacity, requestedBytes );
        }

        RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Replay );
        RuntimeAllocation::RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeAllocation::RuntimeReserveGrowthScope growthScope( owner,
                                                                  RuntimeAllocation::RuntimeReservePhase::Replay,
                                                                  result );
        values.reserve( reserveCapacity );
    }
    else
    {
        values.reserve( reserveCapacity );
    }

    if ( requestedCapacity > values.capacity() )
    {
        ReportReplayRecorderReserveFailure( targetName, requestedCapacity, requestedBytes );
    }
}

void ReserveReplayLauncherVisualSample( ReplayLauncherVisualSample& visual )
{
    // Runtime allocation policy: launcher rays and laser shots are fixed-size
    // visual rings in RuntimeTools/LauncherLaser, so replay reserves matching
    // payload capacity before capture starts.
    visual.rayLines.reserve( REPLAY_LAUNCHER_RAY_LINE_CAPACITY );
    visual.laserShots.reserve( REPLAY_LAUNCHER_LASER_SHOT_CAPACITY );
}

void ReserveReplaySolverFrameSample( ReplaySolverFrameSample& sample )
{
    ReserveReplayLauncherVisualSample( sample.launcherVisual );
}

void ReserveReplayRecorderScratch( std::vector<uint16_t>& contactCountScratch,
                                   std::vector<float>& maxPenetrationScratch,
                                   std::vector<float>& normalImpulseSumScratch,
                                   int bodyCapacity )
{
    const std::size_t bodyCapacitySize = static_cast<std::size_t>( bodyCapacity );
    contactCountScratch.reserve( bodyCapacitySize );
    maxPenetrationScratch.reserve( bodyCapacitySize );
    normalImpulseSumScratch.reserve( bodyCapacitySize );
}

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

uint64_t LauncherVisualMemoryBytes( const ReplayLauncherVisualSample& visual )
{
    return VectorCapacityBytes( visual.rayLines ) + VectorCapacityBytes( visual.laserShots );
}

uint64_t SolverWorldSnapshotMemoryBytes( const ReplaySolverWorldSnapshot& snapshot )
{
    uint64_t bytes = 0;
    bytes += VectorCapacityBytes( snapshot.timeRemaining );
    bytes += VectorCapacityBytes( snapshot.sleepSupportedThisFrame );
    bytes += VectorCapacityBytes( snapshot.sleepInhibitedThisFrame );
    bytes += VectorCapacityBytes( snapshot.sleepState );
    bytes += VectorCapacityBytes( snapshot.sleepCounter );
    bytes += VectorCapacityBytes( snapshot.underwaterSleepLocked );
    bytes += VectorCapacityBytes( snapshot.tornadoCaptureSeconds );
    bytes += VectorCapacityBytes( snapshot.tornadoEjectCooldownSeconds );
    bytes += VectorCapacityBytes( snapshot.collisionVisualContacts );
    bytes += VectorCapacityBytes( snapshot.sleepIslandVisualId );
    bytes += VectorCapacityBytes( snapshot.sleepIslandAssignedVisualId );
    bytes += VectorCapacityBytes( snapshot.sleepSupportEdges );
    bytes += VectorCapacityBytes( snapshot.sleepIslandParent );
    bytes += VectorCapacityBytes( snapshot.sleepIslandRank );
    bytes += VectorCapacityBytes( snapshot.sleepIslandHasAwake );
    bytes += VectorCapacityBytes( snapshot.sleepIslandHasSupportAnchor );
    bytes += VectorCapacityBytes( snapshot.sleepIslandEligible );
    bytes += VectorCapacityBytes( snapshot.sleepIslandCanSleep );
    bytes += VectorCapacityBytes( snapshot.persistentContacts );
    bytes += VectorCapacityBytes( snapshot.persistentContactCache );
    bytes += VectorCapacityBytes( snapshot.persistentContactCounts );
    bytes += VectorCapacityBytes( snapshot.persistentRestingContactCounts );
    bytes += VectorCapacityBytes( snapshot.debugContacts );
    bytes += VectorCapacityBytes( snapshot.pipelineTrace );
    bytes += VectorCapacityBytes( snapshot.collisionCellKeys );
    return bytes;
}

uint64_t PresentationSampleMemoryBytes( const ReplayPresentationSample& sample )
{
    return VectorCapacityBytes( sample.bodies );
}

uint64_t SolverFrameSampleMemoryBytes( const ReplaySolverFrameSample& sample )
{
    return LauncherVisualMemoryBytes( sample.launcherVisual ) + SolverWorldSnapshotMemoryBytes( sample.worldSnapshot ) +
           VectorCapacityBytes( sample.bodies );
}

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

ReplayBodyShapeKind ShapeKindForCollider( const ColliderRecord& collider )
{
    switch ( collider.shapeKind )
    {
    case Physics::ColliderShapeKind::Sphere:
        return ReplayBodyShapeKind::Sphere;
    case Physics::ColliderShapeKind::Box:
        return ReplayBodyShapeKind::Box;
    case Physics::ColliderShapeKind::ConvexHull:
        return ReplayBodyShapeKind::ConvexHull;
    default:
        break;
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

// Concept: replay body samples borrow GameModel only for the stable display
// name. Physics values come from dense stores so capture does not require
// post-step model mirrors.
bool BuildReplayPresentationBodySample( int modelIndex,
                                        const GameModelCollection& models,
                                        const Physics::PhysicsBodyStore& bodyStore,
                                        const Physics::ColliderStore& colliderStore,
                                        ReplayBodyPresentationSample& outBody )
{
    if ( modelIndex < 0 || modelIndex >= bodyStore.Count() || modelIndex >= colliderStore.Count() )
    {
        return false;
    }

    const GameModel* model = models.TryGetModel( modelIndex );
    if ( !model )
    {
        return false;
    }

    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    const Physics::PhysicsBodyRecord& bodyRecord = bodyStore.Records()[bodyIndex];
    const ColliderRecord& colliderRecord = colliderStore.Records()[bodyIndex];

    outBody = ReplayBodyPresentationSample{};
    outBody.id.value = bodyRecord.replayBodyId;
    outBody.modelIndex = modelIndex;
    const char* modelName = model->GetName();
    if ( modelName && modelName[0] != '\0' )
    {
        strncpy_s( outBody.name, sizeof( outBody.name ), modelName, _TRUNCATE );
    }
    outBody.shapeKind = ShapeKindForCollider( colliderRecord );
    outBody.position = bodyRecord.position;
    outBody.linearVelocity = bodyRecord.linearVelocity;
    outBody.angularVelocity = bodyRecord.angularVelocity;
    bodyRecord.orientation.GetComponents( outBody.orientation[0],
                                          outBody.orientation[1],
                                          outBody.orientation[2],
                                          outBody.orientation[3] );
    outBody.mass = bodyRecord.mass;
    outBody.fixed = bodyRecord.isFixed;
    return true;
}

bool BuildReplaySolverBodySample( int modelIndex,
                                  const GameModelCollection& models,
                                  const Physics::PhysicsBodyStore& bodyStore,
                                  const Physics::ColliderStore& colliderStore,
                                  ReplaySolverBodySample& outBody )
{
    ReplayBodyPresentationSample presentationBody;
    if ( !BuildReplayPresentationBodySample( modelIndex, models, bodyStore, colliderStore, presentationBody ) )
    {
        return false;
    }

    const Physics::PhysicsBodyRecord& bodyRecord = bodyStore.Records()[static_cast<std::size_t>( modelIndex )];

    outBody = ReplaySolverBodySample{};
    outBody.id = presentationBody.id;
    outBody.modelIndex = presentationBody.modelIndex;
    strncpy_s( outBody.name, sizeof( outBody.name ), presentationBody.name, _TRUNCATE );
    outBody.shapeKind = presentationBody.shapeKind;
    outBody.position = presentationBody.position;
    outBody.linearVelocity = presentationBody.linearVelocity;
    outBody.angularVelocity = presentationBody.angularVelocity;
    outBody.orientation[0] = presentationBody.orientation[0];
    outBody.orientation[1] = presentationBody.orientation[1];
    outBody.orientation[2] = presentationBody.orientation[2];
    outBody.orientation[3] = presentationBody.orientation[3];
    outBody.mass = presentationBody.mass;
    outBody.inverseMass = bodyRecord.invMass;
    outBody.rotationalInertia = bodyRecord.rotationalInertia;
    outBody.inverseRotationalInertia = bodyRecord.invRotationalInertia;
    outBody.fixed = presentationBody.fixed;
    return true;
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
    // Concept: the recorder is a bounded ring buffer plus optional hash log.
    //
    // Configuration resets all retained samples because changing retention or
    // checkpoint cadence changes the meaning of every normalized scrub offset.
    m_config = config;
    m_config.retentionSeconds = std::clamp( m_config.retentionSeconds, REPLAY_MIN_SECONDS, REPLAY_MAX_SECONDS );
    m_config.checkpointIntervalFrames = (std::max)( 1, m_config.checkpointIntervalFrames );
    m_config.runtimeBodyCapacity = ReplayRuntimeBodyCapacity( m_config );
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
    ReserveReplayRecorderScratch( m_contactCountScratch,
                                  m_maxPenetrationScratch,
                                  m_normalImpulseSumScratch,
                                  m_config.runtimeBodyCapacity );

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
    if ( !m_config.enabled || !input.models || !input.bodyStore || !input.colliderStore )
    {
        return;
    }

    ReplayPresentationSample& sample = AcquireSampleSlot();
    // Invariant: frameIndex is recorder-local monotonic time. Even when older
    // ring-buffer slots are evicted, saved branches and hash logs still compare
    // frames by this increasing index.
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
    const Physics::PhysicsBodyStore& bodyStore = *input.bodyStore;
    const Physics::ColliderStore& colliderStore = *input.colliderStore;
    const int modelCount = bodyStore.Count();
    const std::size_t modelCountSize = static_cast<std::size_t>( modelCount );
    sample.bodies.clear();
    ReserveReplayRecorderSampleVector( sample.bodies,
                                       modelCountSize,
                                       sample.frameIndex,
                                       "ReplayPresentationSample::bodies" );

    m_contactCountScratch.assign( modelCountSize, 0 );
    m_maxPenetrationScratch.assign( modelCountSize, 0.0f );
    m_normalImpulseSumScratch.assign( modelCountSize, 0.0f );

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
    // Concept: presentation hashes summarize what the viewer would need to see
    // and diagnose a frame. They intentionally include world state, body poses,
    // contact summaries, and pipeline counts rather than raw memory addresses.
    hash = HashWorld( hash, sample.world );
    hash = HashInt( hash, static_cast<int>( modelCount ) );
    hash = HashInt( hash, static_cast<int>( sample.contactCount ) );
    hash = HashInt( hash, static_cast<int>( sample.pipelineRecordCount ) );

    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        ReplayBodyPresentationSample body;
        if ( !BuildReplayPresentationBodySample( i, models, bodyStore, colliderStore, body ) )
        {
            continue;
        }
        body.sleeping = bodyIndex < sleepStates.size() && sleepStates[bodyIndex] != 0;
        body.sleepSupported = bodyIndex < sleepSupportedStates.size() && sleepSupportedStates[bodyIndex] != 0;
        body.sleepInhibited = bodyIndex < sleepInhibitedStates.size() && sleepInhibitedStates[bodyIndex] != 0;
        body.collisionContact = bodyIndex < collisionContacts.size() && collisionContacts[bodyIndex] != 0;
        body.sleepIslandVisualId = bodyIndex < sleepIslandIds.size() ? sleepIslandIds[bodyIndex] : 0;
        body.contactCount = bodyIndex < m_contactCountScratch.size() ? m_contactCountScratch[bodyIndex] : 0;
        body.maxPenetration = bodyIndex < m_maxPenetrationScratch.size() ? m_maxPenetrationScratch[bodyIndex] : 0.0f;
        body.normalImpulseSum =
            bodyIndex < m_normalImpulseSumScratch.size() ? m_normalImpulseSumScratch[bodyIndex] : 0.0f;

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

void ReplayRecorder::CaptureFrameFromSolverSample( const ReplaySolverFrameSample& solverSample )
{
    if ( !m_config.enabled )
    {
        return;
    }

    ReplayPresentationSample& sample = AcquireSampleSlot();
    // Why: solver capture already walked models, contacts, and hashes for this
    // committed tick. Mirroring its presentation-facing fields keeps the public
    // presentation timeline intact without repeating that hot-path work.
    sample.frameIndex = solverSample.frameIndex;
    m_nextFrameIndex = sample.frameIndex + 1u;
    sample.branch = NormalizeBranchInfo( solverSample.branch );
    sample.eventCursor = solverSample.eventCursor;
    sample.sceneFrame = solverSample.sceneFrame;
    sample.simulationSeconds = solverSample.simulationSeconds;
    sample.physicsDt = solverSample.physicsDt;
    sample.camera = solverSample.camera;
    sample.world = solverSample.world;
    sample.contactCount = solverSample.contactCount;
    sample.pipelineRecordCount = solverSample.pipelineRecordCount;
    sample.checkpointBoundary =
        ( sample.frameIndex == 0 ) ||
        ( sample.frameIndex % static_cast<ReplayFrameIndex>( m_config.checkpointIntervalFrames ) == 0 );

    sample.bodies.clear();
    ReserveReplayRecorderSampleVector( sample.bodies,
                                       solverSample.bodies.size(),
                                       sample.frameIndex,
                                       "ReplayPresentationMirror::bodies" );
    for ( const ReplaySolverBodySample& solverBody : solverSample.bodies )
    {
        ReplayBodyPresentationSample body;
        body.id = solverBody.id;
        body.modelIndex = solverBody.modelIndex;
        strncpy_s( body.name, sizeof( body.name ), solverBody.name, _TRUNCATE );
        body.shapeKind = solverBody.shapeKind;
        body.position = solverBody.position;
        body.linearVelocity = solverBody.linearVelocity;
        body.angularVelocity = solverBody.angularVelocity;
        body.orientation[0] = solverBody.orientation[0];
        body.orientation[1] = solverBody.orientation[1];
        body.orientation[2] = solverBody.orientation[2];
        body.orientation[3] = solverBody.orientation[3];
        body.mass = solverBody.mass;
        body.fixed = solverBody.fixed;
        body.sleeping = solverBody.sleeping;
        body.sleepSupported = solverBody.sleepSupported;
        body.sleepInhibited = solverBody.sleepInhibited;
        body.collisionContact = solverBody.collisionContact;
        body.sleepIslandVisualId = solverBody.sleepIslandVisualId;
        body.contactCount = solverBody.contactCount;
        body.maxPenetration = solverBody.maxPenetration;
        body.normalImpulseSum = solverBody.normalImpulseSum;
        sample.bodies.push_back( body );
    }

    sample.stateHash = solverSample.presentationHash;
    m_latestStateHash = sample.stateHash;
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

uint64_t ReplayRecorder::CollectMemoryBytes() const
{
    uint64_t bytes = static_cast<uint64_t>( sizeof( *this ) );
    bytes += VectorCapacityBytes( m_samples );
    bytes += VectorCapacityBytes( m_checkpoints );
    bytes += VectorCapacityBytes( m_contactCountScratch );
    bytes += VectorCapacityBytes( m_maxPenetrationScratch );
    bytes += VectorCapacityBytes( m_normalImpulseSumScratch );
    for ( const ReplayPresentationSample& sample : m_samples )
    {
        bytes += PresentationSampleMemoryBytes( sample );
    }
    return bytes;
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
    // Lifetime: returned references stay valid only until a future capture
    // wraps the ring buffer onto the same slot.
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
    // Why: checkpoints keep cheap seek/diagnostic facts beside the dense
    // samples. Full solver restore data lives in solver replay artifacts.
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

    // Invariant: hash-log columns are external validation surface. Keep order,
    // precision, and hex formatting stable unless replay tooling is updated in
    // the same change.
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
    m_config.runtimeBodyCapacity = ReplayRuntimeBodyCapacity( m_config );
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
    ReserveReplayRecorderScratch( m_contactCountScratch,
                                  m_maxPenetrationScratch,
                                  m_normalImpulseSumScratch,
                                  m_config.runtimeBodyCapacity );
    for ( ReplaySolverFrameSample& sample : m_samples )
    {
        ReserveReplaySolverFrameSample( sample );
    }

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
    if ( !m_config.enabled || !input.models || !input.bodyStore || !input.colliderStore )
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
    if ( input.launcherVisual )
    {
        sample.launcherVisual = *input.launcherVisual;
    }
    else
    {
        sample.launcherVisual.rayLines.clear();
        sample.launcherVisual.laserShots.clear();
        sample.launcherVisual.nextRayLine = 0;
        sample.launcherVisual.nextLaserShot = 0;
        sample.launcherVisual.fireMode = ReplayLauncherFireMode::Laser;
        sample.launcherVisual.visualizeRays = false;
        sample.launcherVisual.impulseStrength = 0.0f;
        sample.launcherVisual.projectileSpeed = 0.0f;
    }
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
    const Physics::PhysicsBodyStore& bodyStore = *input.bodyStore;
    const Physics::ColliderStore& colliderStore = *input.colliderStore;
    const int modelCount = bodyStore.Count();
    const std::size_t modelCountSize = static_cast<std::size_t>( modelCount );
    sample.bodies.clear();
    ReserveReplayRecorderSampleVector( sample.bodies,
                                       modelCountSize,
                                       sample.frameIndex,
                                       "ReplaySolverFrameSample::bodies" );

    m_contactCountScratch.assign( modelCountSize, 0 );
    m_maxPenetrationScratch.assign( modelCountSize, 0.0f );
    m_normalImpulseSumScratch.assign( modelCountSize, 0.0f );

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

    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        ReplaySolverBodySample body;
        if ( !BuildReplaySolverBodySample( i, models, bodyStore, colliderStore, body ) )
        {
            continue;
        }
        body.sleeping = bodyIndex < sleepStates.size() && sleepStates[bodyIndex] != 0;
        body.sleepSupported = bodyIndex < sleepSupportedStates.size() && sleepSupportedStates[bodyIndex] != 0;
        body.sleepInhibited = bodyIndex < sleepInhibitedStates.size() && sleepInhibitedStates[bodyIndex] != 0;
        body.collisionContact = bodyIndex < collisionContacts.size() && collisionContacts[bodyIndex] != 0;
        body.sleepIslandVisualId = bodyIndex < sleepIslandIds.size() ? sleepIslandIds[bodyIndex] : 0;
        body.contactCount = bodyIndex < m_contactCountScratch.size() ? m_contactCountScratch[bodyIndex] : 0;
        body.maxPenetration = bodyIndex < m_maxPenetrationScratch.size() ? m_maxPenetrationScratch[bodyIndex] : 0.0f;
        body.normalImpulseSum =
            bodyIndex < m_normalImpulseSumScratch.size() ? m_normalImpulseSumScratch[bodyIndex] : 0.0f;

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

uint64_t ReplaySolverRecorder::CollectMemoryBytes() const
{
    uint64_t bytes = static_cast<uint64_t>( sizeof( *this ) );
    bytes += VectorCapacityBytes( m_samples );
    bytes += VectorCapacityBytes( m_checkpoints );
    bytes += VectorCapacityBytes( m_contactCountScratch );
    bytes += VectorCapacityBytes( m_maxPenetrationScratch );
    bytes += VectorCapacityBytes( m_normalImpulseSumScratch );
    for ( const ReplaySolverFrameSample& sample : m_samples )
    {
        bytes += SolverFrameSampleMemoryBytes( sample );
    }
    return bytes;
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

uint64_t ReplayEventRecorder::CollectMemoryBytes() const
{
    return static_cast<uint64_t>( sizeof( *this ) ) + VectorCapacityBytes( m_events );
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
