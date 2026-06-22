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
constexpr uint32_t REPLAY_V2_BODY_DICTIONARY_ENTRY_BYTES = 76;
constexpr uint32_t REPLAY_V2_FRAME_HEADER_BYTES = 92;
constexpr uint32_t REPLAY_V2_INDEX_ENTRY_BYTES = 24;
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
    static_assert( std::is_trivially_copyable<T>::value, "Replay v2 payload values must be POD" );
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>( &value );
    out.insert( out.end(), bytes, bytes + sizeof( T ) );
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

void AppendBytes( std::vector<uint8_t>& out, const void* data, std::size_t size )
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>( data );
    out.insert( out.end(), bytes, bytes + size );
}

bool ReadBytes( ByteCursor& cursor, void* out, std::size_t size )
{
    if ( cursor.offset > cursor.size || size > cursor.size - cursor.offset )
    {
        return false;
    }

    std::memcpy( out, cursor.data + cursor.offset, size );
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
    input.read( reinterpret_cast<char*>( outBytes.data() ), static_cast<std::streamsize>( outBytes.size() ) );
    return static_cast<std::size_t>( input.gcount() ) == outBytes.size();
}

bool ReadChunkTable( const std::vector<uint8_t>& fileBytes, std::vector<ChunkTableEntry>& outChunks )
{
    outChunks.clear();

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
    if ( !ReadBytes( header, magic, sizeof( magic ) ) || !ReadPod( header, version ) ||
         !ReadPod( header, headerBytes ) || !ReadPod( header, chunkCount ) || !ReadPod( header, flags ) ||
         !ReadPod( header, chunkTableOffset ) || !ReadPod( header, fileSize ) )
    {
        return false;
    }
    (void)flags;

    if ( std::memcmp( magic, REPLAY_V2_MAGIC, sizeof( magic ) ) != 0 || version != REPLAY_V2_VERSION ||
         headerBytes != REPLAY_V2_HEADER_BYTES || fileSize != static_cast<uint64_t>( fileBytes.size() ) )
    {
        return false;
    }

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
        if ( !ReadBytes( table, entry.id, sizeof( entry.id ) ) || !ReadPod( table, entry.offset ) ||
             !ReadPod( table, entry.size ) || !ReadPod( table, entry.recordCount ) || !ReadPod( table, reserved ) )
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

bool ParseBodyDictionary( const std::vector<uint8_t>& fileBytes,
                          const ChunkTableEntry& chunk,
                          std::vector<BodyDictionaryEntry>& outDictionary )
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
        if ( !ReadPod( cursor, entry.id ) || !ReadPod( cursor, entry.modelIndex ) || !ReadPod( cursor, shapeKind ) ||
             !SkipBytes( cursor, 3 ) || !ReadBytes( cursor, entry.name, sizeof( entry.name ) ) )
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

    return cursor.offset == cursor.size &&
           cursor.size ==
               sizeof( uint32_t ) + static_cast<std::size_t>( bodyCount ) * REPLAY_V2_BODY_DICTIONARY_ENTRY_BYTES;
}

bool ParseIndex( const std::vector<uint8_t>& fileBytes,
                 const ChunkTableEntry& chunk,
                 std::vector<IndexedFrame>& outFrames )
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

bool ParsePresentationSamples( const std::vector<uint8_t>& fileBytes,
                               const ChunkTableEntry& chunk,
                               const std::vector<BodyDictionaryEntry>& dictionary,
                               const std::vector<IndexedFrame>& indexedFrames,
                               std::vector<ReplayPresentationSample>& outSamples )
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

            const BodyDictionaryEntry& entry = dictionary[dictionaryIndex];
            body.id.value = entry.id;
            body.modelIndex = entry.modelIndex;
            body.shapeKind = entry.shapeKind;
            std::memcpy( body.name, entry.name, sizeof( body.name ) );
            sample.bodies.push_back( body );
        }

        const std::size_t expectedFrameBytes =
            REPLAY_V2_FRAME_HEADER_BYTES + static_cast<std::size_t>( bodyCount ) * REPLAY_V2_BODY_POSE_BYTES;
        if ( frameCursor.offset != expectedFrameBytes )
        {
            return false;
        }
        outSamples.push_back( std::move( sample ) );
    }

    return true;
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
        result->fileBytes = fileBytes.size();
    }
    return true;
}

bool ReplayV2Artifact::LoadPresentation( const char* path,
                                         std::vector<ReplayPresentationSample>& outSamples,
                                         ReplayV2LoadResult* result )
{
    outSamples.clear();

    std::vector<uint8_t> fileBytes;
    if ( !LoadBinaryFile( path, fileBytes ) )
    {
        return false;
    }

    std::vector<ChunkTableEntry> chunkTable;
    if ( !ReadChunkTable( fileBytes, chunkTable ) )
    {
        return false;
    }

    const ChunkTableEntry* bodyChunk = FindChunk( chunkTable, "BODY" );
    const ChunkTableEntry* presentationChunk = FindChunk( chunkTable, "PRES" );
    const ChunkTableEntry* indexChunk = FindChunk( chunkTable, "INDX" );
    if ( !bodyChunk || !presentationChunk || !indexChunk )
    {
        return false;
    }

    std::vector<BodyDictionaryEntry> dictionary;
    std::vector<IndexedFrame> index;
    if ( !ParseBodyDictionary( fileBytes, *bodyChunk, dictionary ) )
    {
        outSamples.clear();
        return false;
    }
    if ( !ParseIndex( fileBytes, *indexChunk, index ) )
    {
        outSamples.clear();
        return false;
    }
    if ( !ParsePresentationSamples( fileBytes, *presentationChunk, dictionary, index, outSamples ) )
    {
        outSamples.clear();
        return false;
    }

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
