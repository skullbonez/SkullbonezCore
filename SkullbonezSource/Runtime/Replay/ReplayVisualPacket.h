/*
File: SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h
Purpose:
  Defines the frame-local replay presentation packet consumed by rendering and validation.

Summary:
  ReplayPresentation publishes one immutable packet after preparing its
  fixed-capacity line and ribbon buffers. Rendering and probes read
  that same value, so validation cannot accidentally describe a parallel visual
  builder that production never submits.

Glossary:
  Visual packet: Borrowed, read-only spans plus replay-owned semantic metadata
    for one presented ReplayFrameIndex.
  Submission stream: Ordered floats passed toward a render command after replay
    presentation has applied its capacity and priority rules.
  Future node: Causal topology row naming the predicted body, parent, activation
    frame, contact evidence, and depth that make a child path visible.
  Retained marker: Replay-owned entry, rest, and horizon poses that remain
    visible after their activation tick.
  Ghost request: Typed predicted pose and material treatment consumed by the
    ordinary object-shape renderer.
  First difference: Typed location of the earliest unequal packet field or
    submission float; recordIndex names its row and floatIndex names the field
    or component within that row.

Invariants:
  - Buffer spans borrow RunEditorTracer storage for the current render frame only.
  - ReplayPresentation owns packet publication and semantic metadata; the tracer owns
    storage capacity but cannot invent replay identity or reveal state.
  - Production rendering and validation consume the same published packet.
  - Float equality is bit-exact; no epsilon or order normalization is permitted.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp
*/
#pragma once

