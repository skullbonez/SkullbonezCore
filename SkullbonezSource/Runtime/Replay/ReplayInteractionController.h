/*
File: SkullbonezSource/Runtime/Replay/ReplayInteractionController.h
Purpose:
  Owns cold replay interaction commands that mutate replay UI state.

Mental model:
  ReplayInteractionController converts operator replay intent into replay-owned
  state changes. Run still supplies live-world restore APIs because those calls
  touch scene, physics, world, and camera owners outside ReplayRuntime.

Glossary:
  Live restore: Applying a historical replay sample back into the active scene.
  Restore API: Borrowed function table for Run-owned world/physics mutation.
  Live edge: Scrubber position representing the newest branch frame after a
    successful restore.

Invariants:
  - Restore API callbacks are cold user commands, never per-frame physics hooks.
  - Scrubber message, consumed-input state, and live-edge reset are published in
    one place after every restore attempt.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
#pragma once

#include "ReplayRuntime.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}
namespace Basics
{
struct ReplayLiveRestoreApi
{
    // Lifetime: callbacks borrow the active Run instance for one cold replay
    // command. The controller never stores this table beyond the call.
    void* user = nullptr;
    void ( *enterInteractiveSceneRun )( void* user ) = nullptr;
    bool ( *restoreV2ArtifactTargetState )( void* user,
                                            const char* path,
                                            ReplayFrameIndex requestedFrame,
                                            bool makeLiveBranch,
                                            RunReplayV2TargetRestoreResult& outResult,
                                            char* outReason,
                                            std::size_t reasonSize ) = nullptr;
    bool ( *restoreSolverSampleAsLive )( void* user,
                                         const ReplaySolverFrameSample& sample,
                                         char* outReason,
                                         std::size_t reasonSize ) = nullptr;
};

struct ReplayLiveRestoreContext
{
    ReplayRuntime& replayRuntime;
    double now = 0.0;
    ReplayLiveRestoreApi api;
    RunReplayV2TargetRestoreResult* outV2Result = nullptr;
    char* outReason = nullptr;
    std::size_t reasonSize = 0;
};

struct ReplayVelocityEditInputFrame
{
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
};

struct ReplayVelocityEditResetResult
{
    bool endDragGesture = false;
    bool releaseMouseCapture = false;
};

struct ReplayVelocityEditDragStart
{
    int modelIndex = -1;
    int axis = -1;
    bool angular = false;
    float axisT = 0.0f;
    float angle = 0.0f;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
};

struct ReplayVelocityEditApplyContext
{
    ReplayRuntime& replayRuntime;
    GameObjects::GameModelCollection& models;
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
    bool RestoreScrubberSelectionAsLive( const ReplayLiveRestoreContext& context );
    ReplayVelocityEditInputFrame BeginVelocityEditInputFrame( ReplayRuntime& replayRuntime, bool leftDown );
    void SetVelocityEditHoverAxes( ReplayRuntime& replayRuntime, int linearAxis, int angularAxis );
    ReplayVelocityEditResetResult ResetVelocityEditInteraction( ReplayRuntime& replayRuntime, bool clearHoverAxes );
    ReplayVelocityEditResetResult EndVelocityEditDrag( ReplayRuntime& replayRuntime );
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
