/*
File: SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
Purpose:
  Captures bounded replay presentation and solver-state samples.

Summary:
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
#include "ReplayRetainedMemory.h"

#include "../CameraCollection.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../Allocation/RuntimeReserveAllocator.h"
#include "../../Core/Common.h"
#include "../../Core/ByteView.h"
#include "../../Core/FatalError.h"
#include "../Scene/SceneEntityStore.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Environment::WorldEnvironment;
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
constexpr std::size_t REPLAY_RECORDER_SAMPLE_INITIAL_CAPACITY = 128u;
constexpr std::size_t REPLAY_RECORDER_SAMPLE_GROWTH_CHUNK = 256u;
// Runtime allocation policy: retained replay body payloads now grow per active
// scene size instead of preallocating every future slot at game_model_capacity.
// The hard byte cap is per vector reserve request and growth count is telemetry.
constexpr int REPLAY_RECORDER_SAMPLE_RESERVE_GROWTH_LIMIT =
    RuntimeAllocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;
constexpr uint64_t FNV64_OFFSET = 14695981039346656037ull;
constexpr uint64_t FNV64_PRIME = 1099511628211ull;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED = 1u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED = 2u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED = 4u;
constexpr uint32_t REPLAY_LAUNCHER_FIRE_PROJECTILE = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_FIXED = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_TERRAIN_ALIGN = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;

int ReplayRuntimeBodyCapacity( const ReplayRecorderConfig& config )
{
    return std::clamp( config.runtimeBodyCapacity, 1, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
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
    if ( requestedCapacity > static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS ) )
    {
        return requestedCapacity;
    }

    const std::size_t doubled = currentCapacity > 0u ? currentCapacity * 2u : REPLAY_RECORDER_SAMPLE_INITIAL_CAPACITY;
    const std::size_t remainder = requestedCapacity % REPLAY_RECORDER_SAMPLE_GROWTH_CHUNK;
    const std::size_t chunked =
        remainder == 0u ? requestedCapacity : requestedCapacity + ( REPLAY_RECORDER_SAMPLE_GROWTH_CHUNK - remainder );
    const std::size_t reserveCapacity = (std::max)( doubled, chunked );
    return (std::min)( reserveCapacity,
                       static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS ) );
}

std::size_t ReplayRecorderDeltaReserveCapacity( std::size_t currentCapacity, std::size_t requestedCapacity )
{
    // Why: solver-world deltas include contact/debug vectors whose natural
    // capacity is not bounded by SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS, so they use the byte-budget
    // reserve gate without the body-vector element-count clamp.
    if ( requestedCapacity <= currentCapacity )
    {
        return currentCapacity;
    }

    const std::size_t doubled = currentCapacity > 0u ? currentCapacity * 2u : REPLAY_RECORDER_SAMPLE_INITIAL_CAPACITY;
    const std::size_t remainder = requestedCapacity % REPLAY_RECORDER_SAMPLE_GROWTH_CHUNK;
    const std::size_t chunked =
        remainder == 0u ? requestedCapacity : requestedCapacity + ( REPLAY_RECORDER_SAMPLE_GROWTH_CHUNK - remainder );
    return (std::max)( doubled, chunked );
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
    if ( requestedCapacity > static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS ) )
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

    // Invariant: policy approval is required even when allocation-hook
    // measurement is off; guard mode changes attribution, not the hard cap.
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

    if ( requestedCapacity > values.capacity() )
    {
        ReportReplayRecorderReserveFailure( targetName, requestedCapacity, requestedBytes );
    }
}

template <typename T>
void ReserveReplayRecorderDeltaVector( std::vector<T>& values,
                                       std::size_t requestedCapacity,
                                       ReplayFrameIndex frameIndex,
                                       const char* targetName )
{
    if ( requestedCapacity <= values.capacity() )
    {
        return;
    }

    const std::size_t reserveCapacity = ReplayRecorderDeltaReserveCapacity( values.capacity(), requestedCapacity );
    const uint64_t oldBytes = ReplayRecorderVectorBytes<T>( values.capacity() );
    const uint64_t requestedBytes = ReplayRecorderVectorBytes<T>( reserveCapacity );
    if ( requestedBytes > static_cast<uint64_t>( REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES ) ||
         oldBytes > static_cast<uint64_t>( ( std::numeric_limits<int>::max )() ) ||
         requestedBytes > static_cast<uint64_t>( ( std::numeric_limits<int>::max )() ) )
    {
        ReportReplayRecorderReserveFailure( targetName, reserveCapacity, requestedBytes );
    }

    // Invariant: delta payloads share the recorder owner's aggregate byte cap
    // with body vectors instead of receiving a per-vector 64 MiB allowance.
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

// Invariant: this list mirrors ReplaySolverWorldSnapshot vector fields. Dense
// artifact reconstruction, delta storage, and memory accounting all iterate the
// same list so adding solver state cannot silently miss one path.
#define REPLAY_SOLVER_WORLD_VECTOR_FIELDS( VISIT )                                                                     \
    VISIT( timeRemaining )                                                                                             \
    VISIT( sleepSupportedThisFrame )                                                                                   \
    VISIT( sleepInhibitedThisFrame )                                                                                   \
    VISIT( sleepState )                                                                                                \
    VISIT( sleepCounter )                                                                                              \
    VISIT( underwaterSleepLocked )                                                                                     \
    VISIT( tornadoCaptureSeconds )                                                                                     \
    VISIT( tornadoEjectCooldownSeconds )                                                                               \
    VISIT( collisionVisualContacts )                                                                                   \
    VISIT( sleepIslandVisualId )                                                                                       \
    VISIT( sleepIslandAssignedVisualId )                                                                               \
    VISIT( sleepSupportEdges )                                                                                         \
    VISIT( sleepIslandParent )                                                                                         \
    VISIT( sleepIslandRank )                                                                                           \
    VISIT( sleepIslandHasAwake )                                                                                       \
    VISIT( sleepIslandHasSupportAnchor )                                                                               \
    VISIT( sleepIslandEligible )                                                                                       \
    VISIT( sleepIslandCanSleep )                                                                                       \
    VISIT( persistentContacts )                                                                                        \
    VISIT( persistentContactCache )                                                                                    \
    VISIT( persistentContactCounts )                                                                                   \
    VISIT( persistentRestingContactCounts )                                                                            \
    VISIT( debugContacts )                                                                                             \
    VISIT( pipelineTrace )                                                                                             \
    VISIT( collisionCellKeys )

uint64_t ReplayRecorderScratchMemoryBytes( const std::vector<uint16_t>& contactCountScratch,
                                           const std::vector<float>& maxPenetrationScratch,
                                           const std::vector<float>& normalImpulseSumScratch )
{
    return VectorCapacityBytes( contactCountScratch ) + VectorCapacityBytes( maxPenetrationScratch ) +
           VectorCapacityBytes( normalImpulseSumScratch );
}

uint64_t LauncherVisualMemoryBytes( const ReplayLauncherVisualSample& visual )
{
    return VectorCapacityBytes( visual.rayLines ) + VectorCapacityBytes( visual.laserShots );
}

uint64_t SolverWorldSnapshotMemoryBytes( const ReplaySolverWorldSnapshot& snapshot )
{
    uint64_t bytes = 0;
    bytes += VectorCapacityBytes( snapshot.tornadoSystemConfig.vortices );
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

template <typename T> uint64_t SolverVectorDeltaMemoryBytes( const ReplaySolverVectorDelta<T>& delta )
{
    return VectorCapacityBytes( delta.fullValues ) + VectorCapacityBytes( delta.changedValues );
}

uint64_t SolverWorldScalarMemoryBytes( const ReplaySolverWorldScalarState& state )
{
    return VectorCapacityBytes( state.tornadoSystemConfig.vortices );
}

uint64_t SolverWorldDeltaFrameMemoryBytes( const ReplaySolverWorldDeltaFrame& frame )
{
    uint64_t bytes = SolverWorldScalarMemoryBytes( frame.scalarState );
#define ADD_SOLVER_WORLD_DELTA_BYTES( field ) bytes += SolverVectorDeltaMemoryBytes( frame.field );
    REPLAY_SOLVER_WORLD_VECTOR_FIELDS( ADD_SOLVER_WORLD_DELTA_BYTES )
#undef ADD_SOLVER_WORLD_DELTA_BYTES
    return bytes;
}

uint64_t PresentationSampleMemoryBytes( const ReplayPresentationSample& sample )
{
    return VectorCapacityBytes( sample.bodies );
}

uint64_t VisualDeltaFrameMemoryBytes( const ReplayVisualDeltaFrame& frame )
{
    return VectorCapacityBytes( frame.bodyMetadataIndices ) + VectorCapacityBytes( frame.changedBodies );
}

uint64_t SolverDeltaFrameMemoryBytes( const ReplaySolverDeltaFrame& frame )
{
    return VectorCapacityBytes( frame.bodyMetadataIndices ) + VectorCapacityBytes( frame.changedBodies );
}

uint32_t CheckedVisualMetadataIndex( std::size_t index )
{
    if ( index > static_cast<std::size_t>( ( std::numeric_limits<uint32_t>::max )() ) )
    {
        SB_FATAL( "Runtime/Replay",
                  "Replay visual metadata index overflow. index=%llu",
                  static_cast<unsigned long long>( index ) );
    }
    return static_cast<uint32_t>( index );
}

uint32_t CheckedSolverMetadataIndex( std::size_t index )
{
    if ( index > static_cast<std::size_t>( ( std::numeric_limits<uint32_t>::max )() ) )
    {
        SB_FATAL( "Runtime/Replay",
                  "Replay solver metadata index overflow. index=%llu",
                  static_cast<unsigned long long>( index ) );
    }
    return static_cast<uint32_t>( index );
}

uint32_t FloatBits( float value )
{
    uint32_t bits = 0;
    std::memcpy( &bits, &value, sizeof( bits ) );
    return bits;
}

int32_t SignedFloatBits( float value )
{
    const uint32_t bits = FloatBits( value );
    int32_t signedBits = 0;
    std::memcpy( &signedBits, &bits, sizeof( signedBits ) );
    return signedBits;
}

void AppendReplayEventFloatHex( char*& cursor, std::size_t& remaining, float value )
{
    if ( remaining == 0 )
    {
        return;
    }
    const int written = std::snprintf( cursor, remaining, "%08X", FloatBits( value ) );
    if ( written < 0 )
    {
        cursor[0] = '\0';
        return;
    }
    const std::size_t consumed = (std::min)( static_cast<std::size_t>( written ), remaining > 0 ? remaining - 1 : 0 );
    cursor += consumed;
    remaining -= consumed;
}

void AppendReplayEventVectorHex( char*& cursor, std::size_t& remaining, const Vector3& value )
{
    AppendReplayEventFloatHex( cursor, remaining, value.x );
    AppendReplayEventFloatHex( cursor, remaining, value.y );
    AppendReplayEventFloatHex( cursor, remaining, value.z );
}

void AppendReplayEventQuaternionHex( char*& cursor,
                                     std::size_t& remaining,
                                     const SkullbonezCore::Math::Orientation::Quaternion& value )
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
    value.GetComponents( x, y, z, w );
    AppendReplayEventFloatHex( cursor, remaining, x );
    AppendReplayEventFloatHex( cursor, remaining, y );
    AppendReplayEventFloatHex( cursor, remaining, z );
    AppendReplayEventFloatHex( cursor, remaining, w );
}

bool SameFloatBits( float a, float b )
{
    return FloatBits( a ) == FloatBits( b );
}

bool SameVectorBits( const Vector3& a, const Vector3& b )
{
    return SameFloatBits( a.x, b.x ) && SameFloatBits( a.y, b.y ) && SameFloatBits( a.z, b.z );
}

bool SameOrientationBits( const float ( &a )[4], const float ( &b )[4] )
{
    return SameFloatBits( a[0], b[0] ) && SameFloatBits( a[1], b[1] ) && SameFloatBits( a[2], b[2] ) &&
           SameFloatBits( a[3], b[3] );
}

ReplayVisualBodyMetadata VisualMetadataFromBody( const ReplayBodyPresentationSample& body )
{
    ReplayVisualBodyMetadata metadata;
    metadata.id = body.id;
    metadata.modelRow = body.modelRow;
    std::memcpy( metadata.name, body.name, sizeof( metadata.name ) );
    metadata.shapeKind = body.shapeKind;
    metadata.mass = body.mass;
    metadata.fixed = body.fixed;
    return metadata;
}

ReplayVisualBodyState VisualStateFromBody( const ReplayBodyPresentationSample& body )
{
    ReplayVisualBodyState state;
    state.position = body.position;
    state.linearVelocity = body.linearVelocity;
    state.angularVelocity = body.angularVelocity;
    state.orientation[0] = body.orientation[0];
    state.orientation[1] = body.orientation[1];
    state.orientation[2] = body.orientation[2];
    state.orientation[3] = body.orientation[3];
    state.sleeping = body.sleeping;
    state.sleepSupported = body.sleepSupported;
    state.sleepInhibited = body.sleepInhibited;
    state.collisionContact = body.collisionContact;
    state.sleepIslandVisualId = body.sleepIslandVisualId;
    state.contactCount = body.contactCount;
    state.maxPenetration = body.maxPenetration;
    state.normalImpulseSum = body.normalImpulseSum;
    return state;
}

bool SameVisualMetadata( const ReplayVisualBodyMetadata& a, const ReplayVisualBodyMetadata& b )
{
    return a.id.value == b.id.value && a.modelRow.value == b.modelRow.value && a.shapeKind == b.shapeKind &&
           SameFloatBits( a.mass, b.mass ) && a.fixed == b.fixed &&
           std::memcmp( a.name, b.name, sizeof( a.name ) ) == 0;
}

bool SameVisualState( const ReplayVisualBodyState& a, const ReplayVisualBodyState& b )
{
    return SameVectorBits( a.position, b.position ) && SameVectorBits( a.linearVelocity, b.linearVelocity ) &&
           SameVectorBits( a.angularVelocity, b.angularVelocity ) &&
           SameOrientationBits( a.orientation, b.orientation ) && a.sleeping == b.sleeping &&
           a.sleepSupported == b.sleepSupported && a.sleepInhibited == b.sleepInhibited &&
           a.collisionContact == b.collisionContact && a.sleepIslandVisualId == b.sleepIslandVisualId &&
           a.contactCount == b.contactCount && SameFloatBits( a.maxPenetration, b.maxPenetration ) &&
           SameFloatBits( a.normalImpulseSum, b.normalImpulseSum );
}

void CopyPresentationHeader( const ReplayPresentationSample& source, ReplayPresentationSample& out )
{
    out.frameIndex = source.frameIndex;
    out.branch = source.branch;
    out.eventCursor = source.eventCursor;
    out.sceneFrame = source.sceneFrame;
    out.simulationSeconds = source.simulationSeconds;
    out.physicsDt = source.physicsDt;
    out.camera = source.camera;
    out.world = source.world;
    out.stateHash = source.stateHash;
    out.contactCount = source.contactCount;
    out.pipelineRecordCount = source.pipelineRecordCount;
    out.checkpointBoundary = source.checkpointBoundary;
}

void BuildPresentationBodyFromVisual( const ReplayVisualBodyMetadata& metadata,
                                      const ReplayVisualBodyState& state,
                                      ReplayBodyPresentationSample& out )
{
    out = ReplayBodyPresentationSample{};
    out.id = metadata.id;
    out.modelRow = metadata.modelRow;
    std::memcpy( out.name, metadata.name, sizeof( out.name ) );
    out.shapeKind = metadata.shapeKind;
    out.position = state.position;
    out.linearVelocity = state.linearVelocity;
    out.angularVelocity = state.angularVelocity;
    out.orientation[0] = state.orientation[0];
    out.orientation[1] = state.orientation[1];
    out.orientation[2] = state.orientation[2];
    out.orientation[3] = state.orientation[3];
    out.mass = metadata.mass;
    out.fixed = metadata.fixed;
    out.sleeping = state.sleeping;
    out.sleepSupported = state.sleepSupported;
    out.sleepInhibited = state.sleepInhibited;
    out.collisionContact = state.collisionContact;
    out.sleepIslandVisualId = state.sleepIslandVisualId;
    out.contactCount = state.contactCount;
    out.maxPenetration = state.maxPenetration;
    out.normalImpulseSum = state.normalImpulseSum;
}

ReplaySolverBodyMetadata SolverMetadataFromBody( const ReplaySolverBodySample& body )
{
    ReplaySolverBodyMetadata metadata;
    metadata.id = body.id;
    metadata.modelRow = body.modelRow;
    std::memcpy( metadata.name, body.name, sizeof( metadata.name ) );
    metadata.shapeKind = body.shapeKind;
    metadata.mass = body.mass;
    metadata.inverseMass = body.inverseMass;
    metadata.rotationalInertia = body.rotationalInertia;
    metadata.inverseRotationalInertia = body.inverseRotationalInertia;
    return metadata;
}

ReplaySolverBodyState SolverStateFromBody( const ReplaySolverBodySample& body )
{
    ReplaySolverBodyState state;
    state.position = body.position;
    state.linearVelocity = body.linearVelocity;
    state.angularVelocity = body.angularVelocity;
    state.orientation[0] = body.orientation[0];
    state.orientation[1] = body.orientation[1];
    state.orientation[2] = body.orientation[2];
    state.orientation[3] = body.orientation[3];
    state.fixed = body.fixed;
    state.sleeping = body.sleeping;
    state.sleepSupported = body.sleepSupported;
    state.sleepInhibited = body.sleepInhibited;
    state.collisionContact = body.collisionContact;
    state.sleepIslandVisualId = body.sleepIslandVisualId;
    state.contactCount = body.contactCount;
    state.maxPenetration = body.maxPenetration;
    state.normalImpulseSum = body.normalImpulseSum;
    return state;
}

bool SameSolverMetadata( const ReplaySolverBodyMetadata& a, const ReplaySolverBodyMetadata& b )
{
    return a.id.value == b.id.value && a.modelRow.value == b.modelRow.value && a.shapeKind == b.shapeKind &&
           SameFloatBits( a.mass, b.mass ) && SameFloatBits( a.inverseMass, b.inverseMass ) &&
           SameVectorBits( a.rotationalInertia, b.rotationalInertia ) &&
           SameVectorBits( a.inverseRotationalInertia, b.inverseRotationalInertia ) &&
           std::memcmp( a.name, b.name, sizeof( a.name ) ) == 0;
}

bool SameSolverState( const ReplaySolverBodyState& a, const ReplaySolverBodyState& b )
{
    return SameVectorBits( a.position, b.position ) && SameVectorBits( a.linearVelocity, b.linearVelocity ) &&
           SameVectorBits( a.angularVelocity, b.angularVelocity ) &&
           SameOrientationBits( a.orientation, b.orientation ) && a.fixed == b.fixed && a.sleeping == b.sleeping &&
           a.sleepSupported == b.sleepSupported && a.sleepInhibited == b.sleepInhibited &&
           a.collisionContact == b.collisionContact && a.sleepIslandVisualId == b.sleepIslandVisualId &&
           a.contactCount == b.contactCount && SameFloatBits( a.maxPenetration, b.maxPenetration ) &&
           SameFloatBits( a.normalImpulseSum, b.normalImpulseSum );
}

void BuildSolverBodyFromCompact( const ReplaySolverBodyMetadata& metadata,
                                 const ReplaySolverBodyState& state,
                                 ReplaySolverBodySample& out )
{
    out = ReplaySolverBodySample{};
    out.id = metadata.id;
    out.modelRow = metadata.modelRow;
    std::memcpy( out.name, metadata.name, sizeof( out.name ) );
    out.shapeKind = metadata.shapeKind;
    out.position = state.position;
    out.linearVelocity = state.linearVelocity;
    out.angularVelocity = state.angularVelocity;
    out.orientation[0] = state.orientation[0];
    out.orientation[1] = state.orientation[1];
    out.orientation[2] = state.orientation[2];
    out.orientation[3] = state.orientation[3];
    out.mass = metadata.mass;
    out.inverseMass = metadata.inverseMass;
    out.rotationalInertia = metadata.rotationalInertia;
    out.inverseRotationalInertia = metadata.inverseRotationalInertia;
    out.fixed = state.fixed;
    out.sleeping = state.sleeping;
    out.sleepSupported = state.sleepSupported;
    out.sleepInhibited = state.sleepInhibited;
    out.collisionContact = state.collisionContact;
    out.sleepIslandVisualId = state.sleepIslandVisualId;
    out.contactCount = state.contactCount;
    out.maxPenetration = state.maxPenetration;
    out.normalImpulseSum = state.normalImpulseSum;
}

void CopyTornadoSystemConfigWithReserve( Physics::TornadoSystemConfig& target,
                                         const Physics::TornadoSystemConfig& source,
                                         ReplayFrameIndex frameIndex,
                                         const char* targetName )
{
    ReserveReplayRecorderDeltaVector( target.vortices, source.vortices.size(), frameIndex, targetName );
    target = source;
}

void CopySolverWorldScalarsFromSnapshot( ReplaySolverWorldScalarState& target,
                                         const ReplaySolverWorldSnapshot& source,
                                         ReplayFrameIndex frameIndex,
                                         const char* targetName )
{
    target.version = source.version;
    target.modelCount = source.modelCount;
    target.nextSleepIslandVisualId = source.nextSleepIslandVisualId;
    target.sleepEnabled = source.sleepEnabled;
    target.collisionVisualFrameActive = source.collisionVisualFrameActive;
    target.tornadoConfig = source.tornadoConfig;
    CopyTornadoSystemConfigWithReserve( target.tornadoSystemConfig,
                                        source.tornadoSystemConfig,
                                        frameIndex,
                                        targetName );
    target.tornadoSystemElapsedSeconds = source.tornadoSystemElapsedSeconds;
    target.solverStats = source.solverStats;
}

void ApplySolverWorldScalarsToSnapshot( const ReplaySolverWorldScalarState& source,
                                        ReplaySolverWorldSnapshot& target,
                                        ReplayFrameIndex frameIndex,
                                        const char* targetName )
{
    target.version = source.version;
    target.modelCount = source.modelCount;
    target.nextSleepIslandVisualId = source.nextSleepIslandVisualId;
    target.sleepEnabled = source.sleepEnabled;
    target.collisionVisualFrameActive = source.collisionVisualFrameActive;
    target.tornadoConfig = source.tornadoConfig;
    CopyTornadoSystemConfigWithReserve( target.tornadoSystemConfig,
                                        source.tornadoSystemConfig,
                                        frameIndex,
                                        targetName );
    target.tornadoSystemElapsedSeconds = source.tornadoSystemElapsedSeconds;
    target.solverStats = source.solverStats;
}

template <typename T> bool SameSolverValueBytes( const T& a, const T& b )
{
    // Why: the solver hash treats these rows as exact replay state. Byte
    // comparison may over-report changes when padding differs, but it never
    // drops a restore-visible field from the delta stream.
    return std::memcmp( &a, &b, sizeof( T ) ) == 0;
}

template <typename T> void ClearSolverVectorDelta( ReplaySolverVectorDelta<T>& delta )
{
    delta.full = false;
    delta.fullValues.clear();
    delta.changedValues.clear();
}

void ClearSolverWorldDeltaFrame( ReplaySolverWorldDeltaFrame& frame )
{
    frame.scalarState.version = 2;
    frame.scalarState.modelCount = 0;
    frame.scalarState.nextSleepIslandVisualId = 1;
    frame.scalarState.sleepEnabled = true;
    frame.scalarState.collisionVisualFrameActive = false;
    frame.scalarState.tornadoConfig = Physics::TornadoFieldConfig{};
    frame.scalarState.tornadoSystemConfig.enabled = false;
    frame.scalarState.tornadoSystemConfig.visualizeVelocityField = false;
    frame.scalarState.tornadoSystemConfig.vortices.clear();
    frame.scalarState.tornadoSystemElapsedSeconds = 0.0f;
    frame.scalarState.solverStats = ReplaySolverStatsSample{};
#define CLEAR_SOLVER_WORLD_DELTA_FIELD( field ) ClearSolverVectorDelta( frame.field );
    REPLAY_SOLVER_WORLD_VECTOR_FIELDS( CLEAR_SOLVER_WORLD_DELTA_FIELD )
#undef CLEAR_SOLVER_WORLD_DELTA_FIELD
}

template <typename T>
void StoreSolverVectorDelta( ReplaySolverVectorDelta<T>& delta,
                             const std::vector<T>& source,
                             const std::vector<T>& previous,
                             bool forceFull,
                             ReplayFrameIndex frameIndex,
                             const char* targetName )
{
    ClearSolverVectorDelta( delta );
    if ( forceFull || previous.size() != source.size() )
    {
        delta.full = true;
        ReserveReplayRecorderDeltaVector( delta.fullValues, source.size(), frameIndex, targetName );
        delta.fullValues = source;
        return;
    }

    for ( std::size_t i = 0; i < source.size(); ++i )
    {
        if ( SameSolverValueBytes( source[i], previous[i] ) )
        {
            continue;
        }
        ReserveReplayRecorderDeltaVector( delta.changedValues,
                                          delta.changedValues.size() + 1u,
                                          frameIndex,
                                          targetName );
        ReplaySolverIndexedValue<T> changed;
        changed.index = CheckedSolverMetadataIndex( i );
        changed.value = source[i];
        delta.changedValues.push_back( changed );
    }
}

template <typename T>
bool ApplySolverVectorDelta( const ReplaySolverVectorDelta<T>& delta,
                             std::vector<T>& target,
                             ReplayFrameIndex frameIndex,
                             const char* targetName )
{
    if ( delta.full )
    {
        ReserveReplayRecorderDeltaVector( target, delta.fullValues.size(), frameIndex, targetName );
        target = delta.fullValues;
        return true;
    }

    for ( const ReplaySolverIndexedValue<T>& changed : delta.changedValues )
    {
        const std::size_t index = static_cast<std::size_t>( changed.index );
        if ( index >= target.size() )
        {
            return false;
        }
        target[index] = changed.value;
    }
    return true;
}

void CopySolverWorldSnapshotWithReserve( ReplaySolverWorldSnapshot& target,
                                         const ReplaySolverWorldSnapshot& source,
                                         ReplayFrameIndex frameIndex,
                                         const char* targetName )
{
    target.version = source.version;
    target.modelCount = source.modelCount;
    target.nextSleepIslandVisualId = source.nextSleepIslandVisualId;
    target.sleepEnabled = source.sleepEnabled;
    target.collisionVisualFrameActive = source.collisionVisualFrameActive;
    target.tornadoConfig = source.tornadoConfig;
    CopyTornadoSystemConfigWithReserve( target.tornadoSystemConfig,
                                        source.tornadoSystemConfig,
                                        frameIndex,
                                        targetName );
    target.tornadoSystemElapsedSeconds = source.tornadoSystemElapsedSeconds;
    target.solverStats = source.solverStats;
#define COPY_SOLVER_WORLD_VECTOR_FIELD( field )                                                                        \
    ReserveReplayRecorderDeltaVector( target.field, source.field.size(), frameIndex, targetName );                     \
    target.field = source.field;
    REPLAY_SOLVER_WORLD_VECTOR_FIELDS( COPY_SOLVER_WORLD_VECTOR_FIELD )
#undef COPY_SOLVER_WORLD_VECTOR_FIELD
}

void ClearSolverWorldSnapshotValues( ReplaySolverWorldSnapshot& snapshot )
{
    snapshot.version = 2;
    snapshot.modelCount = 0;
    snapshot.nextSleepIslandVisualId = 1;
    snapshot.sleepEnabled = true;
    snapshot.collisionVisualFrameActive = false;
    snapshot.tornadoConfig = Physics::TornadoFieldConfig{};
    snapshot.tornadoSystemConfig.enabled = false;
    snapshot.tornadoSystemConfig.visualizeVelocityField = false;
    snapshot.tornadoSystemConfig.vortices.clear();
    snapshot.tornadoSystemElapsedSeconds = 0.0f;
    snapshot.solverStats = ReplaySolverStatsSample{};
#define CLEAR_SOLVER_WORLD_SNAPSHOT_FIELD( field ) snapshot.field.clear();
    REPLAY_SOLVER_WORLD_VECTOR_FIELDS( CLEAR_SOLVER_WORLD_SNAPSHOT_FIELD )
#undef CLEAR_SOLVER_WORLD_SNAPSHOT_FIELD
}

void StoreSolverWorldDeltaFrame( ReplaySolverWorldDeltaFrame& frame,
                                 const ReplaySolverWorldSnapshot& snapshot,
                                 const ReplaySolverWorldSnapshot& previous,
                                 bool forceKeyframe,
                                 ReplayFrameIndex frameIndex )
{
    // Concept: world-snapshot vectors are compacted independently. A solver
    // frame can carry a full payload for one vector whose length changed while
    // other vectors stay as sparse indexed edits.
    ClearSolverWorldDeltaFrame( frame );
    CopySolverWorldScalarsFromSnapshot( frame.scalarState, snapshot, frameIndex, "ReplaySolverWorldDelta::scalar" );
#define STORE_SOLVER_WORLD_DELTA_FIELD( field )                                                                        \
    StoreSolverVectorDelta( frame.field,                                                                               \
                            snapshot.field,                                                                            \
                            previous.field,                                                                            \
                            forceKeyframe,                                                                             \
                            frameIndex,                                                                                \
                            "ReplaySolverWorldDelta::" #field );
    REPLAY_SOLVER_WORLD_VECTOR_FIELDS( STORE_SOLVER_WORLD_DELTA_FIELD )
#undef STORE_SOLVER_WORLD_DELTA_FIELD
}

bool ApplySolverWorldDeltaFrame( const ReplaySolverWorldDeltaFrame& frame,
                                 ReplaySolverWorldSnapshot& snapshot,
                                 ReplayFrameIndex frameIndex )
{
    ApplySolverWorldScalarsToSnapshot( frame.scalarState, snapshot, frameIndex, "ReplaySolverWorldResolve::scalar" );
#define APPLY_SOLVER_WORLD_DELTA_FIELD( field )                                                                        \
    if ( !ApplySolverVectorDelta( frame.field, snapshot.field, frameIndex, "ReplaySolverWorldResolve::" #field ) )     \
    {                                                                                                                  \
        return false;                                                                                                  \
    }
    REPLAY_SOLVER_WORLD_VECTOR_FIELDS( APPLY_SOLVER_WORLD_DELTA_FIELD )
#undef APPLY_SOLVER_WORLD_DELTA_FIELD
    return true;
}

void CopySolverHeader( const ReplaySolverFrameSample& source, ReplaySolverFrameSample& out )
{
    out.frameIndex = source.frameIndex;
    out.branch = source.branch;
    out.eventCursor = source.eventCursor;
    out.sceneFrame = source.sceneFrame;
    out.simulationSeconds = source.simulationSeconds;
    out.physicsDt = source.physicsDt;
    out.camera = source.camera;
    out.world = source.world;
    ReserveReplayRecorderDeltaVector( out.launcherVisual.rayLines,
                                      source.launcherVisual.rayLines.size(),
                                      source.frameIndex,
                                      "ReplaySolverResolve::launcherRayLines" );
    ReserveReplayRecorderDeltaVector( out.launcherVisual.laserShots,
                                      source.launcherVisual.laserShots.size(),
                                      source.frameIndex,
                                      "ReplaySolverResolve::launcherLaserShots" );
    out.launcherVisual = source.launcherVisual;
    out.presentationHash = source.presentationHash;
    out.solverHash = source.solverHash;
    out.contactCount = source.contactCount;
    out.pipelineRecordCount = source.pipelineRecordCount;
    out.checkpointBoundary = source.checkpointBoundary;
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

uint64_t HashBytes( uint64_t hash, SkullbonezCore::Core::ByteView bytes )
{
    for ( uint8_t byte : bytes )
    {
        hash = HashByte( hash, byte );
    }
    return hash;
}

uint64_t HashUint32( uint64_t hash, uint32_t value )
{
    return HashBytes( hash, SkullbonezCore::Core::ObjectBytes( value ) );
}

uint64_t HashUint64( uint64_t hash, uint64_t value )
{
    return HashBytes( hash, SkullbonezCore::Core::ObjectBytes( value ) );
}

uint64_t HashInt64( uint64_t hash, int64_t value )
{
    return HashBytes( hash, SkullbonezCore::Core::ObjectBytes( value ) );
}

uint64_t HashSize( uint64_t hash, std::size_t value )
{
    return HashUint64( hash, static_cast<uint64_t>( value ) );
}

uint64_t HashInt( uint64_t hash, int value )
{
    const int32_t packed = static_cast<int32_t>( value );
    return HashBytes( hash, SkullbonezCore::Core::ObjectBytes( packed ) );
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
        hash = HashBytes( hash, SkullbonezCore::Core::ObjectBytes( value ) );
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
    hash = HashInt( hash, body.modelRow.value );
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
    hash = HashInt( hash, body.modelRow.value );
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

// Concept: replay body samples borrow SceneEntityStore for stable display names.
// Physics values come from dense stores; transient legacy object record rows are irrelevant
// to capture identity and durable presentation intent.
bool BuildReplayPresentationBodySample( int modelIndex,
                                        const SceneEntityStore& entities,
                                        const Physics::PhysicsBodyStore& bodyStore,
                                        const Physics::ColliderStore& colliderStore,
                                        ReplayBodyPresentationSample& outBody )
{
    if ( modelIndex < 0 || modelIndex >= bodyStore.Count() || modelIndex >= colliderStore.Count() )
    {
        return false;
    }

    const SceneEntityRecord* entity = entities.TryGet( modelIndex );
    if ( !entity )
    {
        return false;
    }

    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    const Physics::PhysicsBodyRecord& bodyRecord = bodyStore.Records()[bodyIndex];
    const auto hotFields = bodyStore.HotFields();
    const ColliderRecord& colliderRecord = colliderStore.Records()[bodyIndex];

    outBody = ReplayBodyPresentationSample{};
    outBody.id.value = bodyRecord.replayBodyId;
    outBody.modelRow = Physics::MakeModelRowHint( modelIndex );
    const char* modelName = entity->displayName;
    if ( modelName && modelName[0] != '\0' )
    {
        strncpy_s( outBody.name, sizeof( outBody.name ), modelName, _TRUNCATE );
    }
    outBody.shapeKind = ShapeKindForCollider( colliderRecord );
    outBody.position = Physics::PhysicsBodyPosition( hotFields, bodyIndex );
    outBody.linearVelocity = Physics::PhysicsBodyLinearVelocity( hotFields, bodyIndex );
    outBody.angularVelocity = Physics::PhysicsBodyAngularVelocity( hotFields, bodyIndex );
    Physics::PhysicsBodyOrientation( hotFields, bodyIndex )
        .GetComponents( outBody.orientation[0],
                        outBody.orientation[1],
                        outBody.orientation[2],
                        outBody.orientation[3] );
    outBody.mass = bodyRecord.mass;
    outBody.fixed = hotFields.fixed[bodyIndex] != 0u;
    return true;
}

bool BuildReplaySolverBodySample( int modelIndex,
                                  const SceneEntityStore& entities,
                                  const Physics::PhysicsBodyStore& bodyStore,
                                  const Physics::ColliderStore& colliderStore,
                                  ReplaySolverBodySample& outBody )
{
    ReplayBodyPresentationSample presentationBody;
    if ( !BuildReplayPresentationBodySample( modelIndex, entities, bodyStore, colliderStore, presentationBody ) )
    {
        return false;
    }

    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    const Physics::PhysicsBodyRecord& bodyRecord = bodyStore.Records()[bodyIndex];
    const auto hotFields = bodyStore.HotFields();

    outBody = ReplaySolverBodySample{};
    outBody.id = presentationBody.id;
    outBody.modelRow = presentationBody.modelRow;
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
    outBody.inverseMass = hotFields.inverseMass[bodyIndex];
    outBody.rotationalInertia = bodyRecord.rotationalInertia;
    outBody.inverseRotationalInertia = Physics::PhysicsBodyInverseInertia( hotFields, bodyIndex );
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

ReplayEventCommand SkullbonezCore::Runtime::ReplayEventCommandOperations::BuildCommand( ReplayEventKind kind,
                                                                                        ReplayFrameIndex frameIndex,
                                                                                        bool useNextFrame,
                                                                                        uint32_t flags,
                                                                                        int32_t value0,
                                                                                        int32_t value1,
                                                                                        int32_t value2,
                                                                                        int32_t value3,
                                                                                        uint64_t data0,
                                                                                        const char* text )
{
    ReplayEventCommand command;
    command.frameIndex = frameIndex;
    command.kind = kind;
    command.flags = flags;
    command.value0 = value0;
    command.value1 = value1;
    command.value2 = value2;
    command.value3 = value3;
    command.data0 = data0;
    command.useNextFrame = useNextFrame;
    if ( text && text[0] != '\0' )
    {
        strncpy_s( command.text, sizeof( command.text ), text, _TRUNCATE );
    }
    return command;
}

ReplayEventCommand
SkullbonezCore::Runtime::ReplayEventCommandOperations::BuildGeneratedSceneConfig( uint32_t flags,
                                                                                  int modelCount,
                                                                                  int solverBallCount,
                                                                                  int solverBoxCount,
                                                                                  uint32_t rngSeed,
                                                                                  int sceneObjectCapacity,
                                                                                  uint32_t generatedObjectTypeOverride )
{
    uint64_t hash = FNV64_OFFSET;
    hash = HashInt( hash, modelCount );
    hash = HashInt( hash, solverBallCount );
    hash = HashInt( hash, solverBoxCount );
    hash = HashInt( hash, static_cast<int32_t>( rngSeed ) );
    hash = HashInt( hash, sceneObjectCapacity );
    hash = HashInt( hash, static_cast<int32_t>( generatedObjectTypeOverride ) );
    return BuildCommand( ReplayEventKind::GeneratedSceneConfig,
                         0,
                         false,
                         flags,
                         modelCount,
                         solverBallCount,
                         solverBoxCount,
                         static_cast<int32_t>( rngSeed ),
                         hash,
                         "generated_scene_config" );
}

ReplayEventCommand
SkullbonezCore::Runtime::ReplayEventCommandOperations::BuildWorldOverride( float previousGravity,
                                                                           float previousFluidHeight,
                                                                           float previousFluidDensity,
                                                                           float gravity,
                                                                           float fluidHeight,
                                                                           float fluidDensity )
{
    uint32_t flags = 0;
    flags |= previousGravity != gravity ? REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED : 0u;
    flags |= previousFluidHeight != fluidHeight ? REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED : 0u;
    flags |= previousFluidDensity != fluidDensity ? REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED : 0u;
    if ( flags == 0 )
    {
        return {};
    }
    uint64_t hash = FNV64_OFFSET;
    hash = HashFloat( hash, gravity );
    hash = HashFloat( hash, fluidHeight );
    hash = HashFloat( hash, fluidDensity );
    return BuildCommand( ReplayEventKind::WorldOverride,
                         0,
                         true,
                         flags,
                         SignedFloatBits( gravity ),
                         SignedFloatBits( fluidHeight ),
                         SignedFloatBits( fluidDensity ),
                         0,
                         hash,
                         "world_override" );
}

ReplayEventCommand SkullbonezCore::Runtime::ReplayEventCommandOperations::BuildLauncherConfig( uint32_t changedFlags,
                                                                                               float impulseStrength,
                                                                                               float projectileSpeed )
{
    if ( changedFlags == 0 )
    {
        return {};
    }
    uint64_t hash = FNV64_OFFSET;
    hash = HashFloat( hash, impulseStrength );
    hash = HashFloat( hash, projectileSpeed );
    return BuildCommand( ReplayEventKind::LauncherConfig,
                         0,
                         true,
                         changedFlags,
                         SignedFloatBits( impulseStrength ),
                         SignedFloatBits( projectileSpeed ),
                         0,
                         0,
                         hash,
                         "launcher_config" );
}

ReplayEventCommand
SkullbonezCore::Runtime::ReplayEventCommandOperations::BuildLauncherFire( const Vector3& rayOrigin,
                                                                          const Vector3& rayDirection,
                                                                          const Vector3& cameraUp,
                                                                          bool projectile,
                                                                          float impulseStrength,
                                                                          float projectileSpeed,
                                                                          int modelCount )
{
    char payload[96] = {};
    char* cursor = payload;
    std::size_t remaining = sizeof( payload );
    const int prefixWritten = std::snprintf( cursor, remaining, "ray9:" );
    if ( prefixWritten > 0 )
    {
        const std::size_t consumed =
            (std::min)( static_cast<std::size_t>( prefixWritten ), remaining > 0 ? remaining - 1 : 0 );
        cursor += consumed;
        remaining -= consumed;
    }
    AppendReplayEventVectorHex( cursor, remaining, rayOrigin );
    AppendReplayEventVectorHex( cursor, remaining, rayDirection );
    AppendReplayEventVectorHex( cursor, remaining, cameraUp );

    uint64_t hash = FNV64_OFFSET;
    hash = HashVector( hash, rayOrigin );
    hash = HashVector( hash, rayDirection );
    hash = HashVector( hash, cameraUp );
    return BuildCommand( ReplayEventKind::LauncherFire,
                         0,
                         true,
                         projectile ? REPLAY_LAUNCHER_FIRE_PROJECTILE : 0u,
                         projectile ? 1 : 0,
                         SignedFloatBits( impulseStrength ),
                         SignedFloatBits( projectileSpeed ),
                         modelCount,
                         hash,
                         payload );
}

ReplayEventCommand
SkullbonezCore::Runtime::ReplayEventCommandOperations::BuildEditorPlace( int objectType,
                                                                         bool fixedObject,
                                                                         bool terrainAlign,
                                                                         int modelCountBefore,
                                                                         const Vector3& terrainPoint,
                                                                         const Vector3& placementScale,
                                                                         float placementYawRadians )
{
    char payload[80] = {};
    char* cursor = payload;
    std::size_t remaining = sizeof( payload );
    const int prefixWritten = std::snprintf( cursor, remaining, "place7:" );
    if ( prefixWritten > 0 )
    {
        const std::size_t consumed =
            (std::min)( static_cast<std::size_t>( prefixWritten ), remaining > 0 ? remaining - 1 : 0 );
        cursor += consumed;
        remaining -= consumed;
    }
    AppendReplayEventVectorHex( cursor, remaining, terrainPoint );
    AppendReplayEventVectorHex( cursor, remaining, placementScale );
    AppendReplayEventFloatHex( cursor, remaining, placementYawRadians );

    uint64_t hash = FNV64_OFFSET;
    hash = HashInt( hash, objectType );
    hash = HashInt( hash, fixedObject ? 1 : 0 );
    hash = HashInt( hash, terrainAlign ? 1 : 0 );
    hash = HashInt( hash, modelCountBefore );
    hash = HashVector( hash, terrainPoint );
    hash = HashVector( hash, placementScale );
    hash = HashFloat( hash, placementYawRadians );
    uint32_t flags = 0;
    flags |= fixedObject ? REPLAY_EDITOR_PLACE_FIXED : 0u;
    flags |= terrainAlign ? REPLAY_EDITOR_PLACE_TERRAIN_ALIGN : 0u;
    return BuildCommand( ReplayEventKind::EditorPlace,
                         0,
                         true,
                         flags,
                         objectType,
                         fixedObject ? 1 : 0,
                         terrainAlign ? 1 : 0,
                         modelCountBefore,
                         hash,
                         payload );
}

ReplayEventCommand SkullbonezCore::Runtime::ReplayEventCommandOperations::BuildEditorTransform(
    int modelIndex,
    uint32_t changedFlags,
    uint32_t replayBodyId,
    const Vector3& position,
    const SkullbonezCore::Math::Orientation::Quaternion& orientation,
    int modelCount,
    int scaleAxis,
    float scaleFactor )
{
    changedFlags &= REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE;
    if ( changedFlags == 0 )
    {
        return {};
    }
    if ( ( changedFlags & REPLAY_EDITOR_TRANSFORM_SCALE ) == 0 )
    {
        scaleAxis = -1;
        scaleFactor = 1.0f;
    }
    else if ( scaleAxis < 0 || scaleAxis > 2 || !std::isfinite( scaleFactor ) || scaleFactor <= 0.0f )
    {
        return {};
    }

    char payload[96] = {};
    char* cursor = payload;
    std::size_t remaining = sizeof( payload );
    const int prefixWritten =
        std::snprintf( cursor, remaining, ( changedFlags & REPLAY_EDITOR_TRANSFORM_SCALE ) ? "xform8:" : "xform7:" );
    if ( prefixWritten > 0 )
    {
        const std::size_t consumed =
            (std::min)( static_cast<std::size_t>( prefixWritten ), remaining > 0 ? remaining - 1 : 0 );
        cursor += consumed;
        remaining -= consumed;
    }
    AppendReplayEventVectorHex( cursor, remaining, position );
    AppendReplayEventQuaternionHex( cursor, remaining, orientation );
    if ( changedFlags & REPLAY_EDITOR_TRANSFORM_SCALE )
    {
        AppendReplayEventFloatHex( cursor, remaining, scaleFactor );
    }

    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;
    orientation.GetComponents( qx, qy, qz, qw );
    uint64_t hash = FNV64_OFFSET;
    hash = HashInt( hash, modelIndex );
    hash = HashInt( hash, static_cast<int32_t>( replayBodyId ) );
    hash = HashInt( hash, modelCount );
    hash = HashInt( hash, static_cast<int32_t>( changedFlags ) );
    hash = HashInt( hash, scaleAxis );
    hash = HashVector( hash, position );
    hash = HashFloat( hash, qx );
    hash = HashFloat( hash, qy );
    hash = HashFloat( hash, qz );
    hash = HashFloat( hash, qw );
    hash = HashFloat( hash, scaleFactor );
    return BuildCommand( ReplayEventKind::EditorTransform,
                         0,
                         true,
                         changedFlags,
                         modelIndex,
                         static_cast<int32_t>( replayBodyId ),
                         modelCount,
                         scaleAxis,
                         hash,
                         payload );
}

uint64_t SkullbonezCore::Runtime::ReplayRecorderOperations::ComputePresentationStateHash(
    const ReplayPresentationSample& sample ) noexcept
{
    uint64_t hash = FNV64_OFFSET;
    hash = HashWorld( hash, sample.world );
    hash = HashInt( hash, static_cast<int>( sample.bodies.size() ) );
    hash = HashInt( hash, static_cast<int>( sample.contactCount ) );
    hash = HashInt( hash, static_cast<int>( sample.pipelineRecordCount ) );
    for ( const ReplayBodyPresentationSample& body : sample.bodies )
    {
        hash = HashBodySample( hash, body );
    }
    return hash;
}

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
    m_visualFrames.clear();
    m_visualBodyMetadata.clear();
    m_visualCarryStates.clear();
    m_visualCarryActive.clear();
    m_visualCarrySeenScratch.clear();
    m_captureBodyScratch.clear();
    m_checkpoints.clear();
    m_contactCountScratch.clear();
    m_maxPenetrationScratch.clear();
    m_normalImpulseSumScratch.clear();
    m_resolvedPresentationSamples.clear();
    m_promotedPresentationSample.bodies.clear();
    m_resolveStateScratch.clear();
    m_resolveActiveScratch.clear();
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
    m_visualFrames.resize( m_samples.size() );
    m_resolvedPresentationSamples.resize( m_samples.size() );
    for ( ReplayPresentationSample& resolved : m_resolvedPresentationSamples )
    {
        resolved.frameIndex = ( std::numeric_limits<ReplayFrameIndex>::max )();
    }
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
    m_visualBodyMetadata.clear();
    m_visualCarryStates.clear();
    m_visualCarryActive.clear();
    m_visualCarrySeenScratch.clear();
    m_captureBodyScratch.clear();
    m_promotedPresentationSample.bodies.clear();
    m_resolveStateScratch.clear();
    m_resolveActiveScratch.clear();
    for ( ReplayPresentationSample& sample : m_resolvedPresentationSamples )
    {
        sample.bodies.clear();
        sample.frameIndex = ( std::numeric_limits<ReplayFrameIndex>::max )();
    }
    for ( ReplayVisualDeltaFrame& frame : m_visualFrames )
    {
        frame.keyframe = false;
        frame.bodyMetadataIndices.clear();
        frame.changedBodies.clear();
    }
    WriteHashLogHeader( sceneLabel );
}

void ReplayRecorder::CaptureFrame( const ReplayCaptureInput& input )
{
    if ( !m_config.enabled || !input.physics || !input.entities || !input.bodyStore || !input.colliderStore )
    {
        return;
    }

    const std::size_t sampleSlot = AcquireSampleSlotIndex();
    ReplayPresentationSample& sample = m_samples[sampleSlot];
    sample.bodies.clear();
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

    Physics::PhysicsEngine& physics = *input.physics;
    const SceneEntityStore& entities = *input.entities;
    const Physics::PhysicsBodyStore& bodyStore = *input.bodyStore;
    const Physics::ColliderStore& colliderStore = *input.colliderStore;
    const int modelCount = bodyStore.Count();
    const std::size_t modelCountSize = static_cast<std::size_t>( modelCount );
    m_captureBodyScratch.clear();
    ReserveReplayRecorderSampleVector( m_captureBodyScratch,
                                       modelCountSize,
                                       sample.frameIndex,
                                       "ReplayPresentationCapture::bodies" );

    m_contactCountScratch.assign( modelCountSize, 0 );
    m_maxPenetrationScratch.assign( modelCountSize, 0.0f );
    m_normalImpulseSumScratch.assign( modelCountSize, 0.0f );

    const std::vector<PhysicsDebugContact>& contacts = Physics::PhysicsEngine::ReadDebugContacts( physics );
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

    sample.pipelineRecordCount = SaturatingUint16( Physics::PhysicsEngine::ReadPipelineTrace( physics ).size() );

    const auto sleepStates = Physics::PhysicsEngine::ReadSleepStates( physics );
    const auto sleepSupportedStates = Physics::PhysicsEngine::ReadSleepSupportedStates( physics );
    const auto sleepInhibitedStates = Physics::PhysicsEngine::ReadSleepInhibitedStates( physics );
    const std::vector<uint8_t>& collisionContacts = Physics::PhysicsEngine::ReadCollisionVisualContacts( physics );
    const auto sleepIslandIds = Physics::PhysicsEngine::ReadSleepIslandVisualIds( physics );

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
        if ( !BuildReplayPresentationBodySample( i, entities, bodyStore, colliderStore, body ) )
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
        m_captureBodyScratch.push_back( body );
    }

    sample.stateHash = hash;
    const bool forceVisualKeyframe = sample.checkpointBoundary || m_sampleCount == 1u;
    StoreVisualFramePayload( sampleSlot, sample, m_captureBodyScratch, forceVisualKeyframe, true );
    ReplayPresentationSample& latestCapture = m_resolvedPresentationSamples[sampleSlot];
    CopyPresentationHeader( sample, latestCapture );
    ReserveReplayRecorderSampleVector( latestCapture.bodies,
                                       m_captureBodyScratch.size(),
                                       sample.frameIndex,
                                       "ReplayPresentationLatestCapture::bodies" );
    latestCapture.bodies = m_captureBodyScratch;
    m_latestStateHash = hash;
    ++m_totalFramesCaptured;

    if ( sample.checkpointBoundary )
    {
        StoreCheckpointSummary( sample, m_captureBodyScratch.size() );
    }
    WriteHashLogRow( sample, m_captureBodyScratch.size() );
}

void ReplayRecorder::CaptureFrameFromSolverSample( const ReplaySolverFrameSample& solverSample )
{
    if ( !m_config.enabled )
    {
        return;
    }

    const std::size_t sampleSlot = AcquireSampleSlotIndex();
    ReplayPresentationSample& sample = m_samples[sampleSlot];
    sample.bodies.clear();
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

    m_captureBodyScratch.clear();
    ReserveReplayRecorderSampleVector( m_captureBodyScratch,
                                       solverSample.bodies.size(),
                                       sample.frameIndex,
                                       "ReplayPresentationMirror::bodies" );
    for ( const ReplaySolverBodySample& solverBody : solverSample.bodies )
    {
        ReplayBodyPresentationSample body;
        body.id = solverBody.id;
        body.modelRow = solverBody.modelRow;
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
        m_captureBodyScratch.push_back( body );
    }

    sample.stateHash = solverSample.presentationHash;
    const bool forceVisualKeyframe = sample.checkpointBoundary || m_sampleCount == 1u;
    StoreVisualFramePayload( sampleSlot, sample, m_captureBodyScratch, forceVisualKeyframe, true );
    // Why: ReportLatestCaptureMismatch consumes this same frame immediately.
    // Keep the already-materialized body list in its resolved slot instead of
    // replaying up to a checkpoint interval of compact presentation deltas.
    ReplayPresentationSample& latestCapture = m_resolvedPresentationSamples[sampleSlot];
    CopyPresentationHeader( sample, latestCapture );
    ReserveReplayRecorderSampleVector( latestCapture.bodies,
                                       m_captureBodyScratch.size(),
                                       sample.frameIndex,
                                       "ReplayPresentationLatestMirror::bodies" );
    latestCapture.bodies = m_captureBodyScratch;
    m_latestStateHash = sample.stateHash;
    ++m_totalFramesCaptured;

    if ( sample.checkpointBoundary )
    {
        StoreCheckpointSummary( sample, m_captureBodyScratch.size() );
    }
    WriteHashLogRow( sample, m_captureBodyScratch.size() );
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
    SkullbonezCore::Core::MainMemoryReplayCategoryBytes categories;
    CollectMemoryCategoryBytes( categories );
    return SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner );
}

void ReplayRecorder::CollectMemoryCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories ) const
{
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationOwner,
        static_cast<uint64_t>( sizeof( *this ) ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationSampleRecords,
        VectorCapacityBytes( m_samples ) + VectorCapacityBytes( m_resolvedPresentationSamples ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationCheckpoints,
        VectorCapacityBytes( m_checkpoints ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationScratch,
        ReplayRecorderScratchMemoryBytes( m_contactCountScratch, m_maxPenetrationScratch, m_normalImpulseSumScratch ) );
    for ( const ReplayPresentationSample& sample : m_samples )
    {
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            categories,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationBodies,
            PresentationSampleMemoryBytes( sample ) );
    }
    for ( const ReplayVisualDeltaFrame& frame : m_visualFrames )
    {
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            categories,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationBodies,
            VisualDeltaFrameMemoryBytes( frame ) );
    }
    for ( const ReplayPresentationSample& sample : m_resolvedPresentationSamples )
    {
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            categories,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationBodies,
            PresentationSampleMemoryBytes( sample ) );
    }
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationBodies,
        VectorCapacityBytes( m_visualBodyMetadata ) + VectorCapacityBytes( m_visualCarryStates ) +
            VectorCapacityBytes( m_visualCarryActive ) + VectorCapacityBytes( m_visualCarrySeenScratch ) +
            VectorCapacityBytes( m_captureBodyScratch ) + VectorCapacityBytes( m_promotedPresentationSample.bodies ) +
            VectorCapacityBytes( m_resolveStateScratch ) + VectorCapacityBytes( m_resolveActiveScratch ) );
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
        ReplayPresentationSample sample;
        if ( ResolveSampleAtOffset( i, sample ) )
        {
            outSamples.push_back( std::move( sample ) );
        }
    }
}

const ReplayPresentationSample* ReplayRecorder::LatestSample() const
{
    if ( m_sampleCount == 0 || m_samples.empty() )
    {
        return nullptr;
    }

    const std::size_t offset = m_sampleCount - 1;
    const std::size_t index = ( m_sampleHead + offset ) % m_samples.size();
    if ( m_resolvedPresentationSamples[index].frameIndex == m_samples[index].frameIndex )
    {
        return &m_resolvedPresentationSamples[index];
    }
    return ResolveSampleAtOffset( offset, m_resolvedPresentationSamples[index] ) ? &m_resolvedPresentationSamples[index]
                                                                                 : nullptr;
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
    const std::size_t resolvedOffset = (std::min)( offset, maxOffset );
    const std::size_t index = ( m_sampleHead + resolvedOffset ) % m_samples.size();
    return ResolveSampleAtOffset( resolvedOffset, m_resolvedPresentationSamples[index] )
               ? &m_resolvedPresentationSamples[index]
               : nullptr;
}


std::size_t ReplayRecorder::AcquireSampleSlotIndex()
{
    // Lifetime: returned slot indices stay valid until the next capture that
    // wraps the ring buffer onto the same slot.
    if ( m_sampleCount < m_samples.size() )
    {
        const std::size_t index = ( m_sampleHead + m_sampleCount ) % m_samples.size();
        ++m_sampleCount;
        return index;
    }

    // Invariant: when the retained head advances, the new oldest frame must be
    // self-contained. Promote it before overwriting the previous keyframe that
    // its deltas may still depend on.
    if ( m_sampleCount > 1u )
    {
        PromoteVisualFrameToKeyframe( 1u );
    }

    const std::size_t index = m_sampleHead;
    m_sampleHead = ( m_sampleHead + 1 ) % m_samples.size();
    if ( index < m_resolvedPresentationSamples.size() )
    {
        m_resolvedPresentationSamples[index].bodies.clear();
    }
    ++m_totalFramesEvicted;
    return index;
}

std::size_t ReplayRecorder::FindOrAddVisualBodyMetadata( const ReplayBodyPresentationSample& body,
                                                         ReplayFrameIndex frameIndex )
{
    const ReplayVisualBodyMetadata metadata = VisualMetadataFromBody( body );
    for ( std::size_t i = 0; i < m_visualBodyMetadata.size(); ++i )
    {
        if ( SameVisualMetadata( m_visualBodyMetadata[i], metadata ) )
        {
            return i;
        }
    }

    ReserveReplayRecorderSampleVector( m_visualBodyMetadata,
                                       m_visualBodyMetadata.size() + 1u,
                                       frameIndex,
                                       "ReplayVisualBodyMetadata" );
    m_visualBodyMetadata.push_back( metadata );

    const std::size_t requiredSize = m_visualBodyMetadata.size();
    ReserveReplayRecorderSampleVector( m_visualCarryStates, requiredSize, frameIndex, "ReplayVisualCarryStates" );
    ReserveReplayRecorderSampleVector( m_visualCarryActive, requiredSize, frameIndex, "ReplayVisualCarryActive" );
    ReserveReplayRecorderSampleVector( m_visualCarrySeenScratch, requiredSize, frameIndex, "ReplayVisualCarrySeen" );
    ReserveReplayRecorderSampleVector( m_resolveStateScratch, requiredSize, frameIndex, "ReplayVisualResolveStates" );
    ReserveReplayRecorderSampleVector( m_resolveActiveScratch, requiredSize, frameIndex, "ReplayVisualResolveActive" );
    m_visualCarryStates.resize( requiredSize );
    m_visualCarryActive.resize( requiredSize, static_cast<uint8_t>( 0 ) );
    m_visualCarrySeenScratch.resize( requiredSize, static_cast<uint8_t>( 0 ) );
    return requiredSize - 1u;
}

void ReplayRecorder::StoreVisualFramePayload( std::size_t slotIndex,
                                              const ReplayPresentationSample& sample,
                                              const std::vector<ReplayBodyPresentationSample>& bodies,
                                              bool forceKeyframe,
                                              bool updateCarry )
{
    if ( slotIndex >= m_visualFrames.size() )
    {
        return;
    }

    ReplayVisualDeltaFrame& frame = m_visualFrames[slotIndex];
    frame.keyframe = forceKeyframe;
    frame.bodyMetadataIndices.clear();
    frame.changedBodies.clear();
    ReserveReplayRecorderSampleVector( frame.bodyMetadataIndices,
                                       bodies.size(),
                                       sample.frameIndex,
                                       "ReplayVisualFrame::bodyOrder" );

    if ( updateCarry )
    {
        std::fill( m_visualCarrySeenScratch.begin(), m_visualCarrySeenScratch.end(), static_cast<uint8_t>( 0 ) );
    }

    for ( const ReplayBodyPresentationSample& body : bodies )
    {
        const std::size_t metadataIndex = FindOrAddVisualBodyMetadata( body, sample.frameIndex );
        const uint32_t packedMetadataIndex = CheckedVisualMetadataIndex( metadataIndex );
        frame.bodyMetadataIndices.push_back( packedMetadataIndex );

        const ReplayVisualBodyState state = VisualStateFromBody( body );
        const bool previousActive =
            metadataIndex < m_visualCarryActive.size() && m_visualCarryActive[metadataIndex] != 0u;
        const bool changed =
            forceKeyframe || !previousActive || !SameVisualState( m_visualCarryStates[metadataIndex], state );
        if ( changed )
        {
            ReserveReplayRecorderSampleVector( frame.changedBodies,
                                               frame.changedBodies.size() + 1u,
                                               sample.frameIndex,
                                               "ReplayVisualFrame::bodyDeltas" );
            ReplayVisualBodyDelta delta;
            delta.metadataIndex = packedMetadataIndex;
            delta.state = state;
            frame.changedBodies.push_back( delta );
        }

        if ( updateCarry )
        {
            m_visualCarryStates[metadataIndex] = state;
            m_visualCarryActive[metadataIndex] = static_cast<uint8_t>( 1 );
            m_visualCarrySeenScratch[metadataIndex] = static_cast<uint8_t>( 1 );
        }
    }

    if ( updateCarry )
    {
        for ( std::size_t i = 0; i < m_visualCarryActive.size(); ++i )
        {
            if ( m_visualCarrySeenScratch[i] == 0u )
            {
                m_visualCarryActive[i] = static_cast<uint8_t>( 0 );
            }
        }
    }
}

bool ReplayRecorder::ResolveSampleAtOffset( std::size_t offset, ReplayPresentationSample& outSample ) const
{
    if ( offset >= m_sampleCount || m_samples.empty() || m_visualFrames.size() != m_samples.size() )
    {
        return false;
    }

    std::size_t keyOffset = offset;
    for ( ;; )
    {
        const std::size_t keyIndex = ( m_sampleHead + keyOffset ) % m_samples.size();
        if ( m_visualFrames[keyIndex].keyframe )
        {
            break;
        }
        if ( keyOffset == 0u )
        {
            return false;
        }
        --keyOffset;
    }

    m_resolveStateScratch.resize( m_visualBodyMetadata.size() );
    m_resolveActiveScratch.resize( m_visualBodyMetadata.size(), static_cast<uint8_t>( 0 ) );
    std::fill( m_resolveActiveScratch.begin(), m_resolveActiveScratch.end(), static_cast<uint8_t>( 0 ) );

    for ( std::size_t frameOffset = keyOffset; frameOffset <= offset; ++frameOffset )
    {
        const std::size_t frameIndex = ( m_sampleHead + frameOffset ) % m_samples.size();
        const ReplayVisualDeltaFrame& frame = m_visualFrames[frameIndex];
        if ( frame.keyframe )
        {
            std::fill( m_resolveActiveScratch.begin(), m_resolveActiveScratch.end(), static_cast<uint8_t>( 0 ) );
        }

        for ( const ReplayVisualBodyDelta& delta : frame.changedBodies )
        {
            const std::size_t metadataIndex = static_cast<std::size_t>( delta.metadataIndex );
            if ( metadataIndex >= m_resolveStateScratch.size() )
            {
                return false;
            }
            m_resolveStateScratch[metadataIndex] = delta.state;
            m_resolveActiveScratch[metadataIndex] = static_cast<uint8_t>( 1 );
        }
    }

    const std::size_t targetIndex = ( m_sampleHead + offset ) % m_samples.size();
    const ReplayPresentationSample& source = m_samples[targetIndex];
    const ReplayVisualDeltaFrame& targetFrame = m_visualFrames[targetIndex];
    CopyPresentationHeader( source, outSample );
    outSample.bodies.clear();
    ReserveReplayRecorderSampleVector( outSample.bodies,
                                       targetFrame.bodyMetadataIndices.size(),
                                       source.frameIndex,
                                       "ReplayPresentationResolve::bodies" );
    for ( uint32_t packedMetadataIndex : targetFrame.bodyMetadataIndices )
    {
        const std::size_t metadataIndex = static_cast<std::size_t>( packedMetadataIndex );
        if ( metadataIndex >= m_visualBodyMetadata.size() || metadataIndex >= m_resolveActiveScratch.size() ||
             m_resolveActiveScratch[metadataIndex] == 0u )
        {
            return false;
        }

        ReplayBodyPresentationSample body;
        BuildPresentationBodyFromVisual( m_visualBodyMetadata[metadataIndex],
                                         m_resolveStateScratch[metadataIndex],
                                         body );
        outSample.bodies.push_back( body );
    }
    return true;
}

void ReplayRecorder::PromoteVisualFrameToKeyframe( std::size_t offset )
{
    if ( offset >= m_sampleCount || m_samples.empty() )
    {
        return;
    }

    const std::size_t index = ( m_sampleHead + offset ) % m_samples.size();
    if ( index >= m_visualFrames.size() || m_visualFrames[index].keyframe )
    {
        return;
    }

    if ( ResolveSampleAtOffset( offset, m_promotedPresentationSample ) )
    {
        StoreVisualFramePayload( index,
                                 m_promotedPresentationSample,
                                 m_promotedPresentationSample.bodies,
                                 true,
                                 false );
    }
}

void ReplayRecorder::StoreCheckpointSummary( const ReplayPresentationSample& sample, std::size_t bodyCount )
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
    checkpoint.bodyCount = static_cast<uint32_t>( (std::min)( bodyCount, static_cast<std::size_t>( 0xffffffffu ) ) );
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

void ReplayRecorder::WriteHashLogRow( const ReplayPresentationSample& sample, std::size_t bodyCount )
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
               static_cast<unsigned long long>( bodyCount ),
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
    m_solverFrames.clear();
    m_solverBodyMetadata.clear();
    m_solverCarryStates.clear();
    m_solverCarryActive.clear();
    m_solverCarrySeenScratch.clear();
    m_solverCaptureBodies.clear();
    m_checkpoints.clear();
    m_contactCountScratch.clear();
    m_maxPenetrationScratch.clear();
    m_normalImpulseSumScratch.clear();
    ClearSolverWorldSnapshotValues( m_solverCaptureWorldSnapshot );
    ClearSolverWorldSnapshotValues( m_solverWorldCarrySnapshot );
    m_solverWorldCarryActive = false;
    m_resolvedSolverSample.bodies.clear();
    m_latestResolvedSolverSample.bodies.clear();
    m_latestResolvedSolverSample.frameIndex = ( std::numeric_limits<ReplayFrameIndex>::max )();
    m_promotedSolverSample.bodies.clear();
    m_solverResolveStateScratch.clear();
    m_solverResolveActiveScratch.clear();
    ClearSolverWorldSnapshotValues( m_solverResolveWorldScratch );
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
    m_solverFrames.resize( m_samples.size() );
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
    m_solverBodyMetadata.clear();
    m_solverCarryStates.clear();
    m_solverCarryActive.clear();
    m_solverCarrySeenScratch.clear();
    m_solverCaptureBodies.clear();
    ClearSolverWorldSnapshotValues( m_solverCaptureWorldSnapshot );
    ClearSolverWorldSnapshotValues( m_solverWorldCarrySnapshot );
    m_solverWorldCarryActive = false;
    m_resolvedSolverSample.bodies.clear();
    m_latestResolvedSolverSample.bodies.clear();
    m_latestResolvedSolverSample.frameIndex = ( std::numeric_limits<ReplayFrameIndex>::max )();
    m_promotedSolverSample.bodies.clear();
    m_solverResolveStateScratch.clear();
    m_solverResolveActiveScratch.clear();
    ClearSolverWorldSnapshotValues( m_solverResolveWorldScratch );
    for ( ReplaySolverDeltaFrame& frame : m_solverFrames )
    {
        frame.keyframe = false;
        frame.bodyMetadataIndices.clear();
        frame.changedBodies.clear();
        ClearSolverWorldDeltaFrame( frame.world );
    }
    WriteHashLogHeader( sceneLabel );
}

void ReplaySolverRecorder::CaptureFrame( const ReplayCaptureInput& input )
{
    if ( !m_config.enabled || !input.physics || !input.entities || !input.bodyStore || !input.colliderStore )
    {
        return;
    }

    const std::size_t sampleSlot = AcquireSampleSlotIndex();
    ReplaySolverFrameSample& sample = m_samples[sampleSlot];
    sample.bodies.clear();
    ClearSolverWorldSnapshotValues( sample.worldSnapshot );
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

    Physics::PhysicsEngine& physics = *input.physics;
    const SceneEntityStore& entities = *input.entities;
    const Physics::PhysicsBodyStore& bodyStore = *input.bodyStore;
    const Physics::ColliderStore& colliderStore = *input.colliderStore;
    const int modelCount = bodyStore.Count();
    const std::size_t modelCountSize = static_cast<std::size_t>( modelCount );
    m_solverCaptureBodies.clear();
    ReserveReplayRecorderSampleVector( m_solverCaptureBodies,
                                       modelCountSize,
                                       sample.frameIndex,
                                       "ReplaySolverCapture::bodies" );

    m_contactCountScratch.assign( modelCountSize, 0 );
    m_maxPenetrationScratch.assign( modelCountSize, 0.0f );
    m_normalImpulseSumScratch.assign( modelCountSize, 0.0f );

    const std::vector<PhysicsDebugContact>& contacts = Physics::PhysicsEngine::ReadDebugContacts( physics );
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

    sample.pipelineRecordCount = SaturatingUint16( Physics::PhysicsEngine::ReadPipelineTrace( physics ).size() );
    physics.CaptureReplaySolverSnapshot(
        m_solverCaptureWorldSnapshot,
        Physics::MakePhysicsBodyCountFromNonNegativeInt( static_cast<int>( modelCount ) ) );

    const auto sleepStates = Physics::PhysicsEngine::ReadSleepStates( physics );
    const auto sleepSupportedStates = Physics::PhysicsEngine::ReadSleepSupportedStates( physics );
    const auto sleepInhibitedStates = Physics::PhysicsEngine::ReadSleepInhibitedStates( physics );
    const std::vector<uint8_t>& collisionContacts = Physics::PhysicsEngine::ReadCollisionVisualContacts( physics );
    const auto sleepIslandIds = Physics::PhysicsEngine::ReadSleepIslandVisualIds( physics );

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
    solverHash = HashSolverWorldSnapshot( solverHash, m_solverCaptureWorldSnapshot );

    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        ReplaySolverBodySample body;
        if ( !BuildReplaySolverBodySample( i, entities, bodyStore, colliderStore, body ) )
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
        m_solverCaptureBodies.push_back( body );
    }

    sample.presentationHash = presentationHash;
    sample.solverHash = solverHash;
    const bool forceSolverKeyframe = sample.checkpointBoundary || m_sampleCount == 1u;
    StoreSolverFramePayload( sampleSlot,
                             sample,
                             m_solverCaptureBodies,
                             m_solverCaptureWorldSnapshot,
                             forceSolverKeyframe,
                             true );
    // Why: the paired presentation capture asks for LatestSample immediately.
    // Reconstructing the sample we just captured from as many as 60 compact
    // delta frames turns every physics tick into an avoidable history replay.
    // Cache one dense latest sample; arbitrary historical reads still rebuild
    // through ResolveSolverSampleAtOffset and compact retention remains bounded.
    CopySolverHeader( sample, m_latestResolvedSolverSample );
    ReserveReplayRecorderSampleVector( m_latestResolvedSolverSample.bodies,
                                       m_solverCaptureBodies.size(),
                                       sample.frameIndex,
                                       "ReplaySolverLatestCapture::bodies" );
    m_latestResolvedSolverSample.bodies = m_solverCaptureBodies;
    CopySolverWorldSnapshotWithReserve( m_latestResolvedSolverSample.worldSnapshot,
                                        m_solverCaptureWorldSnapshot,
                                        sample.frameIndex,
                                        "ReplaySolverLatestCapture::worldSnapshot" );
    m_latestSolverHash = solverHash;
    ++m_totalFramesCaptured;

    if ( sample.checkpointBoundary )
    {
        StoreCheckpointSummary( sample, m_solverCaptureBodies.size() );
    }
    WriteHashLogRow( sample, m_solverCaptureBodies.size() );
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
    SkullbonezCore::Core::MainMemoryReplayCategoryBytes categories;
    CollectMemoryCategoryBytes( categories );
    return SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::EventsOwner );
}

void ReplaySolverRecorder::CollectMemoryCategoryBytes(
    SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories ) const
{
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner,
        static_cast<uint64_t>( sizeof( *this ) ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverSampleRecords,
        VectorCapacityBytes( m_samples ) + VectorCapacityBytes( m_solverFrames ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverCheckpoints,
        VectorCapacityBytes( m_checkpoints ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverScratch,
        ReplayRecorderScratchMemoryBytes( m_contactCountScratch, m_maxPenetrationScratch, m_normalImpulseSumScratch ) );
    for ( const ReplaySolverFrameSample& sample : m_samples )
    {
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            categories,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverBodies,
            VectorCapacityBytes( sample.bodies ) );
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            categories,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverWorldState,
            SolverWorldSnapshotMemoryBytes( sample.worldSnapshot ) );
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            categories,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverLauncherVisuals,
            LauncherVisualMemoryBytes( sample.launcherVisual ) );
    }
    for ( const ReplaySolverDeltaFrame& frame : m_solverFrames )
    {
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            categories,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverBodies,
            SolverDeltaFrameMemoryBytes( frame ) );
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            categories,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverWorldState,
            SolverWorldDeltaFrameMemoryBytes( frame.world ) );
    }
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverBodies,
        VectorCapacityBytes( m_solverBodyMetadata ) + VectorCapacityBytes( m_solverCarryStates ) +
            VectorCapacityBytes( m_solverCarryActive ) + VectorCapacityBytes( m_solverCarrySeenScratch ) +
            VectorCapacityBytes( m_solverCaptureBodies ) + VectorCapacityBytes( m_resolvedSolverSample.bodies ) +
            VectorCapacityBytes( m_latestResolvedSolverSample.bodies ) +
            VectorCapacityBytes( m_promotedSolverSample.bodies ) + VectorCapacityBytes( m_solverResolveStateScratch ) +
            VectorCapacityBytes( m_solverResolveActiveScratch ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverWorldState,
        SolverWorldSnapshotMemoryBytes( m_solverCaptureWorldSnapshot ) +
            SolverWorldSnapshotMemoryBytes( m_solverWorldCarrySnapshot ) +
            SolverWorldSnapshotMemoryBytes( m_solverResolveWorldScratch ) +
            SolverWorldSnapshotMemoryBytes( m_resolvedSolverSample.worldSnapshot ) +
            SolverWorldSnapshotMemoryBytes( m_latestResolvedSolverSample.worldSnapshot ) +
            SolverWorldSnapshotMemoryBytes( m_promotedSolverSample.worldSnapshot ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverLauncherVisuals,
        LauncherVisualMemoryBytes( m_resolvedSolverSample.launcherVisual ) +
            LauncherVisualMemoryBytes( m_latestResolvedSolverSample.launcherVisual ) +
            LauncherVisualMemoryBytes( m_promotedSolverSample.launcherVisual ) );
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
        ReplaySolverFrameSample sample;
        if ( ResolveSolverSampleAtOffset( i, sample ) )
        {
            outSamples.push_back( std::move( sample ) );
        }
    }
}

const ReplaySolverFrameSample* ReplaySolverRecorder::LatestSample() const
{
    if ( m_sampleCount == 0 || m_samples.empty() )
    {
        return nullptr;
    }

    const std::size_t offset = m_sampleCount - 1;
    const std::size_t slot = ( m_sampleHead + offset ) % m_samples.size();
    if ( m_latestResolvedSolverSample.frameIndex == m_samples[slot].frameIndex )
    {
        return &m_latestResolvedSolverSample;
    }
    return ResolveSolverSampleAtOffset( offset, m_latestResolvedSolverSample ) ? &m_latestResolvedSolverSample
                                                                               : nullptr;
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
    const std::size_t resolvedOffset = (std::min)( offset, maxOffset );
    return ResolveSolverSampleAtOffset( resolvedOffset, m_resolvedSolverSample ) ? &m_resolvedSolverSample : nullptr;
}

std::size_t ReplaySolverRecorder::AcquireSampleSlotIndex()
{
    // Lifetime: compact solver frames follow the same ring position as their
    // public sample headers, so a slot index is the join key for reconstruction.
    if ( m_sampleCount < m_samples.size() )
    {
        const std::size_t index = ( m_sampleHead + m_sampleCount ) % m_samples.size();
        ++m_sampleCount;
        return index;
    }

    // Invariant: after wrap, the new oldest solver frame must no longer depend
    // on the evicted frame's body/world carry state.
    if ( m_sampleCount > 1u )
    {
        PromoteSolverFrameToKeyframe( 1u );
    }

    const std::size_t index = m_sampleHead;
    m_sampleHead = ( m_sampleHead + 1 ) % m_samples.size();
    ++m_totalFramesEvicted;
    return index;
}

std::size_t ReplaySolverRecorder::FindOrAddSolverBodyMetadata( const ReplaySolverBodySample& body,
                                                               ReplayFrameIndex frameIndex )
{
    const ReplaySolverBodyMetadata metadata = SolverMetadataFromBody( body );
    for ( std::size_t i = 0; i < m_solverBodyMetadata.size(); ++i )
    {
        if ( SameSolverMetadata( m_solverBodyMetadata[i], metadata ) )
        {
            return i;
        }
    }

    ReserveReplayRecorderSampleVector( m_solverBodyMetadata,
                                       m_solverBodyMetadata.size() + 1u,
                                       frameIndex,
                                       "ReplaySolverBodyMetadata" );
    m_solverBodyMetadata.push_back( metadata );

    const std::size_t requiredSize = m_solverBodyMetadata.size();
    ReserveReplayRecorderSampleVector( m_solverCarryStates, requiredSize, frameIndex, "ReplaySolverCarryStates" );
    ReserveReplayRecorderSampleVector( m_solverCarryActive, requiredSize, frameIndex, "ReplaySolverCarryActive" );
    ReserveReplayRecorderSampleVector( m_solverCarrySeenScratch, requiredSize, frameIndex, "ReplaySolverCarrySeen" );
    ReserveReplayRecorderSampleVector( m_solverResolveStateScratch,
                                       requiredSize,
                                       frameIndex,
                                       "ReplaySolverResolveStates" );
    ReserveReplayRecorderSampleVector( m_solverResolveActiveScratch,
                                       requiredSize,
                                       frameIndex,
                                       "ReplaySolverResolveActive" );
    m_solverCarryStates.resize( requiredSize );
    m_solverCarryActive.resize( requiredSize, static_cast<uint8_t>( 0 ) );
    m_solverCarrySeenScratch.resize( requiredSize, static_cast<uint8_t>( 0 ) );
    return requiredSize - 1u;
}

void ReplaySolverRecorder::StoreSolverFramePayload( std::size_t slotIndex,
                                                    const ReplaySolverFrameSample& sample,
                                                    const std::vector<ReplaySolverBodySample>& bodies,
                                                    const ReplaySolverWorldSnapshot& worldSnapshot,
                                                    bool forceKeyframe,
                                                    bool updateCarry )
{
    // Invariant: slotIndex addresses both the retained sample header and the
    // compact solver payload. Saved replay artifacts still see a dense sample
    // because readers reconstruct through ResolveSolverSampleAtOffset().
    if ( slotIndex >= m_solverFrames.size() )
    {
        return;
    }

    ReplaySolverDeltaFrame& frame = m_solverFrames[slotIndex];
    frame.keyframe = forceKeyframe;
    frame.bodyMetadataIndices.clear();
    frame.changedBodies.clear();
    ReserveReplayRecorderSampleVector( frame.bodyMetadataIndices,
                                       bodies.size(),
                                       sample.frameIndex,
                                       "ReplaySolverFrame::bodyOrder" );

    if ( updateCarry )
    {
        std::fill( m_solverCarrySeenScratch.begin(), m_solverCarrySeenScratch.end(), static_cast<uint8_t>( 0 ) );
    }

    for ( const ReplaySolverBodySample& body : bodies )
    {
        const std::size_t metadataIndex = FindOrAddSolverBodyMetadata( body, sample.frameIndex );
        const uint32_t packedMetadataIndex = CheckedSolverMetadataIndex( metadataIndex );
        frame.bodyMetadataIndices.push_back( packedMetadataIndex );

        const ReplaySolverBodyState state = SolverStateFromBody( body );
        const bool previousActive =
            metadataIndex < m_solverCarryActive.size() && m_solverCarryActive[metadataIndex] != 0u;
        const bool changed =
            forceKeyframe || !previousActive || !SameSolverState( m_solverCarryStates[metadataIndex], state );
        if ( changed )
        {
            ReserveReplayRecorderSampleVector( frame.changedBodies,
                                               frame.changedBodies.size() + 1u,
                                               sample.frameIndex,
                                               "ReplaySolverFrame::bodyDeltas" );
            ReplaySolverBodyDelta delta;
            delta.metadataIndex = packedMetadataIndex;
            delta.state = state;
            frame.changedBodies.push_back( delta );
        }

        if ( updateCarry )
        {
            m_solverCarryStates[metadataIndex] = state;
            m_solverCarryActive[metadataIndex] = static_cast<uint8_t>( 1 );
            m_solverCarrySeenScratch[metadataIndex] = static_cast<uint8_t>( 1 );
        }
    }

    if ( updateCarry )
    {
        for ( std::size_t i = 0; i < m_solverCarryActive.size(); ++i )
        {
            if ( m_solverCarrySeenScratch[i] == 0u )
            {
                m_solverCarryActive[i] = static_cast<uint8_t>( 0 );
            }
        }
    }

    const bool worldKeyframe = forceKeyframe || !m_solverWorldCarryActive;
    StoreSolverWorldDeltaFrame( frame.world,
                                worldSnapshot,
                                m_solverWorldCarrySnapshot,
                                worldKeyframe,
                                sample.frameIndex );
    if ( updateCarry )
    {
        CopySolverWorldSnapshotWithReserve( m_solverWorldCarrySnapshot,
                                            worldSnapshot,
                                            sample.frameIndex,
                                            "ReplaySolverWorldCarry" );
        m_solverWorldCarryActive = true;
    }
}

bool ReplaySolverRecorder::ResolveSolverSampleAtOffset( std::size_t offset, ReplaySolverFrameSample& outSample ) const
{
    // Concept: public solver samples are a compatibility view over compact
    // storage. Start from the nearest retained keyframe, replay sparse deltas,
    // then rebuild the old dense body/world snapshot shape.
    if ( offset >= m_sampleCount || m_samples.empty() || m_solverFrames.size() != m_samples.size() )
    {
        return false;
    }

    std::size_t keyOffset = offset;
    for ( ;; )
    {
        const std::size_t keyIndex = ( m_sampleHead + keyOffset ) % m_samples.size();
        if ( m_solverFrames[keyIndex].keyframe )
        {
            break;
        }
        if ( keyOffset == 0u )
        {
            return false;
        }
        --keyOffset;
    }

    m_solverResolveStateScratch.resize( m_solverBodyMetadata.size() );
    m_solverResolveActiveScratch.resize( m_solverBodyMetadata.size(), static_cast<uint8_t>( 0 ) );
    std::fill( m_solverResolveActiveScratch.begin(), m_solverResolveActiveScratch.end(), static_cast<uint8_t>( 0 ) );
    ClearSolverWorldSnapshotValues( m_solverResolveWorldScratch );

    for ( std::size_t frameOffset = keyOffset; frameOffset <= offset; ++frameOffset )
    {
        const std::size_t frameIndex = ( m_sampleHead + frameOffset ) % m_samples.size();
        const ReplaySolverDeltaFrame& frame = m_solverFrames[frameIndex];
        if ( frame.keyframe )
        {
            std::fill( m_solverResolveActiveScratch.begin(),
                       m_solverResolveActiveScratch.end(),
                       static_cast<uint8_t>( 0 ) );
            ClearSolverWorldSnapshotValues( m_solverResolveWorldScratch );
        }

        for ( const ReplaySolverBodyDelta& delta : frame.changedBodies )
        {
            const std::size_t metadataIndex = static_cast<std::size_t>( delta.metadataIndex );
            if ( metadataIndex >= m_solverResolveStateScratch.size() )
            {
                return false;
            }
            m_solverResolveStateScratch[metadataIndex] = delta.state;
            m_solverResolveActiveScratch[metadataIndex] = static_cast<uint8_t>( 1 );
        }

        if ( !ApplySolverWorldDeltaFrame( frame.world, m_solverResolveWorldScratch, m_samples[frameIndex].frameIndex ) )
        {
            return false;
        }
    }

    const std::size_t targetIndex = ( m_sampleHead + offset ) % m_samples.size();
    const ReplaySolverFrameSample& source = m_samples[targetIndex];
    const ReplaySolverDeltaFrame& targetFrame = m_solverFrames[targetIndex];
    CopySolverHeader( source, outSample );
    outSample.bodies.clear();
    ReserveReplayRecorderSampleVector( outSample.bodies,
                                       targetFrame.bodyMetadataIndices.size(),
                                       source.frameIndex,
                                       "ReplaySolverResolve::bodies" );
    for ( uint32_t packedMetadataIndex : targetFrame.bodyMetadataIndices )
    {
        const std::size_t metadataIndex = static_cast<std::size_t>( packedMetadataIndex );
        if ( metadataIndex >= m_solverBodyMetadata.size() || metadataIndex >= m_solverResolveActiveScratch.size() ||
             m_solverResolveActiveScratch[metadataIndex] == 0u )
        {
            return false;
        }

        ReplaySolverBodySample body;
        BuildSolverBodyFromCompact( m_solverBodyMetadata[metadataIndex],
                                    m_solverResolveStateScratch[metadataIndex],
                                    body );
        outSample.bodies.push_back( body );
    }
    CopySolverWorldSnapshotWithReserve( outSample.worldSnapshot,
                                        m_solverResolveWorldScratch,
                                        source.frameIndex,
                                        "ReplaySolverResolve::worldSnapshot" );
    return true;
}

void ReplaySolverRecorder::PromoteSolverFrameToKeyframe( std::size_t offset )
{
    if ( offset >= m_sampleCount || m_samples.empty() )
    {
        return;
    }

    const std::size_t index = ( m_sampleHead + offset ) % m_samples.size();
    if ( index >= m_solverFrames.size() || m_solverFrames[index].keyframe )
    {
        return;
    }

    if ( ResolveSolverSampleAtOffset( offset, m_promotedSolverSample ) )
    {
        StoreSolverFramePayload( index,
                                 m_promotedSolverSample,
                                 m_promotedSolverSample.bodies,
                                 m_promotedSolverSample.worldSnapshot,
                                 true,
                                 false );
    }
}

void ReplaySolverRecorder::StoreCheckpointSummary( const ReplaySolverFrameSample& sample, std::size_t bodyCount )
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
    checkpoint.bodyCount = static_cast<uint32_t>( (std::min)( bodyCount, static_cast<std::size_t>( 0xffffffffu ) ) );
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

void ReplaySolverRecorder::WriteHashLogRow( const ReplaySolverFrameSample& sample, std::size_t bodyCount )
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
               static_cast<unsigned long long>( bodyCount ),
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
    SkullbonezCore::Core::MainMemoryReplayCategoryBytes categories;
    CollectMemoryCategoryBytes( categories );
    return SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::EventsOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedOwner );
}

void ReplayEventRecorder::CollectMemoryCategoryBytes(
    SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories ) const
{
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::EventsOwner,
        static_cast<uint64_t>( sizeof( *this ) ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes( categories,
                                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::Events,
                                                            VectorCapacityBytes( m_events ) );
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
