/*
File: SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
Purpose:
  Declares the chunked binary v2 replay artifact writer.

Mental model:
  V2 artifacts are saved replay buffers, not live simulation state. The first
  track is presentation-only so backwards scrub can load dense poses cheaply.

Glossary:
  Presentation track: Body poses, camera, and world display fields used for
    smooth visual scrubbing.
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

#include "ReplayRecorder.h"

namespace SkullbonezCore
{
namespace Basics
{
struct ReplayV2SaveResult
{
    std::size_t sampleCount = 0;
    std::size_t bodyDictionaryCount = 0;
    std::size_t fileBytes = 0;
};

class ReplayV2Artifact
{
  public:
    static bool
    SavePresentation( const ReplayRecorder& recorder, const char* path, ReplayV2SaveResult* result = nullptr );
};
} // namespace Basics
} // namespace SkullbonezCore
