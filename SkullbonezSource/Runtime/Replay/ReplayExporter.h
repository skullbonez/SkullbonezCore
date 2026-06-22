/*
File: SkullbonezSource/Runtime/Replay/ReplayExporter.h
Purpose:
  Serializes retained replay samples into a replay artifact file.

Mental model:
  ReplayExporter is a disk writer for recorder-owned data. Callers decide which
  recorder to save; the exporter only turns stable sample arrays into JSON.

Glossary:
  Replay buffer: Bounded in-memory sequence of retained replay samples.
  Presentation sample: Render-facing pose/state captured from a frame.
  Solver sample: Physics-facing state used for rollback and diagnostics.
  Artifact: File written for debugging, sharing, or automated inspection.

Invariants:
  - Save must not mutate the recorder being exported.
  - Field order and names are compatibility surface for replay artifacts.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayExporter.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
*/
#pragma once

#include "ReplayRecorder.h"

namespace SkullbonezCore
{
namespace Basics
{
class ReplayExporter
{
  public:
    static bool Save( const ReplayRecorder& recorder, const char* path );
    static bool Save( const ReplaySolverRecorder& recorder, const char* path );
};
} // namespace Basics
} // namespace SkullbonezCore
