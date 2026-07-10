/*
File: SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
Purpose:
  Declares the chunked binary v2 replay artifact writer.

Mental model:
  V2 artifacts are saved replay buffers, not yet complete branchable timelines.
  The first track is presentation data for smooth scrub, with optional solver
  hash/checkpoint chunks and branch provenance layered in for saved restore
  verification work.

Glossary:
  Presentation track: Body poses, camera, and world display fields used for
    smooth visual scrubbing.
  Solver checkpoint chunk: Sparse saved solver payloads copied from retained
    checkpoint-boundary samples. Saved checkpoint-frame restore verification
    exists; event chunks and arbitrary target restore are separate work.
  Branch provenance chunk: Small records naming live timeline ancestry after a
    hash-verified restore creates a child branch.
  Chunk: A typed byte range in the replay file, found through the chunk table.
  JSON (JavaScript Object Notation): Human-readable metadata encoding used by
    the binary artifact's manifest chunk.

Invariants:
  - V2 presentation artifacts are little-endian and chunk-table based.
  - Binary v2 is the sole saved replay artifact format.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ReplayRecorder.h"

namespace SkullbonezCore
{
namespace Basics
{
struct ReplayV2SaveResult
{
    std::size_t sampleCount = 0;
    std::size_t bodyDictionaryCount = 0;
    std::size_t solverHashCount = 0;
    std::size_t solverCheckpointCount = 0;
    std::size_t eventCount = 0;
    std::size_t eventCursorCount = 0;
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
    // Saves presentation samples only: enough for visual scrub, not enough for
    // authoritative physics rollback.
    static bool
    SavePresentation( const ReplayRecorder& recorder, const char* path, ReplayV2SaveResult* result = nullptr );
    // Saves presentation data plus sparse solver hashes/checkpoints so restore
    // validation can compare source frames against the saved artifact.
    static bool SavePresentationWithSolverHashes( const ReplayRecorder& recorder,
                                                  const ReplaySolverRecorder& solverRecorder,
                                                  const char* path,
                                                  ReplayV2SaveResult* result = nullptr );
    static bool SavePresentationWithSolverHashes( const ReplayRecorder& recorder,
                                                  const ReplaySolverRecorder& solverRecorder,
                                                  const ReplayEventRecorder& eventRecorder,
                                                  const char* path,
                                                  ReplayV2SaveResult* result = nullptr );
    // Loaders are intentionally chunk-specific so tools can inspect a replay
    // without paying to inflate every optional stream.
    static bool LoadPresentation( const char* path,
                                  std::vector<ReplayPresentationSample>& outSamples,
                                  ReplayV2LoadResult* result = nullptr );
    static bool LoadSolverCheckpoints( const char* path,
                                       std::vector<ReplaySolverFrameSample>& outCheckpoints,
                                       ReplayV2SolverCheckpointLoadResult* result = nullptr );
    static bool LoadEvents( const char* path,
                            std::vector<ReplayEventSample>& outEvents,
                            ReplayV2EventLoadResult* result = nullptr );
    static bool LoadSolverHashes( const char* path,
                                  std::vector<ReplayV2SolverHashSample>& outHashes,
                                  ReplayV2SolverHashLoadResult* result = nullptr );
};
} // namespace Basics
} // namespace SkullbonezCore
