/*
File: SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp
Purpose:
  Writes compact chunked binary v2 replay presentation artifacts.

Mental model:
  The presentation v2 file is a seekable pose stream: metadata is deduplicated
  into a body dictionary, while each frame stores only the body dictionary index,
  position, and orientation needed for smooth scrub preview.

Glossary:
  MANI: UTF-8 JSON manifest chunk with human-readable file facts.
  BODY: Body dictionary chunk.
  PRES: Presentation frame chunk with dense 32-byte pose records.
  INDX: Frame seek index into the presentation chunk.

Invariants:
  - Numeric payloads are emitted in the host little-endian layout used by the
    Windows runtime. The manifest marks the file as little-endian.
  - Per-body pose records stay 32 bytes: body dictionary index, position, quat.
  - Legacy v1 JSON exporters remain available for old debug workflows.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
  - tools/replay_query.py
*/
#include "ReplayV2Artifact.h"

#include "../RuntimeFileWriter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

using namespace SkullbonezCore::Basics;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
using Json = nlohmann::ordered_json;

constexpr uint32_t REPLAY_V2_VERSION = 2;
constexpr uint32_t REPLAY_V2_HEADER_BYTES = 40;
constexpr uint32_t REPLAY_V2_CHUNK_ENTRY_BYTES = 28;
constexpr uint32_t REPLAY_V2_BODY_POSE_BYTES = 32;
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
    int32_t modelIndex = -1;
    ReplayBodyShapeKind shapeKind = ReplayBodyShapeKind::Unknown;
    char name[64] = {};
};

struct Chunk
{
    char id[4] = {};
    std::vector<uint8_t> bytes;
    uint32_t recordCount = 0;
};

struct IndexedFrame
{
    ReplayFrameIndex frameIndex = 0;
    uint64_t presentationChunkOffset = 0;
    uint32_t bodyCount = 0;
};

template <typename T> void AppendPod( std::vector<uint8_t>& out, const T& value )
{
    static_assert( std::is_trivially_copyable<T>::value, "Replay v2 payload values must be POD" );
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>( &value );
    out.insert( out.end(), bytes, bytes + sizeof( T ) );
}

void AppendBytes( std::vector<uint8_t>& out, const void* data, std::size_t size )
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>( data );
    out.insert( out.end(), bytes, bytes + size );
}

void AppendChunkId( std::vector<uint8_t>& out, const char id[4] )
{
    AppendBytes( out, id, 4 );
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
    return entry.id == body.id.value && entry.modelIndex == body.modelIndex;
}

uint32_t FindOrAddBody( std::vector<BodyDictionaryEntry>& dictionary, const ReplayBodyPresentationSample& body )
{
    const auto found =
        std::find_if( dictionary.begin(),
                      dictionary.end(),
                      [&body]( const BodyDictionaryEntry& entry ) { return SameDictionaryBody( entry, body ); } );
    if ( found != dictionary.end() )
    {
        return static_cast<uint32_t>( found - dictionary.begin() );
    }

    BodyDictionaryEntry entry;
    entry.id = body.id.value;
    entry.modelIndex = static_cast<int32_t>( body.modelIndex );
    entry.shapeKind = body.shapeKind;
    std::memcpy( entry.name, body.name, sizeof( entry.name ) );
    dictionary.push_back( entry );
    return static_cast<uint32_t>( dictionary.size() - 1u );
}

