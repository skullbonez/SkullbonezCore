/*
File: SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
Purpose:
  Declares the versioned chunked-binary replay artifact writer.

Summary:
  The established ReplayV2Artifact API owns the .skreplay format family. The
  v5 persists v4's complete replay-owned state with canonical Hamilton
  quaternion components. Its reader retains deterministic v2-v4 compatibility.
  Optional solver hash/checkpoint chunks and branch provenance remain layered
  in for saved restore verification work.

Glossary:
  Solver checkpoint chunk: Sparse saved solver payloads copied from retained
    checkpoint-boundary samples. Saved checkpoint-frame restore verification
    exists; event chunks and arbitrary target restore are separate work.
  Branch provenance chunk: Small records naming live timeline ancestry after a
    hash-verified restore creates a child branch.
  Chunk: A typed byte range in the replay file, found through the chunk table.

Invariants:
  - Presentation artifacts are little-endian and chunk-table based.
  - The writer emits v5, the reader accepts v2-v5, and future versions fail closed.
  - The ReplayV2Artifact type name is retained API vocabulary, not the current
    wire-version declaration.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ReplayRecorder.h"
#include "ReplayVisualPacket.h"

namespace SkullbonezCore
{
namespace Runtime
{
struct ReplayV2SaveResult
{
    std::size_t sampleCount = 0;
    std::size_t bodyDictionaryCount = 0;
    std::size_t solverHashCount = 0;
    std::size_t solverCheckpointCount = 0;
    std::size_t eventCount = 0;
    std::size_t eventCursorCount = 0;
    std::size_t visualPacketCount = 0;
    uint64_t visualPredictionHash = 0;
    std::size_t fileBytes = 0;
};

struct ReplayV2LoadResult
{
    std::size_t sampleCount = 0;
    std::size_t bodyDictionaryCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex lastFrame = 0;
};

struct ReplayV2SolverCheckpointLoadResult
{
    std::size_t checkpointCount = 0;
    std::size_t bodyDictionaryCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex lastFrame = 0;
};

struct ReplayV2EventLoadResult
{
    std::size_t eventCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex lastFrame = 0;
};

struct ReplayV2SolverHashSample
{
    ReplayFrameIndex frameIndex = 0;
    int sceneFrame = 0;
    double simulationSeconds = 0.0;
    uint64_t presentationHash = 0;
    uint64_t solverHash = 0;
    uint32_t bodyCount = 0;
    uint16_t contactCount = 0;
    uint16_t pipelineRecordCount = 0;
    bool checkpointBoundary = false;
};

struct ReplayV2SolverHashLoadResult
{
    std::size_t hashCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex lastFrame = 0;
};

class ReplayV2Artifact
{
  public:

    // Saves presentation samples only: enough for exact visual scrub, not
    // enough for authoritative physics rollback.
    static bool SavePresentation( const ReplayRecorder& recorder, const char* path, ReplayV2SaveResult* result = nullptr );

    // Saves presentation data plus sparse solver hashes/checkpoints so restore
    // validation can compare source frames against the saved artifact.
    static bool SavePresentationWithSolverHashes( const ReplayRecorder& recorder, const ReplaySolverRecorder& solverRecorder,
                                                  const ReplayEventRecorder& eventRecorder, const char* path,
                                                  ReplayV2SaveResult* result = nullptr );
    static bool SavePresentationWithSolverHashes( const ReplayRecorder& recorder, const ReplaySolverRecorder& solverRecorder,
                                                  const ReplayEventRecorder& eventRecorder,
                                                  std::span<const ReplayVisualArchiveSample> visualPackets,
                                                  std::span<const uint8_t> visualPredictionState, const char* path,
                                                  ReplayV2SaveResult* result = nullptr );

    // Loaders are intentionally chunk-specific so tools can inspect a replay
    // without paying to inflate every optional stream.
    static bool LoadPresentation( const char* path, std::vector<ReplayPresentationSample>& outSamples,
                                  ReplayV2LoadResult* result = nullptr );
    static bool LoadSolverCheckpoints( const char* path, std::vector<ReplaySolverFrameSample>& outCheckpoints,
                                       ReplayV2SolverCheckpointLoadResult* result = nullptr );
    static bool LoadEvents( const char* path, std::vector<ReplayEventSample>& outEvents,
                            ReplayV2EventLoadResult* result = nullptr );
    static bool LoadSolverHashes( const char* path, std::vector<ReplayV2SolverHashSample>& outHashes,
                                  ReplayV2SolverHashLoadResult* result = nullptr );
    static bool LoadVisualPackets( const char* path, std::vector<ReplayVisualArchiveSample>& outPackets );
    static bool LoadVisualPredictionState( const char* path, std::vector<uint8_t>& outBytes );
};
} // namespace Runtime
} // namespace SkullbonezCore
