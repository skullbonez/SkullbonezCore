/*
File: SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp
Purpose:
  Writes and reads versioned chunked-binary replay artifacts.

Summary:
  The format is presentation-first: metadata is deduplicated into a body
  dictionary, v3 dense frames preserve complete replay-owned body visual state,
  and v4 adds exact per-tick replay packet rows plus the typed prediction state
  used by non-presenting round-trip verification. Optional solver chunks
  provide restore evidence.

Glossary:
  ABI (Application Binary Interface): Byte-level file contract used by saved
    replay artifacts and replay_query tooling.
  JSON (JavaScript Object Notation): Text metadata format used inside the
    manifest chunk.
  MANI: UTF-8 JSON manifest chunk with human-readable file facts.
  BODY: Body dictionary chunk.
  PRES: Presentation frame chunk with dense versioned visual-state records.
  BRAN: Branch provenance records for saved timeline ancestry.
  EVNT: Bounded timeline/runtime intent records needed for authoritative rollback.
  ECUR: Event cursor records attached to sparse solver checkpoints.
  HASH: Optional per-tick presentation/solver hash records.
  SCHK: Optional sparse solver checkpoint records.
  RVIS: Exact full-packet identity, count, byte-length, and digest records.
  RVPD: Typed completed-prediction state with no renderer or worker ownership.
  INDX: Frame seek index into the presentation chunk.
  POD (Plain Old Data): Trivially copyable value written as raw bytes.

Invariants:
  - Numeric payloads are emitted in the host little-endian layout used by the
    Windows runtime. The manifest marks the file as little-endian.
  - V3+ visual rows are 76 bytes and v3+ dictionary rows are 80 bytes.
  - V2 remains readable through a deterministic pose-only migration; v3 remains
    directly readable; versions newer than v4 fail closed.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
  - tools/replay_query.py
*/
#include "ReplayV2Artifact.h"

#include "ReplayArtifactSource.h"
#include "../../Core/ByteView.h"

#include "../Tools/RuntimeFileWriter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
using Json = nlohmann::ordered_json;

// Invariant: these byte counts describe the on-disk ABI for replay artifacts.
// Version 4 retains v2/v3 presentation layouts and adds RVIS packet evidence.
// Readers accept the full supported migration interval and reject future files.
constexpr uint32_t REPLAY_MINIMUM_VERSION = 2;
constexpr uint32_t REPLAY_PRESENTATION_VISUAL_VERSION = 3;
constexpr uint32_t REPLAY_CURRENT_VERSION = 4;
constexpr uint32_t REPLAY_V2_HEADER_BYTES = 40;
constexpr uint32_t REPLAY_V2_CHUNK_ENTRY_BYTES = 28;
constexpr uint32_t REPLAY_V2_BODY_DICTIONARY_ENTRY_BYTES = 76;
constexpr uint32_t REPLAY_V3_BODY_DICTIONARY_ENTRY_BYTES = 80;
constexpr uint32_t REPLAY_V2_FRAME_HEADER_BYTES = 92;
constexpr uint32_t REPLAY_V2_INDEX_ENTRY_BYTES = 24;
constexpr uint32_t REPLAY_V2_BODY_POSE_BYTES = 32;
constexpr uint32_t REPLAY_V3_BODY_VISUAL_STATE_BYTES = 76;
constexpr uint32_t REPLAY_V2_HASH_ENTRY_BYTES = 48;
constexpr uint32_t REPLAY_V2_BRANCH_ENTRY_BYTES = 64;
constexpr uint32_t REPLAY_V2_EVENT_ENTRY_BYTES = 200;
constexpr uint32_t REPLAY_V2_EVENT_CURSOR_ENTRY_BYTES = 24;
constexpr uint32_t REPLAY_V2_SOLVER_BODY_ENTRY_BYTES = 112;
constexpr uint32_t REPLAY_V4_VISUAL_PACKET_ENTRY_BYTES = 296;
constexpr char REPLAY_V2_MAGIC[8] = { 'S', 'K', 'R', 'E', 'P', 'V', '2', '\0' };

enum ReplayV2WorldFlags : uint8_t
{
    REPLAY_V2_WORLD_WATER_HIDDEN = 1u << 0,
    REPLAY_V2_WORLD_TERRAIN_HIDDEN = 1u << 1,
    REPLAY_V2_WORLD_FIXED_STEP = 1u << 2,
    REPLAY_V2_WORLD_SCENE_PHYSICS_ENABLED = 1u << 3,
    REPLAY_V2_WORLD_SCENE_TEXT_ENABLED = 1u << 4
};

struct BodyDictionaryEntry
{
    uint32_t id = 0;
    // Wire compatibility: v2 allocated these four bytes to a model index. V3
    // preserves the captured row only as a same-scene resolver hint;
    // Physics::PhysicsSceneObjectId remains the sole durable identity.
    int32_t bodyOrder = -1;
    ReplayBodyShapeKind shapeKind = ReplayBodyShapeKind::Unknown;
    float mass = 0.0f;
    bool fixed = false;
    char name[64] = {};
};

struct Chunk
{
    char id[4] = {};
    std::vector<uint8_t> bytes; // Raw little-endian payload for this chunk id.
    uint32_t recordCount = 0;
};

struct IndexedFrame
{
    ReplayFrameIndex frameIndex = 0;
    uint64_t presentationChunkOffset = 0;
    uint32_t bodyCount = 0;
};

struct BranchRecord
{
    ReplayBranchInfo branch;
    ReplayFrameIndex firstRetainedFrame = 0;
    ReplayFrameIndex lastRetainedFrame = 0;
};

struct EventCursorRecord
{
    ReplayFrameIndex frameIndex = 0;
    uint32_t eventCursor = 0;
    uint32_t flags = 0;
    uint64_t solverHash = 0;
};

struct ChunkTableEntry
{
    char id[4] = {};
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t recordCount = 0;
};

struct ByteCursor
{
    const uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
};

template <typename T> void AppendPod( std::vector<uint8_t>& out, const T& value )
{
    // Hazard: v2 chunks intentionally write POD bytes directly. Only use this
    // for fixed-layout file records whose fields are mirrored by the reader.
    static_assert( std::is_trivially_copyable<T>::value, "Replay v2 payload values must be POD" );
    const SkullbonezCore::Core::ByteView bytes = SkullbonezCore::Core::ObjectBytes( value );
    out.insert( out.end(), bytes.begin(), bytes.end() );
}

template <typename T> bool ReadPod( ByteCursor& cursor, T& out )
{
    static_assert( std::is_trivially_copyable<T>::value, "Replay v2 payload values must be POD" );
    if ( cursor.offset > cursor.size || sizeof( T ) > cursor.size - cursor.offset )
    {
        return false;
    }

    std::memcpy( &out, cursor.data + cursor.offset, sizeof( T ) );
    cursor.offset += sizeof( T );
    return true;
}

void AppendBytes( std::vector<uint8_t>& out, SkullbonezCore::Core::ByteView bytes )
{
    out.insert( out.end(), bytes.begin(), bytes.end() );
}

template <typename T> void AppendBytes( std::vector<uint8_t>& out, const T& value )
{
    static_assert( !std::is_pointer_v<T>, "AppendBytes requires an object or array extent, not a pointer." );
    AppendBytes( out, SkullbonezCore::Core::ObjectBytes( value ) );
}

template <typename T> bool ReadBytes( ByteCursor& cursor, T& out )
{
    static_assert( std::is_trivially_copyable_v<T>, "Replay byte fields must be trivially copyable." );
    constexpr std::size_t size = sizeof( T );
    if ( cursor.offset > cursor.size || size > cursor.size - cursor.offset )
    {
        return false;
    }

    std::memcpy( std::addressof( out ), cursor.data + cursor.offset, size );
    cursor.offset += size;
    return true;
}

bool SkipBytes( ByteCursor& cursor, std::size_t size )
{
    if ( cursor.offset > cursor.size || size > cursor.size - cursor.offset )
    {
        return false;
    }

    cursor.offset += size;
    return true;
}

void AppendChunkId( std::vector<uint8_t>& out, const char ( &id )[4] )
{
    // Invariant: preserving the array extent writes the four-character chunk
    // id itself; accepting a decayed pointer would serialize an address.
    AppendBytes( out, id );
}

void AppendVec3( std::vector<uint8_t>& out, const Vector3& value )
{
    AppendPod( out, value.x );
    AppendPod( out, value.y );
    AppendPod( out, value.z );
}

void AppendOrientation( std::vector<uint8_t>& out, const float orientation[4] )
{
    AppendPod( out, orientation[0] );
    AppendPod( out, orientation[1] );
    AppendPod( out, orientation[2] );
    AppendPod( out, orientation[3] );
}

uint32_t CheckedU32( std::size_t value )
{
    return value <= static_cast<std::size_t>( ( std::numeric_limits<uint32_t>::max )() )
               ? static_cast<uint32_t>( value )
               : ( std::numeric_limits<uint32_t>::max )();
}

uint8_t WorldFlags( const ReplayWorldPresentationSample& world )
{
    uint8_t flags = 0;
    if ( world.waterHidden )
    {
        flags |= REPLAY_V2_WORLD_WATER_HIDDEN;
    }
    if ( world.terrainHidden )
    {
        flags |= REPLAY_V2_WORLD_TERRAIN_HIDDEN;
    }
    if ( world.fixedStep )
    {
        flags |= REPLAY_V2_WORLD_FIXED_STEP;
    }
    if ( world.scenePhysicsEnabled )
    {
        flags |= REPLAY_V2_WORLD_SCENE_PHYSICS_ENABLED;
    }
    if ( world.sceneTextEnabled )
    {
        flags |= REPLAY_V2_WORLD_SCENE_TEXT_ENABLED;
    }
    return flags;
}

const char* ShapeKindName( ReplayBodyShapeKind kind )
{
    switch ( kind )
    {
    case ReplayBodyShapeKind::Sphere:
        return "sphere";
    case ReplayBodyShapeKind::Box:
        return "box";
    case ReplayBodyShapeKind::ConvexHull:
        return "convexHull";
    case ReplayBodyShapeKind::Unknown:
    default:
        return "unknown";
    }
}

bool SameDictionaryBody( const BodyDictionaryEntry& entry, const ReplayBodyPresentationSample& body )
{
    return entry.id == body.id.value;
}

bool SameDictionaryBody( const BodyDictionaryEntry& entry, const ReplaySolverBodySample& body )
{
    return entry.id == body.id.value;
}

uint32_t FindOrAddBody( std::vector<BodyDictionaryEntry>& dictionary, const ReplayBodyPresentationSample& body )
{
    const auto found = std::find_if(
        dictionary.begin(),
        dictionary.end(),
        [&body]( const BodyDictionaryEntry& entry ) { return SameDictionaryBody( entry, body ); }
    );

    if ( found != dictionary.end() )
    {
        return static_cast<uint32_t>( found - dictionary.begin() );
    }

    BodyDictionaryEntry entry;
    entry.id = body.id.value;
    entry.bodyOrder = body.modelRow.value;
    entry.shapeKind = body.shapeKind;
    entry.mass = body.mass;
    entry.fixed = body.fixed;
    std::memcpy( entry.name, body.name, sizeof( entry.name ) );
    dictionary.push_back( entry );
    return static_cast<uint32_t>( dictionary.size() - 1u );
}

bool FindBodyDictionaryIndex(
    const std::vector<BodyDictionaryEntry>& dictionary,
    const ReplaySolverBodySample& body,
    uint32_t& outIndex
)
{
    const auto found = std::find_if(
        dictionary.begin(),
        dictionary.end(),
        [&body]( const BodyDictionaryEntry& entry ) { return SameDictionaryBody( entry, body ); }
    );

    if ( found == dictionary.end() )
    {
        return false;
    }

    outIndex = static_cast<uint32_t>( found - dictionary.begin() );
    return true;
}

void AppendBodyDictionary( std::vector<uint8_t>& out, const std::vector<BodyDictionaryEntry>& dictionary )
{
    const uint32_t bodyCount = CheckedU32( dictionary.size() );
    AppendPod( out, bodyCount );
    for ( const BodyDictionaryEntry& entry : dictionary )
    {
        const uint8_t shapeKind = static_cast<uint8_t>( entry.shapeKind );
        const uint8_t fixed = entry.fixed ? 1u : 0u;
        const uint8_t reserved[2] = {};

        AppendPod( out, entry.id );
        AppendPod( out, entry.bodyOrder );
        AppendPod( out, shapeKind );
        AppendPod( out, fixed );
        AppendBytes( out, reserved );
        AppendPod( out, entry.mass );
        AppendBytes( out, entry.name );
    }
}

void AppendFrameHeader( std::vector<uint8_t>& out, const ReplayPresentationSample& sample )
{
    const uint8_t checkpointBoundary = sample.checkpointBoundary ? 1u : 0u;
    const uint8_t worldFlags = WorldFlags( sample.world );
    const uint16_t reserved = 0;
    const uint32_t bodyCount = CheckedU32( sample.bodies.size() );

    AppendPod( out, sample.frameIndex );
    AppendPod( out, static_cast<int32_t>( sample.sceneFrame ) );
    AppendPod( out, sample.simulationSeconds );
    AppendPod( out, sample.physicsDt );
    AppendPod( out, sample.stateHash );
    AppendPod( out, sample.contactCount );
    AppendPod( out, sample.pipelineRecordCount );
    AppendPod( out, checkpointBoundary );
    AppendPod( out, worldFlags );
    AppendPod( out, reserved );
    AppendPod( out, sample.world.gravity );
    AppendPod( out, sample.world.fluidHeight );
    AppendPod( out, sample.world.fluidDensity );
    AppendVec3( out, sample.camera.eye );
    AppendVec3( out, sample.camera.view );
    AppendVec3( out, sample.camera.up );
    AppendPod( out, bodyCount );
}

void AppendPresentationFrame(
    std::vector<uint8_t>& out,
    std::vector<BodyDictionaryEntry>& dictionary,
    const ReplayPresentationSample& sample
)
{
    AppendFrameHeader( out, sample );
    for ( const ReplayBodyPresentationSample& body : sample.bodies )
    {
        const uint8_t flags = static_cast<uint8_t>(
            ( body.sleeping ? 1u : 0u ) | ( body.sleepSupported ? 2u : 0u ) | ( body.sleepInhibited ? 4u : 0u ) |
            ( body.collisionContact ? 8u : 0u )
        );

        const uint8_t reservedFlags[3] = {};

        const uint16_t reservedContact = 0;
        const uint32_t dictionaryIndex = FindOrAddBody( dictionary, body );
        AppendPod( out, dictionaryIndex );
        AppendVec3( out, body.position );
        AppendOrientation( out, body.orientation );
        AppendVec3( out, body.linearVelocity );
        AppendVec3( out, body.angularVelocity );
        AppendPod( out, flags );
        AppendBytes( out, reservedFlags );
        AppendPod( out, static_cast<int32_t>( body.sleepIslandVisualId ) );
        AppendPod( out, body.contactCount );
        AppendPod( out, reservedContact );
        AppendPod( out, body.maxPenetration );
        AppendPod( out, body.normalImpulseSum );
    }
}

template <typename T> void AppendCountedPodVector( std::vector<uint8_t>& out, const std::vector<T>& values )
{
    AppendPod( out, CheckedU32( values.size() ) );
    for ( const T& value : values )
    {
        AppendPod( out, value );
    }
}

