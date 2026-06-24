/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.h
Purpose:
  Owns replay recorders and branch state for the runtime replay subsystem.

Mental model:
  ReplayRuntime is the compatibility boundary while replay behavior moves out
  of Run. Existing Run methods can still reach the legacy recorders through
  explicit accessors, but ownership now belongs to the replay subsystem.
*/
#pragma once

#include "ReplayRecorder.h"

namespace SkullbonezCore::Basics
{
struct ReplayV2SaveResult;

class ReplayRuntime
{
  public:
    struct RecordingConfigResult
    {
        ReplayRecorderConfig presentationConfig;
        ReplayRecorderConfig solverConfig;
        ReplayRecorderStats presentationStats;
        ReplayRecorderStats solverStats;
        ReplayEventRecorderStats eventStats;
    };

    ReplayRecorder& Presentation();
    const ReplayRecorder& Presentation() const;

    ReplaySolverRecorder& Solver();
    const ReplaySolverRecorder& Solver() const;

    ReplayEventRecorder& Events();
    const ReplayEventRecorder& Events() const;

    ReplayBranchInfo& Branch();
    const ReplayBranchInfo& Branch() const;

    RecordingConfigResult ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath );
    void FlushHashLogs();
    void ResetBranch();
    void ResetTimeline( const char* sceneLabel );
    bool IsPresentationEnabled() const;
    bool IsCaptureEnabled() const;
    ReplayRecorderStats PresentationStats() const;
    ReplayRecorderStats SolverStats() const;
    ReplayEventRecorderStats EventStats() const;
    ReplayFrameIndex NextEventFrameIndex() const;
    void CaptureFrame( ReplayCaptureInput input );
    void RecordEvent( ReplayEventKind kind,
                      ReplayFrameIndex frameIndex,
                      uint32_t flags,
                      int32_t value0,
                      int32_t value1,
                      int32_t value2,
                      int32_t value3,
                      uint64_t data0,
                      const char* text );
    bool SaveSolverReplay( const char* path ) const;
    bool SavePresentationWithSolverHashes( const char* path, ReplayV2SaveResult* result = nullptr ) const;

  private:
    ReplayRecorder m_presentation; // Bounded replay presentation recorder for recent-frame inspection.
    ReplaySolverRecorder m_solver; // Same-tick solver-state recorder kept in tandem with presentation replay.
    ReplayEventRecorder m_events;  // Bounded intent/event stream kept beside v2 replay tracks.
    ReplayBranchInfo m_branch;     // Current live replay branch provenance.
};
} // namespace SkullbonezCore::Basics
