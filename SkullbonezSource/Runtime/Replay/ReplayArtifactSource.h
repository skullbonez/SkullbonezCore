/*
File: SkullbonezSource/Runtime/Replay/ReplayArtifactSource.h
Purpose:
  Declares ArtifactIO-owned materialization of retained capture rings.

Summary:
  Replay recorders own compact live rings. ReplayArtifactSource is the only
  cold boundary allowed to reconstruct those rings into chronological vectors

  for file serialization.

Invariants:
  - Materialization is cold ArtifactIO work and may allocate output vectors.
  - Capture owners expose no public chronological-copy command.
  - Resolved samples preserve exact retained order and payload bits.

Related:
  - ReplayV2Artifact.cpp
  - ReplayRecorder.h
*/
#pragma once

#include <vector>

namespace SkullbonezCore::Runtime
{
class ReplayRecorder;
class ReplaySolverRecorder;
class ReplayEventRecorder;
struct ReplayPresentationSample;
struct ReplaySolverFrameSample;
struct ReplayEventSample;

class ReplayArtifactSource
{
  public:
    static void MaterializePresentation( const ReplayRecorder& recorder, std::vector<ReplayPresentationSample>& outSamples );
    static void MaterializeSolver( const ReplaySolverRecorder& recorder, std::vector<ReplaySolverFrameSample>& outSamples );
    static void MaterializeEvents( const ReplayEventRecorder& recorder, std::vector<ReplayEventSample>& outEvents );
};
} // namespace SkullbonezCore::Runtime