void AppendCountedIntVector( std::vector<uint8_t>& out, const std::vector<int>& values )
{
    AppendPod( out, CheckedU32( values.size() ) );
    for ( int value : values )
    {
        AppendPod( out, static_cast<int32_t>( value ) );
    }
}

void AppendCountedPairVector( std::vector<uint8_t>& out, const std::vector<std::pair<int, int>>& values )
{
    AppendPod( out, CheckedU32( values.size() ) );
    for ( const std::pair<int, int>& value : values )
    {
        AppendPod( out, static_cast<int32_t>( value.first ) );
        AppendPod( out, static_cast<int32_t>( value.second ) );
    }
}

void AppendTornadoConfig( std::vector<uint8_t>& out, const SkullbonezCore::Gameplay::TornadoFieldConfig& config )
{
    const uint8_t enabled = config.enabled ? 1u : 0u;
    const uint8_t visualizeVelocityField = config.visualizeVelocityField ? 1u : 0u;
    const uint8_t reserved[2] = {};

    AppendPod( out, enabled );
    AppendPod( out, visualizeVelocityField );
    AppendBytes( out, reserved );
    AppendVec3( out, config.center );
    AppendPod( out, config.radius );
    AppendPod( out, config.height );
    AppendPod( out, config.inwardAcceleration );
    AppendPod( out, config.swirlAcceleration );
    AppendPod( out, config.liftAcceleration );
    AppendPod( out, config.ejectAcceleration );
    AppendPod( out, config.ejectUpAcceleration );
    AppendPod( out, config.ejectBand );
    AppendPod( out, config.minCaptureSeconds );
    AppendPod( out, config.ejectCooldownSeconds );
    AppendPod( out, config.maxDeltaVelocity );
}


void AppendTornadoSystemConfig( std::vector<uint8_t>& out, const SkullbonezCore::Gameplay::TornadoSystemConfig& config )
{
    const uint8_t enabled = config.enabled ? 1u : 0u;
    const uint8_t visualizeVelocityField = config.visualizeVelocityField ? 1u : 0u;
    const uint8_t reserved[2] = {};

    AppendPod( out, enabled );
    AppendPod( out, visualizeVelocityField );
    AppendBytes( out, reserved );
    AppendPod( out, CheckedU32( config.vortices.size() ) );
    for ( const SkullbonezCore::Gameplay::TornadoVortexConfig& vortex : config.vortices )
    {
        AppendTornadoConfig( out, vortex.field );
        AppendPod( out, vortex.spawnSeconds );
        AppendPod( out, vortex.timeToLiveSeconds );
        AppendPod( out, vortex.growSeconds );
        AppendPod( out, vortex.shrinkSeconds );
        AppendPod( out, vortex.driftRadius );
        AppendPod( out, vortex.driftSpeed );
        AppendPod( out, vortex.driftPhase );
        AppendPod( out, vortex.repulsionRadius );
        AppendPod( out, vortex.repulsionStrength );
    }
}


void AppendSolverStats( std::vector<uint8_t>& out, const SkullbonezCore::Physics::PhysicsSolverStatsSample& stats )
{
    AppendPod( out, static_cast<int32_t>( stats.rowCount ) );
    AppendPod( out, static_cast<int32_t>( stats.cachePreviousRows ) );
    AppendPod( out, static_cast<int32_t>( stats.cacheHits ) );
    AppendPod( out, static_cast<int32_t>( stats.cacheMisses ) );
    AppendPod( out, static_cast<int32_t>( stats.warmStartedRows ) );
    AppendPod( out, static_cast<int32_t>( stats.positionCorrectionRows ) );
    AppendPod( out, static_cast<int32_t>( stats.solverIterations ) );
    AppendPod( out, stats.positionCorrectionTotal );
    AppendPod( out, stats.positionCorrectionMax );
}

void AppendPersistentContact(
    std::vector<uint8_t>& out,
    const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact
)
{
    AppendPod( out, static_cast<int32_t>( contact.bodyA ) );
    AppendPod( out, static_cast<int32_t>( contact.bodyB ) );
    AppendPod( out, contact.featureId );
    AppendPod( out, contact.key );
    AppendVec3( out, contact.normal );
    AppendVec3( out, contact.tangent1 );
    AppendVec3( out, contact.tangent2 );
    AppendVec3( out, contact.rA );
    AppendVec3( out, contact.rB );
    AppendPod( out, contact.penetration );
    AppendPod( out, contact.normalMass );
    AppendPod( out, contact.tangentMass1 );
    AppendPod( out, contact.tangentMass2 );
    AppendPod( out, contact.bias );
    AppendPod( out, contact.frictionLimit );
    AppendPod( out, contact.accN );
    AppendPod( out, contact.accT1 );
    AppendPod( out, contact.accT2 );
    const uint8_t flags[6] = {
        contact.warmStarted ? 1u : 0u,           contact.isTerrain ? 1u : 0u,
        contact.supportsRestingPolicy ? 1u : 0u, contact.allowsTangentFriction ? 1u : 0u,
        contact.normalCoupledFriction ? 1u : 0u, contact.inhibitsSleep ? 1u : 0u,
    };

    const uint8_t reserved = 0;
    AppendBytes( out, flags );
    AppendPod( out, contact.manifoldPointCount );
    AppendPod( out, reserved );
    AppendVec3( out, contact.terrainNormal );
    AppendPod( out, contact.terrainWarmStart );
}

void AppendContactCache(
    std::vector<uint8_t>& out,
    const SkullbonezCore::Physics::PhysicsSolverContactCacheSample& cache
)
{
    AppendPod( out, cache.key );
    AppendPod( out, cache.accN );
    AppendPod( out, cache.accT1 );
    AppendPod( out, cache.accT2 );
}

void AppendPhysicsDebugContact( std::vector<uint8_t>& out, const SkullbonezCore::Physics::PhysicsDebugContact& contact )
{
    AppendPod( out, static_cast<int32_t>( contact.bodyA ) );
    AppendPod( out, static_cast<int32_t>( contact.bodyB ) );
    AppendPod( out, contact.featureId );
    AppendVec3( out, contact.point );
    AppendVec3( out, contact.normal );
    AppendVec3( out, contact.tangent1 );
    AppendVec3( out, contact.tangent2 );
    AppendPod( out, contact.penetration );
    AppendPod( out, contact.normalImpulse );
}

void AppendPipelineRecord( std::vector<uint8_t>& out, const SkullbonezCore::Physics::PhysicsPipelineRecord& record )
{
    AppendPod( out, static_cast<int32_t>( record.stage ) );
    AppendPod( out, static_cast<int32_t>( record.bodyA ) );
    AppendPod( out, static_cast<int32_t>( record.bodyB ) );
    AppendPod( out, static_cast<int32_t>( record.iteration ) );
    AppendPod( out, record.featureId );
    AppendVec3( out, record.point );
    AppendVec3( out, record.normal );
    AppendPod( out, record.scalarA );
    AppendPod( out, record.scalarB );
    AppendPod( out, record.scalarC );
}

void AppendSolverSnapshot(
    std::vector<uint8_t>& out,
    const SkullbonezCore::Runtime::ReplaySolverWorldSnapshot& snapshot
)
{
    const SkullbonezCore::Physics::PhysicsSolverSnapshot& physics = snapshot.physics;
    const uint8_t sleepEnabled = physics.sleepEnabled ? 1u : 0u;
    const uint8_t collisionVisualFrameActive = physics.collisionVisualFrameActive ? 1u : 0u;
    const uint8_t reserved[2] = {};

    AppendPod( out, physics.version );
    AppendPod( out, static_cast<int32_t>( physics.modelCount ) );
    AppendPod( out, static_cast<int32_t>( physics.nextSleepIslandVisualId ) );
    AppendPod( out, sleepEnabled );
    AppendPod( out, collisionVisualFrameActive );
    AppendBytes( out, reserved );
    AppendTornadoConfig( out, snapshot.tornadoConfig );
    if ( physics.version >= 2 )
    {
        AppendTornadoSystemConfig( out, snapshot.tornadoSystemConfig );
        AppendPod( out, snapshot.tornadoSystemElapsedSeconds );
    }
    AppendCountedPodVector( out, physics.timeRemaining );
    AppendCountedPodVector( out, physics.sleepSupportedThisFrame );
    AppendCountedPodVector( out, physics.sleepInhibitedThisFrame );
    AppendCountedPodVector( out, physics.sleepState );
    AppendCountedPodVector( out, physics.sleepCounter );
    AppendCountedPodVector( out, physics.underwaterSleepLocked );
    AppendCountedPodVector( out, snapshot.tornadoCaptureSeconds );
    AppendCountedPodVector( out, snapshot.tornadoEjectCooldownSeconds );
    AppendCountedPodVector( out, physics.collisionVisualContacts );
    AppendCountedIntVector( out, physics.sleepIslandVisualId );
    AppendCountedIntVector( out, physics.sleepIslandAssignedVisualId );
    AppendCountedPairVector( out, physics.sleepSupportEdges );
    AppendCountedIntVector( out, physics.sleepIslandParent );
    AppendCountedPodVector( out, physics.sleepIslandRank );
    AppendCountedPodVector( out, physics.sleepIslandHasAwake );
    AppendCountedPodVector( out, physics.sleepIslandHasSupportAnchor );
    AppendCountedPodVector( out, physics.sleepIslandEligible );
    AppendCountedPodVector( out, physics.sleepIslandCanSleep );

    AppendPod( out, CheckedU32( physics.persistentContacts.size() ) );
    for ( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact : physics.persistentContacts )
    {
        AppendPersistentContact( out, contact );
    }

    AppendPod( out, CheckedU32( physics.persistentContactCache.size() ) );
    for ( const SkullbonezCore::Physics::PhysicsSolverContactCacheSample& cache : physics.persistentContactCache )
    {
        AppendContactCache( out, cache );
    }

    AppendSolverStats( out, physics.solverStats );
    AppendCountedPodVector( out, physics.persistentContactCounts );
    AppendCountedPodVector( out, physics.persistentRestingContactCounts );

    AppendPod( out, CheckedU32( physics.debugContacts.size() ) );
    for ( const SkullbonezCore::Physics::PhysicsDebugContact& contact : physics.debugContacts )
    {
        AppendPhysicsDebugContact( out, contact );
    }

    AppendPod( out, CheckedU32( physics.pipelineTrace.size() ) );
    for ( const SkullbonezCore::Physics::PhysicsPipelineRecord& record : physics.pipelineTrace )
    {
        AppendPipelineRecord( out, record );
    }

    AppendCountedPodVector( out, physics.collisionCellKeys );
}

bool AppendSolverBodyRecord(
    std::vector<uint8_t>& out,
    const std::vector<BodyDictionaryEntry>& dictionary,
    const ReplaySolverBodySample& body
)
{
    uint32_t dictionaryIndex = 0;
    if ( !FindBodyDictionaryIndex( dictionary, body, dictionaryIndex ) )
    {
        return false;
    }

    const uint8_t flags[5] = {
        body.fixed ? 1u : 0u,          body.sleeping ? 1u : 0u,         body.sleepSupported ? 1u : 0u,
        body.sleepInhibited ? 1u : 0u, body.collisionContact ? 1u : 0u,
    };

    const uint8_t reserved[3] = {};

    const uint16_t reserved16 = 0;
    AppendPod( out, dictionaryIndex );
    AppendVec3( out, body.position );
    AppendVec3( out, body.linearVelocity );
    AppendVec3( out, body.angularVelocity );
    AppendOrientation( out, body.orientation );
    AppendPod( out, body.mass );
    AppendPod( out, body.inverseMass );
    AppendVec3( out, body.rotationalInertia );
    AppendVec3( out, body.inverseRotationalInertia );
    AppendBytes( out, flags );
    AppendBytes( out, reserved );
    AppendPod( out, static_cast<int32_t>( body.sleepIslandVisualId ) );
    AppendPod( out, body.contactCount );
    AppendPod( out, reserved16 );
    AppendPod( out, body.maxPenetration );
    AppendPod( out, body.normalImpulseSum );
    return true;
}

void AppendLauncherVisual( std::vector<uint8_t>& out, const ReplayLauncherVisualSample& launcher )
{
    const uint8_t fireMode = static_cast<uint8_t>( launcher.fireMode );
    const uint8_t visualizeRays = launcher.visualizeRays ? 1u : 0u;
    const uint8_t reserved[2] = {};

    AppendPod( out, static_cast<int32_t>( launcher.nextRayLine ) );
    AppendPod( out, static_cast<int32_t>( launcher.nextLaserShot ) );
    AppendPod( out, fireMode );
    AppendPod( out, visualizeRays );
    AppendBytes( out, reserved );
    AppendPod( out, launcher.impulseStrength );
    AppendPod( out, launcher.projectileSpeed );

    AppendPod( out, CheckedU32( launcher.rayLines.size() ) );
    for ( const ReplayRayCastLineSample& line : launcher.rayLines )
    {
        const uint8_t active = line.active ? 1u : 0u;
        const uint8_t hit = line.hit ? 1u : 0u;
        const uint8_t lineReserved[2] = {};

        AppendVec3( out, line.start );
        AppendVec3( out, line.end );
        AppendPod( out, line.ageSeconds );
        AppendPod( out, active );
        AppendPod( out, hit );
        AppendBytes( out, lineReserved );
    }

    AppendPod( out, CheckedU32( launcher.laserShots.size() ) );
    for ( const LauncherLaserShotSnapshot& shot : launcher.laserShots )
    {
        const uint8_t active = shot.active ? 1u : 0u;
        const uint8_t hit = shot.hit ? 1u : 0u;
        const uint8_t shotReserved[2] = {};

        AppendVec3( out, shot.start );
        AppendVec3( out, shot.end );
        AppendVec3( out, shot.cameraRight );
        AppendVec3( out, shot.cameraUp );
        AppendPod( out, shot.ageSeconds );
        AppendPod( out, shot.lifetimeSeconds );
        AppendPod( out, active );
        AppendPod( out, hit );
        AppendBytes( out, shotReserved );
    }
}

bool RangeFits( std::size_t totalSize, uint64_t offset, uint64_t size )
{
    if ( offset > static_cast<uint64_t>( ( std::numeric_limits<std::size_t>::max )() ) ||
         size > static_cast<uint64_t>( ( std::numeric_limits<std::size_t>::max )() ) )
    {
        return false;
    }

    const std::size_t localOffset = static_cast<std::size_t>( offset );
    const std::size_t localSize = static_cast<std::size_t>( size );
    return localOffset <= totalSize && localSize <= totalSize - localOffset;
}

bool MakeCursor( const std::vector<uint8_t>& bytes, uint64_t offset, uint64_t size, ByteCursor& out )
{
    if ( !RangeFits( bytes.size(), offset, size ) )
    {
        return false;
    }

    out.data = bytes.data() + static_cast<std::size_t>( offset );
    out.size = static_cast<std::size_t>( size );
    out.offset = 0;
    return true;
}

