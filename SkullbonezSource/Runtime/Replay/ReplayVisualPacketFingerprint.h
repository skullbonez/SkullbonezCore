/*
File: SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h
Purpose:
  Declares the canonical typed and render-buffer fingerprint for replay visuals.

Summary:
  The live mega capture and focused packet controls call this one implementation.
  The semantic hash extends typed presentation with process diagnostics. The
  exact hash instead extends typed presentation with facts recomputed from the
  renderer-bound packet spans, so stale tracer statistics cannot hide miswiring.

Glossary:
  Visual-state hash: Digest of presentation-bearing typed values, excluding
    process-local allocation and budget telemetry.
  Semantic hash: Visual-state digest extended with replay diagnostics.
  Exact hash: Visual-state digest extended with ordered renderer-span facts.
  Prefix digest: Digest rebuilt from every currently published trajectory point.

Invariants:
  - Field order mirrors FindReplayVisualPacketDifference.
  - Every published trajectory prefix is rehashed in full so an old point
    mutation cannot hide behind an unchanged record version.
  - Hashes are diagnostics and regression oracles, never durable identity.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h
  - tools/check_replay_visual_fidelity.py
*/
#pragma once

#include "ReplayVisualPacket.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SkullbonezCore::Runtime
{
struct ReplayVisualTrajectoryDigestState
{
    uint32_t bodyId = 0;
    uint32_t version = 0;
    uint16_t branchOrdinal = 0;
    uint8_t lane = 0;
    uint64_t publishedPointCount = 0;
    uint64_t prefixHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
};

struct ReplayVisualPacketFingerprint
{
    uint64_t headerStateHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t trajectoryStateHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t topologyStateHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t markerStateHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t ghostStateHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t visualStateHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t semanticHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t exactHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
};

// These facts are computed from the borrowed packet spans themselves, not from
// tracer bookkeeping. The renderer consumes these exact spans, so the golden
// oracle must fail if publication wires even one lane to the wrong storage.
struct ReplayVisualPacketBufferFacts
{
    bool hasGeometry = false;
    uint64_t combinedLineHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t ordinaryLineHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t priorityLineHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t ordinaryRibbonHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t priorityRibbonHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t expandedVertexHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t ordinaryExpandedVertexHash = REPLAY_VISUAL_BUFFER_FNV_OFFSET;
    uint64_t combinedLineBytes = 0;
    uint64_t ordinaryLineBytes = 0;
    uint64_t priorityLineBytes = 0;
    uint64_t ordinaryRibbonBytes = 0;
    uint64_t priorityRibbonBytes = 0;
    uint64_t expandedVertexBytes = 0;
    uint64_t ordinaryExpandedVertexBytes = 0;
};

ReplayVisualPacketBufferFacts BuildReplayVisualPacketBufferFacts( const ReplayVisualPacket& packet ) noexcept;

// Returns the first stale or inconsistent tracer statistic. A non-null result
// means the side-channel telemetry does not describe the renderer-bound spans.
const char* FindReplayVisualPacketSubmissionSpanMismatch( const ReplayVisualPacket& packet ) noexcept;

ReplayVisualPacketFingerprint
BuildReplayVisualPacketFingerprint( const ReplayVisualPacket& packet,
                                    std::vector<ReplayVisualTrajectoryDigestState>& trajectoryDigests );

// Returns the first typed/header/submission mismatch against one durable RVIS
// row. Raw packet streams are compared by their exact ordered byte digests and
// sizes; the archive verifier separately checks those rows against the capture.
bool ReplayVisualPacketMatchesArchiveSample( const ReplayVisualPacket& packet,
                                             const ReplayVisualArchiveSample& expected,
                                             char* difference,
                                             std::size_t differenceSize );
} // namespace SkullbonezCore::Runtime
