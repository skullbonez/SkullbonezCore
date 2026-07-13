/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.h
Purpose:
  Owns replay recorders and branch state for the runtime replay subsystem.

Summary:
  ReplayRuntime owns replay timelines and workspace behavior. The application
  shell supplies frame-scoped live-owner views and sequences the result; it does
  not implement scrub, restore, prediction, camera, overlay, or probe decisions.

Glossary:
  Presentation track: Render-facing replay samples used for visual scrubbing.
  Solver track: Physics-facing samples and snapshots used for deterministic
    inspection and rollback.
  Cause tree: Replay graph used by the tool UI to explain which contact or
    predicted movement caused another replay body to matter.
  Body store: Physics-owned live body records used for pose and velocity
    authority while legacy object-record mirrors are retired.
  Collider store: Physics-owned shape, material, and radius records paired with
    body handles.
  UI (User Interface): Runtime controls and overlays that expose replay state
    to the player or debugging workflow.
  Velocity edit: Replay tool that displays and edits linear/angular velocity on
    the current path target.
  Render pose override: One-frame draw-pose request consumed by
    RenderInstanceStore during replay scrub or prediction preview.
  Runtime state: UI and tool state that belongs to replay but is still consumed
    by Run while the subsystem is being separated.
  Prediction cache: Incremental future-path data built from predicted solver
    frames; a worker publishes build prefixes while render consumes them.
  Published build prefix: Contiguous prediction frames whose rows are fully
    written and safe for render, automation, or Director readers to inspect.
  Trajectory record: Versioned polyline storage for one replay body and lane.
  Recorder eviction: Removal of the oldest bounded-ring sample when replay
    capture appends beyond the configured retention window.
  Replay memory policy: Runtime-owned preset, retention, and budget request that
    resolves to concrete presentation and solver recorder windows.

Invariants:
  - Stored dense rows use ModelRowHint; ReplayBodyId remains the identity check.
  - Scrub/prediction draw poses are presentation-only value overrides; replay
    must not backup or mutate live legacy object record pose for rendering.
  - Prediction cache cursors must be reset whenever target, ragdoll mode, or
    sample storage changes.
  - Prediction worker tasks must be idle before build scratch, trajectory slots,
    or private-engine state are cleared.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include "ReplayRecorder.h"
#include "ReplayVisualPacket.h"
#include "ReplayPredictionScheduling.h"
#include "../../Assets/AssetKeys.h"
#include "../Scene/SceneCapacity.h"
#include "TrajectoryStore.h"
#include "../RuntimeCameraMode.h"
#include "../RuntimeInteractionController.h"
#include "../RunReplayProbeState.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Core/Common.h"
#include "../../Core/AmortizedTask.h"
#include "../../Maths/Quaternion.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../Physics/PhysicsWorldForces.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <span>
#include <string>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
} // namespace Core
namespace Runtime
{
class SceneController;
}

namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment

namespace Geometry
{
class Terrain;
} // namespace Geometry

namespace Physics
{
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
} // namespace Physics

namespace Threading
{
class WorkerPool;
} // namespace Threading

namespace Runtime
{
class ReplayRuntime;
class InputRouter;
class RunEditorTracer;
class RuntimeTools;
class SceneController;
class DiagnosticsRuntime;
class SimulationSystem;
enum class GeneratedObjectTypeOverride;
struct RunCameraState;
struct RunDebugState;
struct RunMousePickupState;
class RuntimeRenderer;
struct RunSceneState;
struct ReplayV2SaveResult;
struct ReplaySolverSampleRestoreContext;
#ifdef _DEBUG
#endif

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

inline constexpr std::size_t REPLAY_PREDICTION_GHOST_MAX_FRAMES = 24;
inline constexpr std::size_t REPLAY_PREDICTION_GHOST_REQUEST_CAPACITY =
    ( REPLAY_PREDICTION_GHOST_MAX_FRAMES + 2u ) *
    static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
inline constexpr std::size_t REPLAY_PREDICTION_MARKER_CAPACITY =
    static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
inline constexpr std::size_t REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY = 261u;
// Runtime allocation policy: live replay path-target picks rotate inside this
// fixed vector budget instead of growing while gameplay is running.
inline constexpr std::size_t REPLAY_PATH_MAX_ROOT_TARGETS = 100u;
inline constexpr std::size_t REPLAY_CAUSE_TREE_CONTACT_CAPACITY =
    static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS ) * 4u;
inline constexpr std::size_t REPLAY_CAUSE_TREE_ROW_CAPACITY =
    1u + static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS ) +
    REPLAY_CAUSE_TREE_CONTACT_CAPACITY * 3u;
inline constexpr uint32_t REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS = 1u;
inline constexpr uint32_t REPLAY_GENERATED_SCENE_UI_MODEL_COUNT = 2u;
inline constexpr uint32_t REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS = 4u;
inline constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT = 8u;
inline constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_MASK = 3u << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;

enum class RunReplayTrack
{
    Presentation,
    Solver
};

enum class ReplayMemoryPreset : int
{
    LosslessLook = 0,
    Balanced = 1,
    Compact = 2,
    Count
};

struct ReplayMemoryPolicy
{
    // Concept: presets and sliders resolve to concrete recorder windows here so
    // UI code never needs to know how presentation, solver, and event rings are
    // sized or degraded.
    ReplayMemoryPreset preset = ReplayMemoryPreset::LosslessLook;
    int requestedRetentionSeconds = REPLAY_PAST_BUFFER_SECONDS;
    int requestedBudgetMiB = 256;
    int presentationRetentionSeconds = REPLAY_PAST_BUFFER_SECONDS;
    int solverRetentionSeconds = REPLAY_PAST_BUFFER_SECONDS;
    bool budgetClamped = false;
    bool solverWindowReduced = false;
};

struct ReplayMemoryPolicyRequest
{
    // Sentinel -1 means "leave the current policy value unchanged"; UI controls
    // can therefore emit one focused command without mirroring every slider.
    int presetIndex = -1;
    int retentionSeconds = -1;
    int budgetMiB = -1;
};

inline constexpr int REPLAY_MEMORY_POLICY_MIN_SECONDS = 1;
inline constexpr int REPLAY_MEMORY_POLICY_MAX_SECONDS = 600;
inline constexpr int REPLAY_MEMORY_POLICY_MIN_BUDGET_MIB = 32;
inline constexpr int REPLAY_MEMORY_POLICY_MAX_BUDGET_MIB = 512;

