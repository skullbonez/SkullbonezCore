/*
File: SkullbonezSource/Runtime/Prediction/ReplayPrediction.h
Purpose:
  Owns private replay prediction build, publication, and trajectory state.

Summary:
  ReplayPrediction simulates an isolated future and publishes completed prefixes
  while readers consume a never-stored presentation view.
  Trajectory views expose only the store's active, coherently published prefix;
  a completed build bank remains visible while its committed duplicate resumes.
Invariants:
  - Worker publication retains the release/acquire prefix protocol.
  - Presentation consumers cannot observe rows beyond the prepared prefix.
  - Prediction owns its private engine and never mutates live physics stores.
  - Cancellation waits for an in-flight worker slice before clearing state.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Replay/ReplayIdentity.h"
#include "ReplayPredictionPublication.h"
#include "ReplayPredictionPresentation.h"
#include "ReplayPredictionView.h"
#include "ReplayPredictionScheduling.h"
#include "../Replay/ReplayRecorder.h"
#include "../Replay/ReplayVisualPacket.h"
#include "TrajectoryStore.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Maths/Quaternion.h"
#include "../../Physics/PhysicsWorldForces.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <span>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class Profiler;
class SbDiagnosticStore;
} // namespace Core
namespace Physics
{
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
} // namespace Physics
namespace Rendering
{
struct RenderInstancePresentationRecord;
}
namespace Threading
{
class WorkerPool;
}

namespace Runtime
{
class SceneEntityStore;
class ReplayAuthoring;
class ReplayPrediction;
class ReplayPresentation;
class ReplayScrubber;
class ReplaySolverRecorder;
class RuntimeInteractionController;
struct ReplayAuthoringPredictionRequest;
struct RunReplayCameraState;
struct RunReplayPathVisualizerState;

struct RunReplayPredictionBodyBackup
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;
    float inverseMass = 0.0f;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 inverseRotationalInertia = Math::Vector::ZERO_VECTOR;
    bool fixed = false;
};

struct ReplayPredictionBaselineRootPoint
{
    ReplayFrameIndex frameIndex = 0;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
};

struct ReplayPredictionBaselineSnapshot
{
    bool valid = false;
    bool comparisonActive = false;
    Physics::PhysicsSceneObjectId rootId;
    Physics::ModelRowHint rootModelRow;
    ReplayFrameIndex lastFrame = 0;

    // Runtime allocation policy: baseline vectors are captured only while replay
    // prediction is active, reserved under replay_prediction_working_set, and
    // bounded to one sampled root line plus one entry/rest pose per model.
    std::vector<ReplayPredictionBaselineRootPoint> rootPolyline;
    std::vector<ReplayPredictionBaselineBodyPose> bodyPoses;
    bool divergenceValid = false;
    float divergenceUnits = 0.0f;
};

struct RunReplayPredictionRevealClock
{
    // Concept: reveal anchor is the wall-clock start of the causal-unfold
    // animation. The overlay clamps drawn prediction frames to a cursor derived
    // from this anchor, so this rate — not the build — decides how long the
    // operator waits to see a finished path.
    // Why the default is not real time: at 1.0 the wait equalled the horizon, so
    // a 120-second prediction spent two minutes drawing work that had already
    // finished computing, and the delay read as slow simulation. Ordinary
    // predict-and-look now resolves within a frame or two, while authored
    // director phases and the reveal-speed transport command still slow it back
    // down for a deliberate unfold.
    // Invariant: overlay pacing never feeds physics, replay samples, or solver
    // restores, so steady_clock here cannot affect deterministic simulation.
    double secondsPerSecond = 1000.0;        // Causal-unfold speed; 1.0 = real-time, large = effectively immediate.
    std::chrono::steady_clock::time_point anchor = {};
    ReplayFrameIndex presentedFrame = 0;     // Last common reveal clamp consumed by replay presentation.
    ReplayFrameIndex deterministicFrame = 0; // Automation-owned cursor; ignored outside fidelity capture.
    bool deterministicFrameEnabled = false;
    bool anchorValid = false;
};

// Concept: each causal child owns a persistent suffix cursor and the two-box
// marker facts derived before that cursor. The entry pose is the body's frame-0
// in-place pose; lastMotionFrame determines when the later rest marker may
// appear. Advancing one child can never mutate another child's result.
struct ReplayPredictionChildMarkerNodeScanState
{
    RunReplayPathTraceNode node;
    std::size_t scannedFrameCount = 0;
    bool active = false;
    bool hasEntryPose = false;
    int entryModelIndex = -1;
    ReplayFrameIndex lastMotionFrame = 0;
    Math::Vector::Vector3 entryPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion entryOrientation = Math::Orientation::IDENTITY_QUATERNION;

    // Accumulates one body observation. Callers own frame filtering and pass
    // the immutable frame-0 sample that anchors a newly activated entry pose.
    void ObserveBody( ReplayFrameIndex frame, const RunReplayPredictionBodySample& body,
                      const RunReplayPredictionBodySample& initialSample, bool visibleMotion ) noexcept
    {
        if ( !active )
        {
            if ( !visibleMotion )
            {
                return;
            }

            active = true;
            hasEntryPose = true;
            entryModelIndex = body.modelRow.value;
            entryPosition = initialSample.position;
            entryOrientation = initialSample.orientation;
            entryOrientation.Normalise();
            lastMotionFrame = frame;
            return;
        }

        if ( visibleMotion )
        {
            lastMotionFrame = frame;
        }
    }
};

struct ReplayPredictionChildMarkerScanState
{
    // Invariant: nodes describe one generation/source identity. Stable topology
    // preserves each node's suffix cursor, while a changed node is reset before
    // any frame from its new meaning is observed. TestReplayVisualPacket.cpp
    // locks both halves of this state transition.
    uint32_t generation = 0;
    uint32_t topologyVersion = 0;
    std::size_t frameCount = 0;
    std::size_t nodeCount = 0;
    ReplayFrameIndex revealFrame = 0;
    Physics::PhysicsSceneObjectId targetId;
    std::array<ReplayPredictionChildMarkerNodeScanState, REPLAY_VISUAL_FUTURE_NODE_CAPACITY> nodes = {};
    bool usingBuildFrames = false;
    bool initialized = false;
    bool valid = false;

    // Preserves derived facts only when the caller supplies the same topology
    // row at the same stable index; returns false after initializing a new row.
    bool PreserveOrResetNode( std::size_t index, std::size_t previousNodeCount,
                              const RunReplayPathTraceNode& candidate ) noexcept
    {
        ReplayPredictionChildMarkerNodeScanState& state = nodes[index];
        const RunReplayPathTraceNode& retained = state.node;
        const bool unchanged = index < previousNodeCount && retained.id.value == candidate.id.value &&
                               retained.parentId.value == candidate.parentId.value &&
                               retained.modelRow.value == candidate.modelRow.value &&
                               retained.parentModelRow.value == candidate.parentModelRow.value &&
                               retained.firstFrame == candidate.firstFrame && retained.depth == candidate.depth &&
                               retained.contactDerived == candidate.contactDerived;

        if ( unchanged )
        {
            return true;
        }

        state = ReplayPredictionChildMarkerNodeScanState {};
        state.node = candidate;
        return false;
    }

    // Invariant: retained causal markers already hold the scan's effects. An
    // exact key match means repeating the frame-by-node walk cannot publish any
    // new entry/rest pose and is therefore pure wasted presentation work.
    bool Matches( uint32_t candidateGeneration, uint32_t candidateTopologyVersion,
                  Physics::PhysicsSceneObjectId candidateTargetId, std::size_t candidateFrameCount,
                  ReplayFrameIndex candidateRevealFrame, bool candidateUsingBuildFrames ) const noexcept
    {
        return valid && generation == candidateGeneration && topologyVersion == candidateTopologyVersion &&
               targetId.value == candidateTargetId.value && frameCount == candidateFrameCount &&
               revealFrame == candidateRevealFrame && usingBuildFrames == candidateUsingBuildFrames;
    }

    void Commit( uint32_t candidateGeneration, uint32_t candidateTopologyVersion,
                 Physics::PhysicsSceneObjectId candidateTargetId, std::size_t candidateFrameCount,
                 ReplayFrameIndex candidateRevealFrame, bool candidateUsingBuildFrames ) noexcept
    {
        generation = candidateGeneration;
        topologyVersion = candidateTopologyVersion;
        targetId = candidateTargetId;
        frameCount = candidateFrameCount;
        revealFrame = candidateRevealFrame;
        usingBuildFrames = candidateUsingBuildFrames;
        initialized = true;
        valid = true;
    }

    void Reset() noexcept
    {
        generation = 0;
        topologyVersion = 0;
        frameCount = 0;
        nodeCount = 0;
        revealFrame = 0;
        targetId = {};
        usingBuildFrames = false;
        initialized = false;
        valid = false;
    }
};

struct RunReplayPredictionFutureNodeCache
{
    // Concept: future-node cache is render-facing topology derived from
    // prediction frames. Build work writes the scratch vector and cursor fields;
    // draw code reads futureNodes only after a coherent prefix is published.
    std::vector<RunReplayPathTraceNode> futureNodes;
    std::vector<RunReplayPathTraceNode> futureNodeBuildScratch;
    std::size_t futureNodesBuiltFrameCount = 0;
    std::size_t futureNodesBuiltContactIndex = 0;

    // Concept: the contact scan above has futureNodesBuiltFrameCount as its
    // resume cursor; the sparse-contact affected-body pass needs the same or it
    // restarts from body zero on every frame. Without it a completed prediction
    // rescans every body against every frame forever, which both burns the
    // overlay budget on a static horizon and makes affected-body trails appear a
    // few at a time instead of at once.
    // Invariant: the watermark is the frameCount the pass last completed
    // against. A growing horizon can activate a body that had not moved yet, so
    // growth reopens the pass rather than trusting the old answer.
    std::size_t futureNodesAffectedBodyCursor = 0;
    std::size_t futureNodesAffectedFrameCount = 0;
    bool futureNodesAffectedComplete = false;
    Physics::PhysicsSceneObjectId futureNodesBuiltTargetId;

    // Invariant: topologyVersion identifies the published node set/order and
    // firstFrame values. The next counter survives cache clears so a same-root
    // rebuild cannot masquerade as an older child trajectory version.
    uint32_t futureNodesTopologyVersion = 0;
    uint32_t nextFutureNodesTopologyVersion = 1;
    bool futureNodesBuiltRagdollVisuals = false;
    bool futureNodesBuiltFromBuildFrames = false;
    bool futureNodesCacheValid = false;

    // Invariant: once a causal yellow or grey box has been revealed, budgeted
    // line scans may not make it disappear. This fixed cache redraws retained
    // marker poses until a new prediction/future cache resets the story.
    std::array<ReplayPredictionRetainedMarker, REPLAY_PREDICTION_MARKER_CAPACITY> retainedMarkers = {};
    std::size_t retainedMarkerCount = 0;
    ReplayPredictionChildMarkerScanState childMarkerScan;

    void ResetRetainedMarkers() noexcept
    {
        retainedMarkerCount = 0;
        childMarkerScan.Reset();
    }
};

struct RunReplayPredictionTrajectoryBuildState
{
    // Concept: prediction trajectory records follow the same published-prefix
    // contract as buildFrames. Root points are appended when frames publish;
    // child records catch up after the future-node cache publishes topology.
    Physics::PhysicsSceneObjectId rootId;
    bool usingBuildFrames = false;
    std::size_t rootFrameCount = 0;
    std::size_t childFrameCount = 0;
    std::size_t builtNodeCount = 0;

    // Concept: mutual-gravity scenes publish every body's future independently
    // of contact causality. These cursors extend that record bank without
    // disturbing the causal child publication used by ordinary scenes.
    std::size_t allBodyFrameCount = 0;
    std::size_t builtAllBodyCount = 0;

    // Invariant: child trajectory records are drawable only when this version
    // matches the future-node cache version that selected their branch ordinals.
    uint32_t topologyVersion = 0;
    bool allBodyPaths = false;
    bool valid = false;
};

struct ReplayPredictionCommittedPublicationState
{
    // Concept: worker completion leaves the finished build trajectory visible
    // while the committed duplicate catches up under the overlay budget. This
    // snapshot owns the visible-bank facts that trajectoryBuild must relinquish
    // to become the resumable committed-bank cursor.
    RunReplayPredictionTrajectoryBuildState visibleTrajectoryBuild;
    uint32_t generation = 0;
    std::size_t sourceFrameCount = 0;
    bool pending = false;

    void Begin( const RunReplayPredictionTrajectoryBuildState& visibleBuild, uint32_t owningGeneration,
                std::size_t owningSourceFrameCount ) noexcept
    {
        visibleTrajectoryBuild = visibleBuild;
        generation = owningGeneration;
        sourceFrameCount = owningSourceFrameCount;
        pending = true;
    }

    void Reset() noexcept
    {
        visibleTrajectoryBuild = {};
        generation = 0;
        sourceFrameCount = 0;
        pending = false;
    }
};

struct RunReplayPredictionBuildState
{
    bool dirty = true;
    uint32_t generationBeginCount = 0;       // Successful future-simulation generations in this process.

    // Concept: dirty requests do not form a queue. While a worker job is in
    // flight, this bit remembers only that the newest source state needs one
    // replacement build after completion.
    bool pendingLatestRestart = false;

    uint32_t supersededRestartCount = 0;
    uint32_t latestRestartBeginCount = 0;
    bool building = false;
    bool complete = false;
    ReplayPredictionBuildMode buildMode = ReplayPredictionBuildMode::Undecided;
    int nextTick = 1;
    int targetTickCount = 0;
    double lastBuildTime = 0.0;
    double lastBuildWallMs = 0.0;
    double instantBudgetMs = 0.0;
    int probeTickBudget = 8;
    std::chrono::steady_clock::time_point jobStart = {};

    // Runtime allocation policy: prediction buildFrames can be pre-sized for a
    // whole horizon while publication exposes only completed rows. Render reads
    // frames, not the pre-sized build vector, until completion swaps them.
    // Invariant: during a same-target refresh, the building prefix may replace
    // committed frames only after it reaches the reveal cursor captured at job
    // start. This prevents auto-refresh from replaying the causal unfold from
    // frame zero.
    std::vector<RunReplayPredictionFrame> buildFrames;
    std::size_t buildPresentationFrameCount = 2u;
    ReplayPredictionWorkerSchedule schedule;
    ReplayPredictionPublication publication;
    ReplayPredictionPresentationPublication presentationPublication;
};

// Concept: this owner contains the private engine and every mutable value used
// by one isolated future. No live Physics store is reachable through it.
struct ReplayPredictionIsolatedSimulation
{
    ~ReplayPredictionIsolatedSimulation();

    float horizonSeconds = REPLAY_FUTURE_DEFAULT_SECONDS;
    Physics::ModelRowHint targetModelRow;
    Physics::PhysicsSceneObjectId targetId;
    ReplayFrameIndex sourceFrameIndex = 0;
    uint64_t sourceSolverHash = 0;
    double sourceSimulationSeconds = 0.0;

    // Invariant: the worker is the sole writer of probe accumulators and
    // release-publishes measuredTicksPerMs. The frame thread acquire-loads it
    // before choosing a build mode. Same-source velocity restarts retain the
    // calibration; scene/branch/body-count changes reset it.
    std::atomic<double> measuredTicksPerMs { 0.0 };
    double probeElapsedMs = 0.0;
    int probeTicksCompleted = 0;
    int calibratedModelCount = -1;

    // Concept: prediction simulates the future in its own engine. Live stores
    // are never written by prediction, so replay preview state stays isolated.
    // Lifetime: constructed lazily on first prediction begin under the replay
    // reserve owner, seeded through the explicit concrete-owner PhysicsEngine
    // prediction seam, and reused so startup/perf-smoke memory stays flat.
    // Runtime allocation policy: owner replay_prediction_working_set; reason:
    // private prediction needs bounded isolated physics storage for exploration;
    // deletion condition: none, this is the end-state isolation boundary;
    // checker budget: 256 MB hard cap registered by ReplayPredictionReserveOwner().
    std::unique_ptr<Physics::PhysicsEngine> predictionEngine;
    int predictionEngineReserveBytes = 0;    // Monotonic approved byte budget retained with predictionEngine.
    Gameplay::TornadoGameplay predictionTornadoGameplay;
    Physics::PhysicsWorldForces predictionWorldForces;
    bool predictionEngineReady = false;
    ReplaySolverWorldSnapshot predictionWorld;
    std::vector<RunReplayPredictionBodyBackup> predictionBodies;

    // Concept: frames is a retained allocation bank; committedFrameCount is
    // the published prefix. Invalidating prediction must hide that prefix in
    // O(1) without destructing every frame's nested body/contact capacities.
    // Invariant: readers never infer publication from frames.size().
    std::vector<RunReplayPredictionFrame> frames;
    std::size_t committedFrameCount = 0;
};

struct ReplayVelocityDragPreviewState
{
    // Invariant: pointer samples replace this fixed-size value without
    // scheduling simulation. The release edge arms exactly one later
    // generation, and no earlier completion may clear its visible estimate.
    Physics::PhysicsSceneObjectId targetId;
    Math::Vector::Vector3 velocityDelta = Math::Vector::ZERO_VECTOR;
    uint32_t replacementGeneration = 0;
    bool active = false;
    bool awaitingAuthoritativeReplacement = false;

    void Update( Physics::PhysicsSceneObjectId id, const Math::Vector::Vector3& delta ) noexcept
    {
        targetId = id;
        velocityDelta = delta;
        active = id.value != 0;
        awaitingAuthoritativeReplacement = false;
        replacementGeneration = 0;
    }

    bool Finish( uint32_t nextGeneration ) noexcept
    {
        if ( !active )
        {
            return false;
        }

        awaitingAuthoritativeReplacement = true;
        replacementGeneration = nextGeneration;
        return true;
    }

    bool ClearAfterGeneration( uint32_t completedGeneration ) noexcept
    {
        if ( !active || !awaitingAuthoritativeReplacement || completedGeneration < replacementGeneration )
        {
            return false;
        }

        Clear();
        return true;
    }

    void Clear() noexcept
    {
        targetId = {};
        velocityDelta = Math::Vector::ZERO_VECTOR;
        replacementGeneration = 0;
        active = false;
        awaitingAuthoritativeReplacement = false;
    }
};

struct RunReplayPredictionState
{
    RunReplayPredictionState();
    ~RunReplayPredictionState();
    RunReplayPredictionState( const RunReplayPredictionState& ) = delete;
    RunReplayPredictionState& operator=( const RunReplayPredictionState& ) = delete;
    RunReplayPredictionState( RunReplayPredictionState&& ) noexcept = delete;
    RunReplayPredictionState& operator=( RunReplayPredictionState&& ) noexcept = delete;

    // Concept: prediction builders fill buildFrames first, then publish a
    // prefix count. Readers must ask these helpers for the visible range so a
    // worker-owned build can tighten ownership without changing every overlay.
    std::size_t PublishedBuildFrameCount() const noexcept;
    bool HasPublishedBuildFramePrefix( std::size_t minFrameCount = 2u ) const noexcept;
    std::size_t CommittedFrameCount() const noexcept;
    std::span<const RunReplayPredictionFrame> CommittedFrames() const noexcept;
    bool HasCommittedFramePrefix( std::size_t minFrameCount = 2u ) const noexcept;
    static void InvalidateCommittedFrameBank( std::size_t& committedFrameCount ) noexcept;
    static void PromoteFrameBanks( std::vector<RunReplayPredictionFrame>& committedFrames, std::size_t& committedFrameCount,
                                   std::vector<RunReplayPredictionFrame>& completedBuildFrames,
                                   std::size_t frameCount ) noexcept;
    void InvalidateCommittedFrames() noexcept;
    void PromoteBuildFramesToCommitted( std::size_t frameCount ) noexcept;
    bool BuildPrefixShouldBePresented() const noexcept;
    bool BuildPrefixHasBeenPresented() const noexcept;
    bool BuildFramesAreComplete() const noexcept;
    bool FutureTreeReadyForDraw( Physics::PhysicsSceneObjectId rootId, bool usingBuildFrames,
                                 std::size_t frameCount ) const noexcept;
    bool FutureTreeReadyForDraw( const RunReplayPredictionTrajectoryBuildState& trajectory,
                                 Physics::PhysicsSceneObjectId rootId, bool usingBuildFrames,
                                 std::size_t frameCount ) const noexcept;
    void ResetBuildFramePublication() noexcept;
    void PublishBuildFrameSlot( std::size_t frameSlot ) noexcept;

    bool enabled = false;
    bool ragdollVisualsEnabled = false;
    RunReplayPredictionBuildState build;
    ReplayPredictionIsolatedSimulation simulation;
    RunReplayPredictionFutureNodeCache futureNodeCache;

    // Concept: trajectory records are the publication layer between
    // prediction/solver builders and overlay drawing. Main root/child ribbons
    // read these records; auxiliary marker/ragdoll paths keep using frame data
    // when they need orientation or velocity, not just trajectory points.
    ReplayTrajectoryStore trajectoryStore;
    RunReplayPredictionTrajectoryBuildState trajectoryBuild;
    ReplayPredictionCommittedPublicationState committedPublication;

    // Concept: the butterfly baseline is a retained presentation snapshot of
    // the pre-nudge future. It is intentionally smaller than the committed
    // simulation frame list: one cold root polyline, two poses per affected
    // body, and one divergence number, so the warm current prediction can
    // unfold over it.
    ReplayPredictionBaselineSnapshot baseline;
    ReplayVelocityDragPreviewState velocityDragPreview;
    RunReplayPredictionRevealClock revealClock;
};

enum class ReplayPredictionSourcePreparation : uint8_t
{
    Declined,
    ClearCommitted,
    PreserveCommitted
};

enum class ReplayPredictionFrameSourceAction : uint8_t
{
    Stop,
    Continue,
    Begin
};

// Value-only effects emitted by prediction update/preparation for the
// composition root to apply to the scrubber and presentation owners. Keeping
// these counters out of callbacks prevents the private owner from reaching
// back through ReplayRuntime while it advances a worker or cache publication.
struct ReplayPredictionUpdateResult
{
    std::array<uint32_t, SkullbonezCore::Core::MAIN_MEMORY_REPLAY_BUDGET_PASS_COUNT> budgetExpiries = {};
    std::array<uint32_t, SkullbonezCore::Core::MAIN_MEMORY_REPLAY_REBUILD_CAUSE_COUNT> rebuildCauses = {};
    Physics::ModelRowHint repairedTargetModelRow;
    bool targetModelRowRepaired = false;
    bool pinSolverScrubberToPresent = false;
};

// Value boundary for the trajectory store's presentation-owned cursor. The
// prediction owner mutates its store, then publishes only the repaired row and
// retention window metadata that ReplayPresentation must retain.
struct ReplayPastTrajectoryUpdate
{
    Physics::PhysicsSceneObjectId targetId;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex builtThroughFrame = 0;
    uint64_t totalFramesEvicted = 0;
    uint64_t fullRebuildCount = 0;
    uint64_t incrementalTrimCount = 0;
    Physics::ModelRowHint targetModelRow;
    bool apply = false;
    bool targetModelRowRepaired = false;
    bool valid = false;
};

struct ReplayPredictionMemoryStats
{
    SkullbonezCore::Core::MainMemoryReplayCategoryBytes categoryBytes;
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats trajectory;
    std::size_t frameCount = 0;
    std::size_t futureNodeCount = 0;
};

class ReplayPrediction
{
  public:
    ReplayPrediction( Core::SbDiagnosticStore& resultDiagnostics, Core::Profiler* profiler = nullptr )
        : m_profiler( profiler ), m_presentation( resultDiagnostics, profiler )
    {
    }

    const RunReplayPredictionState& State() const noexcept
    {
        return m_state;
    }

    std::span<const RunReplayPredictionFrame> ActiveFrames() const noexcept
    {
        if ( m_state.BuildFramesAreComplete() )
        {
            return m_state.build.buildFrames;
        }

        return m_state.CommittedFrames();
    }

    ReplayPredictionPresentationView PresentationView() const noexcept
    {
        ReplayPredictionPresentationView view;
        const bool presentingBuildPrefix = m_state.BuildPrefixShouldBePresented();
        view.usingBuildFrames = presentingBuildPrefix || m_state.committedPublication.pending;

        if ( presentingBuildPrefix )
        {
            const std::size_t presentedFrameCount = m_state.build.presentationPublication
                                                        .PresentedCount( m_state.PublishedBuildFrameCount(),
                                                                         m_state.build.buildFrames.size() );
            view.frames = { m_state.build.buildFrames.data(), presentedFrameCount };
        }
        else
        {
            view.frames = m_state.CommittedFrames();
        }

        view.futureNodes = m_state.futureNodeCache.futureNodes;
        view.trajectoryRecords = m_state.trajectoryStore.ActiveRecords();
        view.retainedMarkers = { m_state.futureNodeCache.retainedMarkers.data(),
                                 m_state.futureNodeCache.retainedMarkerCount };
        view.baselineBodyPoses = m_state.baseline.bodyPoses;
        view.velocityDragPreview.targetId = m_state.velocityDragPreview.targetId;
        view.velocityDragPreview.velocityDelta = m_state.velocityDragPreview.velocityDelta;
        view.velocityDragPreview.active = m_state.velocityDragPreview.active;
        view.targetId = m_state.simulation.targetId;
        view.baselineRootId = m_state.baseline.rootId;
        const RunReplayPredictionTrajectoryBuildState& presentedTrajectory = m_state.committedPublication.pending
                                                                                  ? m_state.committedPublication
                                                                                        .visibleTrajectoryBuild
                                                                                  : m_state.trajectoryBuild;
        view.trajectoryBuildRootId = presentedTrajectory.rootId;
        view.sourceFrame = m_state.simulation.sourceFrameIndex;
        view.revealFrame = m_state.revealClock.presentedFrame;
        view.generation = m_state.build.generationBeginCount;
        view.topologyVersion = m_state.futureNodeCache.futureNodesTopologyVersion;
        view.trajectoryBuildTopologyVersion = presentedTrajectory.topologyVersion;
        view.trajectoryPublicationVersion = m_state.trajectoryStore.publicationVersion;
        view.trajectoryBuiltNodeCount = presentedTrajectory.builtNodeCount;
        view.trajectoryChildFrameCount = presentedTrajectory.childFrameCount;
        view.buildMode = m_state.build.buildMode;
        view.horizonSeconds = m_state.simulation.horizonSeconds;
        view.revealSecondsPerSecond = m_state.revealClock.secondsPerSecond;
        view.measuredTicksPerMs = m_state.simulation.measuredTicksPerMs.load( std::memory_order_acquire );
        view.lastBuildWallMs = m_state.build.lastBuildWallMs;
        view.enabled = m_state.enabled;
        view.building = m_state.build.building;
        view.complete = m_state.build.complete;
        view.futureNodesCacheValid = m_state.futureNodeCache.futureNodesCacheValid;
        view.trajectoryBuildValid = presentedTrajectory.valid;
        view.trajectoryBuildUsingBuildFrames = presentedTrajectory.usingBuildFrames;
        view.futureTreeReady = m_state.FutureTreeReadyForDraw( presentedTrajectory, view.targetId,
                                                               view.usingBuildFrames, view.frames.size() );
        view.showAllFuturePaths = presentedTrajectory.allBodyPaths;
        view.ragdollVisualsEnabled = m_state.ragdollVisualsEnabled;
        view.baselineValid = m_state.baseline.valid;
        view.baselineComparisonActive = m_state.baseline.comparisonActive;
        view.deterministicRevealEnabled = m_state.revealClock.deterministicFrameEnabled;
        view.generationPermitted = m_generationPermitted;
        return view;
    }

    ReplayPredictionPresentation& PresentationOwner() noexcept
    {
        return m_presentation;
    }

    const ReplayPredictionPresentation& PresentationOwner() const noexcept
    {
        return m_presentation;
    }

    // Prediction owns cause-row composition that combines its future publication with lower
    // Replay samples. ReplayAuthoring supplies only bounded row storage and
    // window state; no lower header names Prediction state.
    bool BuildCauseTreeRows( ReplayAuthoring& authoring, const RunReplayPathVisualizerState& path,
                             const ReplaySolverFrameSample* solverSample,
                             std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                             const Physics::PhysicsBodyStore& bodyStore, const RunReplayCameraState& camera,
                             int& outCameraFocusedRow );
    bool ActivateCauseTreeRow( ReplayAuthoring& authoring, int rowIndex, ReplayPresentation& presentationOwner,
                               ReplayScrubber& scrubberOwner, const ReplaySolverFrameSample* currentSolverSample,
                               const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                               RuntimeInteractionController& interaction, Math::Vector::Vector3& outTargetPosition,
                               float& outTargetRadius );

    bool ToggleEnabled() noexcept
    {
        m_state.enabled = !m_state.enabled;
        return m_state.enabled;
    }
    bool BuildPrefixShouldBePresented() const noexcept
    {
        return m_state.BuildPrefixShouldBePresented();
    }
    bool ToggleRagdollVisualsEnabled() noexcept
    {
        m_state.ragdollVisualsEnabled = !m_state.ragdollVisualsEnabled;
        return m_state.ragdollVisualsEnabled;
    }
    void ClampHorizonSeconds( float minSeconds, float maxSeconds ) noexcept
    {
        m_state.simulation.horizonSeconds = std::clamp( m_state.simulation.horizonSeconds, minSeconds, maxSeconds );
    }

    bool GenerationPermitted() const noexcept
    {
        return m_generationPermitted;
    }
    Core::Profiler* ProfilerBorrow() const noexcept
    {
        return m_profiler;
    }
    void SetGenerationPermitted( bool permitted ) noexcept
    {
        m_generationPermitted = permitted;
    }
    void ForbidGeneration() noexcept
    {
        m_generationPermitted = false;
    }
    void ClearFutureNodeCache();
    void WaitForJobIdle();
    bool PromoteBuildPrefixToCommitted();
    void CancelJob( bool clearSamples );
    void ClearCache();
    // Profiles Predict-off invalidation while preserving the generic cold/reset path.
    void ClearCacheFromReplayInput();
    void MarkDirty() noexcept;
    void EnterOfflineVerification();
    void ResetVerificationMarkers() noexcept;
    void SetVerificationRevealFrame( ReplayFrameIndex frame ) noexcept;

    // Owner commands used by validation and UI paths. These keep rebuild and
    // baseline invalidation coupled to the state transition that requires it.
    void SetEnabled( bool enabled ) noexcept;
    void ApplyAuthoringRequest( const ReplayAuthoringPredictionRequest& request, float minHorizonSeconds,
                                float maxHorizonSeconds );
    void DisableAndClearCache();

    // An explicit transition back to live time freezes the visible committed
    // prefix and cancels any worker; unlike an authored enable toggle, that
    // transition must not request a rebuild.
    void DisableForLiveAdvance() noexcept
    {
        m_state.enabled = false;
    }
    void SetHorizonSeconds( float horizonSeconds ) noexcept;
    bool RevealProgress01( float& outProgress ) const noexcept;
    void SetRevealRatePreservingCursor( double revealRate ) noexcept;
    bool PrepareVelocityMutationBaseline() noexcept;
    void CommitVelocityMutation() noexcept;
    bool ReadyForDeterministicReveal() const noexcept;
    void ArmDeterministicReveal( ReplayFrameIndex frame, bool resetPresentedFrame ) noexcept;
    int RunWorkerRange( const SkullbonezCore::Core::EngineConfig& config, Threading::WorkerPool& workerPool, int modelCount,
                        int beginTickIndex, int endTickIndex );
    ReplayPredictionFrameSourceAction SelectFrameSource( const ReplaySolverFrameSample* latestSolverSample,
                                                         Physics::PhysicsSceneObjectId targetId, bool targetAvailable,
                                                         bool liveAdvanceHeld, double simulationTotalSeconds,
                                                         bool& outWasDirty, bool& outWasPendingLatestRestart );
    void PrepareFrameRebuild( Physics::PhysicsSceneObjectId targetId, Physics::ModelRowHint targetModelRow,
                              ReplayPredictionUpdateResult& result );
    ReplayPredictionSourcePreparation
    BeginFrameSource( Physics::PhysicsEngine& physicsEngine, const SkullbonezCore::Core::EngineConfig& config,
                      bool scenePhysics, double fallbackSourceSimulationSeconds, double simulationTotalSeconds,
                      const ReplaySolverFrameSample* latestSolverSample, Physics::PhysicsSceneObjectId requestedTargetId,
                      Physics::ModelRowHint requestedTargetModelRow, bool targetAvailable,
                      const std::chrono::steady_clock::time_point& budgetStart, double budgetMilliseconds,
                      ReplayPredictionUpdateResult& result );
    bool BeginFrameSimulation( Physics::PhysicsEngine& physicsEngine, const Gameplay::TornadoGameplay& tornadoGameplay,
                               const SceneEntityStore& entities, const SkullbonezCore::Core::EngineConfig& config,
                               const Physics::PhysicsWorldForces& worldForces, Threading::WorkerPool& workerPool,
                               ReplayPredictionSourcePreparation preparation );
    void CompleteFrameSourceBegin( bool began, bool wasDirty, bool wasPendingLatestRestart ) noexcept;
    bool BeginFrameBudgetExpired( const std::chrono::steady_clock::time_point& budgetStart, double budgetMilliseconds,
                                  ReplayPredictionUpdateResult& result );
    bool AdvanceFrameWorker( Threading::WorkerPool& workerPool, double simulationTotalSeconds, bool historicalSamplePaused,
                             float solverTrackPosition, float solverPresentTrackPosition,
                             const std::chrono::steady_clock::time_point& budgetStart, double budgetMilliseconds,
                             ReplayPredictionUpdateResult& result );
    void PublishCompletedFrame( const SceneEntityStore& entities, Physics::PhysicsSceneObjectId targetId );
    void PreparePresentation( const SceneEntityStore& entities, const Physics::ColliderStore& colliderStore,
                              Physics::PhysicsSceneObjectId targetId, Physics::ModelRowHint targetModelRow,
                              bool targetAvailable, double budgetMilliseconds, ReplayPredictionUpdateResult& result );
    bool LoadArchive( std::span<const uint8_t> bytes, RunReplayPathVisualizerState& pathVisualizer, char* outReason,
                      std::size_t reasonSize );
    bool BuildArchive( const RunReplayPathVisualizerState& pathVisualizer, std::vector<uint8_t>& outBytes ) const;
    ReplayPastTrajectoryUpdate RefreshPastTrajectoryStore( const ReplaySolverRecorder& solver,
                                                           const ReplayPastTrajectoryView& path );
    void AppendPastTrajectorySample( const ReplayRecorderStats& solverStats, const ReplayPastTrajectoryView& path,
                                     const ReplaySolverFrameSample& sample, ReplayPastTrajectoryUpdate& update );
    ReplayPredictionMemoryStats CollectMemoryStats() const;

  private:

    // Lifetime: startup-bound diagnostics borrow; worker slices retain no owner state.
    Core::Profiler* m_profiler;
    ReplayPredictionPresentation m_presentation;
    RunReplayPredictionState m_state;
    bool m_generationPermitted = true;
};

inline std::size_t RunReplayPredictionState::PublishedBuildFrameCount() const noexcept
{
    return build.publication.PublishedCount( build.buildFrames.size() );
}

inline bool RunReplayPredictionState::HasPublishedBuildFramePrefix( std::size_t minFrameCount ) const noexcept
{
    return build.building && PublishedBuildFrameCount() >= minFrameCount;
}

inline std::size_t RunReplayPredictionState::CommittedFrameCount() const noexcept
{
    return (std::min)( simulation.committedFrameCount, simulation.frames.size() );
}

inline std::span<const RunReplayPredictionFrame> RunReplayPredictionState::CommittedFrames() const noexcept
{
    return { simulation.frames.data(), CommittedFrameCount() };
}

inline bool RunReplayPredictionState::HasCommittedFramePrefix( std::size_t minFrameCount ) const noexcept
{
    return CommittedFrameCount() >= minFrameCount;
}

inline void RunReplayPredictionState::InvalidateCommittedFrames() noexcept
{
    InvalidateCommittedFrameBank( simulation.committedFrameCount );
}

inline void RunReplayPredictionState::PromoteBuildFramesToCommitted( std::size_t frameCount ) noexcept
{
    PromoteFrameBanks( simulation.frames, simulation.committedFrameCount, build.buildFrames, frameCount );
}

inline void RunReplayPredictionState::InvalidateCommittedFrameBank( std::size_t& committedFrameCount ) noexcept
{
    committedFrameCount = 0;
}

inline void RunReplayPredictionState::PromoteFrameBanks( std::vector<RunReplayPredictionFrame>& committedFrames,
                                                         std::size_t& committedFrameCount,
                                                         std::vector<RunReplayPredictionFrame>& completedBuildFrames,
                                                         std::size_t frameCount ) noexcept
{
    committedFrames.swap( completedBuildFrames );
    committedFrameCount = (std::min)( frameCount, committedFrames.size() );
}

inline bool RunReplayPredictionState::BuildPrefixShouldBePresented() const noexcept
{
    const std::size_t publishedCount = PublishedBuildFrameCount();
    const std::size_t requiredFrameCount = !HasCommittedFramePrefix() || build.buildPresentationFrameCount < 2u
                                               ? std::size_t { 2u }
                                               : build.buildPresentationFrameCount;
    return build.building && publishedCount >= requiredFrameCount;
}

inline bool RunReplayPredictionState::BuildPrefixHasBeenPresented() const noexcept
{
    if ( !BuildPrefixShouldBePresented() )
    {
        return false;
    }

    const std::size_t presentedCount = build.presentationPublication.PresentedCount( PublishedBuildFrameCount(),
                                                                                     build.buildFrames.size() );
    return presentedCount >= build.buildPresentationFrameCount;
}

inline bool RunReplayPredictionState::BuildFramesAreComplete() const noexcept
{
    return BuildPrefixShouldBePresented() && PublishedBuildFrameCount() >= build.buildFrames.size();
}

inline bool RunReplayPredictionState::FutureTreeReadyForDraw( Physics::PhysicsSceneObjectId rootId, bool usingBuildFrames,
                                                              std::size_t frameCount ) const noexcept
{
    return FutureTreeReadyForDraw( trajectoryBuild, rootId, usingBuildFrames, frameCount );
}

inline bool RunReplayPredictionState::FutureTreeReadyForDraw(
    const RunReplayPredictionTrajectoryBuildState& trajectory, Physics::PhysicsSceneObjectId rootId,
    bool usingBuildFrames, std::size_t frameCount ) const noexcept
{
    // Invariant: consumers may submit child paths only when the bounded node
    // cache and trajectory publication describe the same root, source bank,
    // topology generation, and complete frame prefix.
    const std::size_t nodeCount = (std::min)( futureNodeCache.futureNodes.size(),
                                              static_cast<std::size_t>( REPLAY_VISUAL_FUTURE_NODE_CAPACITY ) );
    return nodeCount > 0 && futureNodeCache.futureNodesCacheValid && futureNodeCache.futureNodesTopologyVersion != 0 &&
           trajectory.valid && trajectory.rootId.value == rootId.value &&
           trajectory.usingBuildFrames == usingBuildFrames &&
           trajectory.topologyVersion == futureNodeCache.futureNodesTopologyVersion &&
           trajectory.builtNodeCount == nodeCount && trajectory.childFrameCount >= frameCount;
}

inline void RunReplayPredictionState::ResetBuildFramePublication() noexcept
{
    build.publication.Reset();
    build.presentationPublication.Reset();
    build.buildPresentationFrameCount = 2u;
}

inline void RunReplayPredictionState::PublishBuildFrameSlot( std::size_t frameSlot ) noexcept
{
    build.publication.PublishSlot( frameSlot, build.buildFrames.size() );
}

} // namespace Runtime
} // namespace SkullbonezCore
