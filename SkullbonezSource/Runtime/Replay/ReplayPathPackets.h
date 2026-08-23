/*
File: SkullbonezSource/Runtime/Replay/ReplayPathPackets.h
Purpose:
  Publishes replay path-target and retained/future trajectory selection values without Presentation ownership.

Summary:
  Presentation owns target mutation and trajectory caches. Drawing, development
  UI, and automation consume these bounded value records for one frame.

Glossary:
  Retained path: Solver-history trajectory for the selected replay body.

Invariants:
  - Scene object ids are durable; model rows are validated hints.
  - Published vectors are owner storage and must not grow during steady drawing.

Related:
  - ReplayPresentation.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Physics/PhysicsHandles.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore::Runtime
{
struct RunReplayPathTarget
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow;
    char name[64] = {};
};

struct RunReplayPastTrajectoryBuildState
{
    Physics::PhysicsSceneObjectId targetId;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex builtThroughFrame = 0;

    // Invariant: Eviction progress ties the retained path to the Recorder window so cached points
    // cannot silently outlive the history that established them.
    uint64_t totalFramesEvicted = 0;
    uint64_t fullRebuildCount = 0;
    uint64_t incrementalTrimCount = 0;
    bool valid = false;
};

// Immutable presentation cursor consumed while Prediction maintains the
// retained trajectory lane. Replay owns the vocabulary because the cursor
// describes recorded solver history, not future-simulation authority.
struct ReplayPastTrajectoryView
{
    Physics::PhysicsSceneObjectId targetId;
    Physics::PhysicsSceneObjectId retainedTargetId;
    Physics::ModelRowHint targetModelRow;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex builtThroughFrame = 0;
    uint64_t totalFramesEvicted = 0;
    uint64_t fullRebuildCount = 0;
    uint64_t incrementalTrimCount = 0;
    bool hasTarget = false;
    bool valid = false;
};

enum class ReplayPathColorMode : uint8_t
{
    LaneFlat,
    VelocityHeat,
    TimeGradient,
    PerObjectHue,
    CausalDepth
};

inline const char* ReplayPathColorModeName( ReplayPathColorMode mode ) noexcept
{
    switch ( mode )
    {
    case ReplayPathColorMode::LaneFlat:
        return "Lane flat";
    case ReplayPathColorMode::VelocityHeat:
        return "Velocity heat";
    case ReplayPathColorMode::TimeGradient:
        return "Time gradient";
    case ReplayPathColorMode::PerObjectHue:
        return "Per-object hue";
    case ReplayPathColorMode::CausalDepth:
        return "Causal depth";
    }

    return "Lane flat";
}

struct RunReplayPathVisualizerState
{
    bool hasTarget = false;
    bool pastPathVisible = true;
    ReplayPathColorMode colorMode = ReplayPathColorMode::LaneFlat;
    Physics::PhysicsSceneObjectId targetId;
    Physics::ModelRowHint targetModelRow;
    char targetName[64] = {};

    // Runtime allocation policy: Presentation reserves selected-target rows
    // before gameplay; per-frame path rebuilding reuses storage and never grows
    // this vector.
    // Invariant: predicted future nodes publish separately from Runtime/Prediction.
    std::vector<RunReplayPathTarget> targets;
    RunReplayPastTrajectoryBuildState pastTrajectory;
};
} // namespace SkullbonezCore::Runtime