void AppendBodyDictionary( std::vector<uint8_t>& out, const std::vector<BodyDictionaryEntry>& dictionary )
{
    const uint32_t bodyCount = CheckedU32( dictionary.size() );
    AppendPod( out, bodyCount );
    for ( const BodyDictionaryEntry& entry : dictionary )
    {
        const uint8_t shapeKind = static_cast<uint8_t>( entry.shapeKind );
        const uint8_t reserved[3] = {};
        AppendPod( out, entry.id );
        AppendPod( out, entry.modelIndex );
        AppendPod( out, shapeKind );
        AppendBytes( out, reserved, sizeof( reserved ) );
        AppendBytes( out, entry.name, sizeof( entry.name ) );
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

void AppendPresentationFrame( std::vector<uint8_t>& out,
                              std::vector<BodyDictionaryEntry>& dictionary,
                              const ReplayPresentationSample& sample )
{
    AppendFrameHeader( out, sample );
    for ( const ReplayBodyPresentationSample& body : sample.bodies )
    {
        const uint32_t dictionaryIndex = FindOrAddBody( dictionary, body );
        AppendPod( out, dictionaryIndex );
        AppendVec3( out, body.position );
        AppendOrientation( out, body.orientation );
    }
}

Chunk MakeChunk( const char id[4], std::vector<uint8_t>&& bytes, uint32_t recordCount )
{
    Chunk chunk;
    std::memcpy( chunk.id, id, sizeof( chunk.id ) );
    chunk.bytes = std::move( bytes );
    chunk.recordCount = recordCount;
    return chunk;
}

std::vector<uint8_t> BuildManifest( const std::vector<ReplayPresentationSample>& samples,
                                    const std::vector<BodyDictionaryEntry>& dictionary )
{
    const ReplayPresentationSample& first = samples.front();
    const ReplayPresentationSample& last = samples.back();

    Json manifest;
    manifest["format"] = "skullbonez.skreplay";
    manifest["version"] = REPLAY_V2_VERSION;
    manifest["track"] = "presentation";
    manifest["encoding"] = "little-endian chunked binary";
    manifest["schema"] = "presentation-v2";
    manifest["frameCount"] = samples.size();
    manifest["bodyDictionaryCount"] = dictionary.size();
    manifest["firstFrame"] = first.frameIndex;
    manifest["lastFrame"] = last.frameIndex;
    manifest["firstTimeSeconds"] = first.simulationSeconds;
    manifest["lastTimeSeconds"] = last.simulationSeconds;
    manifest["bodyPoseBytes"] = REPLAY_V2_BODY_POSE_BYTES;
    manifest["chunks"] = Json::array( { "MANI", "BODY", "PRES", "INDX" } );
    manifest["authoritative"] = false;
    manifest["notes"] = "Presentation v2 supports smooth visual scrub; solver checkpoint/event chunks are not present.";

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

bool BuildChunks( const std::vector<ReplayPresentationSample>& samples, std::vector<Chunk>& outChunks )
{
    if ( samples.size() > static_cast<std::size_t>( ( std::numeric_limits<uint32_t>::max )() ) )
    {
        return false;
    }

    std::vector<BodyDictionaryEntry> dictionary;
    std::vector<IndexedFrame> index;
    std::vector<uint8_t> presentationBytes;

    AppendPod( presentationBytes, CheckedU32( samples.size() ) );
    dictionary.reserve( samples.front().bodies.size() );
    index.reserve( samples.size() );

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

    outChunks.push_back( MakeChunk( "MANI", BuildManifest( samples, dictionary ), 1u ) );
    outChunks.push_back( MakeChunk( "BODY", std::move( bodyBytes ), CheckedU32( dictionary.size() ) ) );
    outChunks.push_back( MakeChunk( "PRES", std::move( presentationBytes ), CheckedU32( samples.size() ) ) );
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
    AppendBytes( outBytes, REPLAY_V2_MAGIC, sizeof( REPLAY_V2_MAGIC ) );
    AppendPod( outBytes, REPLAY_V2_VERSION );
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
        AppendBytes( outBytes, chunk.bytes.data(), chunk.bytes.size() );
    }

    return outBytes.size() == static_cast<std::size_t>( fileSize );
}
} // namespace

bool ReplayV2Artifact::SavePresentation( const ReplayRecorder& recorder, const char* path, ReplayV2SaveResult* result )
{
    std::vector<ReplayPresentationSample> samples;
    recorder.CopySamplesChronological( samples );
    if ( samples.empty() )
    {
        return false;
    }

    std::vector<Chunk> chunks;
    if ( !BuildChunks( samples, chunks ) )
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

    output.write( reinterpret_cast<const char*>( fileBytes.data() ), static_cast<std::streamsize>( fileBytes.size() ) );
    if ( !output.good() )
    {
        return false;
    }

    if ( result )
    {
        const Chunk& bodyChunk = chunks[1];
        result->sampleCount = samples.size();
        result->bodyDictionaryCount = bodyChunk.recordCount;
        result->fileBytes = fileBytes.size();
    }
    return true;
}