inline ReplayMemoryPreset ReplayMemoryPresetFromIndex( int presetIndex )
{
    switch ( presetIndex )
    {
    case static_cast<int>( ReplayMemoryPreset::Balanced ):
        return ReplayMemoryPreset::Balanced;
    case static_cast<int>( ReplayMemoryPreset::Compact ):
        return ReplayMemoryPreset::Compact;
    case static_cast<int>( ReplayMemoryPreset::LosslessLook ):
    default:
        return ReplayMemoryPreset::LosslessLook;
    }
}

inline ReplayMemoryPolicy ReplayMemoryPresetPolicy( ReplayMemoryPreset preset )
{
    ReplayMemoryPolicy policy;
    policy.preset = preset;
    switch ( preset )
    {
    case ReplayMemoryPreset::Balanced:
        policy.requestedRetentionSeconds = 45;
        policy.requestedBudgetMiB = 128;
        break;
    case ReplayMemoryPreset::Compact:
        policy.requestedRetentionSeconds = 20;
        policy.requestedBudgetMiB = 64;
        break;
    case ReplayMemoryPreset::LosslessLook:
    default:
        policy.requestedRetentionSeconds = REPLAY_PAST_BUFFER_SECONDS;
        policy.requestedBudgetMiB = 256;
        break;
    }
    return policy;
}

inline ReplayMemoryPolicy ResolveReplayMemoryPolicy( ReplayMemoryPolicy policy )
{
    policy.requestedRetentionSeconds = std::clamp( policy.requestedRetentionSeconds,
                                                   REPLAY_MEMORY_POLICY_MIN_SECONDS,
                                                   REPLAY_MEMORY_POLICY_MAX_SECONDS );
    policy.requestedBudgetMiB = std::clamp( policy.requestedBudgetMiB,
                                            REPLAY_MEMORY_POLICY_MIN_BUDGET_MIB,
                                            REPLAY_MEMORY_POLICY_MAX_BUDGET_MIB );
    policy.presentationRetentionSeconds = policy.requestedRetentionSeconds;
    policy.solverRetentionSeconds = policy.requestedRetentionSeconds;

    if ( policy.preset == ReplayMemoryPreset::Balanced )
    {
        policy.solverRetentionSeconds = (std::min)( policy.solverRetentionSeconds, 30 );
    }
    else if ( policy.preset == ReplayMemoryPreset::Compact )
    {
        policy.solverRetentionSeconds = (std::min)( policy.solverRetentionSeconds, 10 );
    }

    // Why: lower memory-budget requests keep the visual/presentation look as
    // long as possible and shorten solver/debug inspection history first.
    if ( policy.requestedBudgetMiB < 192 )
    {
        policy.solverRetentionSeconds = (std::min)( policy.solverRetentionSeconds, 30 );
    }
    if ( policy.requestedBudgetMiB < 128 )
    {
        policy.solverRetentionSeconds = (std::min)( policy.solverRetentionSeconds, 15 );
    }
    if ( policy.requestedBudgetMiB < 64 )
    {
        policy.solverRetentionSeconds = (std::min)( policy.solverRetentionSeconds, 5 );
        policy.presentationRetentionSeconds = (std::min)( policy.presentationRetentionSeconds, 30 );
    }

    policy.solverRetentionSeconds =
        std::clamp( policy.solverRetentionSeconds, REPLAY_MEMORY_POLICY_MIN_SECONDS, REPLAY_MEMORY_POLICY_MAX_SECONDS );
    policy.presentationRetentionSeconds = std::clamp( policy.presentationRetentionSeconds,
                                                      REPLAY_MEMORY_POLICY_MIN_SECONDS,
                                                      REPLAY_MEMORY_POLICY_MAX_SECONDS );
    policy.solverWindowReduced = policy.solverRetentionSeconds < policy.requestedRetentionSeconds;
    policy.budgetClamped =
        policy.solverWindowReduced || policy.presentationRetentionSeconds < policy.requestedRetentionSeconds;
    return policy;
}

struct RunReplayScrubberState
{
    bool visible = false;
    bool historicalSamplePaused = false;
    bool liveAdvanceHeld = false;
    bool pauseRestoreFlyMode = false;
    bool pauseRestoreLauncherMode = false;
    bool restoreWasDown = false;
    bool restoreConsumedThisFrame = false;
    RunReplayTrack activeTrack = RunReplayTrack::Solver;
    RunReplayTrack saveMessageTrack = RunReplayTrack::Solver;
    float position = 1.0f;                                            // 0 = oldest retained sample, 1 = live edge.
    float presentationPosition = 1.0f;
    float solverPosition = 1.0f;
    int mouseX = 0;
    int mouseY = 0;
    double visibleUntil = 0.0;
    double fadeUpdatedAt = 0.0;                                       // Last scrubber opacity update in runtime seconds.
    float visibleAlpha = 0.0f;                                        // 0 = hidden, 1 = fully faded in.
    double saveMessageUntil = 0.0;
    char saveMessage[96] = {};
};

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

