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
class ReplayRuntime
{
  public:
    ReplayRecorder& Presentation();
    const ReplayRecorder& Presentation() const;

    ReplaySolverRecorder& Solver();
    const ReplaySolverRecorder& Solver() const;

    ReplayEventRecorder& Events();
    const ReplayEventRecorder& Events() const;

    ReplayBranchInfo& Branch();
    const ReplayBranchInfo& Branch() const;

  private:
    ReplayRecorder m_presentation; // Bounded replay presentation recorder for recent-frame inspection.
    ReplaySolverRecorder m_solver; // Same-tick solver-state recorder kept in tandem with presentation replay.
    ReplayEventRecorder m_events;  // Bounded intent/event stream kept beside v2 replay tracks.
    ReplayBranchInfo m_branch;     // Current live replay branch provenance.
};
} // namespace SkullbonezCore::Basics