bool LoadBinaryFile( const char* path, std::vector<uint8_t>& outBytes )
{
    outBytes.clear();
    if ( !path || path[0] == '\0' )
    {
        return false;
    }

    std::ifstream input( path, std::ios::in | std::ios::binary | std::ios::ate );
    if ( !input.is_open() )
    {
        return false;
    }

    const std::streamoff fileSize = input.tellg();
    if ( fileSize <= 0 )
    {
        return false;
    }
    const uint64_t fileSizeBytes = static_cast<uint64_t>( fileSize );
    if ( fileSizeBytes > static_cast<uint64_t>( ( std::numeric_limits<std::size_t>::max )() ) ||
         fileSizeBytes > static_cast<uint64_t>( ( std::numeric_limits<std::streamsize>::max )() ) )
    {
        return false;
    }

    outBytes.resize( static_cast<std::size_t>( fileSizeBytes ) );
    input.seekg( 0, std::ios::beg );
    // Why: std::istream exposes binary storage through its char-based ABI;
    // ownership and all subsequent parsing remain in the typed byte vector.
    input.read( reinterpret_cast<char*>( outBytes.data() ), static_cast<std::streamsize>( outBytes.size() ) );
    return static_cast<std::size_t>( input.gcount() ) == outBytes.size();
}

bool ReadChunkTable(
    const std::vector<uint8_t>& fileBytes,
    std::vector<ChunkTableEntry>& outChunks,
    uint32_t& outVersion
)
{
    outChunks.clear();
    outVersion = 0;

    // Concept: the chunk table is the trusted map of the binary file. Validate
    // the global header first, then validate each chunk range before any parser
    // receives a cursor into payload bytes.
    ByteCursor header;
    if ( !MakeCursor( fileBytes, 0, fileBytes.size(), header ) )
    {
        return false;
    }

    char magic[8] = {};
    uint32_t version = 0;
    uint32_t headerBytes = 0;
    uint32_t chunkCount = 0;
    uint32_t flags = 0;
    uint64_t chunkTableOffset = 0;
    uint64_t fileSize = 0;
    if ( !ReadBytes( header, magic ) || !ReadPod( header, version ) || !ReadPod( header, headerBytes ) ||
         !ReadPod( header, chunkCount ) || !ReadPod( header, flags ) || !ReadPod( header, chunkTableOffset ) ||
         !ReadPod( header, fileSize ) )
    {
        return false;
    }
    (void)flags;

    if ( std::memcmp( magic, REPLAY_V2_MAGIC, sizeof( magic ) ) != 0 ||
         ( version < REPLAY_MINIMUM_VERSION || version > REPLAY_CURRENT_VERSION ) ||
         headerBytes != REPLAY_V2_HEADER_BYTES || fileSize != static_cast<uint64_t>( fileBytes.size() ) )
    {
        return false;
    }
    outVersion = version;

    const uint64_t tableBytes = static_cast<uint64_t>( chunkCount ) * REPLAY_V2_CHUNK_ENTRY_BYTES;
    ByteCursor table;
    if ( !MakeCursor( fileBytes, chunkTableOffset, tableBytes, table ) )
    {
        return false;
    }

    outChunks.reserve( chunkCount );
    for ( uint32_t i = 0; i < chunkCount; ++i )
    {
        ChunkTableEntry entry;
        uint32_t reserved = 0;
        if ( !ReadBytes( table, entry.id ) || !ReadPod( table, entry.offset ) || !ReadPod( table, entry.size ) ||
             !ReadPod( table, entry.recordCount ) || !ReadPod( table, reserved ) )
        {
            return false;
        }
        (void)reserved;

        if ( !RangeFits( fileBytes.size(), entry.offset, entry.size ) )
        {
            return false;
        }
        outChunks.push_back( entry );
    }
    return table.offset == table.size;
}

const ChunkTableEntry* FindChunk( const std::vector<ChunkTableEntry>& chunks, const char id[4] )
{
    for ( const ChunkTableEntry& chunk : chunks )
    {
        if ( std::memcmp( chunk.id, id, 4 ) == 0 )
        {
            return &chunk;
        }
    }
    return nullptr;
}

bool ParseBodyDictionary(
    const std::vector<uint8_t>& fileBytes,
    const ChunkTableEntry& chunk,
    uint32_t version,
    std::vector<BodyDictionaryEntry>& outDictionary
)
{
    outDictionary.clear();

    ByteCursor cursor;
    if ( !MakeCursor( fileBytes, chunk.offset, chunk.size, cursor ) )
    {
        return false;
    }

    uint32_t bodyCount = 0;
    if ( !ReadPod( cursor, bodyCount ) || bodyCount != chunk.recordCount )
    {
        return false;
    }

    outDictionary.reserve( bodyCount );
    for ( uint32_t i = 0; i < bodyCount; ++i )
    {
        BodyDictionaryEntry entry;
        uint8_t shapeKind = 0;
        if ( !ReadPod( cursor, entry.id ) || !ReadPod( cursor, entry.bodyOrder ) || !ReadPod( cursor, shapeKind ) )
        {
            return false;
        }
        if ( version >= REPLAY_PRESENTATION_VISUAL_VERSION )
        {
            uint8_t fixed = 0;
            if ( !ReadPod( cursor, fixed ) || !SkipBytes( cursor, 2 ) || !ReadPod( cursor, entry.mass ) )
            {
                return false;
            }
            entry.fixed = fixed != 0;
        }
        else if ( !SkipBytes( cursor, 3 ) )
        {
            return false;
        }
        if ( !ReadBytes( cursor, entry.name ) )
        {
            return false;
        }

        if ( shapeKind <= static_cast<uint8_t>( ReplayBodyShapeKind::ConvexHull ) )
        {
            entry.shapeKind = static_cast<ReplayBodyShapeKind>( shapeKind );
        }
        else
        {
            entry.shapeKind = ReplayBodyShapeKind::Unknown;
        }
        outDictionary.push_back( entry );
    }

    const uint32_t entryBytes = version >= REPLAY_PRESENTATION_VISUAL_VERSION ? REPLAY_V3_BODY_DICTIONARY_ENTRY_BYTES
                                                                              : REPLAY_V2_BODY_DICTIONARY_ENTRY_BYTES;
    return cursor.offset == cursor.size &&
           cursor.size == sizeof( uint32_t ) + static_cast<std::size_t>( bodyCount ) * entryBytes;
}

bool ParseIndex(
    const std::vector<uint8_t>& fileBytes,
    const ChunkTableEntry& chunk,
    std::vector<IndexedFrame>& outFrames
)
{
    outFrames.clear();

    ByteCursor cursor;
    if ( !MakeCursor( fileBytes, chunk.offset, chunk.size, cursor ) )
    {
        return false;
    }

    uint32_t frameCount = 0;
    if ( !ReadPod( cursor, frameCount ) || frameCount != chunk.recordCount )
    {
        return false;
    }

    outFrames.reserve( frameCount );
    for ( uint32_t i = 0; i < frameCount; ++i )
    {
        IndexedFrame frame;
        uint32_t reserved = 0;
        if ( !ReadPod( cursor, frame.frameIndex ) || !ReadPod( cursor, frame.presentationChunkOffset ) ||
             !ReadPod( cursor, frame.bodyCount ) || !ReadPod( cursor, reserved ) )
        {
            return false;
        }
        (void)reserved;
        outFrames.push_back( frame );
    }

    return cursor.offset == cursor.size &&
           cursor.size == sizeof( uint32_t ) + static_cast<std::size_t>( frameCount ) * REPLAY_V2_INDEX_ENTRY_BYTES;
}

bool ParseBranchRecords(
    const std::vector<uint8_t>& fileBytes,
    const ChunkTableEntry& chunk,
    std::vector<BranchRecord>& outBranches
)
{
    outBranches.clear();

    ByteCursor cursor;
    if ( !MakeCursor( fileBytes, chunk.offset, chunk.size, cursor ) )
    {
        return false;
    }

    uint32_t branchCount = 0;
    if ( !ReadPod( cursor, branchCount ) || branchCount != chunk.recordCount )
    {
        return false;
    }

    outBranches.reserve( branchCount );
    for ( uint32_t i = 0; i < branchCount; ++i )
    {
        BranchRecord record;
        uint32_t reserved = 0;
        uint64_t reserved64 = 0;
        if ( !ReadPod( cursor, record.branch.branchId ) || !ReadPod( cursor, record.branch.parentBranchId ) ||
             !ReadPod( cursor, record.branch.startFrame ) || !ReadPod( cursor, record.firstRetainedFrame ) ||
             !ReadPod( cursor, record.lastRetainedFrame ) || !ReadPod( cursor, record.branch.sourceFrame ) ||
             !ReadPod( cursor, record.branch.sourceSolverHash ) || !SkipBytes( cursor, sizeof( uint32_t ) ) ||
             !ReadPod( cursor, reserved ) || !ReadPod( cursor, reserved64 ) )
        {
            return false;
        }
        if ( record.branch.branchId == 0 || record.lastRetainedFrame < record.firstRetainedFrame )
        {
            return false;
        }
        (void)reserved;
        (void)reserved64;
        outBranches.push_back( record );
    }

    return cursor.offset == cursor.size &&
           cursor.size == sizeof( uint32_t ) + static_cast<std::size_t>( branchCount ) * REPLAY_V2_BRANCH_ENTRY_BYTES;
}

bool ParseEventCursorRecords(
    const std::vector<uint8_t>& fileBytes,
    const ChunkTableEntry& chunk,
    std::vector<EventCursorRecord>& outRecords
)
{
    outRecords.clear();

    ByteCursor cursor;
    if ( !MakeCursor( fileBytes, chunk.offset, chunk.size, cursor ) )
    {
        return false;
    }

    uint32_t cursorCount = 0;
    if ( !ReadPod( cursor, cursorCount ) || cursorCount != chunk.recordCount )
    {
        return false;
    }

    outRecords.reserve( cursorCount );
    for ( uint32_t i = 0; i < cursorCount; ++i )
    {
        EventCursorRecord record;
        if ( !ReadPod( cursor, record.frameIndex ) || !ReadPod( cursor, record.eventCursor ) ||
             !ReadPod( cursor, record.flags ) || !ReadPod( cursor, record.solverHash ) )
        {
            return false;
        }
        outRecords.push_back( record );
    }

    return cursor.offset == cursor.size && cursor.size == sizeof( uint32_t ) + static_cast<std::size_t>( cursorCount ) *
                                                                                   REPLAY_V2_EVENT_CURSOR_ENTRY_BYTES;
}

bool ParseEventRecords(
    const std::vector<uint8_t>& fileBytes,
    const ChunkTableEntry& chunk,
    std::vector<ReplayEventSample>& outEvents
)
{
    outEvents.clear();

    ByteCursor cursor;
    if ( !MakeCursor( fileBytes, chunk.offset, chunk.size, cursor ) )
    {
        return false;
    }

    uint32_t eventCount = 0;
    if ( !ReadPod( cursor, eventCount ) || eventCount != chunk.recordCount )
    {
        return false;
    }

    outEvents.reserve( eventCount );
    for ( uint32_t i = 0; i < eventCount; ++i )
    {
        ReplayEventSample event;
        uint16_t kind = 0;
        uint32_t reserved = 0;
        if ( !ReadPod( cursor, event.frameIndex ) || !ReadPod( cursor, event.sequence ) ||
             !ReadPod( cursor, event.branch.branchId ) || !ReadPod( cursor, event.branch.parentBranchId ) ||
             !ReadPod( cursor, kind ) || !ReadPod( cursor, event.payloadVersion ) || !ReadPod( cursor, event.flags ) ||
             !ReadPod( cursor, event.value0 ) || !ReadPod( cursor, event.value1 ) || !ReadPod( cursor, event.value2 ) ||
             !ReadPod( cursor, event.value3 ) || !ReadPod( cursor, event.data0 ) ||
             !ReadPod( cursor, event.branch.sourceFrame ) || !ReadPod( cursor, event.branch.sourceSolverHash ) ||
             !ReadBytes( cursor, event.text ) || !ReadPod( cursor, reserved ) )
        {
            return false;
        }
        event.kind = static_cast<ReplayEventKind>( kind );
        event.text[sizeof( event.text ) - 1] = '\0';
        (void)reserved;
        outEvents.push_back( event );
    }

    return cursor.offset == cursor.size &&
           cursor.size == sizeof( uint32_t ) + static_cast<std::size_t>( eventCount ) * REPLAY_V2_EVENT_ENTRY_BYTES;
}

ReplayBranchInfo BranchForFrame( const std::vector<BranchRecord>& branches, ReplayFrameIndex frameIndex )
{
    for ( const BranchRecord& record : branches )
    {
        if ( frameIndex >= record.firstRetainedFrame && frameIndex <= record.lastRetainedFrame )
        {
            return record.branch;
        }
    }
    return ReplayBranchInfo();
}

template <typename T> void ApplyBranchMetadata( const std::vector<BranchRecord>& branches, std::vector<T>& samples )
{
    if ( branches.empty() )
    {
        return;
    }
    for ( T& sample : samples )
    {
        sample.branch = BranchForFrame( branches, sample.frameIndex );
    }
}

void ApplyEventCursorMetadata(
    const std::vector<EventCursorRecord>& records,
    std::vector<ReplaySolverFrameSample>& samples
)
{
    if ( records.empty() )
    {
        return;
    }

    for ( ReplaySolverFrameSample& sample : samples )
    {
        for ( const EventCursorRecord& record : records )
        {
            if ( record.frameIndex == sample.frameIndex &&
                 ( record.solverHash == 0 || record.solverHash == sample.solverHash ) )
            {
                sample.eventCursor = record.eventCursor;
                break;
            }
        }
    }
}

bool ReadVec3( ByteCursor& cursor, Vector3& out )
{
    return ReadPod( cursor, out.x ) && ReadPod( cursor, out.y ) && ReadPod( cursor, out.z );
}

bool ReadOrientation( ByteCursor& cursor, float ( &out )[4] )
{
    return ReadPod( cursor, out[0] ) && ReadPod( cursor, out[1] ) && ReadPod( cursor, out[2] ) &&
           ReadPod( cursor, out[3] );
}

void ApplyWorldFlags( uint8_t flags, ReplayWorldPresentationSample& out )
{
    out.waterHidden = ( flags & REPLAY_V2_WORLD_WATER_HIDDEN ) != 0;
    out.terrainHidden = ( flags & REPLAY_V2_WORLD_TERRAIN_HIDDEN ) != 0;
    out.fixedStep = ( flags & REPLAY_V2_WORLD_FIXED_STEP ) != 0;
    out.scenePhysicsEnabled = ( flags & REPLAY_V2_WORLD_SCENE_PHYSICS_ENABLED ) != 0;
    out.sceneTextEnabled = ( flags & REPLAY_V2_WORLD_SCENE_TEXT_ENABLED ) != 0;
}