enum class RunReplayCauseTreeRowKind
{
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

struct RunReplayCauseTreeRow
{
    RunReplayCauseTreeRowKind kind = RunReplayCauseTreeRowKind::Body;
    ReplayBodyId id;
    ReplayBodyId parentId;
    ReplayBodyId counterpartId;
    ReplayFrameIndex firstFrame = 0;
    int depth = 0;
    Physics::ModelRowHint modelRow;
    Physics::ModelRowHint counterpartModelRow;
    int contactIndex = -1;
    int solverRowIndex = -1;
    int pipelineIndex = -1;
    int featureId = 0;
    int manifoldPointCount = 0;
    float penetration = 0.0f;
    float normalImpulse = 0.0f;
    float tangentImpulse = 0.0f;
    float warmStartImpulse = 0.0f;
    float bias = 0.0f;
    float effectiveMass = 0.0f;
    float frictionLimit = 0.0f;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 impulse = Math::Vector::ZERO_VECTOR;
    bool prediction = false;
    bool terrain = false;
    bool warmStarted = false;
    char name[64] = {};
    char detail[160] = {};
};

struct RunReplayCauseTreeState
{
    // Runtime allocation policy: replay cause rows are rebuilt during input and
    // render, so the vector reserves its full replay/physics budget at startup
    // and builders fail closed instead of growing on a frame.
    std::vector<RunReplayCauseTreeRow> rows;
    int selectedRow = -1;
    ReplayBodyId focusedId;
    bool hasWindowPlacement = false;
    int x = 0;
    int y = 0;
    int width = 380;
    int height = 420;
    float scrollY = 0.0f;
    int dragOffsetX = 0;
    int dragOffsetY = 0;
    int resizeStartMouseX = 0;
    int resizeStartMouseY = 0;
    int resizeStartWidth = 0;
    int resizeStartHeight = 0;
    int mouseX = 0;
    int mouseY = 0;
    bool pointerBlocked = true;                                       // Frame input says a higher-priority UI owns this pointer.
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
    ReplayFrameIndex presentedFrame = 0;                               // Last common reveal clamp consumed by replay presentation.
    ReplayFrameIndex deterministicFrame = 0;                           // Automation-owned cursor; ignored outside fidelity capture.
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

struct ReplayTrajectorySubmissionProbeStats
{
    bool hasSubmission = false;
    bool stableWindowReady = false;
    bool noReserveGrowth = true;
    int observedFrameCount = 0;
    int stableFrameCount = 0;
    int stableWindowTargetFrameCount = 120;
    int firstFrame = -1;
    int lastFrame = -1;
    uint64_t stableHash = 0;
    uint64_t vertexBytes = 0;
    uint32_t vertexCount = 0;
    uint32_t segmentCount = 0;
    uint64_t reserveGrowthEventsAtStart = 0;
    uint64_t reserveGrowthEventsAtEnd = 0;
};

struct RunReplayVelocityEditState
{
    bool enabled = false;
    bool keyboardAltWasDown = false;
    int hotLinearAxis = -1;
    int hotAngularAxis = -1;
    float dragStartAxisT = 0.0f;
    float dragStartAngle = 0.0f;
    Math::Vector::Vector3 dragStartLinearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 dragStartAngularVelocity = Math::Vector::ZERO_VECTOR;
};

struct RunLoadedReplayPresentationState
{
    bool enabled = false;
    std::vector<ReplayPresentationSample> samples;
    std::size_t bodyDictionaryCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    char path[260] = {};
};

struct RunReplayV2TargetRestoreResult
{
    std::size_t checkpointCount = 0;
    std::size_t eventCount = 0;
    std::size_t hashCount = 0;
    std::size_t eventsApplied = 0;
    std::size_t bodyCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex checkpointFrame = 0;
    ReplayFrameIndex targetFrame = 0;
    uint32_t eventCursor = 0;
    uint32_t branchId = 0;
    uint32_t parentBranchId = 0;
    uint64_t solverHash = 0;
    uint64_t presentationHash = 0;
    bool generatedTopologyRebuilt = false;
    bool madeLiveBranch = false;
    bool enterInteractiveRequested = false;
};

enum class ReplayLiveRestoreKind : uint8_t
{
    None,
    V2ArtifactTarget,
    SolverSample
};

struct ReplayLiveRestoreRequest
{
    ReplayLiveRestoreKind kind = ReplayLiveRestoreKind::None;
    const ReplaySolverFrameSample* solverSample = nullptr;            // Borrowed until the workspace command is applied this frame.
    ReplayFrameIndex requestedFrame = 0;
    bool makeLiveBranch = false;
    bool enterInteractive = false;
    RunReplayTrack messageTrack = RunReplayTrack::Solver;
    double now = 0.0;
    char path[260] = {};
};

class ReplayRuntime
{
  public:
    struct ReplayOverlayBuildInput
    {
        bool scenePhysicsEnabled = false;
        bool editorModeEnabled = false;
        RuntimeInteractionGesture gesture;
        int sceneFrame = 0;
        double frameSeconds = 0.0;
        double totalSeconds = 0.0;
    };

    struct PathPickInput
    {
        Math::Vector::Vector3 rayOrigin = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 rayDirection = Math::Vector::ZERO_VECTOR;
        bool hasWorldRay = false;
        bool additive = false;
        bool clearOnMiss = false;
    };

    struct PathPickResult
    {
        bool picked = false;
        bool exitInspectionCamera = false;
    };

    struct WorldPointerInput
    {
        // Lifetime: one routed pointer gesture. Every reference is borrowed for
        // the synchronous pick/optional camera-exit operation and is never stored.
        bool leftPressed = false;
        bool suppressWorldAction = false;
        bool editorMode = false;
        bool uiWantsNativeCursor = false;
        bool controlDown = false;
        bool launcherMode = false;
        PathPickInput pick;
        const SceneEntityStore& entities;
        const Physics::PhysicsBodyStore& bodyStore;
        const Physics::ColliderStore& colliderStore;
        std::span<const Rendering::RenderInstancePresentationRecord> presentation;
        Environment::CameraCollection* cameras = nullptr;
        Geometry::Terrain* terrain = nullptr;
        RunCameraState& camera;
        RunCameraMode restoreCameraMode = RunCameraMode::Inspect;
        bool attachedCameraFollow = false;
        bool directorGrabbed = false;
        RuntimeInteractionController& interaction;
        InputRouter& inputRouter;
    };

    // Concept: one borrowed, frame-scoped replay workspace view replaces the
    // three callback packs formerly threaded through Run's UI command helper.
    // Every reference belongs to a replay interaction, camera, or live-body
    // operation; ReplayRuntime never stores this view beyond TickWorkspace.
    struct ReplayWorkspaceInput
    {
        HWND window = nullptr;
        bool uiBlocksMouse = false;
        int wheelDelta = 0;
        PathPickInput pointerRay;
        InputRouter& inputRouter;
        RuntimeInteractionController& interaction;
        Physics::PhysicsEngine& physics;
        const SceneEntityStore& entities;
        std::span<const Rendering::RenderInstancePresentationRecord> presentation;
        Environment::CameraCollection* cameras = nullptr;
        Geometry::Terrain* terrain = nullptr;
        RunCameraState& camera;
        RunMousePickupState& mousePickup;
        RunCameraMode normalizedCurrentMode = RunCameraMode::Demo;
        RunCameraMode normalizedRestoreMode = RunCameraMode::Demo;
        bool attachedFollow = false;
        bool directorGrabbed = false;
        bool editorModeEnabled = false;
        bool scenePhysicsEnabled = false;
        bool uiVisible = false;
        bool uiMinimized = false;
        int screenWidth = 0;
        int screenHeight = 0;
        double now = 0.0;
    };

