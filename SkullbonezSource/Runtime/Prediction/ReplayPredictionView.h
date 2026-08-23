/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h
Purpose:
  Publishes replay prediction values without exposing prediction-owner state.

Summary:
  ReplayPrediction builds and owns the future simulation; presentation,
  rendering, automation, and validation consume immutable rows, spans, and one
  bounded copied cause-evidence packet.

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
  - Cause-evidence packets own their copied rows and never expose segmented-bank authority.
  - This header contains values only: no mutable owner, service, or callback.

Related:
  - ReplayPrediction.h
  - SkullbonezSource/Runtime/App/ReplayPredictionPresentation.h
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
#include "../../Physics/PhysicsSolverSnapshot.h"
#include "../../Physics/PhysicsStageCapacity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore::Runtime
{
struct ReplayPredictionSceneEntityFact
{
    Physics::PhysicsSceneObjectId id;
    Physics::PhysicsSceneObjectId ragdollRootId;
    bool simpleRagdollPart = false;
};

// Lifetime: App assembles this frame-local span before Prediction publication;
// no Scene owner or record type crosses the Prediction boundary.
struct ReplayPredictionSceneView
{
    std::span<const ReplayPredictionSceneEntityFact> entities;

    int Count() const noexcept
    {
        return static_cast<int>( entities.size() );
    }

    const ReplayPredictionSceneEntityFact* TryGet( int modelIndex ) const noexcept
    {
        return modelIndex >= 0 && modelIndex < Count() ? &entities[static_cast<std::size_t>( modelIndex )] : nullptr;
    }

    int FindBySceneObjectId( Physics::PhysicsSceneObjectId id ) const noexcept
    {
        for ( std::size_t modelIndex = 0; modelIndex < entities.size(); ++modelIndex )
        {
            if ( entities[modelIndex].id.value == id.value )
            {
                return static_cast<int>( modelIndex );
            }
        }

        return -1;
    }
};

struct ReplayPredictionEvidenceIdentity
{
    uint32_t generation = 0;
    ReplayPredictionDetailMode mode = ReplayPredictionDetailMode::High;
    uint64_t bankEpoch = 0;
    ReplayFrameIndex frame = 0;
    uint32_t topologyVersion = 0;
    uint64_t publicationVersion = 0;
};

inline bool operator==( const ReplayPredictionEvidenceIdentity& left,
                        const ReplayPredictionEvidenceIdentity& right ) noexcept
{
    return left.generation == right.generation && left.mode == right.mode && left.bankEpoch == right.bankEpoch &&
           left.frame == right.frame && left.topologyVersion == right.topologyVersion &&
           left.publicationVersion == right.publicationVersion;
}

inline bool operator!=( const ReplayPredictionEvidenceIdentity& left,
                        const ReplayPredictionEvidenceIdentity& right ) noexcept
{
    return !( left == right );
}

struct ReplayPredictionCauseEvidenceQuery
{
    ReplayPredictionEvidenceIdentity identity;
    int contactIndex = -1;
    int pipelineIndex = -1;
    int focusedBody = -1;
    int counterpartBody = -1;
    int featureId = -1;
    bool terrain = false;
    bool sourceHighDetail = false;
};

inline constexpr std::size_t REPLAY_PREDICTION_CAUSE_CONTACT_CAPACITY = 8u;

struct ReplayPredictionCauseEvidencePacket
{
    ReplayPredictionEvidenceIdentity identity;
    ReplayPredictionCauseEvidenceQuery query;
    std::array<Physics::PhysicsSolverPersistentContactSample, REPLAY_PREDICTION_CAUSE_CONTACT_CAPACITY> contacts = {};
    std::array<Physics::PhysicsPipelineRecord, Physics::PHYSICS_MAX_PIPELINE_TRACE_RECORDS> pipeline = {};
    std::size_t contactCount = 0u;
    std::size_t pipelineCount = 0u;
    int selectedContactRow = -1;
    int bodyA = -1;
    int bodyB = -1;
    bool terrain = false;
    bool available = false;

    std::span<const Physics::PhysicsSolverPersistentContactSample> ContactRows() const noexcept
    {
        return { contacts.data(), contactCount };
    }

    std::span<const Physics::PhysicsPipelineRecord> PipelineRows() const noexcept
    {
        return { pipeline.data(), pipelineCount };
    }
};

struct RunReplayPredictionBodySample
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR; // m/s-equivalent simulation units.
    bool sleeping = false; // Solver sleep state used by whole-cascade outcome validation.
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
    uint32_t generation = 0; // Successful private-simulation generation owning this published prefix.
    uint32_t topologyVersion = 0;
    uint32_t trajectoryBuildTopologyVersion = 0;
    uint64_t trajectoryPublicationVersion = 0; // O(1) invalidation token for retained trajectory draw lists.
    std::size_t trajectoryBuiltNodeCount = 0;
    std::size_t trajectoryChildFrameCount = 0;
    ReplayPredictionBuildMode buildMode = ReplayPredictionBuildMode::Undecided;
    ReplayPredictionDetailMode detailMode = ReplayPredictionDetailMode::High;
    ReplayPredictionArchiveDetailCapability archiveDetailCapability = ReplayPredictionArchiveDetailCapability::Low;
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
