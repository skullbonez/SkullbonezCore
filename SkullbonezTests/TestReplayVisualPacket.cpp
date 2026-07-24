/*
File: SkullbonezTests/TestReplayVisualPacket.cpp
Purpose:
  Locks typed and ordered first-difference behavior for replay visual packets.

Summary:
  Small stack-owned typed records and float arrays model production packet
  spans. Tests prove semantic, causal-topology, and ghost changes are reported
  before geometry, that reordered or truncated submission buffers identify
  their exact owner lane and float, that an in-flight worker cannot change the
  prepared prefix halfway through one rendered frame, and that prediction draw
  commands append without revisiting a stable publication.

Glossary:
  Packet span: Non-owning view of one ordered production submission stream.
  First difference: Earliest semantic field or float where two packets differ.
  Publication token: Monotonic value that invalidates retained draw commands
    only when a reader-visible trajectory prefix changes.
  All-body path: Space-scene future record selected independently of causal
    child topology.
  Retained attachment: Shared packet operation that joins persistent prediction
    geometry with frame-local moving tails without copying either span.
  Retained chunk: Stable compact range whose continuation repairs only the
    previous chunk's open adjacency tail.

Invariants:
  - Packet comparison is bit-exact and order-sensitive.
  - A count mismatch cannot alias an equal common prefix.
  - Presentation keeps the frame prefix prepared on the frame thread even if
    the worker publishes more prediction rows before rendering consumes it.
  - An unchanged publication token and reveal frame cannot mutate draw storage.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h
  - SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayPredictionPublication.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <string_view>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayVisualPacketFingerprintOperations;
using namespace SkullbonezCore::Runtime::ReplayVisualPacketOperations;

TEST_CASE( "Replay prediction draw cursor resumes at its suffix and reuses stable tokens" )
{
    CHECK( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 19u, 2400, 19u, 2400 ) );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( true, 19u, 2400, 19u, 2400 ) );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 18u, 2400, 19u, 2400 ) );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 19u, 2399, 19u, 2400 ) );

    CHECK( ReplayOverlay::ReplayPredictionFirstUnconsumedPoint( 0u ) == 1u );
    CHECK( ReplayOverlay::ReplayPredictionFirstUnconsumedPoint( 1u ) == 1u );
    CHECK( ReplayOverlay::ReplayPredictionFirstUnconsumedPoint( 128u ) == 128u );
}

TEST_CASE( "Replay retained prediction attachment reuses cached stable submission facts" )
{
    const std::array<float, 4> retainedRibbon = { 1.0f, 2.0f, 3.0f, 4.0f };
    ReplayVisualPacket retained;
    retained.expandedRibbonVertices = retainedRibbon;
    retained.submission.hasGeometry = true;
    retained.submission.vertexHash = HashReplayVisualFloatBuffer( retainedRibbon );
    retained.submission.vertexBytes = retainedRibbon.size() * sizeof( float );
    retained.submission.vertexCount = 6u;
    retained.submission.segmentCount = 1u;

    ReplayVisualPacket frame;
    AttachRetainedPredictionGeometry( frame, retained, 7u, 11u );

    CHECK( frame.retainedPredictionRibbonVertices.data() == retainedRibbon.data() );
    CHECK( frame.retainedPredictionRibbonVertices.size() == retainedRibbon.size() );
    CHECK( frame.retainedPredictionStreamId == 7u );
    CHECK( frame.retainedPredictionRevision == 11u );
    CHECK( frame.submission.vertexHash == retained.submission.vertexHash );
    CHECK( frame.submission.vertexBytes == retained.submission.vertexBytes );
    CHECK( frame.submission.segmentCount == retained.submission.segmentCount );
}

TEST_CASE( "Replay retained ranges preserve canonical geometry across interleaved appends" )
{
    using SkullbonezCore::Rendering::AppendRetainedTrajectoryRecord;
    using SkullbonezCore::Rendering::RetainedTrajectoryDrawRange;
    constexpr std::size_t floatsPerRecord =
        SkullbonezCore::Rendering::RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT;
    const auto record = []( float ax, float bx, float r, float g, float b ) {
        return std::array<float, floatsPerRecord>{ ax, 0.0f, 0.0f,
                                                   bx, 0.0f, 0.0f,
                                                   2.0f,
                                                   r, g, b,
                                                   1.0f, 1.0f, 0.0f,
                                                   ax, 0.0f, 0.0f,
                                                   bx, 0.0f, 0.0f };
    };

    std::vector<float> arena( 3u * floatsPerRecord, 0.0f );
    RetainedTrajectoryDrawRange rangeA = {};
    rangeA.identity = 101u;
    rangeA.firstSegment = 0u;
    rangeA.segmentCapacity = 2u;
    rangeA.sourceVersion = 7u;
    RetainedTrajectoryDrawRange rangeB = {};
    rangeB.identity = 202u;
    rangeB.firstSegment = 2u;
    rangeB.segmentCapacity = 1u;
    rangeB.sourceVersion = 9u;
    REQUIRE( AppendRetainedTrajectoryRecord( arena, rangeA, record( 0.0f, 1.0f, 0.8f, 0.2f, 0.1f ), 0.0001f ) );
    REQUIRE( AppendRetainedTrajectoryRecord( arena, rangeB, record( 10.0f, 11.0f, 0.1f, 0.4f, 0.9f ), 0.0001f ) );
    const std::array<float, floatsPerRecord> rangeBSnapshot = [&] {
        std::array<float, floatsPerRecord> result = {};
        std::copy_n( arena.begin() + static_cast<std::ptrdiff_t>( 2u * floatsPerRecord ),
                     floatsPerRecord,
                     result.begin() );
        return result;
    }();
    REQUIRE( AppendRetainedTrajectoryRecord( arena, rangeA, record( 1.0f, 2.0f, 0.7f, 0.3f, 0.1f ), 0.0001f ) );

    CHECK( rangeA.segmentCount == 2u );
    CHECK( rangeB.segmentCount == 1u );
    CHECK( std::equal( rangeBSnapshot.begin(),
                       rangeBSnapshot.end(),
                       arena.begin() + static_cast<std::ptrdiff_t>( 2u * floatsPerRecord ) ) );

    auto expectedA0 = record( 0.0f, 1.0f, 0.8f, 0.2f, 0.1f );
    expectedA0[16] = 2.0f;
    auto expectedA1 = record( 1.0f, 2.0f, 0.7f, 0.3f, 0.1f );
    expectedA1[13] = 0.0f;
    const auto expectedB = record( 10.0f, 11.0f, 0.1f, 0.4f, 0.9f );
    CHECK( std::equal( expectedA0.begin(), expectedA0.end(), arena.begin() ) );
    CHECK( std::equal(
        expectedA1.begin(), expectedA1.end(), arena.begin() + static_cast<std::ptrdiff_t>( floatsPerRecord ) ) );
    CHECK( std::equal( expectedB.begin(),
                       expectedB.end(),
                       arena.begin() + static_cast<std::ptrdiff_t>( 2u * floatsPerRecord ) ) );
}

TEST_CASE( "Replay retained continuation chunks repair only their shared adjacency tail" )
{
    using SkullbonezCore::Rendering::AppendRetainedTrajectoryContinuationRecord;
    using SkullbonezCore::Rendering::AppendRetainedTrajectoryRecord;
    using SkullbonezCore::Rendering::RetainedTrajectoryDrawRange;
    constexpr std::size_t floatsPerRecord =
        SkullbonezCore::Rendering::RETAINED_TRAJECTORY_FLOATS_PER_SEGMENT;
    const auto record = []( float ax, float bx ) {
        return std::array<float, floatsPerRecord>{ ax, 0.0f, 0.0f,
                                                   bx, 0.0f, 0.0f,
                                                   2.0f,
                                                   0.8f, 0.2f, 0.1f,
                                                   1.0f, 1.0f, 0.0f,
                                                   ax, 0.0f, 0.0f,
                                                   bx, 0.0f, 0.0f };
    };
    std::vector<float> arena( 3u * floatsPerRecord, 0.0f );
    RetainedTrajectoryDrawRange first = {};
    first.firstSegment = 0u;
    first.segmentCapacity = 1u;
    first.sourceVersion = 4u;
    RetainedTrajectoryDrawRange sibling = {};
    sibling.firstSegment = 1u;
    sibling.segmentCapacity = 1u;
    RetainedTrajectoryDrawRange continuation = {};
    continuation.firstSegment = 2u;
    continuation.segmentCapacity = 1u;
    REQUIRE( AppendRetainedTrajectoryRecord( arena, first, record( 0.0f, 1.0f ), 0.0001f ) );
    REQUIRE( AppendRetainedTrajectoryRecord( arena, sibling, record( 10.0f, 11.0f ), 0.0001f ) );
    const std::array<float, floatsPerRecord> siblingSnapshot = [&] {
        std::array<float, floatsPerRecord> result = {};
        std::copy_n( arena.begin() + static_cast<std::ptrdiff_t>( floatsPerRecord ),
                     floatsPerRecord,
                     result.begin() );
        return result;
    }();

    REQUIRE( AppendRetainedTrajectoryContinuationRecord(
        arena, first, continuation, record( 1.0f, 2.0f ), 0.0001f ) );
    CHECK( first.sourceVersion == 5u );
    CHECK( arena[16] == 2.0f );
    CHECK( arena[2u * floatsPerRecord + 13u] == 0.0f );
    CHECK( std::equal( siblingSnapshot.begin(),
                       siblingSnapshot.end(),
                       arena.begin() + static_cast<std::ptrdiff_t>( floatsPerRecord ) ) );
}

TEST_CASE( "Replay space prediction draws every body path instead of causal-only paths" )
{
    ReplayTrajectoryRecordKey selectedRoot;
    selectedRoot.bodyId = SkullbonezCore::Physics::PhysicsSceneObjectId{ 10u };
    selectedRoot.lane = ReplayTrajectoryLane::FutureRoot;
    selectedRoot.branchOrdinal = 0u;

    ReplayTrajectoryRecordKey planetPath = selectedRoot;
    planetPath.bodyId = SkullbonezCore::Physics::PhysicsSceneObjectId{ 20u };
    ReplayTrajectoryRecordKey inactivePlanetPath = planetPath;
    inactivePlanetPath.branchOrdinal = 1u;
    ReplayTrajectoryRecordKey causalChild = planetPath;
    causalChild.lane = ReplayTrajectoryLane::FutureChildOutgoing;
    causalChild.branchOrdinal = 3u;

    CHECK( ReplayOverlay::ReplayPredictionDrawsAllBodyRecord( true, planetPath, 0u, selectedRoot.bodyId ) );
    CHECK_FALSE(
        ReplayOverlay::ReplayPredictionDrawsAllBodyRecord( true, inactivePlanetPath, 0u, selectedRoot.bodyId ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionDrawsAllBodyRecord( true, selectedRoot, 0u, selectedRoot.bodyId ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionDrawsCausalChildRecord( true, causalChild, 0u, 200u ) );
    CHECK( ReplayOverlay::ReplayPredictionDrawsCausalChildRecord( false, causalChild, 0u, 200u ) );
    CHECK( ReplayOverlay::ReplayPredictionUsesAuthoredBodyColor( true, ReplayTrajectoryLane::FutureRoot ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionUsesAuthoredBodyColor( false, ReplayTrajectoryLane::FutureRoot ) );
}

TEST_CASE( "Replay visual presentation keeps one prepared worker prefix for the rendered frame" )
{
    ReplayPredictionPublication workerPublication;
    ReplayPredictionPresentationPublication presentationPublication;
    workerPublication.PublishSlot( 0u, 4u );
    workerPublication.PublishSlot( 1u, 4u );
    presentationPublication.Prepare( workerPublication.PublishedCount( 4u ), 4u );
    REQUIRE( presentationPublication.PresentedCount( workerPublication.PublishedCount( 4u ), 4u ) == 2u );

    // Hazard: the worker may release another completed slot between the frame
    // thread's preparation pass and the renderer's packet build. That slot is
    // next-frame input; exposing it now makes child topology blink while its
    // trajectory cache still describes the two-row prepared prefix.
    workerPublication.PublishSlot( 2u, 4u );
    CHECK( workerPublication.PublishedCount( 4u ) == 3u );
    CHECK( presentationPublication.PresentedCount( workerPublication.PublishedCount( 4u ), 4u ) == 2u );
}

TEST_CASE( "Replay visual archive semantic hash stays canonical and content-sensitive" )
{
    constexpr uint64_t visualStateHash = 0x0123456789ABCDEFull;
    constexpr uint64_t exactPacketHash = 0xFEDCBA9876543210ull;
    constexpr uint32_t topologyVersion = 3u;
    constexpr uint64_t reserveGrowthEvents = 0u;
    const uint64_t expected = BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash,
                                                                             exactPacketHash,
                                                                             topologyVersion,
                                                                             reserveGrowthEvents );

    CHECK( expected == 0x5F1B931D0EE4051Cull );
    CHECK( expected == BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash,
                                                                      exactPacketHash,
                                                                      topologyVersion,
                                                                      reserveGrowthEvents ) );
    CHECK( expected != BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash ^ 1u,
                                                                      exactPacketHash,
                                                                      topologyVersion,
                                                                      reserveGrowthEvents ) );
    CHECK( expected != BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash,
                                                                      exactPacketHash ^ 1u,
                                                                      topologyVersion,
                                                                      reserveGrowthEvents ) );
    CHECK( expected != BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash,
                                                                      exactPacketHash,
                                                                      topologyVersion + 1u,
                                                                      reserveGrowthEvents ) );
}

TEST_CASE( "Replay visual packet reports semantic divergence before buffer bytes" )
{
    const std::array<float, 4> expectedFloats = { 1.0f, 2.0f, 3.0f, 4.0f };
    const std::array<float, 4> actualFloats = { 1.0f, 9.0f, 3.0f, 4.0f };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.header.revealFrame = 120;
    actual.header.revealFrame = 121;
    expected.ordinaryRibbonSegments = expectedFloats;
    actual.ordinaryRibbonSegments = actualFloats;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::RevealFrame );
    CHECK( difference.buffer == ReplayVisualPacketBuffer::None );
    CHECK( difference.expectedBits == 120u );
    CHECK( difference.actualBits == 121u );
}

TEST_CASE( "Replay visual packet preserves ordered lane and exact float diagnostics" )
{
    const std::array<float, 4> expectedFloats = { 1.0f, 2.0f, 3.0f, 4.0f };
    const std::array<float, 4> reorderedFloats = { 1.0f, 3.0f, 2.0f, 4.0f };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.priorityLines = expectedFloats;
    actual.priorityLines = reorderedFloats;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::Buffer );
    CHECK( difference.buffer == ReplayVisualPacketBuffer::PriorityLines );
    CHECK( difference.floatIndex == 1u );
    CHECK_FALSE( difference.countMismatch );
}

TEST_CASE( "Replay visual packet rejects a one-micron submitted vertex change" )
{
    const std::array<float, 4> expectedFloats = { 1.0f, 2.0f, 3.0f, 4.0f };
    std::array<float, 4> actualFloats = expectedFloats;
    actualFloats[2] += 0.000001f;
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.expandedRibbonVertices = expectedFloats;
    actual.expandedRibbonVertices = actualFloats;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::Buffer );
    CHECK( difference.buffer == ReplayVisualPacketBuffer::ExpandedRibbonVertices );
    CHECK( difference.floatIndex == 2u );
    CHECK( difference.expectedBits != difference.actualBits );
    CHECK( HashReplayVisualFloatBuffer( expected.expandedRibbonVertices ) !=
           HashReplayVisualFloatBuffer( actual.expandedRibbonVertices ) );
}

TEST_CASE( "Replay visual fingerprint hashes renderer spans instead of stale submission telemetry" )
{
    const std::array<float, 6> expectedFloats = { 1.0f, 2.0f, 3.0f, 0.2f, 0.3f, 0.4f };
    std::array<float, 6> miswiredFloats = expectedFloats;
    miswiredFloats[1] += 0.000001f;
    ReplayVisualPacket expected;
    ReplayVisualPacket miswired;
    expected.combinedLines = expectedFloats;
    expected.ordinaryLines = expectedFloats;
    miswired.combinedLines = expectedFloats;
    miswired.ordinaryLines = miswiredFloats;

    auto& submission = expected.submission;
    submission.hasGeometry = true;
    submission.ordinaryLineHash = HashReplayVisualFloatBuffer( expectedFloats );
    submission.ordinaryLineBytes = expectedFloats.size() * sizeof( float );
    submission.ordinaryLineVertexCount = 1u;
    const uint64_t emptyStreamHash = HashReplayVisualFloatBuffer( std::span<const float>{} );
    submission.priorityLineHash = emptyStreamHash;
    submission.ordinaryRibbonHash = emptyStreamHash;
    submission.priorityRibbonHash = emptyStreamHash;
    miswired.submission = submission; // Models stats still describing the original tracer vector.

    std::vector<ReplayVisualTrajectoryDigestState> expectedDigests;
    std::vector<ReplayVisualTrajectoryDigestState> miswiredDigests;
    const ReplayVisualPacketFingerprint expectedHash = BuildReplayVisualPacketFingerprint( expected, expectedDigests );
    const ReplayVisualPacketFingerprint miswiredHash = BuildReplayVisualPacketFingerprint( miswired, miswiredDigests );

    const char* expectedMismatch = FindReplayVisualPacketSubmissionSpanMismatch( expected );
    INFO( "unexpected expected-packet mismatch: ", std::string_view( expectedMismatch ? expectedMismatch : "none" ) );
    CHECK( expectedMismatch == nullptr );
    REQUIRE( FindReplayVisualPacketSubmissionSpanMismatch( miswired ) != nullptr );
    CHECK( std::string_view( FindReplayVisualPacketSubmissionSpanMismatch( miswired ) ) ==
           "submission.ordinaryLineHash" );
    CHECK( expectedHash.exactHash != miswiredHash.exactHash );
}

TEST_CASE( "Replay immutable trajectory digest reuse invalidates replaced records" )
{
    std::array<ReplayTrajectoryRecord, 1> records;
    ReplayTrajectoryRecord& record = records[0];
    record.key.bodyId.value = 42u;
    record.key.lane = ReplayTrajectoryLane::FutureRoot;
    record.version = 3u;
    record.points = { { 0u, { 0.0f, 0.0f, 0.0f } }, { 1u, { 1.0f, 0.0f, 0.0f } } };
    record.publishedPointCount = record.points.size();

    ReplayVisualPacket packet;
    packet.header.targetId.value = record.key.bodyId.value;
    packet.header.predictionComplete = true;
    packet.trajectoryRecords = records;

    std::vector<ReplayVisualTrajectoryDigestState> reusableDigests;
    const ReplayVisualPacketFingerprint initial =
        BuildReplayVisualPacketFingerprint( packet,
                                            reusableDigests,
                                            ReplayVisualTrajectoryDigestPolicy::ReuseImmutableRecords );
    const ReplayVisualPacketFingerprint cached =
        BuildReplayVisualPacketFingerprint( packet,
                                            reusableDigests,
                                            ReplayVisualTrajectoryDigestPolicy::ReuseImmutableRecords );
    CHECK( cached.trajectoryStateHash == initial.trajectoryStateHash );

    record.points[1].position.x = 2.0f;
    ++record.version;
    const ReplayVisualPacketFingerprint replaced =
        BuildReplayVisualPacketFingerprint( packet,
                                            reusableDigests,
                                            ReplayVisualTrajectoryDigestPolicy::ReuseImmutableRecords );
    std::vector<ReplayVisualTrajectoryDigestState> strictDigests;
    const ReplayVisualPacketFingerprint strict = BuildReplayVisualPacketFingerprint( packet, strictDigests );
    CHECK( replaced.trajectoryStateHash == strict.trajectoryStateHash );
    CHECK( replaced.trajectoryStateHash != initial.trajectoryStateHash );
}

TEST_CASE( "Replay visual packet rejects an equal prefix with a missing float" )
{
    const std::array<float, 3> complete = { 1.0f, 2.0f, 3.0f };
    const std::array<float, 2> truncated = { 1.0f, 2.0f };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.expandedRibbonVertices = complete;
    actual.expandedRibbonVertices = truncated;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.buffer == ReplayVisualPacketBuffer::ExpandedRibbonVertices );
    CHECK( difference.floatIndex == 2u );
    CHECK( difference.countMismatch );
}

TEST_CASE( "Replay visual packet preserves causal topology order before render bytes" )
{
    std::array<RunReplayPathTraceNode, 2> expectedNodes;
    expectedNodes[0].id.value = 11u;
    expectedNodes[0].firstFrame = 120u;
    expectedNodes[1].id.value = 12u;
    expectedNodes[1].parentId.value = 11u;
    expectedNodes[1].firstFrame = 140u;
    std::array<RunReplayPathTraceNode, 2> reorderedNodes = { expectedNodes[1], expectedNodes[0] };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.header.futureNodeCount = 2u;
    actual.header.futureNodeCount = 2u;
    expected.futureNodes = expectedNodes;
    actual.futureNodes = reorderedNodes;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::FutureNode );
    CHECK( difference.recordIndex == 0u );
    CHECK( difference.floatIndex == 0u );
    CHECK( difference.expectedBits == 11u );
    CHECK( difference.actualBits == 12u );
}

TEST_CASE( "Replay visual packet reports an exact ghost presentation component" )
{
    std::array<ReplayPredictionGhostDrawRequest, 1> expectedGhosts;
    std::array<ReplayPredictionGhostDrawRequest, 1> actualGhosts;
    expectedGhosts[0].modelRow.value = 4;
    actualGhosts[0].modelRow.value = 4;
    expectedGhosts[0].alpha = 0.25f;
    actualGhosts[0].alpha = 0.5f;
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.header.ghostRequestCount = 1u;
    actual.header.ghostRequestCount = 1u;
    expected.ghostRequests = expectedGhosts;
    actual.ghostRequests = actualGhosts;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::GhostRequest );
    CHECK( difference.recordIndex == 0u );
    CHECK( difference.floatIndex == 8u );
    CHECK( difference.expectedBits == std::bit_cast<uint32_t>( 0.25f ) );
    CHECK( difference.actualBits == std::bit_cast<uint32_t>( 0.5f ) );
}

TEST_CASE( "Replay visual packet reports an exact published trajectory point" )
{
    ReplayTrajectoryRecord expectedRecord;
    ReplayTrajectoryRecord actualRecord;
    expectedRecord.key.bodyId.value = 41u;
    actualRecord.key.bodyId.value = 41u;
    expectedRecord.publishedPointCount = 1u;
    actualRecord.publishedPointCount = 1u;
    expectedRecord.points.push_back( ReplayTrajectoryPoint{ 17u, { 1.0f, 2.0f, 3.0f } } );
    actualRecord.points.push_back( ReplayTrajectoryPoint{ 17u, { 1.0f, 2.000001f, 3.0f } } );
    const std::array<ReplayTrajectoryRecord, 1> expectedRecords = { expectedRecord };
    const std::array<ReplayTrajectoryRecord, 1> actualRecords = { actualRecord };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.trajectoryRecords = expectedRecords;
    actual.trajectoryRecords = actualRecords;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::TrajectoryPoint );
    CHECK( difference.recordIndex == 0u );
    CHECK( difference.floatIndex == 2u );
    CHECK( difference.expectedBits == std::bit_cast<uint32_t>( 2.0f ) );
    CHECK( difference.actualBits == std::bit_cast<uint32_t>( 2.000001f ) );
}

TEST_CASE( "Replay visual packet covers a non-vacuous retained horizon pose" )
{
    std::array<ReplayPredictionRetainedMarker, 1> expectedMarkers;
    std::array<ReplayPredictionRetainedMarker, 1> actualMarkers;
    expectedMarkers[0].id.value = 77u;
    actualMarkers[0].id.value = 77u;
    expectedMarkers[0].hasHorizonPose = true;
    actualMarkers[0].hasHorizonPose = true;
    expectedMarkers[0].horizonPosition = { 4.0f, 5.0f, 6.0f };
    actualMarkers[0].horizonPosition = { 4.0f, 5.0f, 6.000001f };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.retainedMarkers = expectedMarkers;
    actual.retainedMarkers = actualMarkers;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::RetainedMarker );
    CHECK( difference.recordIndex == 0u );
    CHECK( difference.floatIndex == 21u );
    CHECK( difference.expectedBits == std::bit_cast<uint32_t>( 6.0f ) );
    CHECK( difference.actualBits == std::bit_cast<uint32_t>( 6.000001f ) );
}

TEST_CASE( "Replay visual packet reports dropped-segment diagnostics" )
{
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.trajectoryDiagnostics.droppedSegments[0] = 12u;
    actual.trajectoryDiagnostics.droppedSegments[0] = 13u;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::TrajectoryDiagnostic );
    CHECK( difference.expectedBits == 12u );
    CHECK( difference.actualBits == 13u );
}

TEST_CASE( "Replay visual fingerprint is stable across an unchanged published prefix" )
{
    ReplayTrajectoryRecord record;
    record.key.bodyId.value = 9u;
    record.version = 3u;
    record.publishedPointCount = 2u;
    record.points.push_back( { 0u, { 1.0f, 2.0f, 3.0f } } );
    record.points.push_back( { 1u, { 4.0f, 5.0f, 6.0f } } );
    const std::array<ReplayTrajectoryRecord, 1> records = { record };
    ReplayVisualPacket packet;
    packet.header.sourceFrame = 44u;
    packet.trajectoryRecords = records;
    packet.submission.ordinaryLineHash = 0x1234u;
    std::vector<ReplayVisualTrajectoryDigestState> digests;

    const ReplayVisualPacketFingerprint first = BuildReplayVisualPacketFingerprint( packet, digests );
    const ReplayVisualPacketFingerprint second = BuildReplayVisualPacketFingerprint( packet, digests );
    CHECK( first.semanticHash == second.semanticHash );
    CHECK( first.exactHash == second.exactHash );
}

TEST_CASE( "Replay visual fingerprint catches mutation inside a published prefix" )
{
    std::array<ReplayTrajectoryRecord, 1> records;
    records[0].key.bodyId.value = 9u;
    records[0].version = 3u;
    records[0].publishedPointCount = 2u;
    records[0].points.push_back( { 0u, { 1.0f, 2.0f, 3.0f } } );
    records[0].points.push_back( { 1u, { 4.0f, 5.0f, 6.0f } } );
    ReplayVisualPacket packet;
    packet.trajectoryRecords = records;
    std::vector<ReplayVisualTrajectoryDigestState> digests;

    const ReplayVisualPacketFingerprint before = BuildReplayVisualPacketFingerprint( packet, digests );
    records[0].points[0].position.y = 2.000001f;
    const ReplayVisualPacketFingerprint after = BuildReplayVisualPacketFingerprint( packet, digests );

    CHECK( before.visualStateHash != after.visualStateHash );
    CHECK( before.semanticHash != after.semanticHash );
    CHECK( before.exactHash != after.exactHash );
}

TEST_CASE( "Replay visual fingerprint excludes the completed prediction worker bank" )
{
    std::array<ReplayTrajectoryRecord, 2> expectedRecords;
    expectedRecords[0].key.bodyId.value = 9u;
    expectedRecords[0].key.lane = ReplayTrajectoryLane::FutureChildOutgoing;
    expectedRecords[0].key.branchOrdinal = 0u;
    expectedRecords[0].publishedPointCount = 1u;
    expectedRecords[0].points.push_back( { 0u, { 1.0f, 2.0f, 3.0f } } );
    expectedRecords[1] = expectedRecords[0];
    expectedRecords[1].key.branchOrdinal = REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
    expectedRecords[1].version = 4u;
    std::array<ReplayTrajectoryRecord, 2> actualRecords = expectedRecords;
    actualRecords[1].points[0].position.y = 200.0f;

    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.header.predictionComplete = true;
    actual.header.predictionComplete = true;
    expected.header.futureNodeCount = 1u;
    actual.header.futureNodeCount = 1u;
    expected.trajectoryRecords = expectedRecords;
    actual.trajectoryRecords = actualRecords;
    std::vector<ReplayVisualTrajectoryDigestState> expectedDigests;
    std::vector<ReplayVisualTrajectoryDigestState> actualDigests;

    const ReplayVisualPacketFingerprint expectedHash = BuildReplayVisualPacketFingerprint( expected, expectedDigests );
    const ReplayVisualPacketFingerprint inactiveMutationHash =
        BuildReplayVisualPacketFingerprint( actual, actualDigests );
    CHECK( expectedHash.visualStateHash == inactiveMutationHash.visualStateHash );
    CHECK( expectedHash.exactHash == inactiveMutationHash.exactHash );
    CHECK( expectedHash.semanticHash != inactiveMutationHash.semanticHash );

    actualRecords[0].points[0].position.y = 2.000001f;
    const ReplayVisualPacketFingerprint activeMutationHash =
        BuildReplayVisualPacketFingerprint( actual, actualDigests );
    CHECK( expectedHash.visualStateHash != activeMutationHash.visualStateHash );
    CHECK( expectedHash.exactHash != activeMutationHash.exactHash );
}

TEST_CASE( "Replay visual fingerprint covers ghost semantics before submission bytes" )
{
    std::array<ReplayPredictionGhostDrawRequest, 1> expectedGhosts;
    std::array<ReplayPredictionGhostDrawRequest, 1> actualGhosts;
    expectedGhosts[0].modelRow.value = 12;
    actualGhosts[0].modelRow.value = 12;
    expectedGhosts[0].position = { 1.0f, 2.0f, 3.0f };
    actualGhosts[0].position = expectedGhosts[0].position;
    expectedGhosts[0].alpha = 0.25f;
    actualGhosts[0].alpha = 0.250001f;
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.ghostRequests = expectedGhosts;
    actual.ghostRequests = actualGhosts;
    expected.submission.vertexHash = 0x55u;
    actual.submission.vertexHash = expected.submission.vertexHash;
    std::vector<ReplayVisualTrajectoryDigestState> expectedDigests;
    std::vector<ReplayVisualTrajectoryDigestState> actualDigests;

    const ReplayVisualPacketFingerprint expectedHash = BuildReplayVisualPacketFingerprint( expected, expectedDigests );
    const ReplayVisualPacketFingerprint actualHash = BuildReplayVisualPacketFingerprint( actual, actualDigests );
    CHECK( expectedHash.semanticHash != actualHash.semanticHash );
    CHECK( expectedHash.exactHash != actualHash.exactHash );
}

TEST_CASE( "Replay visual and exact fingerprints exclude process-local diagnostics" )
{
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.header.sourceFrame = 7u;
    actual.header.sourceFrame = 7u;
    expected.trajectoryDiagnostics.droppedSegments[0] = 2u;
    actual.trajectoryDiagnostics.droppedSegments[0] = 3u;
    expected.header.replayReserveGrowthEvents = 11u;
    actual.header.replayReserveGrowthEvents = 12u;
    std::vector<ReplayVisualTrajectoryDigestState> expectedDigests;
    std::vector<ReplayVisualTrajectoryDigestState> actualDigests;

    const ReplayVisualPacketFingerprint expectedHash = BuildReplayVisualPacketFingerprint( expected, expectedDigests );
    const ReplayVisualPacketFingerprint actualHash = BuildReplayVisualPacketFingerprint( actual, actualDigests );
    CHECK( expectedHash.visualStateHash == actualHash.visualStateHash );
    CHECK( expectedHash.semanticHash != actualHash.semanticHash );
    CHECK( expectedHash.exactHash == actualHash.exactHash );
}
