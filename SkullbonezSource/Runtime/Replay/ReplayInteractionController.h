/*
File: SkullbonezSource/Runtime/Replay/ReplayInteractionController.h
Purpose:
  Builds replay restore commands and publishes their scrubber result state.

Summary:
  ReplayInteractionController converts operator replay intent into replay-owned
  state changes. It builds a one-frame restore command; ReplayRuntime owns the
  cross-owner transaction and returns the result for UI publication.

Glossary:
  Live restore: Applying a historical replay sample back into the active scene.
  Restore request: Value command naming the retained sample or saved artifact
    target that should become live.
  Live edge: Scrubber position representing the newest branch frame after a
    successful restore.

Invariants:
  - Restore requests borrow retained samples only until the current workspace
    command is applied.
  - Scrubber message, consumed-input state, and live-edge reset are published in
    one place after every restore attempt.
  - Velocity authoring does not pass through this controller; ReplayAuthoring
    owns its retained edit values and replay composition sequences the command.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
#pragma once

#include "ReplayRuntime.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace Runtime
{
// Invariant: replay input clamping and editor visualization share this exact
// scale so gizmo affordances cannot advertise velocity the command rejects.
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_MAX = 140.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_ANGULAR_MAX = 5.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_EXTRA = 36.0f;

class ReplayInteractionController
{
  public:
    bool BuildScrubberRestoreRequest( ReplayRuntime& replayRuntime,
                                      double now,
                                      ReplayLiveRestoreRequest& outRequest,
                                      char* outReason = nullptr,
                                      std::size_t reasonSize = 0 );
    void CompleteScrubberRestore( ReplayRuntime& replayRuntime,
                                  const ReplayLiveRestoreRequest& request,
                                  bool restored,
                                  const RunReplayV2TargetRestoreResult& v2Result,
                                  const char* reason,
                                  RunReplayV2TargetRestoreResult* outV2Result = nullptr,
                                  char* outReason = nullptr,
                                  std::size_t reasonSize = 0 );

  private:
    static void WriteReason( char* outReason, std::size_t reasonSize, const char* reason );
    static void PublishScrubberRestoreResult( RunReplayScrubberState& scrubber,
                                              double now,
                                              bool restored,
                                              RunReplayTrack messageTrack );
};
} // namespace Runtime
} // namespace SkullbonezCore
