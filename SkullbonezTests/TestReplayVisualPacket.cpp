/*
File: SkullbonezTests/TestReplayVisualPacket.cpp
Purpose:
  Locks typed and ordered first-difference behavior for replay visual packets.

Summary:
  Small stack-owned typed records and float arrays model production packet
  spans. Tests prove semantic, causal-topology, and ghost changes are reported
  before geometry and that reordered or truncated submission buffers identify
  their exact owner lane and float.

Glossary:
  Packet span: Non-owning view of one ordered production submission stream.
  First difference: Earliest semantic field or float where two packets differ.

Invariants:
  - Packet comparison is bit-exact and order-sensitive.
  - A count mismatch cannot alias an equal common prefix.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h"

#include <array>
#include <bit>

using namespace SkullbonezCore::Runtime;

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