bool ParsePresentationSamples(
    const std::vector<uint8_t>& fileBytes,
    const ChunkTableEntry& chunk,
    uint32_t version,
    const std::vector<BodyDictionaryEntry>& dictionary,
    const std::vector<IndexedFrame>& indexedFrames,
    std::vector<ReplayPresentationSample>& outSamples
)
{
    outSamples.clear();

    ByteCursor presentation;
    if ( !MakeCursor( fileBytes, chunk.offset, chunk.size, presentation ) )
    {
        return false;
    }

    uint32_t frameCount = 0;
    if ( !ReadPod( presentation, frameCount ) || frameCount != chunk.recordCount ||
         frameCount != static_cast<uint32_t>( indexedFrames.size() ) )
    {
        return false;
    }

    outSamples.reserve( frameCount );
    for ( const IndexedFrame& indexed : indexedFrames )
    {
        // Invariant: INDX offsets are relative to the PRES payload, not the
        // whole file. Add the chunk offset only after proving the relative seek
        // stays inside the presentation chunk.
        if ( indexed.presentationChunkOffset > chunk.size )
        {
            return false;
        }
        const uint64_t absoluteFrameOffset = chunk.offset + indexed.presentationChunkOffset;
        if ( absoluteFrameOffset < chunk.offset )
        {
            return false;
        }

        ByteCursor frameCursor;
        if ( !MakeCursor( fileBytes, absoluteFrameOffset, chunk.size - indexed.presentationChunkOffset, frameCursor ) )
        {
            return false;
        }

        ReplayPresentationSample sample;
        uint8_t checkpointBoundary = 0;
        uint8_t worldFlags = 0;
        uint16_t reserved = 0;
        uint32_t bodyCount = 0;
        if ( !ReadPod( frameCursor, sample.frameIndex ) || !ReadPod( frameCursor, sample.sceneFrame ) ||
             !ReadPod( frameCursor, sample.simulationSeconds ) || !ReadPod( frameCursor, sample.physicsDt ) ||
             !ReadPod( frameCursor, sample.stateHash ) || !ReadPod( frameCursor, sample.contactCount ) ||
             !ReadPod( frameCursor, sample.pipelineRecordCount ) || !ReadPod( frameCursor, checkpointBoundary ) ||
             !ReadPod( frameCursor, worldFlags ) || !ReadPod( frameCursor, reserved ) ||
             !ReadPod( frameCursor, sample.world.gravity ) || !ReadPod( frameCursor, sample.world.fluidHeight ) ||
             !ReadPod( frameCursor, sample.world.fluidDensity ) || !ReadVec3( frameCursor, sample.camera.eye ) ||
             !ReadVec3( frameCursor, sample.camera.view ) || !ReadVec3( frameCursor, sample.camera.up ) ||
             !ReadPod( frameCursor, bodyCount ) )
        {
            return false;
        }
        (void)reserved;

        if ( sample.frameIndex != indexed.frameIndex || bodyCount != indexed.bodyCount )
        {
            return false;
        }

        sample.checkpointBoundary = checkpointBoundary != 0;
        ApplyWorldFlags( worldFlags, sample.world );
        sample.bodies.reserve( bodyCount );
        for ( uint32_t i = 0; i < bodyCount; ++i )
        {
            uint32_t dictionaryIndex = 0;
            ReplayBodyPresentationSample body;
            if ( !ReadPod( frameCursor, dictionaryIndex ) || dictionaryIndex >= dictionary.size() ||
                 !ReadVec3( frameCursor, body.position ) || !ReadOrientation( frameCursor, body.orientation ) )
            {
                return false;
            }

            if ( version >= REPLAY_PRESENTATION_VISUAL_VERSION )
            {
                uint8_t flags = 0;
                int32_t sleepIslandVisualId = 0;
                uint16_t reservedContact = 0;
                if ( !ReadVec3( frameCursor, body.linearVelocity ) || !ReadVec3( frameCursor, body.angularVelocity ) ||
                     !ReadPod( frameCursor, flags ) || !SkipBytes( frameCursor, 3 ) ||
                     !ReadPod( frameCursor, sleepIslandVisualId ) || !ReadPod( frameCursor, body.contactCount ) ||
                     !ReadPod( frameCursor, reservedContact ) || !ReadPod( frameCursor, body.maxPenetration ) ||
                     !ReadPod( frameCursor, body.normalImpulseSum ) )
                {
                    return false;
                }
                (void)reservedContact;
                body.sleeping = ( flags & 1u ) != 0;
                body.sleepSupported = ( flags & 2u ) != 0;
                body.sleepInhibited = ( flags & 4u ) != 0;
                body.collisionContact = ( flags & 8u ) != 0;
                body.sleepIslandVisualId = sleepIslandVisualId;
            }

            const BodyDictionaryEntry& entry = dictionary[dictionaryIndex];
            body.id.value = entry.id;
            body.modelRow = SkullbonezCore::Physics::MakeModelRowHint( entry.bodyOrder );
            body.shapeKind = entry.shapeKind;
            body.mass = entry.mass;
            body.fixed = entry.fixed;
            std::memcpy( body.name, entry.name, sizeof( body.name ) );
            sample.bodies.push_back( body );
        }

        const uint32_t bodyBytes = version >= REPLAY_PRESENTATION_VISUAL_VERSION ? REPLAY_V3_BODY_VISUAL_STATE_BYTES
                                                                                 : REPLAY_V2_BODY_POSE_BYTES;
        const std::size_t expectedFrameBytes =
            REPLAY_V2_FRAME_HEADER_BYTES + static_cast<std::size_t>( bodyCount ) * bodyBytes;
        if ( frameCursor.offset != expectedFrameBytes )
        {
            return false;
        }
        if ( version >= REPLAY_PRESENTATION_VISUAL_VERSION &&
             ReplayRecorderOperations::ComputePresentationStateHash( sample ) != sample.stateHash )
        {
            // Invariant: v3 is not merely parseable. Every loaded body field
            // must reproduce the writer's presentation hash before scrub can
            // expose the sample to rendering.
            return false;
        }
        outSamples.push_back( std::move( sample ) );
    }

    return true;
}

template <typename T> bool ReadCountedPodVector( ByteCursor& cursor, std::vector<T>& outValues )
{
    outValues.clear();
    uint32_t count = 0;
    if ( !ReadPod( cursor, count ) )
    {
        return false;
    }

    outValues.resize( count );
    for ( T& value : outValues )
    {
        if ( !ReadPod( cursor, value ) )
        {
            return false;
        }
    }
    return true;
}

bool ReadCountedIntVector( ByteCursor& cursor, std::vector<int>& outValues )
{
    outValues.clear();
    uint32_t count = 0;
    if ( !ReadPod( cursor, count ) )
    {
        return false;
    }

    outValues.resize( count );
    for ( int& value : outValues )
    {
        int32_t stored = 0;
        if ( !ReadPod( cursor, stored ) )
        {
            return false;
        }
        value = stored;
    }
    return true;
}

bool ReadCountedPairVector( ByteCursor& cursor, std::vector<std::pair<int, int>>& outValues )
{
    outValues.clear();
    uint32_t count = 0;
    if ( !ReadPod( cursor, count ) )
    {
        return false;
    }

    outValues.resize( count );
    for ( std::pair<int, int>& value : outValues )
    {
        int32_t first = 0;
        int32_t second = 0;
        if ( !ReadPod( cursor, first ) || !ReadPod( cursor, second ) )
        {
            return false;
        }
        value.first = first;
        value.second = second;
    }
    return true;
}

template <typename T, typename ReadFunc>
bool ReadCountedStructVector( ByteCursor& cursor, std::vector<T>& outValues, ReadFunc readFunc )
{
    outValues.clear();
    uint32_t count = 0;
    if ( !ReadPod( cursor, count ) )
    {
        return false;
    }

    outValues.resize( count );
    for ( T& value : outValues )
    {
        if ( !readFunc( cursor, value ) )
        {
            return false;
        }
    }
    return true;
}

bool ReadTornadoConfig( ByteCursor& cursor, SkullbonezCore::Gameplay::TornadoFieldConfig& outConfig )
{
    uint8_t enabled = 0;
    uint8_t visualizeVelocityField = 0;
    if ( !ReadPod( cursor, enabled ) || !ReadPod( cursor, visualizeVelocityField ) || !SkipBytes( cursor, 2 ) ||
         !ReadVec3( cursor, outConfig.center ) || !ReadPod( cursor, outConfig.radius ) ||
         !ReadPod( cursor, outConfig.height ) || !ReadPod( cursor, outConfig.inwardAcceleration ) ||
         !ReadPod( cursor, outConfig.swirlAcceleration ) || !ReadPod( cursor, outConfig.liftAcceleration ) ||
         !ReadPod( cursor, outConfig.ejectAcceleration ) || !ReadPod( cursor, outConfig.ejectUpAcceleration ) ||
         !ReadPod( cursor, outConfig.ejectBand ) || !ReadPod( cursor, outConfig.minCaptureSeconds ) ||
         !ReadPod( cursor, outConfig.ejectCooldownSeconds ) || !ReadPod( cursor, outConfig.maxDeltaVelocity ) )
    {
        return false;
    }

    outConfig.enabled = enabled != 0;
    outConfig.visualizeVelocityField = visualizeVelocityField != 0;
    return true;
}


bool ReadTornadoVortexConfig( ByteCursor& cursor, SkullbonezCore::Gameplay::TornadoVortexConfig& outConfig )
{
    if ( !ReadTornadoConfig( cursor, outConfig.field ) || !ReadPod( cursor, outConfig.spawnSeconds ) ||
         !ReadPod( cursor, outConfig.timeToLiveSeconds ) || !ReadPod( cursor, outConfig.growSeconds ) ||
         !ReadPod( cursor, outConfig.shrinkSeconds ) || !ReadPod( cursor, outConfig.driftRadius ) ||
         !ReadPod( cursor, outConfig.driftSpeed ) || !ReadPod( cursor, outConfig.driftPhase ) ||
         !ReadPod( cursor, outConfig.repulsionRadius ) || !ReadPod( cursor, outConfig.repulsionStrength ) )
    {
        return false;
    }
    return true;
}


bool ReadTornadoSystemConfig( ByteCursor& cursor, SkullbonezCore::Gameplay::TornadoSystemConfig& outConfig )
{
    uint8_t enabled = 0;
    uint8_t visualizeVelocityField = 0;
    uint32_t count = 0;
    if ( !ReadPod( cursor, enabled ) || !ReadPod( cursor, visualizeVelocityField ) || !SkipBytes( cursor, 2 ) ||
         !ReadPod( cursor, count ) )
    {
        return false;
    }
    // Lane R: artifact input must fail before restore reaches Gameplay's fatal
    // fixed-capacity invariant; replay files never receive a truncation path.
    if ( count > SkullbonezCore::Gameplay::MAX_TORNADO_ACTIVE_FORCE_FIELDS )
    {
        return false;
    }

    outConfig = SkullbonezCore::Gameplay::TornadoSystemConfig();
    outConfig.enabled = enabled != 0;
    outConfig.visualizeVelocityField = visualizeVelocityField != 0;
    outConfig.vortices.resize( count );
    for ( SkullbonezCore::Gameplay::TornadoVortexConfig& vortex : outConfig.vortices )
    {
        if ( !ReadTornadoVortexConfig( cursor, vortex ) )
        {
            return false;
        }
    }
    return true;
}


bool ReadSolverStats( ByteCursor& cursor, SkullbonezCore::Physics::PhysicsSolverStatsSample& outStats )
{
    int32_t rowCount = 0;
    int32_t cachePreviousRows = 0;
    int32_t cacheHits = 0;
    int32_t cacheMisses = 0;
    int32_t warmStartedRows = 0;
    int32_t positionCorrectionRows = 0;
    int32_t solverIterations = 0;
    if ( !ReadPod( cursor, rowCount ) || !ReadPod( cursor, cachePreviousRows ) || !ReadPod( cursor, cacheHits ) ||
         !ReadPod( cursor, cacheMisses ) || !ReadPod( cursor, warmStartedRows ) ||
         !ReadPod( cursor, positionCorrectionRows ) || !ReadPod( cursor, solverIterations ) ||
         !ReadPod( cursor, outStats.positionCorrectionTotal ) || !ReadPod( cursor, outStats.positionCorrectionMax ) )
    {
        return false;
    }

    outStats.rowCount = rowCount;
    outStats.cachePreviousRows = cachePreviousRows;
    outStats.cacheHits = cacheHits;
    outStats.cacheMisses = cacheMisses;
    outStats.warmStartedRows = warmStartedRows;
    outStats.positionCorrectionRows = positionCorrectionRows;
    outStats.solverIterations = solverIterations;
    return true;
}

bool ReadPersistentContact(
    ByteCursor& cursor,
    SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& outContact
)
{
    int32_t bodyA = 0;
    int32_t bodyB = 0;
    uint8_t warmStarted = 0;
    uint8_t isTerrain = 0;
    uint8_t supportsRestingPolicy = 0;
    uint8_t allowsTangentFriction = 0;
    uint8_t normalCoupledFriction = 0;
    uint8_t inhibitsSleep = 0;
    uint8_t reserved = 0;
    if ( !ReadPod( cursor, bodyA ) || !ReadPod( cursor, bodyB ) || !ReadPod( cursor, outContact.featureId ) ||
         !ReadPod( cursor, outContact.key ) || !ReadVec3( cursor, outContact.normal ) ||
         !ReadVec3( cursor, outContact.tangent1 ) || !ReadVec3( cursor, outContact.tangent2 ) ||
         !ReadVec3( cursor, outContact.rA ) || !ReadVec3( cursor, outContact.rB ) ||
         !ReadPod( cursor, outContact.penetration ) || !ReadPod( cursor, outContact.normalMass ) ||
         !ReadPod( cursor, outContact.tangentMass1 ) || !ReadPod( cursor, outContact.tangentMass2 ) ||
         !ReadPod( cursor, outContact.bias ) || !ReadPod( cursor, outContact.frictionLimit ) ||
         !ReadPod( cursor, outContact.accN ) || !ReadPod( cursor, outContact.accT1 ) ||
         !ReadPod( cursor, outContact.accT2 ) || !ReadPod( cursor, warmStarted ) || !ReadPod( cursor, isTerrain ) ||
         !ReadPod( cursor, supportsRestingPolicy ) || !ReadPod( cursor, allowsTangentFriction ) ||
         !ReadPod( cursor, normalCoupledFriction ) || !ReadPod( cursor, inhibitsSleep ) ||
         !ReadPod( cursor, outContact.manifoldPointCount ) || !ReadPod( cursor, reserved ) ||
         !ReadVec3( cursor, outContact.terrainNormal ) || !ReadPod( cursor, outContact.terrainWarmStart ) )
    {
        return false;
    }

    outContact.bodyA = bodyA;
    outContact.bodyB = bodyB;
    outContact.warmStarted = warmStarted != 0;
    outContact.isTerrain = isTerrain != 0;
    outContact.supportsRestingPolicy = supportsRestingPolicy != 0;
    outContact.allowsTangentFriction = allowsTangentFriction != 0;
    outContact.normalCoupledFriction = normalCoupledFriction != 0;
    outContact.inhibitsSleep = inhibitsSleep != 0;
    (void)reserved;
    return true;
}

bool ReadContactCache( ByteCursor& cursor, SkullbonezCore::Physics::PhysicsSolverContactCacheSample& outCache )
{
    return ReadPod( cursor, outCache.key ) && ReadPod( cursor, outCache.accN ) && ReadPod( cursor, outCache.accT1 ) &&
           ReadPod( cursor, outCache.accT2 );
}

bool ReadPhysicsDebugContact( ByteCursor& cursor, SkullbonezCore::Physics::PhysicsDebugContact& outContact )
{
    int32_t bodyA = 0;
    int32_t bodyB = 0;
    if ( !ReadPod( cursor, bodyA ) || !ReadPod( cursor, bodyB ) || !ReadPod( cursor, outContact.featureId ) ||
         !ReadVec3( cursor, outContact.point ) || !ReadVec3( cursor, outContact.normal ) ||
         !ReadVec3( cursor, outContact.tangent1 ) || !ReadVec3( cursor, outContact.tangent2 ) ||
         !ReadPod( cursor, outContact.penetration ) || !ReadPod( cursor, outContact.normalImpulse ) )
    {
        return false;
    }

    outContact.bodyA = bodyA;
    outContact.bodyB = bodyB;
    return true;
}

