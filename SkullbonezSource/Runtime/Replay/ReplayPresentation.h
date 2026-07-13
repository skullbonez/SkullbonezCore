/*
File: SkullbonezSource/Runtime/Replay/ReplayPresentation.h
Purpose:
  Defines replay path, camera, and overlay presentation values.

Summary:
  ReplayPresentation owns renderer-facing selection and path-display state; M2 moves definitions only.

Glossary:
  Path target: Stable replay body selected for visualization.

Invariants:
  - ReplayBodyId is identity; ModelRowHint is only a dense-row hint.
  - M2 preserves the moved definition bodies verbatim.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayIdentity.h"
#include "ReplayRecorder.h"
#include "ReplayVisualPacket.h"
#include "../RuntimeCameraMode.h"
#include "../../Assets/AssetKeys.h"
#include "../../Core/Common.h"
#include "../../Physics/PhysicsHandles.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
struct RunReplayPathTarget
{
    ReplayBodyId id;
    Physics::ModelRowHint modelRow;
    char name[64] = {};
};

struct RunReplayPastTrajectoryBuildState
{
    // Concept: retained solver paths are built from the bounded solver ring and
    // then appended as new samples arrive. The eviction counter keeps the store
    // from outliving the recorder window it represents.
    ReplayBodyId targetId;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex builtThroughFrame = 0;
    uint64_t totalFramesEvicted = 0;
    // Structural perf evidence: one selection rebuild is allowed; ordinary
    // live retention must advance through version-stable incremental trims.
    uint64_t fullRebuildCount = 0;
    uint64_t incrementalTrimCount = 0;
    bool valid = false;
};

enum class RunReplayCameraFocusKind
{
    None,
    Body,
    Manifold,
    SolverRow,
    PredictionContact,
    PredictionMotion
};

struct RunReplayCameraState
{
    bool active = false;
    RunCameraMode restoreCameraMode = RunCameraMode::Demo;
    bool hasRestorePose = false;
    bool ownsSimulationPause = false;
    uint32_t restoreCameraHash = CAMERA_FREE;
    Math::Vector::Vector3 restoreEye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 restoreView = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 restoreUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    RunReplayCameraFocusKind focusKind = RunReplayCameraFocusKind::None;
    ReplayBodyId focusedId;
    ReplayBodyId counterpartId;
    int focusedRow = -1;
    Math::Vector::Vector3 targetPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 targetNormal = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 impulseVector = Math::Vector::ZERO_VECTOR;
    float targetRadius = 1.0f;
    RunReplayCauseTreeRowKind focusRowKind = RunReplayCauseTreeRowKind::Body;
    Physics::ModelRowHint focusModelRow;
    Physics::ModelRowHint focusCounterpartModelRow;
    int focusContactIndex = -1;
    int focusSolverRowIndex = -1;
    int focusFeatureId = 0;
    bool focusTerrain = false;
};

struct RunReplayPathVisualizerState
{
    // Concept: the retained/past lane is an operator-visible overlay choice.
    // A selected target remains the authority for *what* could draw; this flag
    // only answers whether the solver-history lane should be emitted this
    // frame.
    bool hasTarget = false;
    bool pastPathVisible = true;
    ReplayBodyId targetId;
    Physics::ModelRowHint targetModelRow;
    char targetName[64] = {};
    std::vector<RunReplayPathTraceNode> futureNodes;
    std::vector<RunReplayPathTarget> targets;
    RunReplayPastTrajectoryBuildState pastTrajectory;
};

} // namespace Runtime
} // namespace SkullbonezCore
