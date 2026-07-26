/*
File: SkullbonezSource/Runtime/Replay/ReplayArtifactHashLog.h
Purpose:
  Owns Replay's paired presentation and solver hash-log files.

Summary:
  ReplayArtifactHashLog is the ArtifactIO owner for diagnostic CSV streams.
  Capture publishes completed sample values; this owner alone opens, formats,
  appends, and flushes their files.

Invariants:
  - Presentation and solver paths derive from one requested base path.
  - Column order, precision, and hexadecimal spelling are stable validation ABI.
  - Capture owners retain no file handle or formatting command.

Related:
  - ReplayArtifactHashLog.cpp
  - ReplayTimeline.cpp
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayRecorder.h"

#include <fstream>

namespace SkullbonezCore::Runtime
{
class ReplayArtifactHashLog
{
  public:
    void Configure( const char* requestedPath,
                    ReplayRecorderConfig& presentationConfig,
                    ReplayRecorderConfig& solverConfig,
                    const ReplayRecorderStats& presentationStats,
                    const ReplayRecorderStats& solverStats );
    void ResetTimeline( const char* sceneLabel );
    void AppendPresentation( const ReplayPresentationSample& sample );
    void AppendSolver( const ReplaySolverFrameSample& sample );
    void Flush();

  private:
    std::ofstream m_presentation;
    std::ofstream m_solver;
    int m_presentationRetentionSeconds = 0;
    int m_presentationRetentionFrames = 0;
    int m_presentationCheckpointInterval = 0;
    int m_solverRetentionSeconds = 0;
    int m_solverRetentionFrames = 0;
    int m_solverCheckpointInterval = 0;
};
} // namespace SkullbonezCore::Runtime
