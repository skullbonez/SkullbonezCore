/*
File: SkullbonezSource/Runtime/Replay/ReplayInteractionController.h
Purpose:
  Owns replay restore commands and retained drag-start values that mutate
  replay UI state.

Mental model:
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
  - Active drag kind, body, axis, and angular mode remain controller gesture
    payload; this owner retains only values sampled at gesture start.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
#pragma once

#include "ReplayRuntime.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace Physics
{
class PhysicsEngine;
}
namespace Basics
{
// Invariant: replay input clamping and editor visualization share this exact
// scale so gizmo affordances cannot advertise velocity the command rejects.
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_MAX = 140.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_ANGULAR_MAX = 5.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_EXTRA = 36.0f;

struct ReplayVelocityEditInputFrame
{
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
};

struct ReplayVelocityEditDragStart
{
    float axisT = 0.0f;
    float angle = 0.0f;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
};

struct ReplayVelocityEditApplyContext
{
    ReplayRuntime& replayRuntime;
    Physics::PhysicsEngine& physics;
    Physics::PhysicsBodyHandle body;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float linearVelocityLimit = 0.0f;
    float angularVelocityLimit = 0.0f;
    double visibleUntil = 0.0;
};

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
    ReplayVelocityEditInputFrame BeginVelocityEditInputFrame( bool leftDown, bool leftPressed, bool leftReleased );
    void SetVelocityEditHoverAxes( ReplayRuntime& replayRuntime, int linearAxis, int angularAxis );
    void ResetVelocityEditInteraction( ReplayRuntime& replayRuntime, bool clearHoverAxes );
    void EndVelocityEditDrag( ReplayRuntime& replayRuntime );
    void BeginVelocityEditDrag( ReplayRuntime& replayRuntime, const ReplayVelocityEditDragStart& start );
    void SelectVelocityEditTarget( ReplayRuntime& replayRuntime, double visibleUntil );
    bool ApplyVelocityEditToBody( const ReplayVelocityEditApplyContext& context );

  private:
    static void WriteReason( char* outReason, std::size_t reasonSize, const char* reason );
    static void PublishScrubberRestoreResult( RunReplayScrubberState& scrubber,
                                              double now,
                                              bool restored,
                                              RunReplayTrack messageTrack );
};
} // namespace Basics
} // namespace SkullbonezCore