#include "ReplayRecorder.h"
#include "TrajectoryStore.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Maths/Quaternion.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace SkullbonezCore::Runtime
{

inline constexpr uint32_t REPLAY_VISUAL_PACKET_SCHEMA_VERSION = 1u;
inline constexpr uint64_t REPLAY_VISUAL_BUFFER_FNV_OFFSET = 1469598103934665603ull;
inline constexpr uint64_t REPLAY_VISUAL_BUFFER_FNV_PRIME = 1099511628211ull;
// Shared with prediction trajectory branch ordinals. Completed presentation
// reads the committed [0, futureNodeCount) child bank; the worker-owned bank
// begins at this capacity and is never renderer-visible after completion.
inline constexpr uint16_t REPLAY_VISUAL_FUTURE_NODE_CAPACITY = 240u;

namespace ReplayVisualPacketOperations
{
inline uint64_t HashReplayVisualFloatBuffer( std::span<const float> values ) noexcept
{
    uint64_t hash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    const auto appendBytes = [&hash]( const void* data, std::size_t byteCount )
    {
        const uint8_t* bytes = static_cast<const uint8_t*>( data );
        for ( std::size_t index = 0; index < byteCount; ++index )
        {
            hash ^= static_cast<uint64_t>( bytes[index] );
            hash *= REPLAY_VISUAL_BUFFER_FNV_PRIME;
        }
    };
    const uint64_t floatCount = static_cast<uint64_t>( values.size() );
    appendBytes( &floatCount, sizeof( floatCount ) );
    if ( !values.empty() )
    {
        appendBytes( values.data(), values.size_bytes() );
    }
    return hash;
}
} // namespace ReplayVisualPacketOperations

// These records live here because both ReplayPresentation's mutable caches and
// the immutable render packet use the same typed vocabulary. Moving
// them into a packet-specific copy would let validation drift from production.
struct RunReplayPathTraceNode
{
    ReplayBodyId id;
    ReplayBodyId parentId;
    Physics::ModelRowHint modelRow;
    Physics::ModelRowHint parentModelRow;
    ReplayFrameIndex firstFrame = 0;
    Math::Vector::Vector3 contactPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 contactNormal = Math::Vector::ZERO_VECTOR;
    int depth = 0;
    bool contactDerived = true;
};

struct ReplayPredictionGhostDrawRequest
{
    Physics::ModelRowHint modelRow;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    float alpha = 1.0f;
    float tintR = 1.0f;
    float tintG = 1.0f;
    float tintB = 1.0f;
    float tintStrength = 0.0f;
};

struct ReplayPredictionRetainedMarker
{
    ReplayBodyId id;
    Physics::ModelRowHint modelRow;
    bool hasEntryPose = false;
    bool hasRestPose = false;
    bool hasHorizonPose = false;
    Math::Vector::Vector3 entryPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion entryOrientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 restPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion restOrientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 horizonPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion horizonOrientation = Math::Orientation::IDENTITY_QUATERNION;
};

enum class ReplayVisualPacketBuffer : uint8_t
{
    None,
    CombinedLines,
    OrdinaryLines,
    PriorityLines,
    OrdinaryRibbonSegments,
    PriorityRibbonSegments,
    ExpandedRibbonVertices
};

enum class ReplayVisualPacketField : uint8_t
{
    None,
    SchemaVersion,
    SourceFrame,
    RevealFrame,
    TargetId,
    BranchId,
    EventCursor,
    TopologyVersion,
    PublishedFrameCount,
    FutureNodeCount,
    GhostRequestCount,
    PredictionEnabled,
    PredictionBuilding,
    PredictionComplete,
    CameraEye,
    CameraUp,
    ReplayReserveGrowthEvents,
    TrajectoryRecordCount,
    TrajectoryRecord,
    TrajectoryPointCount,
    TrajectoryPoint,
    FutureNode,
    RetainedMarkerCount,
    RetainedMarker,
    GhostRequest,
    TrajectoryDiagnostic,
    SubmissionDiagnostic,
    Buffer
};

struct ReplayVisualPacketHeader
{
    uint32_t schemaVersion = REPLAY_VISUAL_PACKET_SCHEMA_VERSION;
    ReplayFrameIndex sourceFrame = 0;
    ReplayFrameIndex revealFrame = 0;
    ReplayBodyId targetId;
    uint32_t branchId = 0;
    uint32_t eventCursor = 0;
    uint32_t topologyVersion = 0;
    uint32_t publishedFrameCount = 0;
    uint32_t futureNodeCount = 0;
    uint32_t ghostRequestCount = 0;
    uint64_t replayReserveGrowthEvents = 0;
    Math::Vector::Vector3 cameraEye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 cameraUp = Math::Vector::ZERO_VECTOR;
    bool predictionEnabled = false;
    bool predictionBuilding = false;
    bool predictionComplete = false;
};

struct ReplayVisualPacketDifference
{
    ReplayVisualPacketField field = ReplayVisualPacketField::None;
    ReplayVisualPacketBuffer buffer = ReplayVisualPacketBuffer::None;
    std::size_t floatIndex = 0;
    std::size_t recordIndex = 0;
    uint64_t expectedBits = 0;
    uint64_t actualBits = 0;
    bool countMismatch = false;
};

struct ReplayVisualPacket
{
    ReplayVisualPacketHeader header;
    std::span<const float> combinedLines;
    std::span<const float> ordinaryLines;
    std::span<const float> priorityLines;
    std::span<const float> ordinaryRibbonSegments;
    std::span<const float> priorityRibbonSegments;
    std::span<const float> expandedRibbonVertices;
    std::span<const ReplayTrajectoryRecord> trajectoryRecords;
    std::span<const RunReplayPathTraceNode> futureNodes;
    std::span<const ReplayPredictionRetainedMarker> retainedMarkers;
    std::span<const ReplayPredictionGhostDrawRequest> ghostRequests;
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats trajectoryDiagnostics;
    SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats submission;

    bool HasGeometry() const noexcept
    {
        return !combinedLines.empty() || !expandedRibbonVertices.empty();
    }
};

// Concept: the artifact retains one typed identity/submission row per presented
// packet beside the complete prediction-state archive. Exact hashes plus per-lane
// counts and byte lengths make this a bounded oracle without serializing renderer
// resources, spans, pointers, or vector ownership.
struct ReplayVisualArchiveSample
{
    ReplayFrameIndex sourceFrame = 0;
    ReplayFrameIndex revealFrame = 0;
    uint64_t semanticHash = 0;
    uint64_t visualStateHash = 0;
    uint64_t exactPacketHash = 0;
    uint32_t schemaVersion = 0;
    uint32_t targetId = 0;
    uint32_t branchId = 0;
    uint32_t eventCursor = 0;
    uint32_t topologyVersion = 0;
    uint32_t publishedFrameCount = 0;
    uint32_t predictionEnabled = 0;
    uint32_t predictionBuilding = 0;
    uint32_t predictionComplete = 0;
    Math::Vector::Vector3 cameraEye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 cameraUp = Math::Vector::ZERO_VECTOR;
    uint64_t combinedLineHash = 0;
    uint64_t ordinaryLineHash = 0;
    uint64_t priorityLineHash = 0;
    uint64_t priorityLineCanonicalHash = 0;
    uint64_t ordinaryRibbonHash = 0;
    uint64_t priorityRibbonHash = 0;
    uint64_t priorityRibbonCanonicalHash = 0;
    uint64_t expandedVertexHash = 0;
    uint64_t ordinaryExpandedVertexHash = 0;
    uint64_t droppedSegmentCount = 0;
    uint64_t replayReserveGrowthEvents = 0;
    uint64_t combinedLineBytes = 0;
    uint64_t ordinaryLineBytes = 0;
    uint64_t priorityLineBytes = 0;
    uint64_t ordinaryRibbonBytes = 0;
    uint64_t priorityRibbonBytes = 0;
    uint64_t expandedVertexBytes = 0;
    uint64_t ordinaryExpandedVertexBytes = 0;
    uint32_t hasGeometry = 0;
    uint32_t trajectoryRecordCount = 0;
    uint32_t futureNodeCount = 0;
    uint32_t retainedMarkerCount = 0;
    uint32_t ghostRequestCount = 0;
    uint32_t combinedLineVertexCount = 0;
    uint32_t ordinaryLineVertexCount = 0;
    uint32_t priorityLineVertexCount = 0;
    uint32_t ordinaryRibbonSegmentCount = 0;
    uint32_t priorityRibbonSegmentCount = 0;
    uint32_t expandedVertexCount = 0;
    uint32_t ordinaryExpandedVertexCount = 0;
    uint32_t segmentCount = 0;
};

namespace ReplayVisualPacketOperations
{
inline bool FindReplayVisualBufferDifference( std::span<const float> expected,
                                              std::span<const float> actual,
                                              ReplayVisualPacketBuffer buffer,
                                              ReplayVisualPacketDifference& outDifference ) noexcept
{
    const std::size_t commonCount = (std::min)( expected.size(), actual.size() );
    for ( std::size_t index = 0; index < commonCount; ++index )
    {
        const uint32_t expectedBits = std::bit_cast<uint32_t>( expected[index] );
        const uint32_t actualBits = std::bit_cast<uint32_t>( actual[index] );
        if ( expectedBits != actualBits )
        {
            outDifference = {};
            outDifference.field = ReplayVisualPacketField::Buffer;
            outDifference.buffer = buffer;
            outDifference.floatIndex = index;
            outDifference.expectedBits = expectedBits;
            outDifference.actualBits = actualBits;
            return true;
        }
    }
    if ( expected.size() != actual.size() )
    {
        outDifference = {};
        outDifference.field = ReplayVisualPacketField::Buffer;
        outDifference.buffer = buffer;
        outDifference.floatIndex = commonCount;
        outDifference.countMismatch = true;
        return true;
    }
    return false;
}

inline bool FindReplayVisualValueDifference( uint64_t expected,
                                             uint64_t actual,
                                             ReplayVisualPacketField field,
                                             std::size_t recordIndex,
                                             std::size_t elementIndex,
                                             ReplayVisualPacketDifference& outDifference ) noexcept
{
    if ( expected == actual )
    {
        return false;
    }
    outDifference = {};
    outDifference.field = field;
    outDifference.recordIndex = recordIndex;
    outDifference.floatIndex = elementIndex;
    outDifference.expectedBits = expected;
    outDifference.actualBits = actual;
    return true;
}

// Concept: semantic comparison walks replay identity and presentation records
// before raw render buffers. A failure therefore names the owner record that
// diverged instead of reporting only an opaque hash or byte offset.
inline bool FindReplayVisualFloatDifference( float expected,
                                             float actual,
                                             ReplayVisualPacketField field,
                                             std::size_t recordIndex,
                                             std::size_t componentIndex,
                                             ReplayVisualPacketDifference& outDifference ) noexcept
{
    return FindReplayVisualValueDifference( std::bit_cast<uint32_t>( expected ),
                                            std::bit_cast<uint32_t>( actual ),
                                            field,
                                            recordIndex,
                                            componentIndex,
                                            outDifference );
}

inline bool FindReplayVisualVectorDifference( const Math::Vector::Vector3& expected,
                                              const Math::Vector::Vector3& actual,
                                              ReplayVisualPacketField field,
                                              std::size_t recordIndex,
                                              std::size_t firstComponentIndex,
                                              ReplayVisualPacketDifference& outDifference ) noexcept
{
    return FindReplayVisualFloatDifference( expected.x,
                                            actual.x,
                                            field,
                                            recordIndex,
                                            firstComponentIndex,
                                            outDifference ) ||
           FindReplayVisualFloatDifference( expected.y,
                                            actual.y,
                                            field,
                                            recordIndex,
                                            firstComponentIndex + 1u,
                                            outDifference ) ||
           FindReplayVisualFloatDifference( expected.z,
                                            actual.z,
                                            field,
                                            recordIndex,
                                            firstComponentIndex + 2u,
                                            outDifference );
}

inline bool FindReplayVisualQuaternionDifference( const Math::Orientation::Quaternion& expected,
                                                  const Math::Orientation::Quaternion& actual,
                                                  ReplayVisualPacketField field,
                                                  std::size_t recordIndex,
                                                  std::size_t firstComponentIndex,
                                                  ReplayVisualPacketDifference& outDifference ) noexcept
{
    float expectedX = 0.0f;
    float expectedY = 0.0f;
    float expectedZ = 0.0f;
    float expectedW = 1.0f;
    float actualX = 0.0f;
    float actualY = 0.0f;
    float actualZ = 0.0f;
    float actualW = 1.0f;
    expected.GetComponents( expectedX, expectedY, expectedZ, expectedW );
    actual.GetComponents( actualX, actualY, actualZ, actualW );
    return FindReplayVisualFloatDifference( expectedX,
                                            actualX,
                                            field,
                                            recordIndex,
                                            firstComponentIndex,
                                            outDifference ) ||
           FindReplayVisualFloatDifference( expectedY,
                                            actualY,
                                            field,
                                            recordIndex,
                                            firstComponentIndex + 1u,
                                            outDifference ) ||
           FindReplayVisualFloatDifference( expectedZ,
                                            actualZ,
                                            field,
                                            recordIndex,
                                            firstComponentIndex + 2u,
                                            outDifference ) ||
           FindReplayVisualFloatDifference( expectedW,
                                            actualW,
                                            field,
                                            recordIndex,
                                            firstComponentIndex + 3u,
                                            outDifference );
}

inline bool FindReplayTrajectoryDifference( std::span<const ReplayTrajectoryRecord> expected,
                                            std::span<const ReplayTrajectoryRecord> actual,
                                            ReplayVisualPacketDifference& outDifference ) noexcept
{
    if ( FindReplayVisualValueDifference( expected.size(),
                                          actual.size(),
                                          ReplayVisualPacketField::TrajectoryRecordCount,
                                          0u,
                                          0u,
                                          outDifference ) )
    {
        outDifference.countMismatch = true;
        return true;
    }
    for ( std::size_t recordIndex = 0; recordIndex < expected.size(); ++recordIndex )
    {
        const ReplayTrajectoryRecord& expectedRecord = expected[recordIndex];
        const ReplayTrajectoryRecord& actualRecord = actual[recordIndex];
        if ( FindReplayVisualValueDifference( expectedRecord.key.bodyId.value,
                                              actualRecord.key.bodyId.value,
                                              ReplayVisualPacketField::TrajectoryRecord,
                                              recordIndex,
                                              0u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( static_cast<uint8_t>( expectedRecord.key.lane ),
                                              static_cast<uint8_t>( actualRecord.key.lane ),
                                              ReplayVisualPacketField::TrajectoryRecord,
                                              recordIndex,
                                              1u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedRecord.key.branchOrdinal,
                                              actualRecord.key.branchOrdinal,
                                              ReplayVisualPacketField::TrajectoryRecord,
                                              recordIndex,
                                              2u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedRecord.version,
                                              actualRecord.version,
                                              ReplayVisualPacketField::TrajectoryRecord,
                                              recordIndex,
                                              3u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedRecord.publishedPointCount,
                                              actualRecord.publishedPointCount,
                                              ReplayVisualPacketField::TrajectoryRecord,
                                              recordIndex,
                                              4u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedRecord.styleId,
                                              actualRecord.styleId,
                                              ReplayVisualPacketField::TrajectoryRecord,
                                              recordIndex,
                                              5u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedRecord.parentId.value,
                                              actualRecord.parentId.value,
                                              ReplayVisualPacketField::TrajectoryRecord,
                                              recordIndex,
                                              6u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( static_cast<uint64_t>( static_cast<int64_t>( expectedRecord.depth ) ),
                                              static_cast<uint64_t>( static_cast<int64_t>( actualRecord.depth ) ),
                                              ReplayVisualPacketField::TrajectoryRecord,
                                              recordIndex,
                                              7u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedRecord.firstFrame,
                                              actualRecord.firstFrame,
                                              ReplayVisualPacketField::TrajectoryRecord,
                                              recordIndex,
                                              8u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedRecord.contactDerived,
                                              actualRecord.contactDerived,
                                              ReplayVisualPacketField::TrajectoryRecord,
                                              recordIndex,
                                              9u,
                                              outDifference ) )
        {
            return true;
        }
        const std::size_t expectedPointCount =
            (std::min)( expectedRecord.publishedPointCount, expectedRecord.points.size() );
        const std::size_t actualPointCount = (std::min)( actualRecord.publishedPointCount, actualRecord.points.size() );
        if ( FindReplayVisualValueDifference( expectedPointCount,
                                              actualPointCount,
                                              ReplayVisualPacketField::TrajectoryPointCount,
                                              recordIndex,
                                              0u,
                                              outDifference ) )
        {
            outDifference.countMismatch = true;
            return true;
        }
        for ( std::size_t pointIndex = 0; pointIndex < expectedPointCount; ++pointIndex )
        {
            const ReplayTrajectoryPoint& expectedPoint = expectedRecord.points[pointIndex];
            const ReplayTrajectoryPoint& actualPoint = actualRecord.points[pointIndex];
            if ( FindReplayVisualValueDifference( expectedPoint.frameIndex,
                                                  actualPoint.frameIndex,
                                                  ReplayVisualPacketField::TrajectoryPoint,
                                                  recordIndex,
                                                  pointIndex * 4u,
                                                  outDifference ) ||
                 FindReplayVisualVectorDifference( expectedPoint.position,
                                                   actualPoint.position,
                                                   ReplayVisualPacketField::TrajectoryPoint,
                                                   recordIndex,
                                                   pointIndex * 4u + 1u,
                                                   outDifference ) )
            {
                return true;
            }
        }
    }
    return false;
}

inline bool FindReplayFutureNodeDifference( std::span<const RunReplayPathTraceNode> expected,
                                            std::span<const RunReplayPathTraceNode> actual,
                                            ReplayVisualPacketDifference& outDifference ) noexcept
{
    if ( FindReplayVisualValueDifference( expected.size(),
                                          actual.size(),
                                          ReplayVisualPacketField::FutureNodeCount,
                                          0u,
                                          0u,
                                          outDifference ) )
    {
        outDifference.countMismatch = true;
        return true;
    }
    for ( std::size_t index = 0; index < expected.size(); ++index )
    {
        const RunReplayPathTraceNode& expectedNode = expected[index];
        const RunReplayPathTraceNode& actualNode = actual[index];
        if ( FindReplayVisualValueDifference( expectedNode.id.value,
                                              actualNode.id.value,
                                              ReplayVisualPacketField::FutureNode,
                                              index,
                                              0u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedNode.parentId.value,
                                              actualNode.parentId.value,
                                              ReplayVisualPacketField::FutureNode,
                                              index,
                                              1u,
                                              outDifference ) ||
             FindReplayVisualValueDifference(
                 static_cast<uint64_t>( static_cast<int64_t>( expectedNode.modelRow.value ) ),
                 static_cast<uint64_t>( static_cast<int64_t>( actualNode.modelRow.value ) ),
                 ReplayVisualPacketField::FutureNode,
                 index,
                 2u,
                 outDifference ) ||
             FindReplayVisualValueDifference(
                 static_cast<uint64_t>( static_cast<int64_t>( expectedNode.parentModelRow.value ) ),
                 static_cast<uint64_t>( static_cast<int64_t>( actualNode.parentModelRow.value ) ),
                 ReplayVisualPacketField::FutureNode,
                 index,
                 3u,
                 outDifference ) ||
             FindReplayVisualValueDifference( expectedNode.firstFrame,
                                              actualNode.firstFrame,
                                              ReplayVisualPacketField::FutureNode,
                                              index,
                                              4u,
                                              outDifference ) ||
             FindReplayVisualVectorDifference( expectedNode.contactPoint,
                                               actualNode.contactPoint,
                                               ReplayVisualPacketField::FutureNode,
                                               index,
                                               5u,
                                               outDifference ) ||
             FindReplayVisualVectorDifference( expectedNode.contactNormal,
                                               actualNode.contactNormal,
                                               ReplayVisualPacketField::FutureNode,
                                               index,
                                               8u,
                                               outDifference ) ||
             FindReplayVisualValueDifference( static_cast<uint64_t>( static_cast<int64_t>( expectedNode.depth ) ),
                                              static_cast<uint64_t>( static_cast<int64_t>( actualNode.depth ) ),
                                              ReplayVisualPacketField::FutureNode,
                                              index,
                                              11u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedNode.contactDerived,
                                              actualNode.contactDerived,
                                              ReplayVisualPacketField::FutureNode,
                                              index,
                                              12u,
                                              outDifference ) )
        {
            return true;
        }
    }
    return false;
}

inline bool FindReplayRetainedMarkerDifference( std::span<const ReplayPredictionRetainedMarker> expected,
                                                std::span<const ReplayPredictionRetainedMarker> actual,
                                                ReplayVisualPacketDifference& outDifference ) noexcept
{
    if ( FindReplayVisualValueDifference( expected.size(),
                                          actual.size(),
                                          ReplayVisualPacketField::RetainedMarkerCount,
                                          0u,
                                          0u,
                                          outDifference ) )
    {
        outDifference.countMismatch = true;
        return true;
    }
    for ( std::size_t index = 0; index < expected.size(); ++index )
    {
        const ReplayPredictionRetainedMarker& expectedMarker = expected[index];
        const ReplayPredictionRetainedMarker& actualMarker = actual[index];
        if ( FindReplayVisualValueDifference( expectedMarker.id.value,
                                              actualMarker.id.value,
                                              ReplayVisualPacketField::RetainedMarker,
                                              index,
                                              0u,
                                              outDifference ) ||
             FindReplayVisualValueDifference(
                 static_cast<uint64_t>( static_cast<int64_t>( expectedMarker.modelRow.value ) ),
                 static_cast<uint64_t>( static_cast<int64_t>( actualMarker.modelRow.value ) ),
                 ReplayVisualPacketField::RetainedMarker,
                 index,
                 1u,
                 outDifference ) ||
             FindReplayVisualValueDifference( expectedMarker.hasEntryPose,
                                              actualMarker.hasEntryPose,
                                              ReplayVisualPacketField::RetainedMarker,
                                              index,
                                              2u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedMarker.hasRestPose,
                                              actualMarker.hasRestPose,
                                              ReplayVisualPacketField::RetainedMarker,
                                              index,
                                              3u,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expectedMarker.hasHorizonPose,
                                              actualMarker.hasHorizonPose,
                                              ReplayVisualPacketField::RetainedMarker,
                                              index,
                                              4u,
                                              outDifference ) ||
             FindReplayVisualVectorDifference( expectedMarker.entryPosition,
                                               actualMarker.entryPosition,
                                               ReplayVisualPacketField::RetainedMarker,
                                               index,
                                               5u,
                                               outDifference ) ||
             FindReplayVisualQuaternionDifference( expectedMarker.entryOrientation,
                                                   actualMarker.entryOrientation,
                                                   ReplayVisualPacketField::RetainedMarker,
                                                   index,
                                                   8u,
                                                   outDifference ) ||
             FindReplayVisualVectorDifference( expectedMarker.restPosition,
                                               actualMarker.restPosition,
                                               ReplayVisualPacketField::RetainedMarker,
                                               index,
                                               12u,
                                               outDifference ) ||
             FindReplayVisualQuaternionDifference( expectedMarker.restOrientation,
                                                   actualMarker.restOrientation,
                                                   ReplayVisualPacketField::RetainedMarker,
                                                   index,
                                                   15u,
                                                   outDifference ) ||
             FindReplayVisualVectorDifference( expectedMarker.horizonPosition,
                                               actualMarker.horizonPosition,
                                               ReplayVisualPacketField::RetainedMarker,
                                               index,
                                               19u,
                                               outDifference ) ||
             FindReplayVisualQuaternionDifference( expectedMarker.horizonOrientation,
                                                   actualMarker.horizonOrientation,
                                                   ReplayVisualPacketField::RetainedMarker,
                                                   index,
                                                   22u,
                                                   outDifference ) )
        {
            return true;
        }
    }
    return false;
}

inline bool FindReplayGhostRequestDifference( std::span<const ReplayPredictionGhostDrawRequest> expected,
                                              std::span<const ReplayPredictionGhostDrawRequest> actual,
                                              ReplayVisualPacketDifference& outDifference ) noexcept
{
    if ( FindReplayVisualValueDifference( expected.size(),
                                          actual.size(),
                                          ReplayVisualPacketField::GhostRequestCount,
                                          0u,
                                          0u,
                                          outDifference ) )
    {
        outDifference.countMismatch = true;
        return true;
    }
    for ( std::size_t index = 0; index < expected.size(); ++index )
    {
        const ReplayPredictionGhostDrawRequest& expectedGhost = expected[index];
        const ReplayPredictionGhostDrawRequest& actualGhost = actual[index];
        if ( FindReplayVisualValueDifference(
                 static_cast<uint64_t>( static_cast<int64_t>( expectedGhost.modelRow.value ) ),
                 static_cast<uint64_t>( static_cast<int64_t>( actualGhost.modelRow.value ) ),
                 ReplayVisualPacketField::GhostRequest,
                 index,
                 0u,
                 outDifference ) ||
             FindReplayVisualVectorDifference( expectedGhost.position,
                                               actualGhost.position,
                                               ReplayVisualPacketField::GhostRequest,
                                               index,
                                               1u,
                                               outDifference ) ||
             FindReplayVisualQuaternionDifference( expectedGhost.orientation,
                                                   actualGhost.orientation,
                                                   ReplayVisualPacketField::GhostRequest,
                                                   index,
                                                   4u,
                                                   outDifference ) ||
             FindReplayVisualFloatDifference( expectedGhost.alpha,
                                              actualGhost.alpha,
                                              ReplayVisualPacketField::GhostRequest,
                                              index,
                                              8u,
                                              outDifference ) ||
             FindReplayVisualFloatDifference( expectedGhost.tintR,
                                              actualGhost.tintR,
                                              ReplayVisualPacketField::GhostRequest,
                                              index,
                                              9u,
                                              outDifference ) ||
             FindReplayVisualFloatDifference( expectedGhost.tintG,
                                              actualGhost.tintG,
                                              ReplayVisualPacketField::GhostRequest,
                                              index,
                                              10u,
                                              outDifference ) ||
             FindReplayVisualFloatDifference( expectedGhost.tintB,
                                              actualGhost.tintB,
                                              ReplayVisualPacketField::GhostRequest,
                                              index,
                                              11u,
                                              outDifference ) ||
             FindReplayVisualFloatDifference( expectedGhost.tintStrength,
                                              actualGhost.tintStrength,
                                              ReplayVisualPacketField::GhostRequest,
                                              index,
                                              12u,
                                              outDifference ) )
        {
            return true;
        }
    }
    return false;
}

inline bool
FindReplayTrajectoryDiagnosticDifference( const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& expected,
                                          const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& actual,
                                          ReplayVisualPacketDifference& outDifference ) noexcept
{
    std::size_t fieldIndex = 0u;
#define SB_REPLAY_VISUAL_COMPARE_DIAGNOSTIC( member )                                                                  \
    if ( FindReplayVisualValueDifference( expected.member,                                                             \
                                          actual.member,                                                               \
                                          ReplayVisualPacketField::TrajectoryDiagnostic,                               \
                                          0u,                                                                          \
                                          fieldIndex++,                                                                \
                                          outDifference ) )                                                            \
    {                                                                                                                  \
        return true;                                                                                                   \
    }
    SB_REPLAY_VISUAL_COMPARE_DIAGNOSTIC( storeBytes );
    SB_REPLAY_VISUAL_COMPARE_DIAGNOSTIC( recordCount );
    SB_REPLAY_VISUAL_COMPARE_DIAGNOSTIC( pointCount );
    SB_REPLAY_VISUAL_COMPARE_DIAGNOSTIC( publishedPointCount );
    SB_REPLAY_VISUAL_COMPARE_DIAGNOSTIC( versionChurn );
    SB_REPLAY_VISUAL_COMPARE_DIAGNOSTIC( maxRecordVersion );
#undef SB_REPLAY_VISUAL_COMPARE_DIAGNOSTIC
    for ( std::size_t index = 0; index < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT; ++index )
    {
        if ( FindReplayVisualValueDifference( expected.emittedSegments[index],
                                              actual.emittedSegments[index],
                                              ReplayVisualPacketField::TrajectoryDiagnostic,
                                              index,
                                              fieldIndex,
                                              outDifference ) ||
             FindReplayVisualValueDifference( expected.droppedSegments[index],
                                              actual.droppedSegments[index],
                                              ReplayVisualPacketField::TrajectoryDiagnostic,
                                              index,
                                              fieldIndex + 1u,
                                              outDifference ) )
        {
            return true;
        }
    }
    fieldIndex += 2u;
    for ( std::size_t index = 0; index < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_BUDGET_PASS_COUNT; ++index )
    {
        if ( FindReplayVisualValueDifference( expected.budgetExpiries[index],
                                              actual.budgetExpiries[index],
                                              ReplayVisualPacketField::TrajectoryDiagnostic,
                                              index,
                                              fieldIndex,
                                              outDifference ) )
        {
            return true;
        }
    }
    ++fieldIndex;
    for ( std::size_t index = 0; index < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_REBUILD_CAUSE_COUNT; ++index )
    {
        if ( FindReplayVisualValueDifference( expected.rebuildCauses[index],
                                              actual.rebuildCauses[index],
                                              ReplayVisualPacketField::TrajectoryDiagnostic,
                                              index,
                                              fieldIndex,
                                              outDifference ) )
        {
            return true;
        }
    }
    return false;
}

inline bool FindReplaySubmissionDiagnosticDifference(
    const SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats& expected,
    const SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats& actual,
    ReplayVisualPacketDifference& outDifference ) noexcept
{
    std::size_t fieldIndex = 0u;
#define SB_REPLAY_VISUAL_COMPARE_SUBMISSION( member )                                                                  \
    if ( FindReplayVisualValueDifference( expected.member,                                                             \
                                          actual.member,                                                               \
                                          ReplayVisualPacketField::SubmissionDiagnostic,                               \
                                          0u,                                                                          \
                                          fieldIndex++,                                                                \
                                          outDifference ) )                                                            \
    {                                                                                                                  \
        return true;                                                                                                   \
    }
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( hasGeometry );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( ordinaryLineHash );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( ordinaryLineBytes );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( ordinaryLineVertexCount );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( priorityLineHash );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( priorityLineCanonicalHash );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( priorityLineBytes );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( priorityLineVertexCount );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( ordinaryRibbonHash );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( ordinaryRibbonBytes );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( ordinaryRibbonSegmentCount );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( priorityRibbonHash );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( priorityRibbonCanonicalHash );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( priorityRibbonBytes );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( priorityRibbonSegmentCount );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( vertexHash );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( ordinaryVertexHash );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( ordinaryVertexBytes );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( ordinaryVertexCount );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( vertexBytes );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( vertexCount );
    SB_REPLAY_VISUAL_COMPARE_SUBMISSION( segmentCount );
#undef SB_REPLAY_VISUAL_COMPARE_SUBMISSION
    return false;
}

inline bool FindReplayVisualPacketDifference( const ReplayVisualPacket& expected,
                                              const ReplayVisualPacket& actual,
                                              ReplayVisualPacketDifference& outDifference ) noexcept
{
#define SB_REPLAY_VISUAL_COMPARE_HEADER( member, differenceField )                                                     \
    if ( expected.header.member != actual.header.member )                                                              \
    {                                                                                                                  \
        outDifference = {};                                                                                            \
        outDifference.field = differenceField;                                                                         \
        outDifference.expectedBits = static_cast<uint64_t>( expected.header.member );                                  \
        outDifference.actualBits = static_cast<uint64_t>( actual.header.member );                                      \
        return true;                                                                                                   \
    }
    SB_REPLAY_VISUAL_COMPARE_HEADER( schemaVersion, ReplayVisualPacketField::SchemaVersion );
    SB_REPLAY_VISUAL_COMPARE_HEADER( sourceFrame, ReplayVisualPacketField::SourceFrame );
    SB_REPLAY_VISUAL_COMPARE_HEADER( revealFrame, ReplayVisualPacketField::RevealFrame );
    if ( expected.header.targetId.value != actual.header.targetId.value )
    {
        outDifference = {};
        outDifference.field = ReplayVisualPacketField::TargetId;
        outDifference.expectedBits = expected.header.targetId.value;
        outDifference.actualBits = actual.header.targetId.value;
        return true;
    }
    SB_REPLAY_VISUAL_COMPARE_HEADER( branchId, ReplayVisualPacketField::BranchId );
    SB_REPLAY_VISUAL_COMPARE_HEADER( eventCursor, ReplayVisualPacketField::EventCursor );
    SB_REPLAY_VISUAL_COMPARE_HEADER( topologyVersion, ReplayVisualPacketField::TopologyVersion );
    SB_REPLAY_VISUAL_COMPARE_HEADER( publishedFrameCount, ReplayVisualPacketField::PublishedFrameCount );
    SB_REPLAY_VISUAL_COMPARE_HEADER( futureNodeCount, ReplayVisualPacketField::FutureNodeCount );
    SB_REPLAY_VISUAL_COMPARE_HEADER( ghostRequestCount, ReplayVisualPacketField::GhostRequestCount );
    SB_REPLAY_VISUAL_COMPARE_HEADER( replayReserveGrowthEvents, ReplayVisualPacketField::ReplayReserveGrowthEvents );
    SB_REPLAY_VISUAL_COMPARE_HEADER( predictionEnabled, ReplayVisualPacketField::PredictionEnabled );
    SB_REPLAY_VISUAL_COMPARE_HEADER( predictionBuilding, ReplayVisualPacketField::PredictionBuilding );
    SB_REPLAY_VISUAL_COMPARE_HEADER( predictionComplete, ReplayVisualPacketField::PredictionComplete );
#undef SB_REPLAY_VISUAL_COMPARE_HEADER

    if ( FindReplayVisualVectorDifference( expected.header.cameraEye,
                                           actual.header.cameraEye,
                                           ReplayVisualPacketField::CameraEye,
                                           0u,
                                           0u,
                                           outDifference ) ||
         FindReplayVisualVectorDifference( expected.header.cameraUp,
                                           actual.header.cameraUp,
                                           ReplayVisualPacketField::CameraUp,
                                           0u,
                                           0u,
                                           outDifference ) ||
         FindReplayTrajectoryDifference( expected.trajectoryRecords, actual.trajectoryRecords, outDifference ) ||
         FindReplayFutureNodeDifference( expected.futureNodes, actual.futureNodes, outDifference ) ||
         FindReplayRetainedMarkerDifference( expected.retainedMarkers, actual.retainedMarkers, outDifference ) ||
         FindReplayGhostRequestDifference( expected.ghostRequests, actual.ghostRequests, outDifference ) ||
         FindReplayTrajectoryDiagnosticDifference( expected.trajectoryDiagnostics,
                                                   actual.trajectoryDiagnostics,
                                                   outDifference ) ||
         FindReplaySubmissionDiagnosticDifference( expected.submission, actual.submission, outDifference ) )
    {
        return true;
    }

    return FindReplayVisualBufferDifference( expected.combinedLines,
                                             actual.combinedLines,
                                             ReplayVisualPacketBuffer::CombinedLines,
                                             outDifference ) ||
           FindReplayVisualBufferDifference( expected.ordinaryLines,
                                             actual.ordinaryLines,
                                             ReplayVisualPacketBuffer::OrdinaryLines,
                                             outDifference ) ||
           FindReplayVisualBufferDifference( expected.priorityLines,
                                             actual.priorityLines,
                                             ReplayVisualPacketBuffer::PriorityLines,
                                             outDifference ) ||
           FindReplayVisualBufferDifference( expected.ordinaryRibbonSegments,
                                             actual.ordinaryRibbonSegments,
                                             ReplayVisualPacketBuffer::OrdinaryRibbonSegments,
                                             outDifference ) ||
           FindReplayVisualBufferDifference( expected.priorityRibbonSegments,
                                             actual.priorityRibbonSegments,
                                             ReplayVisualPacketBuffer::PriorityRibbonSegments,
                                             outDifference ) ||
           FindReplayVisualBufferDifference( expected.expandedRibbonVertices,
                                             actual.expandedRibbonVertices,
                                             ReplayVisualPacketBuffer::ExpandedRibbonVertices,
                                             outDifference );
}
} // namespace ReplayVisualPacketOperations

} // namespace SkullbonezCore::Runtime