    struct ReplayWorkspaceOutput
    {
        ReplayLiveRestoreRequest restoreRequest;
        bool consumesMouse = false;
        bool enterInteractive = false;
    };

    struct ReplayLiveRestoreOutcome
    {
        bool requested = false;
        bool restored = false;
        bool enterInteractive = false;
    };

    struct ReplayStartupRequest
    {
        const char* loadPath = nullptr;
        bool loadProbe = false;
#ifdef _DEBUG
        const char* checkpointProbePath = nullptr;
        const char* targetProbePath = nullptr;
        const char* branchProbePath = nullptr;
        const char* failureProbePath = nullptr;
#endif
    };

    struct ReplayStartupResult
    {
        SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
        bool skipExecute = false;
    };
    struct RecordingConfigResult
    {
        ReplayRecorderConfig presentationConfig;
        ReplayRecorderConfig solverConfig;
        ReplayRecorderStats presentationStats;
        ReplayRecorderStats solverStats;
        ReplayEventRecorderStats eventStats;
    };

    // Concept: scene load/reset code sends replay-owned timeline facts here so
    // ReplayRuntime can clear scrubber, branch, loaded-artifact, path, velocity,
    // and event recorder state without Run reopening each owned struct.
    struct SceneTimelineResetInput
    {
        const char* sceneLabel = nullptr;
        bool preserveBranchMetadata = false;
        bool isSceneMode = false;
        int modelCount = 0;
        int solverBallCount = 0;
        int solverBoxCount = 0;
        uint32_t rngSeed = 0;
        int gameModelCapacity = 0;
        uint32_t generatedObjectTypeOverride = 0;
        bool hasUiModelCountOverride = false;
        bool hasUiSolverCountOverride = false;
    };

    static bool SceneTimelineResetClearsBranch( const SceneTimelineResetInput& input )
    {
        return !input.preserveBranchMetadata;
    }

    static bool SceneTimelineRecordsGeneratedConfig( const SceneTimelineResetInput& input )
    {
        return !( input.isSceneMode && input.solverBallCount <= 0 && input.solverBoxCount <= 0 );
    }

    static uint32_t SceneTimelineGeneratedConfigFlags( const SceneTimelineResetInput& input )
    {
        uint32_t flags = 0;
        flags |=
            ( input.solverBallCount > 0 || input.solverBoxCount > 0 ) ? REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS : 0u;
        flags |= input.hasUiModelCountOverride ? REPLAY_GENERATED_SCENE_UI_MODEL_COUNT : 0u;
        flags |= input.hasUiSolverCountOverride ? REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS : 0u;
        flags |= ( input.generatedObjectTypeOverride << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT ) &
                 REPLAY_GENERATED_SCENE_OVERRIDE_MASK;
        return flags;
    }

    // The two-phase reset result exposes camera cleanup facts to focused replay
    // tests; ResetSceneTimeline applies those facts through borrowed camera/input
    // owners without calling back into the application shell.
    struct SceneTimelineResetResult
    {
        bool exitInspectionCamera = false;
        bool timelineStarted = false;
    };

    struct SceneTimelineResetOwners
    {
        InputRouter& inputRouter;
        RuntimeInteractionController& interaction;
        Environment::CameraCollection* cameras = nullptr;
        Geometry::Terrain* terrain = nullptr;
        RunCameraState& camera;
        RunCameraMode normalizedRestoreMode = RunCameraMode::Demo;
        bool attachedFollow = false;
        bool directorGrabbed = false;
    };

    struct ReplayStartupLoadInput;
    struct ReplayRestoreTransaction;
    struct ReplayArtifactTopologyOwners;

    // Concept: replay interaction ticks receive InputRouter-owned pointer edges;
    // ReplayRuntime owns gesture state but never advances duplicate button memory.
    struct PointerButtonEdges
    {
        bool leftPressed = false;
        bool leftReleased = false;
    };

    struct ScrubberInputFrame
    {
        bool leftPressed = false;
        bool leftReleased = false;
        bool restorePressed = false;
    };

    struct ScrubberUnavailableResult
    {
        bool exitInspectionCamera = false;
    };

    struct KeyboardVelocityEditInput
    {
        bool altDown = false;
        WorldInteractionOwner currentWorldOwner = WorldInteractionOwner::None;
        double now = 0.0;
    };

    enum class KeyboardVelocityEditCameraAction
    {
        None,
        EnterInspection,
        ExitInspection
    };

    // Concept: replay mutates its Alt-edge and velocity-edit state internally,
    // then publishes only the cross-owner effects that the frame coordinator
    // must sequence. No callback can reach back into the application shell.
    struct KeyboardVelocityEditResult
    {
        bool cancelToolDrag = false;
        bool enterInteractive = false;
        KeyboardVelocityEditCameraAction cameraAction = KeyboardVelocityEditCameraAction::None;
        bool setWorldOwner = false;
        WorldInteractionOwner worldOwner = WorldInteractionOwner::None;
    };

    ReplayRuntime();

    ReplayRecorder& Presentation();
    const ReplayRecorder& Presentation() const;

    ReplaySolverRecorder& Solver();
    const ReplaySolverRecorder& Solver() const;

    ReplayEventRecorder& Events();
    const ReplayEventRecorder& Events() const;

    ReplayBranchInfo& Branch();
    const ReplayBranchInfo& Branch() const;

    RunLoadedReplayPresentationState& LoadedPresentation();
    const RunLoadedReplayPresentationState& LoadedPresentation() const;

    RunReplayScrubberState& Scrubber();
    const RunReplayScrubberState& Scrubber() const;

    RunReplayCameraState& Camera();
    const RunReplayCameraState& Camera() const;

    RunReplayPathVisualizerState& PathVisualizer();
    const RunReplayPathVisualizerState& PathVisualizer() const;