bool ReadPipelineRecord( ByteCursor& cursor, SkullbonezCore::Physics::PhysicsPipelineRecord& outRecord )
{
    int32_t stage = 0;
    int32_t bodyA = 0;
    int32_t bodyB = 0;
    int32_t iteration = 0;
    if ( !ReadPod( cursor, stage ) || !ReadPod( cursor, bodyA ) || !ReadPod( cursor, bodyB ) ||
         !ReadPod( cursor, iteration ) || !ReadPod( cursor, outRecord.featureId ) ||
         !ReadVec3( cursor, outRecord.point ) || !ReadVec3( cursor, outRecord.normal ) ||
         !ReadPod( cursor, outRecord.scalarA ) || !ReadPod( cursor, outRecord.scalarB ) ||
         !ReadPod( cursor, outRecord.scalarC ) )
    {
        return false;
    }

    outRecord.stage = static_cast<SkullbonezCore::Physics::PhysicsPipelineStage>( stage );
    outRecord.bodyA = bodyA;
    outRecord.bodyB = bodyB;
    outRecord.iteration = iteration;
    return true;
}

bool ReadSolverSnapshot( ByteCursor& cursor, SkullbonezCore::Runtime::ReplaySolverWorldSnapshot& outSnapshot )
{
    outSnapshot = SkullbonezCore::Runtime::ReplaySolverWorldSnapshot();
    SkullbonezCore::Physics::PhysicsSolverSnapshot& physics = outSnapshot.physics;
    int32_t modelCount = 0;
    int32_t nextSleepIslandVisualId = 0;
    uint8_t sleepEnabled = 0;
    uint8_t collisionVisualFrameActive = 0;
    if ( !ReadPod( cursor, physics.version ) || !ReadPod( cursor, modelCount ) ||
         !ReadPod( cursor, nextSleepIslandVisualId ) || !ReadPod( cursor, sleepEnabled ) ||
         !ReadPod( cursor, collisionVisualFrameActive ) || !SkipBytes( cursor, 2 ) ||
         !ReadTornadoConfig( cursor, outSnapshot.tornadoConfig ) )
    {
        return false;
    }
    if ( physics.version < 1 || physics.version > 2 )
    {
        return false;
    }
    if ( physics.version >= 2 )
    {
        if ( !ReadTornadoSystemConfig( cursor, outSnapshot.tornadoSystemConfig ) ||
             !ReadPod( cursor, outSnapshot.tornadoSystemElapsedSeconds ) )
        {
            return false;
        }
    }

    if ( !ReadCountedPodVector( cursor, physics.timeRemaining ) ||
         !ReadCountedPodVector( cursor, physics.sleepSupportedThisFrame ) ||
         !ReadCountedPodVector( cursor, physics.sleepInhibitedThisFrame ) ||
         !ReadCountedPodVector( cursor, physics.sleepState ) || !ReadCountedPodVector( cursor, physics.sleepCounter ) ||
         !ReadCountedPodVector( cursor, physics.underwaterSleepLocked ) ||
         !ReadCountedPodVector( cursor, outSnapshot.tornadoCaptureSeconds ) ||
         !ReadCountedPodVector( cursor, outSnapshot.tornadoEjectCooldownSeconds ) ||
         !ReadCountedPodVector( cursor, physics.collisionVisualContacts ) ||
         !ReadCountedIntVector( cursor, physics.sleepIslandVisualId ) ||
         !ReadCountedIntVector( cursor, physics.sleepIslandAssignedVisualId ) ||
         !ReadCountedPairVector( cursor, physics.sleepSupportEdges ) ||
         !ReadCountedIntVector( cursor, physics.sleepIslandParent ) ||
         !ReadCountedPodVector( cursor, physics.sleepIslandRank ) ||
         !ReadCountedPodVector( cursor, physics.sleepIslandHasAwake ) ||
         !ReadCountedPodVector( cursor, physics.sleepIslandHasSupportAnchor ) ||
         !ReadCountedPodVector( cursor, physics.sleepIslandEligible ) ||
         !ReadCountedPodVector( cursor, physics.sleepIslandCanSleep ) ||
         !ReadCountedStructVector( cursor, physics.persistentContacts, ReadPersistentContact ) ||
         !ReadCountedStructVector( cursor, physics.persistentContactCache, ReadContactCache ) ||
         !ReadSolverStats( cursor, physics.solverStats ) ||
         !ReadCountedPodVector( cursor, physics.persistentContactCounts ) ||
         !ReadCountedPodVector( cursor, physics.persistentRestingContactCounts ) ||
         !ReadCountedStructVector( cursor, physics.debugContacts, ReadPhysicsDebugContact ) ||
         !ReadCountedStructVector( cursor, physics.pipelineTrace, ReadPipelineRecord ) ||
         !ReadCountedPodVector( cursor, physics.collisionCellKeys ) )
    {
        return false;
    }

    physics.modelCount = modelCount;
    physics.nextSleepIslandVisualId = nextSleepIslandVisualId;
    physics.sleepEnabled = sleepEnabled != 0;
    physics.collisionVisualFrameActive = collisionVisualFrameActive != 0;
    return true;
}

bool ReadLauncherVisual( ByteCursor& cursor, ReplayLauncherVisualSample& outLauncher )
{
    outLauncher = ReplayLauncherVisualSample();
    int32_t nextRayLine = 0;
    int32_t nextLaserShot = 0;
    uint8_t fireMode = 0;
    uint8_t visualizeRays = 0;
    if ( !ReadPod( cursor, nextRayLine ) || !ReadPod( cursor, nextLaserShot ) || !ReadPod( cursor, fireMode ) ||
         !ReadPod( cursor, visualizeRays ) || !SkipBytes( cursor, 2 ) ||
         !ReadPod( cursor, outLauncher.impulseStrength ) || !ReadPod( cursor, outLauncher.projectileSpeed ) )
    {
        return false;
    }
    outLauncher.nextRayLine = nextRayLine;
    outLauncher.nextLaserShot = nextLaserShot;
    outLauncher.fireMode = fireMode == static_cast<uint8_t>( ReplayLauncherFireMode::Projectile )
                               ? ReplayLauncherFireMode::Projectile
                               : ReplayLauncherFireMode::Laser;
    outLauncher.visualizeRays = visualizeRays != 0;

    uint32_t rayLineCount = 0;
    if ( !ReadPod( cursor, rayLineCount ) )
    {
        return false;
    }
    outLauncher.rayLines.resize( rayLineCount );
    for ( ReplayRayCastLineSample& line : outLauncher.rayLines )
    {
        uint8_t active = 0;
        uint8_t hit = 0;
        if ( !ReadVec3( cursor, line.start ) || !ReadVec3( cursor, line.end ) || !ReadPod( cursor, line.ageSeconds ) ||
             !ReadPod( cursor, active ) || !ReadPod( cursor, hit ) || !SkipBytes( cursor, 2 ) )
        {
            return false;
        }
        line.active = active != 0;
        line.hit = hit != 0;
    }

    uint32_t laserShotCount = 0;
    if ( !ReadPod( cursor, laserShotCount ) )
    {
        return false;
    }
    outLauncher.laserShots.resize( laserShotCount );
    for ( LauncherLaserShotSnapshot& shot : outLauncher.laserShots )
    {
        uint8_t active = 0;
        uint8_t hit = 0;
        if ( !ReadVec3( cursor, shot.start ) || !ReadVec3( cursor, shot.end ) ||
             !ReadVec3( cursor, shot.cameraRight ) || !ReadVec3( cursor, shot.cameraUp ) ||
             !ReadPod( cursor, shot.ageSeconds ) || !ReadPod( cursor, shot.lifetimeSeconds ) ||
             !ReadPod( cursor, active ) || !ReadPod( cursor, hit ) || !SkipBytes( cursor, 2 ) )
        {
            return false;
        }
        shot.active = active != 0;
        shot.hit = hit != 0;
    }
    return true;
}

bool ReadSolverBody(
    ByteCursor& cursor,
    const std::vector<BodyDictionaryEntry>& dictionary,
    ReplaySolverBodySample& outBody
)
{
    outBody = ReplaySolverBodySample();
    uint32_t dictionaryIndex = 0;
    uint8_t fixed = 0;
    uint8_t sleeping = 0;
    uint8_t sleepSupported = 0;
    uint8_t sleepInhibited = 0;
    uint8_t collisionContact = 0;
    int32_t sleepIslandVisualId = 0;
    uint16_t reserved16 = 0;
    if ( !ReadPod( cursor, dictionaryIndex ) || dictionaryIndex >= dictionary.size() ||
         !ReadVec3( cursor, outBody.position ) || !ReadVec3( cursor, outBody.linearVelocity ) ||
         !ReadVec3( cursor, outBody.angularVelocity ) || !ReadOrientation( cursor, outBody.orientation ) ||
         !ReadPod( cursor, outBody.mass ) || !ReadPod( cursor, outBody.inverseMass ) ||
         !ReadVec3( cursor, outBody.rotationalInertia ) || !ReadVec3( cursor, outBody.inverseRotationalInertia ) ||
         !ReadPod( cursor, fixed ) || !ReadPod( cursor, sleeping ) || !ReadPod( cursor, sleepSupported ) ||
         !ReadPod( cursor, sleepInhibited ) || !ReadPod( cursor, collisionContact ) || !SkipBytes( cursor, 3 ) ||
         !ReadPod( cursor, sleepIslandVisualId ) || !ReadPod( cursor, outBody.contactCount ) ||
         !ReadPod( cursor, reserved16 ) || !ReadPod( cursor, outBody.maxPenetration ) ||
         !ReadPod( cursor, outBody.normalImpulseSum ) )
    {
        return false;
    }

    const BodyDictionaryEntry& entry = dictionary[dictionaryIndex];
    outBody.id.value = entry.id;
    outBody.modelRow = SkullbonezCore::Physics::MakeModelRowHint( entry.bodyOrder );
    outBody.shapeKind = entry.shapeKind;
    std::memcpy( outBody.name, entry.name, sizeof( outBody.name ) );
    outBody.fixed = fixed != 0;
    outBody.sleeping = sleeping != 0;
    outBody.sleepSupported = sleepSupported != 0;
    outBody.sleepInhibited = sleepInhibited != 0;
    outBody.collisionContact = collisionContact != 0;
    outBody.sleepIslandVisualId = sleepIslandVisualId;
    (void)reserved16;
    return true;
}

bool ParseSolverCheckpoints(
    const std::vector<uint8_t>& fileBytes,
    const ChunkTableEntry& chunk,
    const std::vector<BodyDictionaryEntry>& dictionary,
    std::vector<ReplaySolverFrameSample>& outCheckpoints
)
{
    outCheckpoints.clear();

    ByteCursor cursor;
    if ( !MakeCursor( fileBytes, chunk.offset, chunk.size, cursor ) )
    {
        return false;
    }

    uint32_t checkpointCount = 0;
    if ( !ReadPod( cursor, checkpointCount ) || checkpointCount != chunk.recordCount )
    {
        return false;
    }

    outCheckpoints.reserve( checkpointCount );
    for ( uint32_t i = 0; i < checkpointCount; ++i )
    {
        ReplaySolverFrameSample sample;
        int32_t sceneFrame = 0;
        uint8_t checkpointBoundary = 0;
        uint8_t worldFlags = 0;
        uint16_t reserved = 0;
        uint32_t bodyCount = 0;
        if ( !ReadPod( cursor, sample.frameIndex ) || !ReadPod( cursor, sceneFrame ) ||
             !ReadPod( cursor, sample.simulationSeconds ) || !ReadPod( cursor, sample.physicsDt ) ||
             !ReadPod( cursor, sample.presentationHash ) || !ReadPod( cursor, sample.solverHash ) ||
             !ReadPod( cursor, sample.contactCount ) || !ReadPod( cursor, sample.pipelineRecordCount ) ||
             !ReadPod( cursor, checkpointBoundary ) || !ReadPod( cursor, worldFlags ) || !ReadPod( cursor, reserved ) ||
             !ReadPod( cursor, sample.world.gravity ) || !ReadPod( cursor, sample.world.fluidHeight ) ||
             !ReadPod( cursor, sample.world.fluidDensity ) || !ReadVec3( cursor, sample.camera.eye ) ||
             !ReadVec3( cursor, sample.camera.view ) || !ReadVec3( cursor, sample.camera.up ) ||
             !ReadLauncherVisual( cursor, sample.launcherVisual ) ||
             !ReadSolverSnapshot( cursor, sample.worldSnapshot ) || !ReadPod( cursor, bodyCount ) )
        {
            return false;
        }
        (void)reserved;
        sample.sceneFrame = sceneFrame;
        sample.checkpointBoundary = checkpointBoundary != 0;
        ApplyWorldFlags( worldFlags, sample.world );
        sample.bodies.resize( bodyCount );
        for ( ReplaySolverBodySample& body : sample.bodies )
        {
            if ( !ReadSolverBody( cursor, dictionary, body ) )
            {
                return false;
            }
        }
        outCheckpoints.push_back( std::move( sample ) );
    }
    return cursor.offset == cursor.size;
}

bool ParseSolverHashRecords(
    const std::vector<uint8_t>& fileBytes,
    const ChunkTableEntry& chunk,
    std::vector<ReplayV2SolverHashSample>& outHashes
)
{
    outHashes.clear();

    ByteCursor cursor;
    if ( !MakeCursor( fileBytes, chunk.offset, chunk.size, cursor ) )
    {
        return false;
    }

    uint32_t hashCount = 0;
    if ( !ReadPod( cursor, hashCount ) || hashCount != chunk.recordCount )
    {
        return false;
    }

    outHashes.reserve( hashCount );
    for ( uint32_t i = 0; i < hashCount; ++i )
    {
        ReplayV2SolverHashSample sample;
        int32_t sceneFrame = 0;
        uint8_t checkpointBoundary = 0;
        uint8_t reserved[3] = {};

        if ( !ReadPod( cursor, sample.frameIndex ) || !ReadPod( cursor, sceneFrame ) ||
             !ReadPod( cursor, sample.simulationSeconds ) || !ReadPod( cursor, sample.presentationHash ) ||
             !ReadPod( cursor, sample.solverHash ) || !ReadPod( cursor, sample.bodyCount ) ||
             !ReadPod( cursor, sample.contactCount ) || !ReadPod( cursor, sample.pipelineRecordCount ) ||
             !ReadPod( cursor, checkpointBoundary ) || !ReadBytes( cursor, reserved ) )
        {
            return false;
        }

        sample.sceneFrame = sceneFrame;
        sample.checkpointBoundary = checkpointBoundary != 0;
        outHashes.push_back( sample );
    }

    return cursor.offset == cursor.size &&
           cursor.size == sizeof( uint32_t ) + static_cast<std::size_t>( hashCount ) * REPLAY_V2_HASH_ENTRY_BYTES;
}

Chunk MakeChunk( const char id[4], std::vector<uint8_t>&& bytes, uint32_t recordCount )
{
    Chunk chunk;
    std::memcpy( chunk.id, id, sizeof( chunk.id ) );
    chunk.bytes = std::move( bytes );
    chunk.recordCount = recordCount;
    return chunk;
}

