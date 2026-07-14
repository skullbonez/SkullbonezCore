/*
File: SkullbonezSource/Runtime/Replay/ReplayPrediction.h
Purpose:
  Owns private replay prediction build, publication, and trajectory state.

Summary:
  ReplayPrediction simulates an isolated future and publishes completed prefixes
  while readers consume a never-stored presentation view.

Glossary:
  Published prefix: Contiguous prediction rows safe for readers.

Invariants:
  - Worker publication retains the release/acquire prefix protocol.
  - Prediction owns its private engine and never mutates live physics stores.
  - Cancellation waits for an in-flight worker slice before clearing state.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayIdentity.h"
#include "ReplayPredictionScheduling.h"
#include "ReplayRecorder.h"
#include "ReplayVisualPacket.h"
#include "TrajectoryStore.h"
#include "../../Core/AmortizedTask.h"
#include "../../Maths/Quaternion.h"
#include "../../Physics/PhysicsWorldForces.h"

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
}
namespace Physics
{
class PhysicsEngine;
}
namespace Threading
{
class WorkerPool;
}

namespace Runtime
{
class ReplayRuntime;
// Concept: this named value operation keeps prediction slices typed through the
// WorkerPool boundary. Its borrowed owners remain valid until cancellation
// waits for the task's in-flight flag to clear.
struct ReplayPredictionWorkerOperation
{
    ReplayRuntime* replayRuntime = nullptr;
    const SkullbonezCore::Core::EngineConfig* config = nullptr;
    Threading::WorkerPool* workerPool = nullptr;
    int modelCount = 0;

    void operator()( int beginTickIndex, int endTickIndex ) const;
};

using ReplayPredictionAmortizedTask = Threading::AmortizedTask<ReplayPredictionWorkerOperation>;

struct RunReplayPredictionBodyBackup
{
    ReplayBodyId id;
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

struct RunReplayPredictionBodySample
{
    ReplayBodyId id;
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

struct ReplayPredictionBaselineRootPoint
{
    ReplayFrameIndex frameIndex = 0;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
};

struct ReplayPredictionBaselineBodyPose
{
    ReplayBodyId id;
    Physics::ModelRowHint modelRow;
    bool hasEntryPose = false;
    bool hasRestPose = false;
    Math::Vector::Vector3 entryPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion entryOrientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 restPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion restOrientation = Math::Orientation::IDENTITY_QUATERNION;
};

struct ReplayPredictionBaselineSnapshot
{
    bool valid = false;
    bool comparisonActive = false;
    ReplayBodyId rootId;
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
    // from this anchor so the tree unfolds over real time instead of popping in
    // whole.
    // Invariant: overlay pacing never feeds physics, replay samples, or solver
    // restores, so steady_clock here cannot affect deterministic simulation.
    double secondsPerSecond = 1.0;                                    // Runtime-authored causal-unfold speed; 1.0 = real-time.
    std::chrono::steady_clock::time_point anchor = {};
    ReplayFrameIndex presentedFrame = 0;                              // Last common reveal clamp consumed by replay presentation.
    ReplayFrameIndex deterministicFrame = 0;                          // Automation-owned cursor; ignored outside fidelity capture.
    bool deterministicFrameEnabled = false;
    bool anchorValid = false;
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
    ReplayBodyId futureNodesBuiltTargetId;
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
};

struct RunReplayPredictionTrajectoryBuildState
{
    // Concept: prediction trajectory records follow the same published-prefix
    // contract as buildFrames. Root points are appended when frames publish;
    // child records catch up after the future-node cache publishes topology.
    ReplayBodyId rootId;
    bool usingBuildFrames = false;
    std::size_t rootFrameCount = 0;
    std::size_t childFrameCount = 0;
    std::size_t builtNodeCount = 0;
    // Invariant: child trajectory records are drawable only when this version
    // matches the future-node cache version that selected their branch ordinals.
    uint32_t topologyVersion = 0;
    bool valid = false;
};

struct RunReplayPredictionBuildState
{
    bool dirty = true;
    uint32_t generationBeginCount = 0;                                // Successful future-simulation generations in this process.
    // Concept: velocity edits do not form a queue. While an instant worker job
    // is in flight, this bit remembers only that the newest live state needs one
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
    // whole horizon while only buildFrameCount rows are populated. Render reads
    // frames, not the pre-sized build vector, until completion swaps them.
    // Invariant: buildFrameCount is the single published prefix cursor. Worker
    // stepping publishes it with release ordering only after the
    // corresponding frame rows and trajectory slots are complete. Readers use
    // PublishedBuildFrameCount() as the acquire edge before inspecting rows.
    // Invariant: during a same-target refresh, the building prefix may replace
    // committed frames only after it reaches the reveal cursor captured at job
    // start. This prevents auto-refresh from replaying the causal unfold from
    // frame zero.
    std::vector<RunReplayPredictionFrame> buildFrames;
    std::atomic<std::size_t> buildFrameCount{ 0 };
    std::size_t buildPresentationFrameCount = 2u;
    // Concept: the amortized task owns prediction physics/capture slices while
    // the frame loop only submits ticks and consumes the published prefix.
    // Hazard: cancellation must wait for an in-flight slice before clearing
    // buildFrames, trajectory records, or the private prediction engine.
    std::unique_ptr<ReplayPredictionAmortizedTask> workerTask;
    std::atomic<bool> workerFailed{ false };
};

struct RunReplayPredictionSimulationState
{
    float horizonSeconds = REPLAY_FUTURE_BUFFER_SECONDS;
    Physics::ModelRowHint targetModelRow;
    ReplayBodyId targetId;
    ReplayFrameIndex sourceFrameIndex = 0;
    uint64_t sourceSolverHash = 0;
    double sourceSimulationSeconds = 0.0;
    // Invariant: the worker is the sole writer of probe accumulators and
    // release-publishes measuredTicksPerMs. The frame thread acquire-loads it
    // before choosing a build mode. Same-source velocity restarts retain the
    // calibration; scene/branch/body-count changes reset it.
    std::atomic<double> measuredTicksPerMs{ 0.0 };
    double probeElapsedMs = 0.0;
    int probeTicksCompleted = 0;
    int calibratedModelCount = -1;
    // Concept: prediction simulates the future in its own engine. Live stores
    // are never written by prediction, so replay preview state stays isolated.
    // Lifetime: constructed lazily on first prediction begin under the replay
    // reserve owner, pre-sized by copying the current live physics facade, and
    // reused across prediction builds so startup/perf-smoke memory stays flat.
    // Runtime allocation policy: owner replay_prediction_working_set; reason:
    // private prediction needs a bounded physics copy for exploratory replay;
    // deletion condition: none, this is the end-state isolation boundary;
    // checker budget: 256 MB hard cap registered by ReplayPredictionReserveOwner().
    std::unique_ptr<Physics::PhysicsEngine> predictionEngine;
    Physics::PhysicsWorldForces predictionWorldForces;
    bool predictionEngineReady = false;
    ReplaySolverWorldSnapshot predictionWorld;
    std::vector<RunReplayPredictionBodyBackup> predictionBodies;
    std::vector<RunReplayPredictionFrame> frames;
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
    bool BuildPrefixShouldBePresented() const noexcept;
    bool BuildFramesAreComplete() const noexcept;
    void ResetBuildFramePublication() noexcept;
    void PublishBuildFrameSlot( std::size_t frameSlot ) noexcept;

    bool enabled = false;
    bool ragdollVisualsEnabled = false;
    RunReplayPredictionBuildState build;
    RunReplayPredictionSimulationState simulation;
    RunReplayPredictionFutureNodeCache futureNodeCache;
    // Concept: trajectory records are the publication layer between
    // prediction/solver builders and overlay drawing. Main root/child ribbons
    // read these records; auxiliary marker/ragdoll paths keep using frame data
    // when they need orientation or velocity, not just trajectory points.
    ReplayTrajectoryStore trajectoryStore;
    RunReplayPredictionTrajectoryBuildState trajectoryBuild;
    // Concept: the butterfly baseline is a retained presentation snapshot of
    // the pre-nudge future. It is intentionally smaller than the committed
    // simulation frame list: one cold root polyline, two poses per affected
    // body, and one divergence number, so the warm current prediction can
    // unfold over it.
    ReplayPredictionBaselineSnapshot baseline;
    RunReplayPredictionRevealClock revealClock;
};

struct ReplayPredictionPresentationView
{
    std::span<const RunReplayPredictionFrame> frames;
    std::span<const RunReplayPathTraceNode> futureNodes;
    std::span<const ReplayTrajectoryRecord> trajectoryRecords;
    std::span<const ReplayPredictionRetainedMarker> retainedMarkers;
    std::span<const ReplayPredictionBaselineBodyPose> baselineBodyPoses;
    ReplayFrameIndex sourceFrame = 0;
    ReplayFrameIndex revealFrame = 0;
    uint32_t topologyVersion = 0;
    bool enabled = false;
    bool building = false;
    bool complete = false;
    bool ragdollVisualsEnabled = false;
    bool baselineValid = false;
    bool baselineComparisonActive = false;
};

class ReplayPrediction
{
  public:
    RunReplayPredictionState& State() noexcept
    {
        return m_state;
    }
    const RunReplayPredictionState& State() const noexcept
    {
        return m_state;
    }

    std::span<const RunReplayPredictionFrame> ActiveFrames() const noexcept
    {
        const std::vector<RunReplayPredictionFrame>& frames =
            m_state.BuildFramesAreComplete() ? m_state.build.buildFrames : m_state.simulation.frames;
        return { frames.data(), frames.size() };
    }

    ReplayPredictionPresentationView PresentationView() const noexcept
    {
        ReplayPredictionPresentationView view;
        view.frames = ActiveFrames();
        view.futureNodes = m_state.futureNodeCache.futureNodes;
        view.trajectoryRecords = m_state.trajectoryStore.records;
        view.retainedMarkers = { m_state.futureNodeCache.retainedMarkers.data(),
                                 m_state.futureNodeCache.retainedMarkerCount };
        view.baselineBodyPoses = m_state.baseline.bodyPoses;
        view.sourceFrame = m_state.simulation.sourceFrameIndex;
        view.revealFrame = m_state.revealClock.presentedFrame;
        view.topologyVersion = m_state.futureNodeCache.futureNodesTopologyVersion;
        view.enabled = m_state.enabled;
        view.building = m_state.build.building;
        view.complete = m_state.build.complete;
        view.ragdollVisualsEnabled = m_state.ragdollVisualsEnabled;
        view.baselineValid = m_state.baseline.valid;
        view.baselineComparisonActive = m_state.baseline.comparisonActive;
        return view;
    }

    bool GenerationPermitted() const noexcept
    {
        return m_generationPermitted;
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
    void MarkDirty() noexcept;
    void EnterOfflineVerification();
    void ResetVerificationMarkers() noexcept;
    void SetVerificationRevealFrame( ReplayFrameIndex frame ) noexcept;

  private:
    RunReplayPredictionState m_state;
    bool m_generationPermitted = true;
};

inline std::size_t RunReplayPredictionState::PublishedBuildFrameCount() const noexcept
{
    const std::size_t publishedCount = build.buildFrameCount.load( std::memory_order_acquire );
    return publishedCount < build.buildFrames.size() ? publishedCount : build.buildFrames.size();
}

inline bool RunReplayPredictionState::HasPublishedBuildFramePrefix( std::size_t minFrameCount ) const noexcept
{
    return build.building && PublishedBuildFrameCount() >= minFrameCount;
}

inline bool RunReplayPredictionState::BuildPrefixShouldBePresented() const noexcept
{
    const std::size_t publishedCount = PublishedBuildFrameCount();
    const std::size_t requiredFrameCount = simulation.frames.empty() || build.buildPresentationFrameCount < 2u
                                               ? std::size_t{ 2u }
                                               : build.buildPresentationFrameCount;
    return build.building && publishedCount >= requiredFrameCount;
}

inline bool RunReplayPredictionState::BuildFramesAreComplete() const noexcept
{
    return BuildPrefixShouldBePresented() && PublishedBuildFrameCount() >= build.buildFrames.size();
}

inline void RunReplayPredictionState::ResetBuildFramePublication() noexcept
{
    build.buildFrameCount.store( 0, std::memory_order_release );
    build.buildPresentationFrameCount = 2u;
    build.workerFailed.store( false, std::memory_order_release );
}

inline void RunReplayPredictionState::PublishBuildFrameSlot( std::size_t frameSlot ) noexcept
{
    const std::size_t publishedCount = frameSlot < build.buildFrames.size() ? frameSlot + 1u : build.buildFrames.size();
    if ( publishedCount > build.buildFrameCount.load( std::memory_order_relaxed ) )
    {
        build.buildFrameCount.store( publishedCount, std::memory_order_release );
    }
}

} // namespace Runtime
} // namespace SkullbonezCore