    RunReplayPredictionState& Prediction();
    const RunReplayPredictionState& Prediction() const;
    // Lifetime: the view borrows the active retained prediction buffer and is
    // valid only until replay prediction state mutates.
    std::span<const RunReplayPredictionFrame> ActivePredictionFrames() const;
    void ClearPredictionFutureNodeCache();
    void WaitForPredictionJobIdle();
    // Promotes the currently visible worker-built prediction prefix into the
    // committed preview and releases private build scratch. Returns false when
    // no coherent prefix is available or the committed root trajectory cannot be
    // published under the replay reserve budget.
    bool PromotePredictionBuildPrefixToCommitted();
    void CancelPredictionJob( bool clearSamples );
    void ClearPredictionCache();
    void MarkPredictionDirty();
    void ClearPathVisualizerState();

    RunReplayCauseTreeState& CauseTree();
    const RunReplayCauseTreeState& CauseTree() const;

    RunReplayVelocityEditState& VelocityEdit();
    const RunReplayVelocityEditState& VelocityEdit() const;
    bool SetVelocityEditEnabled( bool enabled );
    void SetVelocityEditAltKeyDown( bool isDown );
    KeyboardVelocityEditResult ApplyKeyboardVelocityEdit( const KeyboardVelocityEditInput& input );
    float TrackPosition( RunReplayTrack track ) const;
    void SetTrackPosition( RunReplayTrack track, float position );
    void SyncActiveTrackPosition();
    void SetAllTrackPositions( float position );
    bool ResetScrubberState();
    ScrubberInputFrame BeginScrubberInputFrame( bool leftPressed, bool leftReleased, bool restoreDown );
    ScrubberUnavailableResult ResetUnavailableScrubberSurface( bool loadedPresentation );
    PointerButtonEdges BeginCauseTreeInputFrame( bool leftPressed, bool leftReleased );
    void ClearCauseTreeFocusSelection();
    bool SetLiveAdvanceHeld( bool held );
    // Concept: Render/input code asks replay-owned state for intent-level
    // predicates instead of reading scrubber, path, focus, or velocity structs.
    bool LiveAdvanceHeld() const;
    bool HasPathVisualizerTarget() const;
    bool HasCameraFocus() const;
    bool VelocityEditActive() const;
    float SolverPresentTrackPosition() const;
    static bool TimelineHasFuture( float presentT );
    static bool AtPresentTrackPosition( float position, float presentT );
    static bool TrackPositionIsFuture( float position, float presentT );
    static float SolverNormalizedFromTrack( float position, float presentT );
    static float PredictionNormalizedFromTrack( float position, float presentT );
    bool ShouldRenderScrubber( bool editorModeEnabled,
                               bool uiVisible,
                               bool uiMinimized,
                               RuntimeInteractionGestureKind gesture ) const;
    bool ShouldUseInspectionCamera() const;
    bool InspectionActive() const;
    bool InspectionMouseLookActive( bool rightMouseDown, bool uiWantsNativeCursor, bool uiBlocksCameraMouse ) const;
    bool ArmLoadedPresentationScrubber( float normalized, double now );
    void ClearCameraFocusForRestore();

    // Configures bounded recorder storage. runtimeBodyCapacity must be the
    // scene/run body cap known before capture so replay frames do not allocate.
    RecordingConfigResult
    ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath, int runtimeBodyCapacity );
    // Applies a UI or tool policy request. A true return means recorder windows
    // changed or queued policy state changed before recording was configured.
    bool ApplyMemoryPolicyRequest( const ReplayMemoryPolicyRequest& request );
    // Exposes the resolved policy for diagnostics/UI; callers must not infer
    // recorder capacity from raw requested fields.
    const ReplayMemoryPolicy& MemoryPolicy() const;
    void FlushHashLogs();
    void ResetBranch();
    void ResetTimeline( const char* sceneLabel );
    SceneTimelineResetResult BeginSceneTimelineReset( const SceneTimelineResetInput& input );
    SceneTimelineResetResult FinishSceneTimelineReset( const SceneTimelineResetInput& input );
    static SceneTimelineResetInput DescribeSceneTimeline( const SceneController& sceneController,
                                                          const RunSceneState& scene,
                                                          int gameModelCapacity,
                                                          uint32_t generatedObjectTypeOverride );
    void ResetSceneTimeline( const SceneTimelineResetInput& input, const SceneTimelineResetOwners& owners );
    bool ApplySolverSampleState( const ReplaySolverSampleRestoreContext& owners,
                                 const ReplaySolverFrameSample& sample,
                                 char* outReason,
                                 std::size_t reasonSize );
    bool CaptureCurrentSolverHash( const ReplaySolverSampleRestoreContext& owners,
                                   const ReplaySolverFrameSample& reference,
                                   uint64_t& outSolverHash,
                                   uint64_t& outPresentationHash,
                                   std::size_t& outBodyCount );
    bool RestoreSolverSampleAsLive( const ReplayRestoreTransaction& transaction,
                                    const ReplaySolverFrameSample& sample,
                                    char* outReason,
                                    std::size_t reasonSize );
    bool RestoreV2ArtifactTargetState( const ReplayRestoreTransaction& transaction,
                                       const ReplayArtifactTopologyOwners& topologyOwners,
                                       const char* path,
                                       ReplayFrameIndex requestedFrame,
                                       bool makeLiveBranch,
                                       RunReplayV2TargetRestoreResult& outResult,
                                       char* outReason,
                                       std::size_t reasonSize );
    ReplayLiveRestoreOutcome ApplyLiveRestoreRequest( const ReplayRestoreTransaction& transaction,
                                                      const ReplayArtifactTopologyOwners& topologyOwners,
                                                      const ReplayLiveRestoreRequest& request );
#ifdef _DEBUG
    struct ReplayProbeTickResult
    {
        SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
        bool enterInteractive = false;
    };
    RunReplayProbeState& Probes();
    const RunReplayProbeState& Probes() const;
    // Debug probes compose the same restore transaction and topology operands
    // as production restore. No whole-runtime probe fixture or Run backdoor is accepted.
    ReplayProbeTickResult TickProbes( const ReplayRestoreTransaction& transaction,
                                      const ReplayArtifactTopologyOwners& topology );