std::vector<BranchRecord> BuildBranchRecords( const std::vector<ReplayPresentationSample>& samples )
{
    std::vector<BranchRecord> records;
    for ( const ReplayPresentationSample& sample : samples )
    {
        ReplayBranchInfo branch = sample.branch;
        if ( branch.branchId == 0 )
        {
            branch.branchId = 1;
        }

        auto existing = std::find_if(
            records.begin(),
            records.end(),
            [branch]( const BranchRecord& record ) { return record.branch.branchId == branch.branchId; }
        );

        if ( existing != records.end() )
        {
            existing->firstRetainedFrame = (std::min)( existing->firstRetainedFrame, sample.frameIndex );
            existing->lastRetainedFrame = (std::max)( existing->lastRetainedFrame, sample.frameIndex );
            continue;
        }

        BranchRecord record;
        record.branch = branch;
        record.firstRetainedFrame = sample.frameIndex;
        record.lastRetainedFrame = sample.frameIndex;
        records.push_back( record );
    }
    return records;
}

std::vector<uint8_t> BuildBranchChunk( const std::vector<BranchRecord>& records )
{
    std::vector<uint8_t> bytes;
    AppendPod( bytes, CheckedU32( records.size() ) );
    for ( const BranchRecord& record : records )
    {
        const uint32_t restoredBranchFlag = record.branch.parentBranchId != 0 ? 1u : 0u;
        const uint32_t sourceHashFlag = record.branch.sourceSolverHash != 0 ? 2u : 0u;
        const uint32_t flags = restoredBranchFlag | sourceHashFlag;
        const uint32_t reserved = 0;
        const uint64_t reserved64 = 0;
        AppendPod( bytes, record.branch.branchId );
        AppendPod( bytes, record.branch.parentBranchId );
        AppendPod( bytes, record.branch.startFrame );
        AppendPod( bytes, record.firstRetainedFrame );
        AppendPod( bytes, record.lastRetainedFrame );
        AppendPod( bytes, record.branch.sourceFrame );
        AppendPod( bytes, record.branch.sourceSolverHash );
        AppendPod( bytes, flags );
        AppendPod( bytes, reserved );
        AppendPod( bytes, reserved64 );
    }
    return bytes;
}

std::vector<uint8_t> BuildEventChunk( const std::vector<ReplayEventSample>& events )
{
    std::vector<uint8_t> bytes;
    AppendPod( bytes, CheckedU32( events.size() ) );
    for ( const ReplayEventSample& event : events )
    {
        const uint16_t kind = static_cast<uint16_t>( event.kind );
        const uint32_t reserved = 0;
        AppendPod( bytes, event.frameIndex );
        AppendPod( bytes, event.sequence );
        AppendPod( bytes, event.branch.branchId );
        AppendPod( bytes, event.branch.parentBranchId );
        AppendPod( bytes, kind );
        AppendPod( bytes, event.payloadVersion );
        AppendPod( bytes, event.flags );
        AppendPod( bytes, event.value0 );
        AppendPod( bytes, event.value1 );
        AppendPod( bytes, event.value2 );
        AppendPod( bytes, event.value3 );
        AppendPod( bytes, event.data0 );
        AppendPod( bytes, event.branch.sourceFrame );
        AppendPod( bytes, event.branch.sourceSolverHash );
        AppendBytes( bytes, event.text );
        AppendPod( bytes, reserved );
    }
    return bytes;
}

std::vector<EventCursorRecord> BuildEventCursorRecords( const std::vector<ReplaySolverFrameSample>* solverSamples )
{
    std::vector<EventCursorRecord> records;
    if ( !solverSamples )
    {
        return records;
    }

    for ( const ReplaySolverFrameSample& sample : *solverSamples )
    {
        if ( !sample.checkpointBoundary )
        {
            continue;
        }

        EventCursorRecord record;
        record.frameIndex = sample.frameIndex;
        record.eventCursor = sample.eventCursor;
        record.solverHash = sample.solverHash;
        records.push_back( record );
    }
    return records;
}

std::vector<uint8_t> BuildEventCursorChunk( const std::vector<EventCursorRecord>& records )
{
    std::vector<uint8_t> bytes;
    AppendPod( bytes, CheckedU32( records.size() ) );
    for ( const EventCursorRecord& record : records )
    {
        AppendPod( bytes, record.frameIndex );
        AppendPod( bytes, record.eventCursor );
        AppendPod( bytes, record.flags );
        AppendPod( bytes, record.solverHash );
    }
    return bytes;
}

