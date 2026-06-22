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

Invariants:
  - V2 presentation artifacts are little-endian and chunk-table based.
  - The writer does not delete or replace the legacy JSON exporter.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
*/
#pragma once

#include <cstddef>
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

class ReplayV2Artifact
{
  public:
    static bool
    SavePresentation( const ReplayRecorder& recorder, const char* path, ReplayV2SaveResult* result = nullptr );
    static bool SavePresentationWithSolverHashes( const ReplayRecorder& recorder,
                                                  const ReplaySolverRecorder& solverRecorder,
                                                  const char* path,
                                                  ReplayV2SaveResult* result = nullptr );
    static bool LoadPresentation( const char* path,
                                  std::vector<ReplayPresentationSample>& outSamples,
                                  ReplayV2LoadResult* result = nullptr );
    static bool LoadSolverCheckpoints( const char* path,
                                       std::vector<ReplaySolverFrameSample>& outCheckpoints,
                                       ReplayV2SolverCheckpointLoadResult* result = nullptr );
};
} // namespace Basics
} // namespace SkullbonezCore