  private:
    SkullbonezCore::Core::SbResult TickScrubProbe( const ReplayRestoreTransaction& transaction );
    SkullbonezCore::Core::SbResult TickRestoreProbe( const ReplayRestoreTransaction& transaction );
    SkullbonezCore::Core::SbResult TickSaveProbe( const ReplayRestoreTransaction& transaction,
                                                  const ReplayArtifactTopologyOwners& topology,
                                                  bool& outEnterInteractive );
    SkullbonezCore::Core::SbResult VerifyLoadedPresentationProbe( const ReplayRestoreTransaction& transaction,
                                                                  RunMousePickupState& mousePickup,
                                                                  RunCameraMode normalizedCurrentMode,
                                                                  double now,
                                                                  float normalized );
    SkullbonezCore::Core::SbResult VerifySolverCheckpointFileProbe( const ReplayRestoreTransaction& transaction,
                                                                    const char* path );
    SkullbonezCore::Core::SbResult VerifySolverTargetFileProbe( const ReplayRestoreTransaction& transaction,
                                                                const ReplayArtifactTopologyOwners& topology,
                                                                const char* path );
    SkullbonezCore::Core::SbResult VerifySolverBranchFileProbe( const ReplayRestoreTransaction& transaction,
                                                                const ReplayArtifactTopologyOwners& topology,
                                                                RunMousePickupState& mousePickup,
                                                                RunCameraMode normalizedCurrentMode,
                                                                double now,
                                                                const char* path );
    SkullbonezCore::Core::SbResult VerifySolverFailureFileProbe( const ReplayRestoreTransaction& transaction,
                                                                 const ReplayArtifactTopologyOwners& topology,
                                                                 const char* path );

