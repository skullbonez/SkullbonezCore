/*
File: SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.cpp
Purpose:
  Implements the shared canonical replay visual-packet fingerprint.

Summary:
  The codec hashes packet headers, trajectories, causal nodes, retained marker
  poses, ghost transforms/material values, trajectory diagnostics, and exact
  tracer submission hashes/counts in one stable field order.

Invariants:
  - Float fields are hashed by exact IEEE-754 bits.
  - Every presentation-active record and published point participates.
  - A completed prediction excludes its inactive worker trajectory bank; exact
    renderer spans remain ordered and byte-exact.
  - Any new packet field must be added here and to the typed comparator tests.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h
  - SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayVisualPacketFingerprint.h"
#include "../../Core/ByteView.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::Runtime
{
using namespace ReplayVisualPacketOperations;
namespace
{
void HashByte( uint64_t& hash, uint8_t value )
{
    hash ^= static_cast<uint64_t>( value );
    hash *= REPLAY_VISUAL_BUFFER_FNV_PRIME;
}

template <typename T> void HashScalar( uint64_t& hash, T value )
{

    for ( uint8_t byte : SkullbonezCore::Core::ObjectBytes( value ) )
    {
        HashByte( hash, byte );
    }
}

void HashFloat( uint64_t& hash, float value )
{
    uint32_t bits = 0;
    std::memcpy( &bits, &value, sizeof( bits ) );
    HashScalar( hash, bits );
}

void HashVector( uint64_t& hash, const Math::Vector::Vector3& value )
{
    HashFloat( hash, value.x );
    HashFloat( hash, value.y );
    HashFloat( hash, value.z );
}

void HashQuaternion( uint64_t& hash, const Math::Orientation::Quaternion& value )
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
    value.GetComponents( x, y, z, w );
    HashFloat( hash, x );
    HashFloat( hash, y );
    HashFloat( hash, z );
    HashFloat( hash, w );
}

bool TrajectoryRecordParticipatesInCompletedPresentation( const ReplayVisualPacket& packet,
                                                          const ReplayTrajectoryRecord& record )
{

    // During a growing prediction the renderer may switch between committed
    // and worker banks according to prefix readiness, which is intentionally
    // left conservative here. Once completion is published, ReplayPredictionDrawing
    // draws only the committed root (branch 0) and committed child range.

    if ( packet.header.predictionBuilding || !packet.header.predictionComplete )
    {
        return true;
    }

    if ( record.key.lane == ReplayTrajectoryLane::FutureRoot )
    {
        return record.key.branchOrdinal == 0u;
    }

    if ( record.key.lane == ReplayTrajectoryLane::FutureChildIncoming ||
         record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing )
    {
        const uint32_t ordinal = record.key.branchOrdinal;
        return ordinal < packet.header.futureNodeCount && ordinal < REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
    }

    return true;
}
} // namespace

namespace ReplayVisualPacketFingerprintOperations
{
ReplayVisualPacketBufferFacts BuildReplayVisualPacketBufferFacts( const ReplayVisualPacket& packet ) noexcept
{
    ReplayVisualPacketBufferFacts facts;
    const bool hasRetainedPrediction = !packet.retainedPredictionRibbonVertices.empty() ||
                                       !packet.retainedPredictionPriorityRibbonVertices.empty();

    facts.hasGeometry = packet.HasGeometry();
    facts.combinedLineHash = HashReplayVisualFloatBuffers( packet.retainedPredictionOrdinaryLines, packet.ordinaryLines,
                                                           packet.retainedPredictionPriorityLines, packet.priorityLines );

    facts.ordinaryLineHash = HashReplayVisualFloatBuffers( packet.retainedPredictionOrdinaryLines, packet.ordinaryLines );

    facts.priorityLineHash = HashReplayVisualFloatBuffers( packet.retainedPredictionPriorityLines, packet.priorityLines );

    facts.ordinaryRibbonHash = hasRetainedPrediction
                                   ? HashReplayVisualFloatBuffers( packet.retainedPredictionOrdinaryRibbonSegments,
                                                                   packet.ordinaryRibbonSegments )
                                   : HashReplayVisualFloatBuffer( packet.ordinaryRibbonSegments );

    facts.priorityRibbonHash = hasRetainedPrediction
                                   ? HashReplayVisualFloatBuffers( packet.retainedPredictionPriorityRibbonSegments,
                                                                   packet.priorityRibbonSegments )
                                   : HashReplayVisualFloatBuffer( packet.priorityRibbonSegments );

    facts.expandedVertexHash = hasRetainedPrediction
                                   ? HashReplayVisualFloatBuffers( packet.retainedPredictionRibbonVertices,
                                                                   packet.expandedRibbonVertices,
                                                                   packet.retainedPredictionPriorityRibbonVertices,
                                                                   packet.priorityExpandedRibbonVertices )
                                   : HashReplayVisualFloatBuffers( packet.expandedRibbonVertices,
                                                                   packet.priorityExpandedRibbonVertices );

    facts.combinedLineBytes = packet.retainedPredictionOrdinaryLines.size_bytes() + packet.ordinaryLines.size_bytes() +
                              packet.retainedPredictionPriorityLines.size_bytes() + packet.priorityLines.size_bytes();

    facts.ordinaryLineBytes = packet.retainedPredictionOrdinaryLines.size_bytes() + packet.ordinaryLines.size_bytes();
    facts.priorityLineBytes = packet.retainedPredictionPriorityLines.size_bytes() + packet.priorityLines.size_bytes();
    facts.ordinaryRibbonBytes = packet.retainedPredictionOrdinaryRibbonSegments.size_bytes() +
                                packet.ordinaryRibbonSegments.size_bytes();

    facts.priorityRibbonBytes = packet.retainedPredictionPriorityRibbonSegments.size_bytes() +
                                packet.priorityRibbonSegments.size_bytes();

    facts.expandedVertexBytes = packet.retainedPredictionRibbonVertices.size_bytes() +
                                packet.expandedRibbonVertices.size_bytes() +
                                packet.retainedPredictionPriorityRibbonVertices.size_bytes() +
                                packet.priorityExpandedRibbonVertices.size_bytes();

    // The ordinary expanded lane is the leading region before priority marker
    // vertices. Its boundary is telemetry, so clamp here; the mandatory seam
    // check below rejects an invalid boundary before capture can succeed.
    facts.ordinaryExpandedVertexBytes = (std::min)( packet.submission.ordinaryVertexBytes, facts.expandedVertexBytes );
    facts.ordinaryExpandedVertexHash = hasRetainedPrediction
                                           ? HashReplayVisualFloatBuffers( packet.retainedPredictionRibbonVertices,
                                                                           packet.expandedRibbonVertices )
                                           : HashReplayVisualFloatBuffer( packet.expandedRibbonVertices );

    return facts;
}

const char* FindReplayVisualPacketSubmissionSpanMismatch( const ReplayVisualPacket& packet ) noexcept
{
    constexpr uint64_t LINE_FLOATS_PER_VERTEX = 6u;
    constexpr uint64_t RIBBON_FLOATS_PER_SEGMENT = 13u;
    constexpr uint64_t RIBBON_FLOATS_PER_VERTEX = 19u;
    constexpr uint64_t RIBBON_VERTICES_PER_SEGMENT = 6u;
    const ReplayVisualPacketBufferFacts facts = BuildReplayVisualPacketBufferFacts( packet );
    const auto& submission = packet.submission;
    const auto emptyCompatibleHash = []( uint64_t submitted, uint64_t direct, uint64_t bytes )
    { return submitted == direct || ( bytes == 0u && submitted == 0u ); };

    if ( submission.hasGeometry != facts.hasGeometry )
    {
        return "submission.hasGeometry";
    }

    if ( submission.ordinaryLineHash != facts.ordinaryLineHash )
    {
        return "submission.ordinaryLineHash";
    }

    if ( submission.priorityLineHash != facts.priorityLineHash )
    {
        return "submission.priorityLineHash";
    }

    if ( submission.ordinaryRibbonHash != facts.ordinaryRibbonHash )
    {
        return "submission.ordinaryRibbonHash";
    }

    if ( submission.priorityRibbonHash != facts.priorityRibbonHash )
    {
        return "submission.priorityRibbonHash";
    }

    if ( !emptyCompatibleHash( submission.vertexHash, facts.expandedVertexHash, facts.expandedVertexBytes ) )
    {
        return "submission.vertexHash";
    }

    if ( !emptyCompatibleHash( submission.ordinaryVertexHash, facts.ordinaryExpandedVertexHash,
                               facts.ordinaryExpandedVertexBytes ) )
    {
        return "submission.ordinaryVertexHash";
    }

    if ( submission.ordinaryLineBytes != facts.ordinaryLineBytes )
    {
        return "submission.ordinaryLineBytes";
    }

    if ( submission.priorityLineBytes != facts.priorityLineBytes )
    {
        return "submission.priorityLineBytes";
    }

    if ( submission.ordinaryRibbonBytes != facts.ordinaryRibbonBytes )
    {
        return "submission.ordinaryRibbonBytes";
    }

    if ( submission.priorityRibbonBytes != facts.priorityRibbonBytes )
    {
        return "submission.priorityRibbonBytes";
    }

    if ( submission.vertexBytes != facts.expandedVertexBytes )
    {
        return "submission.vertexBytes";
    }

    if ( submission.ordinaryVertexBytes > facts.expandedVertexBytes ||
         submission.ordinaryVertexBytes % sizeof( float ) != 0u )
    {
        return "submission.ordinaryVertexBytes";
    }

    if ( static_cast<uint64_t>( submission.ordinaryLineVertexCount ) * LINE_FLOATS_PER_VERTEX * sizeof( float ) !=
         facts.ordinaryLineBytes )
    {
        return "submission.ordinaryLineVertexCount";
    }

    if ( static_cast<uint64_t>( submission.priorityLineVertexCount ) * LINE_FLOATS_PER_VERTEX * sizeof( float ) !=
         facts.priorityLineBytes )
    {
        return "submission.priorityLineVertexCount";
    }

    if ( static_cast<uint64_t>( submission.ordinaryRibbonSegmentCount ) * RIBBON_FLOATS_PER_SEGMENT * sizeof( float ) !=
         facts.ordinaryRibbonBytes )
    {
        return "submission.ordinaryRibbonSegmentCount";
    }

    if ( static_cast<uint64_t>( submission.priorityRibbonSegmentCount ) * RIBBON_FLOATS_PER_SEGMENT * sizeof( float ) !=
         facts.priorityRibbonBytes )
    {
        return "submission.priorityRibbonSegmentCount";
    }

    if ( static_cast<uint64_t>( submission.vertexCount ) * RIBBON_FLOATS_PER_VERTEX * sizeof( float ) !=
         facts.expandedVertexBytes )
    {
        return "submission.vertexCount";
    }

    if ( static_cast<uint64_t>( submission.ordinaryVertexCount ) * RIBBON_FLOATS_PER_VERTEX * sizeof( float ) !=
         submission.ordinaryVertexBytes )
    {
        return "submission.ordinaryVertexCount";
    }

    if ( static_cast<uint64_t>( submission.segmentCount ) * RIBBON_VERTICES_PER_SEGMENT != submission.vertexCount )
    {
        return "submission.segmentCount";
    }

    return nullptr;
}

ReplayVisualPacketFingerprint
BuildReplayVisualPacketFingerprint( const ReplayVisualPacket& packet,
                                    std::vector<ReplayVisualTrajectoryDigestState>& trajectoryDigests,
                                    ReplayVisualTrajectoryDigestPolicy digestPolicy )
{
    ReplayVisualPacketFingerprint fingerprint;
    uint64_t& hash = fingerprint.semanticHash;
    const auto hashBool = [&]( bool value ) { HashScalar( hash, static_cast<uint8_t>( value ? 1u : 0u ) ); };

    HashScalar( hash, packet.header.schemaVersion );
    HashScalar( hash, packet.header.sourceFrame );
    HashScalar( hash, packet.header.revealFrame );
    HashScalar( hash, packet.header.targetId.value );
    HashScalar( hash, packet.header.branchId );
    HashScalar( hash, packet.header.eventCursor );
    HashScalar( hash, packet.header.publishedFrameCount );
    HashScalar( hash, packet.header.futureNodeCount );
    HashScalar( hash, packet.header.ghostRequestCount );
    HashVector( hash, packet.header.cameraEye );
    HashVector( hash, packet.header.cameraUp );
    hashBool( packet.header.predictionEnabled );
    hashBool( packet.header.predictionBuilding );
    hashBool( packet.header.predictionComplete );
    fingerprint.headerStateHash = hash;

    uint64_t presentedTrajectoryRecordCount = 0;

    for ( const ReplayTrajectoryRecord& record : packet.trajectoryRecords )
    {

        if ( TrajectoryRecordParticipatesInCompletedPresentation( packet, record ) )
        {
            ++presentedTrajectoryRecordCount;
        }
    }

    HashScalar( hash, presentedTrajectoryRecordCount );
    uint64_t internalTrajectoryStateHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    HashScalar( internalTrajectoryStateHash, static_cast<uint64_t>( packet.trajectoryRecords.size() ) );

    if ( trajectoryDigests.size() < packet.trajectoryRecords.size() )
    {
        trajectoryDigests.resize( packet.trajectoryRecords.size() );
    }

    for ( std::size_t recordIndex = 0; recordIndex < packet.trajectoryRecords.size(); ++recordIndex )
    {
        const ReplayTrajectoryRecord& record = packet.trajectoryRecords[recordIndex];
        uint64_t recordPresentationHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
        HashScalar( recordPresentationHash, record.key.bodyId.value );
        HashScalar( recordPresentationHash, static_cast<uint8_t>( record.key.lane ) );
        HashScalar( recordPresentationHash, record.key.branchOrdinal );
        HashScalar( recordPresentationHash, static_cast<uint64_t>( record.publishedPointCount ) );
        HashScalar( recordPresentationHash, record.styleId );
        HashScalar( recordPresentationHash, record.parentId.value );
        HashScalar( recordPresentationHash, record.depth );
        HashScalar( recordPresentationHash, record.firstFrame );
        HashScalar( recordPresentationHash, static_cast<uint8_t>( record.contactDerived ? 1u : 0u ) );
        const std::size_t pointCount = (std::min)( record.publishedPointCount, record.points.size() );
        HashScalar( recordPresentationHash, static_cast<uint64_t>( pointCount ) );
        ReplayVisualTrajectoryDigestState& digest = trajectoryDigests[recordIndex];
        const bool reuseDigest = digestPolicy == ReplayVisualTrajectoryDigestPolicy::ReuseImmutableRecords &&
                                 digest.bodyId == record.key.bodyId.value &&
                                 digest.lane == static_cast<uint8_t>( record.key.lane ) &&
                                 digest.branchOrdinal == record.key.branchOrdinal && digest.version == record.version &&
                                 digest.publishedPointCount == pointCount;

        if ( !reuseDigest )
        {
            digest = {};

            digest.bodyId = record.key.bodyId.value;
            digest.lane = static_cast<uint8_t>( record.key.lane );
            digest.branchOrdinal = record.key.branchOrdinal;
            digest.version = record.version;

            for ( std::size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex )
            {
                const ReplayTrajectoryPoint& point = record.points[pointIndex];
                HashScalar( digest.prefixHash, point.frameIndex );
                HashVector( digest.prefixHash, point.position );
            }

            digest.publishedPointCount = pointCount;
        }

        HashScalar( recordPresentationHash, digest.prefixHash );

        // Semantic diagnostics retain the complete ordered store, including
        // inactive worker-bank records and their replacement versions. The
        // visual lane admits only records the completed overlay can select.
        HashScalar( internalTrajectoryStateHash, record.version );
        HashScalar( internalTrajectoryStateHash, recordPresentationHash );

        if ( TrajectoryRecordParticipatesInCompletedPresentation( packet, record ) )
        {
            HashScalar( hash, recordPresentationHash );
        }
    }

    fingerprint.trajectoryStateHash = hash;

    HashScalar( hash, static_cast<uint64_t>( packet.futureNodes.size() ) );

    for ( const RunReplayPathTraceNode& node : packet.futureNodes )
    {
        HashScalar( hash, node.id.value );
        HashScalar( hash, node.parentId.value );
        HashScalar( hash, node.modelRow.value );
        HashScalar( hash, node.parentModelRow.value );
        HashScalar( hash, node.firstFrame );
        HashVector( hash, node.contactPoint );
        HashVector( hash, node.contactNormal );
        HashScalar( hash, node.depth );
        hashBool( node.contactDerived );
    }

    fingerprint.topologyStateHash = hash;

    HashScalar( hash, static_cast<uint64_t>( packet.retainedMarkers.size() ) );

    for ( const ReplayPredictionRetainedMarker& marker : packet.retainedMarkers )
    {
        HashScalar( hash, marker.id.value );
        HashScalar( hash, marker.modelRow.value );
        hashBool( marker.hasEntryPose );
        hashBool( marker.hasRestPose );
        hashBool( marker.hasHorizonPose );
        HashVector( hash, marker.entryPosition );
        HashQuaternion( hash, marker.entryOrientation );
        HashVector( hash, marker.restPosition );
        HashQuaternion( hash, marker.restOrientation );
        HashVector( hash, marker.horizonPosition );
        HashQuaternion( hash, marker.horizonOrientation );
    }

    fingerprint.markerStateHash = hash;

    HashScalar( hash, static_cast<uint64_t>( packet.ghostRequests.size() ) );

    for ( const ReplayPredictionGhostDrawRequest& ghost : packet.ghostRequests )
    {
        HashScalar( hash, ghost.modelRow.value );
        HashVector( hash, ghost.position );
        HashQuaternion( hash, ghost.orientation );
        HashFloat( hash, ghost.alpha );
        HashFloat( hash, ghost.tintR );
        HashFloat( hash, ghost.tintG );
        HashFloat( hash, ghost.tintB );
        HashFloat( hash, ghost.tintStrength );
    }

    fingerprint.ghostStateHash = hash;

    // Cross-process reconstruction ends here: absolute cache-generation ids,
    // reserve counters, and trajectory diagnostics are validation telemetry,
    // not values consumed by rendering. The complete ordered topology above
    // remains in the visual hash, so a real causal/visual change still fails.
    fingerprint.visualStateHash = hash;
    HashScalar( hash, packet.header.topologyVersion );
    HashScalar( hash, packet.header.replayReserveGrowthEvents );
    HashScalar( hash, internalTrajectoryStateHash );
    const auto& diagnostics = packet.trajectoryDiagnostics;
    HashScalar( hash, diagnostics.storeBytes );
    HashScalar( hash, diagnostics.recordCount );
    HashScalar( hash, diagnostics.pointCount );
    HashScalar( hash, diagnostics.publishedPointCount );
    HashScalar( hash, diagnostics.versionChurn );
    HashScalar( hash, diagnostics.maxRecordVersion );

    for ( uint64_t value : diagnostics.emittedSegments )
    {
        HashScalar( hash, value );
    }

    for ( uint64_t value : diagnostics.droppedSegments )
    {
        HashScalar( hash, value );
    }

    for ( uint64_t value : diagnostics.budgetExpiries )
    {
        HashScalar( hash, value );
    }

    for ( uint64_t value : diagnostics.rebuildCauses )
    {
        HashScalar( hash, value );
    }

    // Exact presentation deliberately branches from visualStateHash. Build
    // budget/retry telemetry can differ in an offline projection even when
    // every typed value and submitted byte is identical.
    fingerprint.exactHash = fingerprint.visualStateHash;
    uint64_t& exactHash = fingerprint.exactHash;
    const ReplayVisualPacketBufferFacts facts = BuildReplayVisualPacketBufferFacts( packet );
    HashScalar( exactHash, static_cast<uint8_t>( facts.hasGeometry ? 1u : 0u ) );
#define SB_HASH_REPLAY_BUFFER_FACT( member ) HashScalar( exactHash, facts.member )
    SB_HASH_REPLAY_BUFFER_FACT( combinedLineHash );
    SB_HASH_REPLAY_BUFFER_FACT( combinedLineBytes );
    SB_HASH_REPLAY_BUFFER_FACT( ordinaryLineHash );
    SB_HASH_REPLAY_BUFFER_FACT( ordinaryLineBytes );
    SB_HASH_REPLAY_BUFFER_FACT( priorityLineHash );
    SB_HASH_REPLAY_BUFFER_FACT( priorityLineBytes );
    SB_HASH_REPLAY_BUFFER_FACT( ordinaryRibbonHash );
    SB_HASH_REPLAY_BUFFER_FACT( ordinaryRibbonBytes );
    SB_HASH_REPLAY_BUFFER_FACT( priorityRibbonHash );
    SB_HASH_REPLAY_BUFFER_FACT( priorityRibbonBytes );
    SB_HASH_REPLAY_BUFFER_FACT( expandedVertexHash );
    SB_HASH_REPLAY_BUFFER_FACT( expandedVertexBytes );
    SB_HASH_REPLAY_BUFFER_FACT( ordinaryExpandedVertexHash );
    SB_HASH_REPLAY_BUFFER_FACT( ordinaryExpandedVertexBytes );
#undef SB_HASH_REPLAY_BUFFER_FACT

    // Canonical marker hashes and structural counts are supplementary. The
    // ordered hashes above are always derived from renderer-bound packet spans.
    const auto& submission = packet.submission;
    HashScalar( exactHash, submission.priorityLineCanonicalHash );
    HashScalar( exactHash, submission.priorityRibbonCanonicalHash );
    HashScalar( exactHash, submission.ordinaryLineVertexCount );
    HashScalar( exactHash, submission.priorityLineVertexCount );
    HashScalar( exactHash, submission.ordinaryRibbonSegmentCount );
    HashScalar( exactHash, submission.priorityRibbonSegmentCount );
    HashScalar( exactHash, submission.vertexCount );
    HashScalar( exactHash, submission.ordinaryVertexCount );
    HashScalar( exactHash, submission.segmentCount );
    return fingerprint;
}

bool ReplayVisualPacketMatchesArchiveSample( const ReplayVisualPacket& packet, const ReplayVisualArchiveSample& expected,
                                             char* difference, std::size_t differenceSize )
{
    const auto fail = [&]( const char* field )
    {
        std::snprintf( difference, differenceSize, "visual packet mismatch at reveal %llu: %s",
                       static_cast<unsigned long long>( expected.revealFrame ), field );

        return false;
    };

    const auto floatBitsEqual = []( float lhs, float rhs )
    { return std::bit_cast<uint32_t>( lhs ) == std::bit_cast<uint32_t>( rhs ); };

    if ( packet.header.sourceFrame != expected.sourceFrame )
    {
        return fail( "header.sourceFrame" );
    }

    if ( packet.header.revealFrame != expected.revealFrame )
    {
        return fail( "header.revealFrame" );
    }

    if ( packet.header.schemaVersion != expected.schemaVersion )
    {
        return fail( "header.schemaVersion" );
    }

    if ( packet.header.targetId.value != expected.targetId )
    {
        return fail( "header.targetId" );
    }

    if ( packet.header.branchId != expected.branchId )
    {
        return fail( "header.branchId" );
    }

    if ( packet.header.eventCursor != expected.eventCursor )
    {
        return fail( "header.eventCursor" );
    }

    if ( packet.header.topologyVersion != expected.topologyVersion )
    {
        return fail( "header.topologyVersion" );
    }

    if ( packet.header.publishedFrameCount != expected.publishedFrameCount )
    {
        return fail( "header.publishedFrameCount" );
    }

    if ( packet.header.futureNodeCount != expected.futureNodeCount )
    {
        return fail( "header.futureNodeCount" );
    }

    if ( packet.header.ghostRequestCount != expected.ghostRequestCount )
    {
        return fail( "header.ghostRequestCount" );
    }

    if ( packet.header.replayReserveGrowthEvents != expected.replayReserveGrowthEvents )
    {
        return fail( "header.replayReserveGrowthEvents" );
    }

    if ( packet.header.predictionEnabled != ( expected.predictionEnabled != 0u ) )
    {
        return fail( "header.predictionEnabled" );
    }

    if ( packet.header.predictionBuilding != ( expected.predictionBuilding != 0u ) )
    {
        return fail( "header.predictionBuilding" );
    }

    if ( packet.header.predictionComplete != ( expected.predictionComplete != 0u ) )
    {
        return fail( "header.predictionComplete" );
    }

    if ( !floatBitsEqual( packet.header.cameraEye.x, expected.cameraEye.x ) ||
         !floatBitsEqual( packet.header.cameraEye.y, expected.cameraEye.y ) ||
         !floatBitsEqual( packet.header.cameraEye.z, expected.cameraEye.z ) )
    {
        return fail( "header.cameraEye" );
    }

    if ( !floatBitsEqual( packet.header.cameraUp.x, expected.cameraUp.x ) ||
         !floatBitsEqual( packet.header.cameraUp.y, expected.cameraUp.y ) ||
         !floatBitsEqual( packet.header.cameraUp.z, expected.cameraUp.z ) )
    {
        return fail( "header.cameraUp" );
    }

    if ( packet.trajectoryRecords.size() != expected.trajectoryRecordCount )
    {
        return fail( "trajectoryRecordCount" );
    }

    if ( packet.futureNodes.size() != expected.futureNodeCount )
    {
        return fail( "futureNodeCount" );
    }

    if ( packet.retainedMarkers.size() != expected.retainedMarkerCount )
    {
        return fail( "retainedMarkerCount" );
    }

    if ( packet.ghostRequests.size() != expected.ghostRequestCount )
    {
        return fail( "ghostRequestCount" );
    }

    uint64_t droppedSegmentCount = 0;

    for ( const uint64_t dropped : packet.trajectoryDiagnostics.droppedSegments )
    {
        droppedSegmentCount += dropped;
    }

    if ( droppedSegmentCount != expected.droppedSegmentCount )
    {
        return fail( "trajectoryDiagnostics.droppedSegments" );
    }

    if ( const char* mismatch = FindReplayVisualPacketSubmissionSpanMismatch( packet ) )
    {
        return fail( mismatch );
    }

    const ReplayVisualPacketBufferFacts facts = BuildReplayVisualPacketBufferFacts( packet );

    if ( facts.combinedLineHash != expected.combinedLineHash )
    {
        return fail( "combinedLines.hash" );
    }

    if ( facts.combinedLineBytes != expected.combinedLineBytes )
    {
        return fail( "combinedLines.bytes" );
    }

    if ( facts.combinedLineBytes / ( sizeof( float ) * 6u ) != expected.combinedLineVertexCount )
    {
        return fail( "combinedLines.vertices" );
    }

    const auto& submission = packet.submission;
#define SB_COMPARE_REPLAY_VISUAL( actual, archived, field )                                                                 \
    if ( ( actual ) != ( archived ) )                                                                                       \
    return fail( field )
    SB_COMPARE_REPLAY_VISUAL( facts.ordinaryLineHash, expected.ordinaryLineHash, "ordinaryLines.hash" );
    SB_COMPARE_REPLAY_VISUAL( facts.priorityLineHash, expected.priorityLineHash, "priorityLines.hash" );
    SB_COMPARE_REPLAY_VISUAL( submission.priorityLineCanonicalHash, expected.priorityLineCanonicalHash,
                              "priorityLines.canonicalHash" );

    SB_COMPARE_REPLAY_VISUAL( facts.ordinaryRibbonHash, expected.ordinaryRibbonHash, "ordinaryRibbonSegments.hash" );
    SB_COMPARE_REPLAY_VISUAL( facts.priorityRibbonHash, expected.priorityRibbonHash, "priorityRibbonSegments.hash" );
    SB_COMPARE_REPLAY_VISUAL( submission.priorityRibbonCanonicalHash, expected.priorityRibbonCanonicalHash,
                              "priorityRibbonSegments.canonicalHash" );

    SB_COMPARE_REPLAY_VISUAL( facts.expandedVertexHash, expected.expandedVertexHash, "expandedRibbonVertices.hash" );
    SB_COMPARE_REPLAY_VISUAL( facts.ordinaryExpandedVertexHash, expected.ordinaryExpandedVertexHash,
                              "ordinaryExpandedVertices.hash" );

    SB_COMPARE_REPLAY_VISUAL( facts.ordinaryLineBytes, expected.ordinaryLineBytes, "ordinaryLines.bytes" );
    SB_COMPARE_REPLAY_VISUAL( facts.priorityLineBytes, expected.priorityLineBytes, "priorityLines.bytes" );
    SB_COMPARE_REPLAY_VISUAL( facts.ordinaryRibbonBytes, expected.ordinaryRibbonBytes, "ordinaryRibbonSegments.bytes" );
    SB_COMPARE_REPLAY_VISUAL( facts.priorityRibbonBytes, expected.priorityRibbonBytes, "priorityRibbonSegments.bytes" );
    SB_COMPARE_REPLAY_VISUAL( facts.expandedVertexBytes, expected.expandedVertexBytes, "expandedRibbonVertices.bytes" );
    SB_COMPARE_REPLAY_VISUAL( facts.ordinaryExpandedVertexBytes, expected.ordinaryExpandedVertexBytes,
                              "ordinaryExpandedVertices.bytes" );

    SB_COMPARE_REPLAY_VISUAL( facts.hasGeometry, expected.hasGeometry != 0u, "packet.hasGeometry" );
    SB_COMPARE_REPLAY_VISUAL( submission.ordinaryLineVertexCount, expected.ordinaryLineVertexCount,
                              "ordinaryLines.vertices" );

    SB_COMPARE_REPLAY_VISUAL( submission.priorityLineVertexCount, expected.priorityLineVertexCount,
                              "priorityLines.vertices" );

    SB_COMPARE_REPLAY_VISUAL( submission.ordinaryRibbonSegmentCount, expected.ordinaryRibbonSegmentCount,
                              "ordinaryRibbonSegments.count" );

    SB_COMPARE_REPLAY_VISUAL( submission.priorityRibbonSegmentCount, expected.priorityRibbonSegmentCount,
                              "priorityRibbonSegments.count" );

    SB_COMPARE_REPLAY_VISUAL( submission.vertexCount, expected.expandedVertexCount, "expandedRibbonVertices.count" );
    SB_COMPARE_REPLAY_VISUAL( submission.ordinaryVertexCount, expected.ordinaryExpandedVertexCount,
                              "ordinaryExpandedVertices.count" );

    SB_COMPARE_REPLAY_VISUAL( submission.segmentCount, expected.segmentCount, "segments.count" );
#undef SB_COMPARE_REPLAY_VISUAL
    return true;
}
} // namespace ReplayVisualPacketFingerprintOperations
} // namespace SkullbonezCore::Runtime
