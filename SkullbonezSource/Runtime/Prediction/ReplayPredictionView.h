/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h
Purpose:
  Publishes replay prediction values without exposing prediction-owner state.

Summary:
  ReplayPrediction builds and owns the future simulation; presentation,
  rendering, automation, and validation consume these immutable rows and spans.

Glossary:
  Prediction frame: One future fixed-step sample and its optional contact evidence.
  Presentation view: Never-stored spans over the prediction prefix currently
    safe for readers.
  Past trajectory view: Immutable cursor describing presentation-owned retained
    path progress to the prediction owner.
  All-body paths: Space-scene mode where each simulated body's future is visible
    whether or not it participates in a causal contact edge.

Invariants:
  - Physics::PhysicsSceneObjectId is durable identity; ModelRowHint is only a staleable lookup hint.
  - View spans borrow prediction storage and must not survive owner mutation.
  - This header contains values only: no mutable owner, service, or callback.

Related:
  - ReplayPrediction.h
  - ReplayPredictionPresentation.h
  - ReplayVisualPacket.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Replay/ReplayIdentity.h"
#include "../Replay/ReplayPathPackets.h"
#include "ReplayPredictionPackets.h"
#include "../Replay/ReplayVisualPacket.h"
#include "../../Maths/Quaternion.h"
#include "../../Physics/PhysicsDebugData.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore::Runtime
{
struct RunReplayPredictionBodySample
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR; // m/s-equivalent simulation units.
    bool sleeping = false;                                            // Solver sleep state used by whole-cascade outcome validation.
};

struct RunReplayPredictionFrame
{
    // Concept: body samples are authoritative for the root trajectory, while
    // debugContacts are optional evidence for the contact-derived cause tree.
    // contactsIncomplete means the frame stayed usable after contact scratch
    // reserve failed, so UI/reporting can label the tree as partial.
    ReplayFrameIndex frameIndex = 0;
    double simulationSeconds = 0.0;
    float tornadoSystemElapsedSeconds = 0.0f;
    std::vector<RunReplayPredictionBodySample> bodies;
    std::vector<Physics::PhysicsDebugContact> debugContacts;
    bool contactsIncomplete = false;
};

struct ReplayPredictionBaselineBodyPose
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow;
    bool hasEntryPose = false;
    bool hasRestPose = false;
    Math::Vector::Vector3 entryPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion entryOrientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 restPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion restOrientation = Math::Orientation::IDENTITY_QUATERNION;
};

struct ReplayVelocityDragPreviewView
{
    Physics::PhysicsSceneObjectId targetId;
    Math::Vector::Vector3 velocityDelta = Math::Vector::ZERO_VECTOR;
    bool active = false;
};

struct ReplayPredictionPresentationView
{
    std::span<const RunReplayPredictionFrame> frames;
    std::span<const RunReplayPathTraceNode> futureNodes;
    std::span<const ReplayTrajectoryRecord> trajectoryRecords;
    std::span<const ReplayPredictionRetainedMarker> retainedMarkers;
    std::span<const ReplayPredictionBaselineBodyPose> baselineBodyPoses;
    ReplayVelocityDragPreviewView velocityDragPreview;
    Physics::PhysicsSceneObjectId targetId;
    Physics::PhysicsSceneObjectId baselineRootId;
    Physics::PhysicsSceneObjectId trajectoryBuildRootId;
    ReplayFrameIndex sourceFrame = 0;
    ReplayFrameIndex revealFrame = 0;
    uint32_t generation = 0;                                          // Successful private-simulation generation owning this published prefix.
    uint32_t topologyVersion = 0;
    uint32_t trajectoryBuildTopologyVersion = 0;
    uint64_t trajectoryPublicationVersion = 0;                        // O(1) invalidation token for retained trajectory draw lists.
    std::size_t trajectoryBuiltNodeCount = 0;
    std::size_t trajectoryChildFrameCount = 0;
    ReplayPredictionBuildMode buildMode = ReplayPredictionBuildMode::Undecided;
    ReplayPredictionDetailMode detailMode = ReplayPredictionDetailMode::High;
    ReplayPredictionPathPresentation pathPresentation = ReplayPredictionPathPresentation::SelectedCausalTree;
    float horizonSeconds = 0.0f;
    double revealSecondsPerSecond = 1.0;
    double measuredTicksPerMs = 0.0;
    double lastBuildWallMs = 0.0;
    bool enabled = false;
    bool building = false;
    bool complete = false;
    bool usingBuildFrames = false;
    bool futureNodesCacheValid = false;
    bool trajectoryBuildValid = false;
    bool trajectoryBuildUsingBuildFrames = false;
    bool futureTreeReady = false;
    bool ragdollVisualsEnabled = false;
    bool baselineValid = false;
    bool baselineComparisonActive = false;
    bool deterministicRevealEnabled = false;
    bool generationPermitted = true;
};

} // namespace SkullbonezCore::Runtime