  public:
#endif
    bool IsPresentationEnabled() const;
    bool IsCaptureEnabled() const;
    ReplayRecorderStats PresentationStats() const;
    ReplayRecorderStats SolverStats() const;
    ReplayEventRecorderStats EventStats() const;
    ReplayFrameIndex NextEventFrameIndex() const;
    // Refreshes the selected past-root trajectory from retained solver samples.
    // The method is cheap when the cursor already matches the recorder window.
    void RefreshPastTrajectoryStoreFromSolverSamples();
    void CaptureFrame( ReplayCaptureInput input );
    bool ApplyPresentationSampleForRender( Rendering::RenderInstanceStore& renderInstances,
                                           const Physics::PhysicsBodyStore& bodyStore,
                                           const Physics::ColliderStore& colliderStore,
                                           const ReplayPresentationSample& sample );
    bool ApplySolverSampleForRender( Rendering::RenderInstanceStore& renderInstances,
                                     const Physics::PhysicsBodyStore& bodyStore,
                                     const Physics::ColliderStore& colliderStore,
                                     const ReplaySolverFrameSample& sample );
    bool ApplyPredictionFrameForRender( Rendering::RenderInstanceStore& renderInstances,
                                        const Physics::PhysicsBodyStore& bodyStore,
                                        const Physics::ColliderStore& colliderStore,
                                        const RunReplayPredictionFrame& frame );
    bool HasLoadedPresentation() const;
    const ReplayPresentationSample* LoadedPresentationSampleAtNormalized( float normalized ) const;
    const ReplayPresentationSample* LoadedPresentationLatestSample() const;
    bool IsScrubPaused() const;
    const ReplayPresentationSample* CurrentScrubSample() const;
    const ReplaySolverFrameSample* CurrentSolverScrubSample() const;
    const RunReplayPredictionFrame* CurrentPredictionScrubFrame() const;
    // Resolves camera-focus pose/radius from replay samples or live physics
    // stores; legacy object record metadata remains outside this body-authority query.
    bool ResolveCauseTreeBodyPosition( ReplayBodyId id,
                                       const Physics::PhysicsBodyStore& bodyStore,
                                       const Physics::ColliderStore& colliderStore,
                                       Math::Vector::Vector3& outPosition,
                                       float* outRadius ) const;
    // Resolves the current velocity-edit target to live physics authority. The
    // stored model index is a staleable hint, not identity.
    Physics::PhysicsBodyHandle ResolveVelocityEditBodyHandle( const Physics::PhysicsBodyStore& bodyStore ) const;
    bool BuildCauseTreeRows( std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                             const Physics::PhysicsBodyStore& bodyStore );
    bool
    BuildPredictionGhostDrawRequests( std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                      const Physics::PhysicsBodyStore& bodyStore );
    bool BuildFocusModelMask( const Physics::PhysicsBodyStore& bodyStore, int modelCount );
    std::vector<uint8_t>& FocusModelMask();
    const std::vector<uint8_t>& FocusModelMask() const;
    bool HasLauncherVisualBackup() const;
    void StoreLauncherVisualBackup( const ReplayLauncherVisualSample& sample );
    const ReplayLauncherVisualSample& LauncherVisualBackup() const;
    ReplayLauncherVisualSample& LauncherVisualCaptureScratch();
    void ClearLauncherVisualBackup();
    // Accumulates one rendered replay overlay pass into the repro-session
    // trajectory counters exposed through memory diagnostics.
    void RecordReplayTrajectoryFrameStats( const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& frameStats );
    // Publishes the tracer's borrowed buffer spans with replay-owned identity,
    // reveal, topology, and ghost metadata for this render frame.
    void PublishReplayVisualPacket( ReplayVisualPacket packet, uint64_t replayReserveGrowthEvents );
    const ReplayVisualPacket& PublishedReplayVisualPacket() const;
    void RecordReplayTrajectorySubmissionFrame(
        const SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats& submissionStats,
        int frameNumber,
        uint64_t reserveGrowthEventCount );
    const ReplayTrajectorySubmissionProbeStats& ReplayTrajectorySubmissionProbe() const;
    void RecordReplayTrajectoryBudgetExpiry( SkullbonezCore::Core::MainMemoryReplayBudgetPass pass );
    void RecordReplayTrajectoryRebuildCause( SkullbonezCore::Core::MainMemoryReplayRebuildCause cause );
    SkullbonezCore::Core::MainMemoryReplayStats CollectMemoryStats() const;
    void RecordEvent( ReplayEventKind kind,
                      ReplayFrameIndex frameIndex,
                      uint32_t flags,
                      int32_t value0,
                      int32_t value1,
                      int32_t value2,
                      int32_t value3,
                      uint64_t data0,
                      const char* text );
    void RecordWorldOverrideEvent( float previousGravity,
                                   float previousFluidHeight,
                                   float previousFluidDensity,
                                   float gravity,
                                   float fluidHeight,
                                   float fluidDensity );
    void RecordLauncherConfigEvent( uint32_t changedFlags, float impulseStrength, float projectileSpeed );
    void RecordLauncherFireEvent( const Math::Vector::Vector3& rayOrigin,
                                  const Math::Vector::Vector3& rayDirection,
                                  const Math::Vector::Vector3& cameraUp,
                                  bool projectile,
                                  float impulseStrength,
                                  float projectileSpeed,
                                  int modelCount );
    void RecordEditorPlaceEvent( int objectType,
                                 bool fixedObject,
                                 bool terrainAlign,
                                 int modelCountBefore,
                                 const Math::Vector::Vector3& terrainPoint,
                                 const Math::Vector::Vector3& placementScale,
                                 float placementYawRadians );
    // Records exact transform payload values supplied by the caller; replay must
    // not reread legacy object record pose after physics store authority has the body row.
    void RecordEditorTransformEvent( int modelIndex,
                                     uint32_t changedFlags,
                                     uint32_t replayBodyId,
                                     const Math::Vector::Vector3& position,
                                     const Math::Orientation::Quaternion& orientation,
                                     int modelCount,
                                     int scaleAxis,
                                     float scaleFactor );
    // Writes the current presentation, solver hashes/checkpoints, and event
    // stream to an explicit cold-I/O binary v2 path.
    bool SavePresentationWithSolverHashes( const char* path, ReplayV2SaveResult* result = nullptr ) const;
    // Owns scrubber save path sequencing and status publication so Run does not
    // retain a behavior-free import/export forwarding module.
    bool SavePresentationFromScrubber( double now );
    bool LoadPresentationArtifact( const char* path,
                                   bool activateScrubber,
                                   double now,
                                   InputRouter& inputRouter,
                                   RuntimeInteractionController& interaction,
                                   Environment::CameraCollection* cameras,
                                   Geometry::Terrain* terrain,
                                   RunCameraState& camera,
                                   RunMousePickupState& mousePickup,
                                   RunCameraMode normalizedCurrentMode,
                                   RunCameraMode normalizedRestoreMode,
                                   bool attachedFollow,
                                   bool directorGrabbed );
    void TickWorkspace( const ReplayWorkspaceInput& input, ReplayWorkspaceOutput& output );
    void ConfigureStartupWorkflows( const ReplayStartupRequest& request );
    ReplayStartupResult RunStartupWorkflows( const ReplayStartupLoadInput& loadInput
#ifdef _DEBUG
                                             ,
                                             const ReplayRestoreTransaction& probeTransaction,
                                             const ReplayArtifactTopologyOwners& probeTopology,
                                             RunMousePickupState& probeMousePickup,
                                             RunCameraMode probeNormalizedCurrentMode,
                                             double probeNow
#endif
    );
    // Appends replay-owned records after RuntimeTools has rebuilt the shared
    // fixed-capacity tracer. RuntimeRenderer only submits the completed buffer.
    void AppendOverlayTrace( Physics::PhysicsEngine& physics,
                             const SceneEntityStore& entities,
                             const SkullbonezCore::Core::EngineConfig& config,
                             const Physics::PhysicsWorldForces& worldForces,
                             Threading::WorkerPool& workerPool,
                             RunEditorTracer& tracer,
                             const ReplayOverlayBuildInput& input );
    // Emits replay-owned fixed-capacity tracer records; Run/RuntimeRenderer
    // only sequence the completed record buffer into render submission.
    void RenderPathVisualizer( Physics::PhysicsEngine& physics,
                               const SceneEntityStore& entities,
                               const SkullbonezCore::Core::EngineConfig& config,
                               const Physics::PhysicsWorldForces& worldForces,
                               Threading::WorkerPool& workerPool,
                               RunEditorTracer& tracer,
                               bool scenePhysicsEnabled,
                               int currentFrame,
                               double frameSeconds,
                               double totalSeconds );
    void RenderCauseFocusOverlay( const Physics::PhysicsBodyStore& bodyStore,
                                  const Physics::ColliderStore& colliderStore,
                                  const SceneEntityStore& entities,
                                  RunEditorTracer& tracer );
    void RenderVelocityEditOverlay( Physics::PhysicsEngine& physics,
                                    bool editorModeEnabled,
                                    const RuntimeInteractionGesture& gesture,
                                    RunEditorTracer& tracer );
    PathPickResult TryPickPathTarget( const PathPickInput& input,
                                      const SceneEntityStore& entities,
                                      const Physics::PhysicsBodyStore& bodyStore,
                                      const Physics::ColliderStore& colliderStore,
                                      std::span<const Rendering::RenderInstancePresentationRecord> presentation );
    bool RouteWorldPointer( const WorldPointerInput& input );
    bool SetPathTarget( const char* name, int modelIndex, const Physics::PhysicsBodyStore& bodyStore );
    bool BeginToolGesture( RuntimeInteractionController& interaction,
                           RuntimeInteractionGestureKind kind,
                           WorldInteractionOwner owner,
                           RuntimePointerButton button,
                           int startX,
                           int startY,
                           Physics::PhysicsBodyHandle body = {},
                           int axis = -1,
                           bool angular = false );
    void EndToolGesture( RuntimeInteractionController& interaction, RuntimeInteractionGestureKind kind );
    void CancelToolGesture( RuntimeInteractionController& interaction );
    void CancelToolDragState( RuntimeInteractionController& interaction, InputRouter& inputRouter );
    bool HasActiveInteractionState() const;
    // Clears replay gesture/camera state as one replay-owned scene transition.
    // The owner bundle is borrowed for this synchronous operation only.
    void ClearInteractionForSceneLoad( const SceneTimelineResetOwners& owners );
    // Clears replay-owned transient state and reports whether the camera owner
    // must execute an inspection-camera exit after the state transition.
    bool ClearInteractionForRuntimeTransition( RuntimeInteractionController& interaction, InputRouter& inputRouter );