std::vector<uint8_t> BuildVisualPacketChunk( std::span<const ReplayVisualArchiveSample> samples )
{
    std::vector<uint8_t> bytes;
    AppendPod( bytes, CheckedU32( samples.size() ) );
    std::vector<uint32_t> publishedTopologyVersions;
    publishedTopologyVersions.reserve( samples.size() );
    for ( const ReplayVisualArchiveSample& sample : samples )
    {
        uint32_t canonicalTopologyVersion = 0u;
        if ( sample.topologyVersion != 0u )
        {
            const auto found =
                std::find( publishedTopologyVersions.begin(), publishedTopologyVersions.end(), sample.topologyVersion );
            if ( found == publishedTopologyVersions.end() )
            {
                publishedTopologyVersions.push_back( sample.topologyVersion );
                canonicalTopologyVersion = CheckedU32( publishedTopologyVersions.size() );
            }
            else
            {
                canonicalTopologyVersion = CheckedU32(
                    static_cast<std::size_t>( std::distance( publishedTopologyVersions.begin(), found ) ) + 1u
                );
            }
        }
        constexpr uint64_t canonicalReplayReserveGrowthEvents = 0u;
        // Concept: live semantic telemetry contains raw schedule counters. RVIS
        // instead hashes the unchanged visual/exact content with the same
        // canonical topology and reserve values written into this row.
        const uint64_t canonicalSemanticHash =
            ReplayVisualPacketOperations::BuildCanonicalReplayVisualArchiveSemanticHash(
                sample.visualStateHash,
                sample.exactPacketHash,
                canonicalTopologyVersion,
                canonicalReplayReserveGrowthEvents
            );
#define SB_APPEND_REPLAY_VISUAL_FIELD( member ) AppendPod( bytes, sample.member )
        SB_APPEND_REPLAY_VISUAL_FIELD( sourceFrame );
        SB_APPEND_REPLAY_VISUAL_FIELD( revealFrame );
        AppendPod( bytes, canonicalSemanticHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( visualStateHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( exactPacketHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( schemaVersion );
        SB_APPEND_REPLAY_VISUAL_FIELD( targetId );
        SB_APPEND_REPLAY_VISUAL_FIELD( branchId );
        SB_APPEND_REPLAY_VISUAL_FIELD( eventCursor );
        AppendPod( bytes, canonicalTopologyVersion );
        SB_APPEND_REPLAY_VISUAL_FIELD( publishedFrameCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( predictionEnabled );
        SB_APPEND_REPLAY_VISUAL_FIELD( predictionBuilding );
        SB_APPEND_REPLAY_VISUAL_FIELD( predictionComplete );
        SB_APPEND_REPLAY_VISUAL_FIELD( cameraEye.x );
        SB_APPEND_REPLAY_VISUAL_FIELD( cameraEye.y );
        SB_APPEND_REPLAY_VISUAL_FIELD( cameraEye.z );
        SB_APPEND_REPLAY_VISUAL_FIELD( cameraUp.x );
        SB_APPEND_REPLAY_VISUAL_FIELD( cameraUp.y );
        SB_APPEND_REPLAY_VISUAL_FIELD( cameraUp.z );
        SB_APPEND_REPLAY_VISUAL_FIELD( combinedLineHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( ordinaryLineHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( priorityLineHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( priorityLineCanonicalHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( ordinaryRibbonHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( priorityRibbonHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( priorityRibbonCanonicalHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( expandedVertexHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( ordinaryExpandedVertexHash );
        SB_APPEND_REPLAY_VISUAL_FIELD( droppedSegmentCount );
        AppendPod( bytes, canonicalReplayReserveGrowthEvents );
        SB_APPEND_REPLAY_VISUAL_FIELD( combinedLineBytes );
        SB_APPEND_REPLAY_VISUAL_FIELD( ordinaryLineBytes );
        SB_APPEND_REPLAY_VISUAL_FIELD( priorityLineBytes );
        SB_APPEND_REPLAY_VISUAL_FIELD( ordinaryRibbonBytes );
        SB_APPEND_REPLAY_VISUAL_FIELD( priorityRibbonBytes );
        SB_APPEND_REPLAY_VISUAL_FIELD( expandedVertexBytes );
        SB_APPEND_REPLAY_VISUAL_FIELD( ordinaryExpandedVertexBytes );
        SB_APPEND_REPLAY_VISUAL_FIELD( hasGeometry );
        SB_APPEND_REPLAY_VISUAL_FIELD( trajectoryRecordCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( futureNodeCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( retainedMarkerCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( ghostRequestCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( combinedLineVertexCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( ordinaryLineVertexCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( priorityLineVertexCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( ordinaryRibbonSegmentCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( priorityRibbonSegmentCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( expandedVertexCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( ordinaryExpandedVertexCount );
        SB_APPEND_REPLAY_VISUAL_FIELD( segmentCount );
#undef SB_APPEND_REPLAY_VISUAL_FIELD
    }
    return bytes;
}

bool ParseVisualPacketChunk(
    const std::vector<uint8_t>& fileBytes,
    const ChunkTableEntry& chunk,
    std::vector<ReplayVisualArchiveSample>& outSamples
)
{
    ByteCursor cursor;
    if ( !MakeCursor( fileBytes, chunk.offset, chunk.size, cursor ) )
    {
        return false;
    }
    uint32_t sampleCount = 0;
    if ( !ReadPod( cursor, sampleCount ) || sampleCount != chunk.recordCount ||
         chunk.size !=
             sizeof( sampleCount ) + static_cast<uint64_t>( sampleCount ) * REPLAY_V4_VISUAL_PACKET_ENTRY_BYTES )
    {
        return false;
    }
    outSamples.clear();
    outSamples.reserve( sampleCount );
    for ( uint32_t index = 0; index < sampleCount; ++index )
    {
        ReplayVisualArchiveSample sample;
#define SB_READ_REPLAY_VISUAL_FIELD( member ) ReadPod( cursor, sample.member )
        if ( !SB_READ_REPLAY_VISUAL_FIELD( sourceFrame ) || !SB_READ_REPLAY_VISUAL_FIELD( revealFrame ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( semanticHash ) || !SB_READ_REPLAY_VISUAL_FIELD( visualStateHash ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( exactPacketHash ) || !SB_READ_REPLAY_VISUAL_FIELD( schemaVersion ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( targetId ) || !SB_READ_REPLAY_VISUAL_FIELD( branchId ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( eventCursor ) || !SB_READ_REPLAY_VISUAL_FIELD( topologyVersion ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( publishedFrameCount ) || !SB_READ_REPLAY_VISUAL_FIELD( predictionEnabled ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( predictionBuilding ) || !SB_READ_REPLAY_VISUAL_FIELD( predictionComplete ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( cameraEye.x ) || !SB_READ_REPLAY_VISUAL_FIELD( cameraEye.y ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( cameraEye.z ) || !SB_READ_REPLAY_VISUAL_FIELD( cameraUp.x ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( cameraUp.y ) || !SB_READ_REPLAY_VISUAL_FIELD( cameraUp.z ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( combinedLineHash ) || !SB_READ_REPLAY_VISUAL_FIELD( ordinaryLineHash ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( priorityLineHash ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( priorityLineCanonicalHash ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( ordinaryRibbonHash ) || !SB_READ_REPLAY_VISUAL_FIELD( priorityRibbonHash ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( priorityRibbonCanonicalHash ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( expandedVertexHash ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( ordinaryExpandedVertexHash ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( droppedSegmentCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( replayReserveGrowthEvents ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( combinedLineBytes ) || !SB_READ_REPLAY_VISUAL_FIELD( ordinaryLineBytes ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( priorityLineBytes ) || !SB_READ_REPLAY_VISUAL_FIELD( ordinaryRibbonBytes ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( priorityRibbonBytes ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( expandedVertexBytes ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( ordinaryExpandedVertexBytes ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( hasGeometry ) || !SB_READ_REPLAY_VISUAL_FIELD( trajectoryRecordCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( futureNodeCount ) || !SB_READ_REPLAY_VISUAL_FIELD( retainedMarkerCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( ghostRequestCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( combinedLineVertexCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( ordinaryLineVertexCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( priorityLineVertexCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( ordinaryRibbonSegmentCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( priorityRibbonSegmentCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( expandedVertexCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( ordinaryExpandedVertexCount ) ||
             !SB_READ_REPLAY_VISUAL_FIELD( segmentCount ) )
        {
            return false;
        }
#undef SB_READ_REPLAY_VISUAL_FIELD
        if ( sample.semanticHash == 0 || sample.visualStateHash == 0 || sample.exactPacketHash == 0 ||
             sample.schemaVersion != REPLAY_VISUAL_PACKET_SCHEMA_VERSION || sample.targetId == 0 ||
             sample.predictionEnabled > 1u || sample.predictionBuilding > 1u || sample.predictionComplete > 1u ||
             sample.hasGeometry > 1u || sample.revealFrame != index )
        {
            return false;
        }
        outSamples.push_back( sample );
    }
    return cursor.offset == cursor.size;
}

uint64_t HashVisualPredictionState( std::span<const uint8_t> bytes )
{
    uint64_t hash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    for ( const uint8_t byte : bytes )
    {
        hash ^= static_cast<uint64_t>( byte );
        hash *= REPLAY_VISUAL_BUFFER_FNV_PRIME;
    }
    return hash;
}

std::vector<uint8_t> BuildManifest(
    const std::vector<ReplayPresentationSample>& samples,
    const std::vector<BodyDictionaryEntry>& dictionary,
    std::size_t branchCount,
    std::size_t eventCount,
    std::size_t eventCursorCount,
    std::size_t solverHashCount,
    std::size_t solverCheckpointCount,
    std::size_t visualPacketCount,
    std::size_t visualPredictionBytes,
    uint64_t visualPredictionHash
)
{
    const ReplayPresentationSample& first = samples.front();
    const ReplayPresentationSample& last = samples.back();
    Json chunks = Json::array( { "MANI", "BODY", "PRES", "BRAN" } );
    Json tracks = Json::array( { "presentation", "branchProvenance" } );
    std::string schema = "presentation-v4-visual-state+branch-provenance";
    if ( eventCount > 0 )
    {
        chunks.push_back( "EVNT" );
        tracks.push_back( "events" );
        schema += "+events";
    }
    if ( eventCursorCount > 0 )
    {
        chunks.push_back( "ECUR" );
        tracks.push_back( "eventCursors" );
        schema += "+event-cursors";
    }
    if ( solverHashCount > 0 )
    {
        chunks.push_back( "HASH" );
        tracks.push_back( "solverHashes" );
        schema += "+solver-hashes";
    }
    if ( solverCheckpointCount > 0 )
    {
        chunks.push_back( "SCHK" );
        tracks.push_back( "solverCheckpoints" );
        schema += "+solver-checkpoints";
    }
    if ( visualPacketCount > 0 )
    {
        chunks.push_back( "RVIS" );
        tracks.push_back( "replayVisualPackets" );
        schema += "+replay-visual-packets";
    }
    if ( visualPredictionBytes > 0 )
    {
        chunks.push_back( "RVPD" );
        tracks.push_back( "replayVisualPredictionState" );
        schema += "+replay-visual-prediction-state";
    }
    chunks.push_back( "INDX" );

    Json manifest;
    manifest["format"] = "skullbonez.skreplay";
    manifest["version"] = REPLAY_CURRENT_VERSION;
    manifest["track"] = "presentation";
    manifest["tracks"] = tracks;
    manifest["encoding"] = "little-endian chunked binary";
    manifest["schema"] = schema;
    manifest["frameCount"] = samples.size();
    manifest["bodyDictionaryCount"] = dictionary.size();
    manifest["branchCount"] = branchCount;
    manifest["eventCount"] = eventCount;
    manifest["eventCursorCount"] = eventCursorCount;
    manifest["solverHashCount"] = solverHashCount;
    manifest["solverCheckpointCount"] = solverCheckpointCount;
    manifest["visualPacketCount"] = visualPacketCount;
    manifest["visualPredictionBytes"] = visualPredictionBytes;
    manifest["visualPredictionHash"] = visualPredictionHash;
    manifest["firstFrame"] = first.frameIndex;
    manifest["lastFrame"] = last.frameIndex;
    manifest["firstTimeSeconds"] = first.simulationSeconds;
    manifest["lastTimeSeconds"] = last.simulationSeconds;
    manifest["bodyDictionaryEntryBytes"] = REPLAY_V3_BODY_DICTIONARY_ENTRY_BYTES;
    manifest["bodyPoseBytes"] = REPLAY_V3_BODY_VISUAL_STATE_BYTES;
    manifest["branchEntryBytes"] = REPLAY_V2_BRANCH_ENTRY_BYTES;
    manifest["eventEntryBytes"] = eventCount > 0 ? REPLAY_V2_EVENT_ENTRY_BYTES : 0u;
    manifest["eventCursorEntryBytes"] = eventCursorCount > 0 ? REPLAY_V2_EVENT_CURSOR_ENTRY_BYTES : 0u;
    manifest["solverHashBytes"] = solverHashCount > 0 ? REPLAY_V2_HASH_ENTRY_BYTES : 0u;
    manifest["solverBodyBytes"] = solverCheckpointCount > 0 ? REPLAY_V2_SOLVER_BODY_ENTRY_BYTES : 0u;
    manifest["visualPacketEntryBytes"] = visualPacketCount > 0 ? REPLAY_V4_VISUAL_PACKET_ENTRY_BYTES : 0u;
    manifest["chunks"] = chunks;
    manifest["authoritative"] = false;
    manifest["notes"] =
        "Presentation v4 retains v3 per-body visual state and adds exact full-packet semantic/render digests for "
        "prediction-disabled saved/load/scrub verification. Older chunk layouts remain readable.";

    const std::string jsonText = manifest.dump();
    return std::vector<uint8_t>( jsonText.begin(), jsonText.end() );
}

std::vector<uint8_t> BuildIndex( const std::vector<IndexedFrame>& frames )
{
    std::vector<uint8_t> bytes;
    AppendPod( bytes, CheckedU32( frames.size() ) );
    for ( const IndexedFrame& frame : frames )
    {
        const uint32_t reserved = 0;
        AppendPod( bytes, frame.frameIndex );
        AppendPod( bytes, frame.presentationChunkOffset );
        AppendPod( bytes, frame.bodyCount );
        AppendPod( bytes, reserved );
    }
    return bytes;
}

std::vector<uint8_t> BuildHashChunk( const std::vector<ReplaySolverFrameSample>& solverSamples )
{
    std::vector<uint8_t> bytes;
    AppendPod( bytes, CheckedU32( solverSamples.size() ) );
    for ( const ReplaySolverFrameSample& sample : solverSamples )
    {
        const uint8_t checkpointBoundary = sample.checkpointBoundary ? 1u : 0u;
        const uint8_t reserved[3] = {};

        AppendPod( bytes, sample.frameIndex );
        AppendPod( bytes, static_cast<int32_t>( sample.sceneFrame ) );
        AppendPod( bytes, sample.simulationSeconds );
        AppendPod( bytes, sample.presentationHash );
        AppendPod( bytes, sample.solverHash );
        AppendPod( bytes, CheckedU32( sample.bodies.size() ) );
        AppendPod( bytes, sample.contactCount );
        AppendPod( bytes, sample.pipelineRecordCount );
        AppendPod( bytes, checkpointBoundary );
        AppendBytes( bytes, reserved );
    }
    return bytes;
}

std::size_t CountSolverCheckpoints( const std::vector<ReplaySolverFrameSample>& solverSamples )
{
    return static_cast<std::size_t>( std::count_if(
        solverSamples.begin(),
        solverSamples.end(),
        []( const ReplaySolverFrameSample& sample ) { return sample.checkpointBoundary; }
    ) );
}

bool BuildSolverCheckpointChunk(
    const std::vector<ReplaySolverFrameSample>& solverSamples,
    const std::vector<BodyDictionaryEntry>& dictionary,
    std::vector<uint8_t>& outBytes,
    std::size_t& outCheckpointCount
)
{
    outBytes.clear();
    outCheckpointCount = CountSolverCheckpoints( solverSamples );
    AppendPod( outBytes, CheckedU32( outCheckpointCount ) );
    for ( const ReplaySolverFrameSample& sample : solverSamples )
    {
        if ( !sample.checkpointBoundary )
        {
            continue;
        }

        const uint8_t checkpointBoundary = sample.checkpointBoundary ? 1u : 0u;
        const uint8_t worldFlags = WorldFlags( sample.world );
        const uint16_t reserved16 = 0;
        AppendPod( outBytes, sample.frameIndex );
        AppendPod( outBytes, static_cast<int32_t>( sample.sceneFrame ) );
        AppendPod( outBytes, sample.simulationSeconds );
        AppendPod( outBytes, sample.physicsDt );
        AppendPod( outBytes, sample.presentationHash );
        AppendPod( outBytes, sample.solverHash );
        AppendPod( outBytes, sample.contactCount );
        AppendPod( outBytes, sample.pipelineRecordCount );
        AppendPod( outBytes, checkpointBoundary );
        AppendPod( outBytes, worldFlags );
        AppendPod( outBytes, reserved16 );
        AppendPod( outBytes, sample.world.gravity );
        AppendPod( outBytes, sample.world.fluidHeight );
        AppendPod( outBytes, sample.world.fluidDensity );
        AppendVec3( outBytes, sample.camera.eye );
        AppendVec3( outBytes, sample.camera.view );
        AppendVec3( outBytes, sample.camera.up );
        AppendLauncherVisual( outBytes, sample.launcherVisual );
        AppendSolverSnapshot( outBytes, sample.worldSnapshot );
        AppendPod( outBytes, CheckedU32( sample.bodies.size() ) );
        for ( const ReplaySolverBodySample& body : sample.bodies )
        {
            if ( !AppendSolverBodyRecord( outBytes, dictionary, body ) )
            {
                return false;
            }
        }
    }
    return true;
}

bool BuildChunks(
    const std::vector<ReplayPresentationSample>& samples,
    const std::vector<ReplaySolverFrameSample>* solverSamples,
    const std::vector<ReplayEventSample>* eventSamples,
    std::span<const ReplayVisualArchiveSample> visualPackets,
    std::span<const uint8_t> visualPredictionState,
    std::vector<Chunk>& outChunks
)
{
    if ( samples.size() > static_cast<std::size_t>( ( std::numeric_limits<uint32_t>::max )() ) )
    {
        return false;
    }
    if ( solverSamples && solverSamples->size() > static_cast<std::size_t>( ( std::numeric_limits<uint32_t>::max )() ) )
    {
        return false;
    }
    if ( eventSamples && eventSamples->size() > static_cast<std::size_t>( ( std::numeric_limits<uint32_t>::max )() ) )
    {
        return false;
    }
    if ( visualPackets.size() > static_cast<std::size_t>( ( std::numeric_limits<uint32_t>::max )() ) )
    {
        return false;
    }

    std::vector<BodyDictionaryEntry> dictionary;
    std::vector<IndexedFrame> index;
    std::vector<uint8_t> presentationBytes;
    std::vector<BranchRecord> branchRecords = BuildBranchRecords( samples );

    AppendPod( presentationBytes, CheckedU32( samples.size() ) );
    dictionary.reserve( samples.front().bodies.size() );
    index.reserve( samples.size() );

    // Concept: BODY deduplicates body identity, PRES stores dense render poses,
    // and optional solver/event chunks attach restore data to the same frame
    // ids. Keep this relationship stable for replay_query and old artifacts.
    for ( const ReplayPresentationSample& sample : samples )
    {
        IndexedFrame frame;
        frame.frameIndex = sample.frameIndex;
        frame.presentationChunkOffset = static_cast<uint64_t>( presentationBytes.size() );
        frame.bodyCount = CheckedU32( sample.bodies.size() );
        index.push_back( frame );
        AppendPresentationFrame( presentationBytes, dictionary, sample );
    }

    std::vector<uint8_t> bodyBytes;
    AppendBodyDictionary( bodyBytes, dictionary );

    std::vector<uint8_t> checkpointBytes;
    std::size_t solverCheckpointCount = 0u;
    if ( solverSamples && !solverSamples->empty() &&
         !BuildSolverCheckpointChunk( *solverSamples, dictionary, checkpointBytes, solverCheckpointCount ) )
    {
        return false;
    }

    const std::size_t solverHashCount = solverSamples ? solverSamples->size() : 0u;
    const std::size_t eventCount = eventSamples ? eventSamples->size() : 0u;
    const std::vector<EventCursorRecord> eventCursorRecords =
        eventCount > 0 ? BuildEventCursorRecords( solverSamples ) : std::vector<EventCursorRecord>();
    const std::size_t eventCursorCount = eventCursorRecords.size();
    const uint64_t visualPredictionHash = HashVisualPredictionState( visualPredictionState );
    outChunks.push_back( MakeChunk(
        "MANI",
        BuildManifest(
            samples,
            dictionary,
            branchRecords.size(),
            eventCount,
            eventCursorCount,
            solverHashCount,
            solverCheckpointCount,
            visualPackets.size(),
            visualPredictionState.size(),
            visualPredictionHash
        ),
        1u
    ) );
    outChunks.push_back( MakeChunk( "BODY", std::move( bodyBytes ), CheckedU32( dictionary.size() ) ) );
    outChunks.push_back( MakeChunk( "PRES", std::move( presentationBytes ), CheckedU32( samples.size() ) ) );
    outChunks.push_back( MakeChunk( "BRAN", BuildBranchChunk( branchRecords ), CheckedU32( branchRecords.size() ) ) );
    if ( eventSamples && !eventSamples->empty() )
    {
        outChunks.push_back(
            MakeChunk( "EVNT", BuildEventChunk( *eventSamples ), CheckedU32( eventSamples->size() ) )
        );
    }
    if ( !eventCursorRecords.empty() )
    {
        outChunks.push_back(
            MakeChunk( "ECUR", BuildEventCursorChunk( eventCursorRecords ), CheckedU32( eventCursorRecords.size() ) )
        );
    }
    if ( solverSamples && !solverSamples->empty() )
    {
        outChunks.push_back(
            MakeChunk( "HASH", BuildHashChunk( *solverSamples ), CheckedU32( solverSamples->size() ) )
        );
    }
    if ( solverCheckpointCount > 0u )
    {
        outChunks.push_back( MakeChunk( "SCHK", std::move( checkpointBytes ), CheckedU32( solverCheckpointCount ) ) );
    }
    if ( !visualPackets.empty() )
    {
        outChunks.push_back(
            MakeChunk( "RVIS", BuildVisualPacketChunk( visualPackets ), CheckedU32( visualPackets.size() ) )
        );
    }
    if ( !visualPredictionState.empty() )
    {
        outChunks.push_back(
            MakeChunk( "RVPD", std::vector<uint8_t>( visualPredictionState.begin(), visualPredictionState.end() ), 1u )
        );
    }
    outChunks.push_back( MakeChunk( "INDX", BuildIndex( index ), CheckedU32( index.size() ) ) );
    return true;
}

bool BuildFileBytes( const std::vector<Chunk>& chunks, std::vector<uint8_t>& outBytes )
{
    if ( chunks.size() > static_cast<std::size_t>( ( std::numeric_limits<uint32_t>::max )() ) )
    {
        return false;
    }

    const uint32_t chunkCount = CheckedU32( chunks.size() );
    const uint64_t chunkTableOffset = REPLAY_V2_HEADER_BYTES;
    uint64_t nextChunkOffset = chunkTableOffset + ( static_cast<uint64_t>( chunkCount ) * REPLAY_V2_CHUNK_ENTRY_BYTES );

    std::vector<uint64_t> chunkOffsets;
    chunkOffsets.reserve( chunks.size() );
    // Why: payload offsets are computed before writing bytes so the chunk table
    // can be emitted once at the front without seeking or patching the file.
    for ( const Chunk& chunk : chunks )
    {
        chunkOffsets.push_back( nextChunkOffset );
        nextChunkOffset += static_cast<uint64_t>( chunk.bytes.size() );
    }

    const uint64_t fileSize = nextChunkOffset;
    if ( fileSize > static_cast<uint64_t>( ( std::numeric_limits<std::size_t>::max )() ) )
    {
        return false;
    }

    outBytes.clear();
    outBytes.reserve( static_cast<std::size_t>( fileSize ) );
    AppendBytes( outBytes, REPLAY_V2_MAGIC );
    AppendPod( outBytes, REPLAY_CURRENT_VERSION );
    AppendPod( outBytes, REPLAY_V2_HEADER_BYTES );
    AppendPod( outBytes, chunkCount );
    AppendPod( outBytes, static_cast<uint32_t>( 0 ) );
    AppendPod( outBytes, chunkTableOffset );
    AppendPod( outBytes, fileSize );

    for ( std::size_t i = 0; i < chunks.size(); ++i )
    {
        const Chunk& chunk = chunks[i];
        AppendChunkId( outBytes, chunk.id );
        AppendPod( outBytes, chunkOffsets[i] );
        AppendPod( outBytes, static_cast<uint64_t>( chunk.bytes.size() ) );
        AppendPod( outBytes, chunk.recordCount );
        AppendPod( outBytes, static_cast<uint32_t>( 0 ) );
    }

    for ( const Chunk& chunk : chunks )
    {
        AppendBytes( outBytes, { chunk.bytes.data(), chunk.bytes.size() } );
    }

    return outBytes.size() == static_cast<std::size_t>( fileSize );
}
} // namespace

void ReplayArtifactSource::MaterializePresentation(
    const ReplayRecorder& recorder,
    std::vector<ReplayPresentationSample>& outSamples
)
{
    outSamples.clear();
    outSamples.reserve( recorder.m_sampleCount );
    if ( recorder.m_sampleCount == 0 || recorder.m_samples.empty() )
    {
        return;
    }

    for ( std::size_t i = 0; i < recorder.m_sampleCount; ++i )
    {
        ReplayPresentationSample sample;
        if ( recorder.ResolveSampleAtOffset( i, sample ) )
        {
            outSamples.push_back( std::move( sample ) );
        }
    }
}

void ReplayArtifactSource::MaterializeSolver(
    const ReplaySolverRecorder& recorder,
    std::vector<ReplaySolverFrameSample>& outSamples
)
{
    outSamples.clear();
    outSamples.reserve( recorder.m_sampleCount );
    if ( recorder.m_sampleCount == 0 || recorder.m_samples.empty() )
    {
        return;
    }

    for ( std::size_t i = 0; i < recorder.m_sampleCount; ++i )
    {
        ReplaySolverFrameSample sample;
        if ( recorder.ResolveSolverSampleAtOffset( i, sample ) )
        {
            outSamples.push_back( std::move( sample ) );
        }
    }
}

void ReplayArtifactSource::MaterializeEvents(
    const ReplayEventRecorder& recorder,
    std::vector<ReplayEventSample>& outEvents
)
{
    outEvents.clear();
    outEvents.reserve( recorder.m_eventCount );
    if ( recorder.m_eventCount == 0 || recorder.m_events.empty() )
    {
        return;
    }

    for ( std::size_t i = 0; i < recorder.m_eventCount; ++i )
    {
        const std::size_t index = ( recorder.m_eventHead + i ) % recorder.m_events.size();
        outEvents.push_back( recorder.m_events[index] );
    }
}

bool ReplayV2Artifact::SavePresentation( const ReplayRecorder& recorder, const char* path, ReplayV2SaveResult* result )
{
    std::vector<ReplayPresentationSample> samples;
    ReplayArtifactSource::MaterializePresentation( recorder, samples );
    if ( samples.empty() )
    {
        return false;
    }

    std::vector<Chunk> chunks;
    if ( !BuildChunks( samples, nullptr, nullptr, {}, {}, chunks ) )
    {
        return false;
    }

    std::vector<uint8_t> fileBytes;
    if ( !BuildFileBytes( chunks, fileBytes ) )
    {
        return false;
    }

    if ( !RuntimeFileWriter::EnsureParentDirectory( path ) )
    {
        return false;
    }

    std::ofstream output( path, std::ios::out | std::ios::binary | std::ios::trunc );
    if ( !output.is_open() )
    {
        return false;
    }

    if ( fileBytes.size() > static_cast<std::size_t>( ( std::numeric_limits<std::streamsize>::max )() ) )
    {
        return false;
    }

    // Why: std::ostream's binary write ABI is char-based; fileBytes remains the
    // typed owner and the stream borrows it only for this synchronous call.
    output.write( reinterpret_cast<const char*>( fileBytes.data() ), static_cast<std::streamsize>( fileBytes.size() ) );
    if ( !output.good() )
    {
        return false;
    }
    output.close();
    if ( output.fail() )
    {
        return false;
    }

    if ( result )
    {
        const Chunk& bodyChunk = chunks[1];
        result->sampleCount = samples.size();
        result->bodyDictionaryCount = bodyChunk.recordCount;
        result->solverHashCount = 0;
        result->solverCheckpointCount = 0;
        result->eventCount = 0;
        result->eventCursorCount = 0;
        result->visualPacketCount = 0;
        result->visualPredictionHash = 0;
        result->fileBytes = fileBytes.size();
    }
    return true;
}

namespace
{
bool SavePresentationWithTracks(
    const ReplayRecorder& recorder,
    const ReplaySolverRecorder& solverRecorder,
    const ReplayEventRecorder* eventRecorder,
    std::span<const ReplayVisualArchiveSample> visualPackets,
    std::span<const uint8_t> visualPredictionState,
    const char* path,
    ReplayV2SaveResult* result
)
{
    std::vector<ReplayPresentationSample> samples;
    ReplayArtifactSource::MaterializePresentation( recorder, samples );
    if ( samples.empty() )
    {
        return false;
    }

    std::vector<ReplaySolverFrameSample> solverSamples;
    ReplayArtifactSource::MaterializeSolver( solverRecorder, solverSamples );

    std::vector<ReplayEventSample> eventSamples;
    if ( eventRecorder )
    {
        ReplayArtifactSource::MaterializeEvents( *eventRecorder, eventSamples );
    }

    std::vector<Chunk> chunks;
    if ( !BuildChunks( samples, &solverSamples, &eventSamples, visualPackets, visualPredictionState, chunks ) )
    {
        return false;
    }

    std::vector<uint8_t> fileBytes;
    if ( !BuildFileBytes( chunks, fileBytes ) )
    {
        return false;
    }

    if ( !RuntimeFileWriter::EnsureParentDirectory( path ) )
    {
        return false;
    }

    std::ofstream output( path, std::ios::out | std::ios::binary | std::ios::trunc );
    if ( !output.is_open() )
    {
        return false;
    }

    if ( fileBytes.size() > static_cast<std::size_t>( ( std::numeric_limits<std::streamsize>::max )() ) )
    {
        return false;
    }

    // Why: std::ostream's binary write ABI is char-based; fileBytes remains the
    // typed owner and the stream borrows it only for this synchronous call.
    output.write( reinterpret_cast<const char*>( fileBytes.data() ), static_cast<std::streamsize>( fileBytes.size() ) );
    if ( !output.good() )
    {
        return false;
    }
    output.close();
    if ( output.fail() )
    {
        return false;
    }

    if ( result )
    {
        const Chunk& bodyChunk = chunks[1];
        result->sampleCount = samples.size();
        result->bodyDictionaryCount = bodyChunk.recordCount;
        result->solverHashCount = solverSamples.size();
        result->solverCheckpointCount = CountSolverCheckpoints( solverSamples );
        result->eventCount = eventSamples.size();
        result->eventCursorCount = !eventSamples.empty() ? CountSolverCheckpoints( solverSamples ) : 0;
        result->visualPacketCount = visualPackets.size();
        result->visualPredictionHash = HashVisualPredictionState( visualPredictionState );
        result->fileBytes = fileBytes.size();
    }
    return true;
}
} // namespace

bool ReplayV2Artifact::SavePresentationWithSolverHashes(
    const ReplayRecorder& recorder,
    const ReplaySolverRecorder& solverRecorder,
    const char* path,
    ReplayV2SaveResult* result
)
{
    return SavePresentationWithTracks( recorder, solverRecorder, nullptr, {}, {}, path, result );
}

bool ReplayV2Artifact::SavePresentationWithSolverHashes(
    const ReplayRecorder& recorder,
    const ReplaySolverRecorder& solverRecorder,
    const ReplayEventRecorder& eventRecorder,
    const char* path,
    ReplayV2SaveResult* result
)
{
    return SavePresentationWithTracks( recorder, solverRecorder, &eventRecorder, {}, {}, path, result );
}

bool ReplayV2Artifact::SavePresentationWithSolverHashes(
    const ReplayRecorder& recorder,
    const ReplaySolverRecorder& solverRecorder,
    const ReplayEventRecorder& eventRecorder,
    std::span<const ReplayVisualArchiveSample> visualPackets,
    std::span<const uint8_t> visualPredictionState,
    const char* path,
    ReplayV2SaveResult* result
)
{
    return SavePresentationWithTracks(
        recorder,
        solverRecorder,
        &eventRecorder,
        visualPackets,
        visualPredictionState,
        path,
        result
    );
}

bool ReplayV2Artifact::LoadPresentation(
    const char* path,
    std::vector<ReplayPresentationSample>& outSamples,
    ReplayV2LoadResult* result
)
{
    outSamples.clear();

    std::vector<uint8_t> fileBytes;
    if ( !LoadBinaryFile( path, fileBytes ) )
    {
        return false;
    }

    std::vector<ChunkTableEntry> chunkTable;
    uint32_t version = 0;
    if ( !ReadChunkTable( fileBytes, chunkTable, version ) )
    {
        return false;
    }

    const ChunkTableEntry* bodyChunk = FindChunk( chunkTable, "BODY" );
    const ChunkTableEntry* presentationChunk = FindChunk( chunkTable, "PRES" );
    const ChunkTableEntry* indexChunk = FindChunk( chunkTable, "INDX" );
    const ChunkTableEntry* branchChunk = FindChunk( chunkTable, "BRAN" );
    // Invariant: BODY, PRES, and INDX form the minimum render-preview artifact.
    // Branch data is optional so old or partial files remain readable.
    if ( !bodyChunk || !presentationChunk || !indexChunk )
    {
        return false;
    }

    std::vector<BodyDictionaryEntry> dictionary;
    std::vector<IndexedFrame> index;
    std::vector<BranchRecord> branches;
    if ( !ParseBodyDictionary( fileBytes, *bodyChunk, version, dictionary ) )
    {
        outSamples.clear();
        return false;
    }
    if ( !ParseIndex( fileBytes, *indexChunk, index ) )
    {
        outSamples.clear();
        return false;
    }
    if ( branchChunk && !ParseBranchRecords( fileBytes, *branchChunk, branches ) )
    {
        outSamples.clear();
        return false;
    }
    if ( !ParsePresentationSamples( fileBytes, *presentationChunk, version, dictionary, index, outSamples ) )
    {
        outSamples.clear();
        return false;
    }
    ApplyBranchMetadata( branches, outSamples );

    if ( result )
    {
        result->sampleCount = outSamples.size();
        result->bodyDictionaryCount = dictionary.size();
        result->fileBytes = fileBytes.size();
        if ( !outSamples.empty() )
        {
            result->firstFrame = outSamples.front().frameIndex;
            result->lastFrame = outSamples.back().frameIndex;
        }
    }
    return !outSamples.empty();
}

bool ReplayV2Artifact::LoadSolverCheckpoints(
    const char* path,
    std::vector<ReplaySolverFrameSample>& outCheckpoints,
    ReplayV2SolverCheckpointLoadResult* result
)
{
    outCheckpoints.clear();

    std::vector<uint8_t> fileBytes;
    if ( !LoadBinaryFile( path, fileBytes ) )
    {
        return false;
    }

    std::vector<ChunkTableEntry> chunkTable;
    uint32_t version = 0;
    if ( !ReadChunkTable( fileBytes, chunkTable, version ) )
    {
        return false;
    }

    const ChunkTableEntry* bodyChunk = FindChunk( chunkTable, "BODY" );
    const ChunkTableEntry* checkpointChunk = FindChunk( chunkTable, "SCHK" );
    const ChunkTableEntry* branchChunk = FindChunk( chunkTable, "BRAN" );
    const ChunkTableEntry* eventCursorChunk = FindChunk( chunkTable, "ECUR" );
    // Invariant: solver restore requires a body dictionary and checkpoint
    // payloads. Branch/event cursors improve rollback provenance but are not
    // required for basic checkpoint loading.
    if ( !bodyChunk || !checkpointChunk )
    {
        return false;
    }

    std::vector<BodyDictionaryEntry> dictionary;
    std::vector<BranchRecord> branches;
    std::vector<EventCursorRecord> eventCursors;
    if ( !ParseBodyDictionary( fileBytes, *bodyChunk, version, dictionary ) )
    {
        return false;
    }
    if ( branchChunk && !ParseBranchRecords( fileBytes, *branchChunk, branches ) )
    {
        return false;
    }
    if ( eventCursorChunk && !ParseEventCursorRecords( fileBytes, *eventCursorChunk, eventCursors ) )
    {
        return false;
    }
    if ( !ParseSolverCheckpoints( fileBytes, *checkpointChunk, dictionary, outCheckpoints ) )
    {
        outCheckpoints.clear();
        return false;
    }
    ApplyBranchMetadata( branches, outCheckpoints );
    ApplyEventCursorMetadata( eventCursors, outCheckpoints );

    if ( result )
    {
        result->checkpointCount = outCheckpoints.size();
        result->bodyDictionaryCount = dictionary.size();
        result->fileBytes = fileBytes.size();
        if ( !outCheckpoints.empty() )
        {
            result->firstFrame = outCheckpoints.front().frameIndex;
            result->lastFrame = outCheckpoints.back().frameIndex;
        }
    }
    return !outCheckpoints.empty();
}

bool ReplayV2Artifact::LoadEvents(
    const char* path,
    std::vector<ReplayEventSample>& outEvents,
    ReplayV2EventLoadResult* result
)
{
    outEvents.clear();

    std::vector<uint8_t> fileBytes;
    if ( !LoadBinaryFile( path, fileBytes ) )
    {
        return false;
    }

    std::vector<ChunkTableEntry> chunkTable;
    uint32_t version = 0;
    if ( !ReadChunkTable( fileBytes, chunkTable, version ) )
    {
        return false;
    }

    const ChunkTableEntry* eventChunk = FindChunk( chunkTable, "EVNT" );
    (void)version;
    if ( !eventChunk )
    {
        return false;
    }

    if ( !ParseEventRecords( fileBytes, *eventChunk, outEvents ) )
    {
        outEvents.clear();
        return false;
    }

    if ( result )
    {
        result->eventCount = outEvents.size();
        result->fileBytes = fileBytes.size();
        if ( !outEvents.empty() )
        {
            result->firstFrame = outEvents.front().frameIndex;
            result->lastFrame = outEvents.back().frameIndex;
        }
    }
    return !outEvents.empty();
}

bool ReplayV2Artifact::LoadSolverHashes(
    const char* path,
    std::vector<ReplayV2SolverHashSample>& outHashes,
    ReplayV2SolverHashLoadResult* result
)
{
    outHashes.clear();

    std::vector<uint8_t> fileBytes;
    if ( !LoadBinaryFile( path, fileBytes ) )
    {
        return false;
    }

    std::vector<ChunkTableEntry> chunkTable;
    uint32_t version = 0;
    if ( !ReadChunkTable( fileBytes, chunkTable, version ) )
    {
        return false;
    }

    const ChunkTableEntry* hashChunk = FindChunk( chunkTable, "HASH" );
    (void)version;
    if ( !hashChunk )
    {
        return false;
    }

    if ( !ParseSolverHashRecords( fileBytes, *hashChunk, outHashes ) )
    {
        outHashes.clear();
        return false;
    }

    if ( result )
    {
        result->hashCount = outHashes.size();
        result->fileBytes = fileBytes.size();
        if ( !outHashes.empty() )
        {
            result->firstFrame = outHashes.front().frameIndex;
            result->lastFrame = outHashes.back().frameIndex;
        }
    }
    return !outHashes.empty();
}

bool ReplayV2Artifact::LoadVisualPackets( const char* path, std::vector<ReplayVisualArchiveSample>& outPackets )
{
    outPackets.clear();
    std::vector<uint8_t> fileBytes;
    if ( !LoadBinaryFile( path, fileBytes ) )
    {
        return false;
    }

    std::vector<ChunkTableEntry> chunkTable;
    uint32_t version = 0;
    if ( !ReadChunkTable( fileBytes, chunkTable, version ) || version < 4u )
    {
        return false;
    }
    const ChunkTableEntry* visualChunk = FindChunk( chunkTable, "RVIS" );
    if ( !visualChunk || !ParseVisualPacketChunk( fileBytes, *visualChunk, outPackets ) )
    {
        outPackets.clear();
        return false;
    }
    return !outPackets.empty();
}

bool ReplayV2Artifact::LoadVisualPredictionState( const char* path, std::vector<uint8_t>& outBytes )
{
    outBytes.clear();
    std::vector<uint8_t> fileBytes;
    if ( !LoadBinaryFile( path, fileBytes ) )
    {
        return false;
    }
    std::vector<ChunkTableEntry> chunkTable;
    uint32_t version = 0;
    if ( !ReadChunkTable( fileBytes, chunkTable, version ) || version < 4u )
    {
        return false;
    }
    const ChunkTableEntry* predictionChunk = FindChunk( chunkTable, "RVPD" );
    if ( !predictionChunk || predictionChunk->recordCount != 1u || predictionChunk->offset > fileBytes.size() ||
         predictionChunk->size > fileBytes.size() - static_cast<std::size_t>( predictionChunk->offset ) )
    {
        return false;
    }
    const std::size_t begin = static_cast<std::size_t>( predictionChunk->offset );
    const std::size_t end = begin + static_cast<std::size_t>( predictionChunk->size );
    outBytes.assign(
        fileBytes.begin() + static_cast<std::ptrdiff_t>( begin ),
        fileBytes.begin() + static_cast<std::ptrdiff_t>( end )
    );

    return !outBytes.empty();
}
