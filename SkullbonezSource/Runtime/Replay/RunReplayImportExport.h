/*
File: SkullbonezSource/Runtime/Replay/RunReplayImportExport.h
Purpose:
  Declares replay artifact save helpers shared by replay scrubber tools.

Mental model:
  Replay scrubber UI owns transient button/message state, while ReplayRuntime
  owns the selected replay buffers and artifact serialization.

Glossary:
  Replay artifact: On-disk solver or presentation replay file.
  Scrubber message: Short-lived UI status text shown near the replay controls.

Invariants:
  - Numbered replay filenames must stay stable for automation and operator use.
  - Save messages update the active scrubber track that triggered the artifact.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayImportExport.cpp
  - SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.inl
*/
#pragma once

#include "ReplayRuntime.h"

namespace SkullbonezCore
{
namespace Basics
{
bool SaveReplayBufferFromScrubber( ReplayRuntime& replayRuntime, RunReplayTrack track, double now );
}
} // namespace SkullbonezCore