  private:
    bool TickCauseTreeInput( bool uiBlocksMouse,
                             int wheelDelta,
                             InputRouter& inputRouter,
                             RuntimeInteractionController& interaction,
                             const Physics::PhysicsBodyStore& bodyStore,
                             const Physics::ColliderStore& colliderStore,
                             std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                             Environment::CameraCollection* cameras,
                             Geometry::Terrain* terrain,
                             RunCameraState& camera,
                             RunMousePickupState& mousePickup,
                             RunCameraMode normalizedCurrentMode,
                             RunCameraMode normalizedRestoreMode,
                             bool attachedFollow,
                             bool directorGrabbed,
                             bool editorModeEnabled,
                             int screenWidth,
                             int screenHeight,
                             bool& outEnterInteractive );
    bool TickVelocityEditInput( bool uiBlocksMouse,
                                const PathPickInput& pointerRay,
                                InputRouter& inputRouter,
                                RuntimeInteractionController& interaction,
                                Physics::PhysicsEngine& physics,
                                const SceneEntityStore& entities,
                                std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                Environment::CameraCollection* cameras,
                                Geometry::Terrain* terrain,
                                RunCameraState& camera,
                                RunMousePickupState& mousePickup,
                                RunCameraMode normalizedCurrentMode,
                                RunCameraMode normalizedRestoreMode,
                                bool attachedFollow,
                                bool directorGrabbed,
                                bool editorModeEnabled,
                                bool scenePhysicsEnabled,
                                int screenWidth,
                                int screenHeight,
                                double now,
                                bool& outEnterInteractive );
    bool TickScrubberInput( HWND hwnd,
                            bool uiBlocksMouse,
                            InputRouter& inputRouter,
                            RuntimeInteractionController& interaction,
                            Environment::CameraCollection* cameras,
                            Geometry::Terrain* terrain,
                            RunCameraState& camera,
                            RunMousePickupState& mousePickup,
                            RunCameraMode normalizedCurrentMode,
                            RunCameraMode normalizedRestoreMode,
                            bool attachedFollow,
                            bool directorGrabbed,
                            bool editorModeEnabled,
                            bool scenePhysicsEnabled,
                            bool uiVisible,
                            bool uiMinimized,
                            int screenWidth,
                            int screenHeight,
                            double now,
                            bool& outEnterInteractive,
                            ReplayLiveRestoreRequest& outRestoreRequest );

  public:
    void EnterInspectionCamera( Environment::CameraCollection* cameras,
                                RunCameraState& camera,
                                RunCameraMode normalizedCurrentMode,
                                RuntimeInteractionController& interaction,
                                InputRouter& inputRouter,
                                RunMousePickupState& mousePickup );
    void ExitInspectionCamera( Environment::CameraCollection* cameras,
                               Geometry::Terrain* terrain,
                               RunCameraState& camera,
                               RunCameraMode normalizedRestoreMode,
                               bool attachedFollow,
                               bool directorGrabbed,
                               RuntimeInteractionController& interaction,
                               InputRouter& inputRouter );

  private:
    void ReportLatestCaptureMismatch();
    void AppendSolverTrajectorySampleToStore( const ReplaySolverFrameSample& sample );
    bool RestoreV2ArtifactTargetStateImpl( const ReplayRestoreTransaction& transaction,
                                           const ReplayArtifactTopologyOwners& topologyOwners,
                                           const char* path,
                                           ReplayFrameIndex requestedFrame,
                                           bool makeLiveBranch,
                                           bool injectTargetHashMismatchForProbe,
                                           RunReplayV2TargetRestoreResult& outResult,
                                           char* outReason,
                                           std::size_t reasonSize );
    bool CaptureCurrentSolverSample( const ReplaySolverSampleRestoreContext& owners,
                                     const ReplaySolverFrameSample& reference,
                                     ReplaySolverFrameSample& outSample );

    ReplayRecorder m_presentation;                                    // Bounded replay presentation recorder for recent-frame inspection.
    ReplaySolverRecorder m_solver;                                    // Same-tick solver-state recorder kept in tandem with presentation replay.
    ReplayEventRecorder m_events;                                     // Bounded intent/event stream kept beside v2 replay tracks.
    ReplayBranchInfo m_branch;                                        // Current live replay branch provenance.
    ReplayMemoryPolicy m_memoryPolicy;                                // Resolved recorder-window policy owned by ReplayRuntime.
    struct StartupWorkflowState
    {
        char loadPath[260] = {};
        bool loadProbe = false;
#ifdef _DEBUG
        char checkpointProbePath[260] = {};
        char targetProbePath[260] = {};
        char branchProbePath[260] = {};
        char failureProbePath[260] = {};
#endif
    } m_startupWorkflows;
#ifdef _DEBUG
    RunReplayProbeState m_probes;                                     // CLI-only replay validation state owned with the workflows it drives.
#endif
    RunLoadedReplayPresentationState m_loadedPresentation;
    RunReplayScrubberState m_scrubber;
    RunReplayCameraState m_camera;
    RunReplayPathVisualizerState m_pathVisualizer;
    RunReplayPredictionState m_prediction;
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats
        m_trajectoryVisualStats;                                      // Cumulative replay trajectory diagnostics for the current process.
    ReplayTrajectorySubmissionProbeStats
        m_trajectorySubmissionProbe;                                  // Submitted replay-ribbon stability window for validation reports.
    ReplayVisualPacket m_publishedVisualPacket;                        // Frame-local immutable seam consumed by renderer and probes.
    RunReplayCauseTreeState m_causeTree;
    RunReplayVelocityEditState m_velocityEdit;
    std::vector<ReplayPredictionGhostDrawRequest> m_predictionGhostDrawRequests;
    std::vector<uint8_t> m_focusModelMask;
    ReplayLauncherVisualSample
        m_launcherVisualBackup;                                       // Live launcher visuals restored after replay presentation overrides.
    ReplayLauncherVisualSample
        m_launcherVisualCaptureScratch;                               // Reserved post-physics capture payload reused every replay tick.
    // Invariant: replay render pose matching is a per-frame mark table capped by
    // the live model budget. It must not allocate while scrub/prediction views
    // are applied during rendering.
    std::array<uint8_t, SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS> m_renderPoseBodyMatched = {};
    std::string m_recordingHashLogPath;
    int m_presentationSaveSequence = 0;                               // Next numbered binary-v2 scrubber path candidate.
    int m_recordingRuntimeBodyCapacity = 0;
    uint32_t m_captureMismatchReports = 0;                            // Process-lifetime throttle for paired presentation/solver capture diagnostics.
    bool m_captureMismatchSuppressed = false;
    bool m_launcherVisualBackupActive = false;
    bool m_recordingConfigured = false;
    bool m_recordingEnabled = false;
};
} // namespace Runtime
} // namespace SkullbonezCore
