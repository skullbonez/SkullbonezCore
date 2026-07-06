/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.h
Purpose:
  Owns replay recorders and branch state for the runtime replay subsystem.

Mental model:
  ReplayRuntime is the compatibility boundary while replay behavior moves out
  of Run. Existing Run methods can still reach the legacy recorders through
  explicit accessors, but ownership now belongs to the replay subsystem.

Glossary:
  Presentation track: Render-facing replay samples used for visual scrubbing.
  Solver track: Physics-facing samples and snapshots used for deterministic
    inspection and rollback.
  Cause tree: Replay graph used by the tool UI to explain which contact or
    predicted movement caused another replay body to matter.
  Body store: Physics-owned live body records used for pose and velocity
    authority while legacy GameModel mirrors are retired.
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
    frames under a render-frame budget.

Invariants:
  - Stored indices are hints; ReplayBodyId remains the identity check.
  - Scrub/prediction draw poses are presentation-only value overrides; replay
    must not backup or mutate live GameModel pose for rendering.
  - Prediction cache cursors must be reset whenever target, ragdoll mode, or
    sample storage changes.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
*/
#pragma once

#include "ReplayRecorder.h"
#include "../RuntimeCameraMode.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Core/Common.h"
#include "../../Maths/Quaternion.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <array>
#include <chrono>

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
} // namespace Physics

namespace Basics
{
struct ReplayV2SaveResult;

inline constexpr std::size_t REPLAY_PREDICTION_GHOST_MAX_FRAMES = 24;
inline constexpr std::size_t REPLAY_PREDICTION_MARKER_CAPACITY = static_cast<std::size_t>( MAX_GAME_MODELS );
inline constexpr std::size_t REPLAY_CAUSE_TREE_CONTACT_CAPACITY = static_cast<std::size_t>( MAX_GAME_MODELS ) * 4u;
inline constexpr std::size_t REPLAY_CAUSE_TREE_ROW_CAPACITY =
    1u + static_cast<std::size_t>( MAX_GAME_MODELS ) + REPLAY_CAUSE_TREE_CONTACT_CAPACITY * 3u;

enum class RunReplayTrack
{
    Presentation,
    Solver
};

struct RunReplayScrubberState
{
    bool visible = false;
    bool dragging = false;
    bool historicalSamplePaused = false;
    bool liveAdvanceHeld = false;
    bool branchHovered = false;
    bool pauseHovered = false;
    bool pauseRestoreFlyMode = false;
    bool pauseRestoreLauncherMode = false;
    bool mouseCaptured = false;
    bool saveHovered = false;
    bool loadHovered = false;
    bool restoreWasDown = false;
    bool restoreConsumedThisFrame = false;
    RunReplayTrack activeTrack = RunReplayTrack::Solver;
    RunReplayTrack saveHoveredTrack = RunReplayTrack::Solver;
    RunReplayTrack saveMessageTrack = RunReplayTrack::Solver;
    bool leftWasDown = false;
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

struct RunReplayPathTraceNode
{
    ReplayBodyId id;
    ReplayBodyId parentId;
    int modelIndex = -1;                                              // Fast lookup hint; ReplayBodyId remains authority.
    int parentModelIndex = -1;                                        // Fast lookup hint for contact-chain parents.
    ReplayFrameIndex firstFrame = 0;
    Math::Vector::Vector3 contactPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 contactNormal = Math::Vector::ZERO_VECTOR;
    int depth = 0;
    bool contactDerived = true;                                       // False when prediction inferred the child from pose divergence.
};

struct RunReplayPathTarget
{
    ReplayBodyId id;
    int modelIndex = -1;
    char name[64] = {};
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
    int focusModelIndex = -1;
    int focusCounterpartModelIndex = -1;
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
    int modelIndex = -1;
    int counterpartModelIndex = -1;
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
    int hoveredRow = -1;
    int selectedRow = -1;
    ReplayBodyId focusedId;
    bool hasWindowPlacement = false;
    int x = 0;
    int y = 0;
    int width = 380;
    int height = 420;
    float scrollY = 0.0f;
    bool draggingWindow = false;
    bool resizingWindow = false;
    bool leftWasDown = false;
    int dragOffsetX = 0;
    int dragOffsetY = 0;
    int resizeStartMouseX = 0;
    int resizeStartMouseY = 0;
    int resizeStartWidth = 0;
    int resizeStartHeight = 0;
};

struct RunReplayPathVisualizerState
{
    bool hasTarget = false;
    ReplayBodyId targetId;
    int targetModelIndex = -1;
    char targetName[64] = {};
    std::vector<RunReplayPathTraceNode> futureNodes;
    std::vector<RunReplayPathTarget> targets;
};

struct RunReplayPredictionBodyBackup
{
    ReplayBodyId id;
    int modelIndex = -1;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;
    float inverseMass = 0.0f;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 inverseRotationalInertia = Math::Vector::ZERO_VECTOR;
    float fixedContactHighlightSeconds = 0.0f;
    bool fixed = false;
};

struct RunReplayPredictionBodySample
{
    ReplayBodyId id;
    int modelIndex = -1;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR; // m/s-equivalent simulation units.
};

struct RunReplayPredictionFrame
{
    ReplayFrameIndex frameIndex = 0;
    double simulationSeconds = 0.0;
    float tornadoSystemElapsedSeconds = 0.0f;
    std::vector<RunReplayPredictionBodySample> bodies;
    std::vector<Physics::PhysicsDebugContact> debugContacts;
};

struct ReplayPredictionGhostDrawRequest
{
    int modelIndex = -1;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    float alpha = 1.0f;
};

struct ReplayPredictionRetainedMarker
{
    ReplayBodyId id;
    int modelIndex = -1;
    bool hasEntryPose = false;
    bool hasRestPose = false;
    bool hasHorizonPose = false;
    Math::Vector::Vector3 entryPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion entryOrientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 restPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion restOrientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 horizonPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion horizonOrientation = Math::Orientation::IDENTITY_QUATERNION;
};

struct RunReplayPredictionState
{
    bool enabled = false;
    bool checkboxHovered = false;
    bool ragdollVisualsEnabled = false;
    bool ragdollVisualsHovered = false;
    bool decreaseHovered = false;
    bool increaseHovered = false;
    bool horizonHovered = false;
    bool horizonDragging = false;
    bool dirty = true;
    bool building = false;
    bool complete = false;
    float horizonSeconds = REPLAY_FUTURE_BUFFER_SECONDS;
    int targetModelIndex = -1;
    int nextTick = 1;
    int targetTickCount = 0;
    ReplayBodyId targetId;
    ReplayFrameIndex sourceFrameIndex = 0;
    uint64_t sourceSolverHash = 0;
    double sourceSimulationSeconds = 0.0;
    double lastBuildTime = 0.0;
    ReplaySolverWorldSnapshot predictionWorld;
    ReplaySolverWorldSnapshot liveRestoreWorld;
    std::vector<RunReplayPredictionBodyBackup> predictionBodies;
    std::vector<RunReplayPredictionBodyBackup> liveRestoreBodies;
    std::vector<RunReplayPredictionFrame> frames;
    // Runtime allocation policy: prediction buildFrames can be pre-sized for a
    // whole horizon while only buildFrameCount rows are populated. Render reads
    // frames, not the pre-sized build vector, until completion swaps them.
    std::vector<RunReplayPredictionFrame> buildFrames;
    std::size_t buildFrameCount = 0;
    // Renderable future-impact topology. Build work publishes coherent prefixes
    // here so the draw path never reads the scratch vector while it is mid-frame.
    std::vector<RunReplayPathTraceNode> futureNodes;
    // Scratch future-impact topology advanced under the visualizer budget.
    std::vector<RunReplayPathTraceNode> futureNodeBuildScratch;
    // Incremental tree cursors. Prediction can contain thousands of frames, so
    // futureNodeBuildScratch is built over multiple render frames and copied to
    // futureNodes only at coherent prefix boundaries.
    std::size_t futureNodesBuiltFrameCount = 0;
    std::size_t futureNodesBuiltContactIndex = 0;
    ReplayBodyId futureNodesBuiltTargetId;
    bool futureNodesBuiltRagdollVisuals = false;
    bool futureNodesBuiltFromBuildFrames = false;
    bool futureNodesCacheValid = false;
    // Invariant: once a causal yellow or grey box has been revealed, budgeted
    // line scans may not make it disappear. This fixed cache redraws retained
    // marker poses until a new prediction/future cache resets the story.
    std::array<ReplayPredictionRetainedMarker, REPLAY_PREDICTION_MARKER_CAPACITY> retainedMarkers = {};
    std::size_t retainedMarkerCount = 0;
    // Concept: reveal anchor — wall-clock start of the causal-unfold animation.
    // The overlay clamps drawn prediction frames to a cursor derived from this
    // anchor so the tree unfolds over real time instead of popping in whole.
    // Overlay-only pacing state: it never feeds physics, replay samples, or
    // solver restores, so steady_clock here cannot affect determinism.
    std::chrono::steady_clock::time_point revealAnchor = {};
    bool revealAnchorValid = false;
};

struct RunReplayVelocityEditState
{
    bool enabled = false;
    bool toggleHovered = false;
    bool keyboardAltWasDown = false;
    bool dragging = false;
    bool draggingAngular = false;
    bool mouseCaptured = false;
    bool leftWasDown = false;
    int hotLinearAxis = -1;
    int hotAngularAxis = -1;
    int activeAxis = -1;
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
};

class ReplayRuntime
{
  public:
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

    // Run still owns process/UI side effects such as leaving inspection camera;
    // the replay command reports those actions instead of calling back into Run.
    struct SceneTimelineResetResult
    {
        bool exitInspectionCamera = false;
        bool timelineStarted = false;
    };

    // Concept: replay interaction ticks pass raw button/key snapshots to
    // ReplayRuntime, which owns edge memory for scrubber and cause-tree controls.
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
    const std::vector<RunReplayPredictionFrame>& ActivePredictionFrames() const;
    void ClearPredictionFutureNodeCache();
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
    float TrackPosition( RunReplayTrack track ) const;
    void SetTrackPosition( RunReplayTrack track, float position );
    void SyncActiveTrackPosition();
    void SetAllTrackPositions( float position );
    bool ResetScrubberState();
    ScrubberInputFrame BeginScrubberInputFrame( bool leftDown, bool restoreDown );
    ScrubberUnavailableResult ResetUnavailableScrubberSurface( bool loadedPresentation, bool leftDown );
    PointerButtonEdges BeginCauseTreeInputFrame( bool leftDown );
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
    bool ShouldRenderScrubber( bool editorModeEnabled, bool uiVisible, bool uiMinimized ) const;
    bool ShouldUseInspectionCamera() const;
    bool InspectionActive() const;
    bool InspectionMouseLookActive( bool rightMouseDown, bool uiWantsNativeCursor, bool uiBlocksCameraMouse ) const;
    bool ArmLoadedPresentationScrubber( float normalized, double now );
    void ClearCameraFocusForRestore();

    // Configures bounded recorder storage. runtimeBodyCapacity must be the
    // scene/run body cap known before capture so replay frames do not allocate.
    RecordingConfigResult
    ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath, int runtimeBodyCapacity );
    void FlushHashLogs();
    void ResetBranch();
    void ResetTimeline( const char* sceneLabel );
    SceneTimelineResetResult BeginSceneTimelineReset( const SceneTimelineResetInput& input );
    SceneTimelineResetResult FinishSceneTimelineReset( const SceneTimelineResetInput& input );
    bool IsPresentationEnabled() const;
    bool IsCaptureEnabled() const;
    ReplayRecorderStats PresentationStats() const;
    ReplayRecorderStats SolverStats() const;
    ReplayEventRecorderStats EventStats() const;
    ReplayFrameIndex NextEventFrameIndex() const;
    void CaptureFrame( ReplayCaptureInput input );
    bool ApplyPresentationSampleForRender( Physics::PhysicsEngine& physicsEngine,
                                           const ReplayPresentationSample& sample );
    bool ApplySolverSampleForRender( Physics::PhysicsEngine& physicsEngine, const ReplaySolverFrameSample& sample );
    bool ApplyPredictionFrameForRender( Physics::PhysicsEngine& physicsEngine, const RunReplayPredictionFrame& frame );
    bool HasLoadedPresentation() const;
    const ReplayPresentationSample* LoadedPresentationSampleAtNormalized( float normalized ) const;
    const ReplayPresentationSample* LoadedPresentationLatestSample() const;
    bool IsScrubPaused() const;
    const ReplayPresentationSample* CurrentScrubSample() const;
    const ReplaySolverFrameSample* CurrentSolverScrubSample() const;
    const RunReplayPredictionFrame* CurrentPredictionScrubFrame() const;
    // Resolves camera-focus pose/radius from replay samples or live physics
    // stores; GameModel metadata remains outside this body-authority query.
    bool ResolveCauseTreeBodyPosition( ReplayBodyId id,
                                       const Physics::PhysicsBodyStore& bodyStore,
                                       const Physics::ColliderStore& colliderStore,
                                       Math::Vector::Vector3& outPosition,
                                       float* outRadius ) const;
    // Resolves the current velocity-edit target to live physics authority. The
    // stored model index is a staleable hint, not identity.
    Physics::PhysicsBodyHandle ResolveVelocityEditBodyHandle( const Physics::PhysicsBodyStore& bodyStore ) const;
    bool BuildCauseTreeRows( const std::vector<Rendering::RenderInstancePresentationRecord>& presentationRecords,
                             const Physics::PhysicsBodyStore& bodyStore );
    bool BuildPredictionGhostDrawRequests(
        const std::vector<Rendering::RenderInstancePresentationRecord>& presentationRecords,
        const Physics::PhysicsBodyStore& bodyStore );
    const std::vector<ReplayPredictionGhostDrawRequest>& PredictionGhostDrawRequests() const;
    bool BuildFocusModelMask( const Physics::PhysicsBodyStore& bodyStore, int modelCount );
    std::vector<uint8_t>& FocusModelMask();
    const std::vector<uint8_t>& FocusModelMask() const;
    bool HasLauncherVisualBackup() const;
    void StoreLauncherVisualBackup( const ReplayLauncherVisualSample& sample );
    const ReplayLauncherVisualSample& LauncherVisualBackup() const;
    void ClearLauncherVisualBackup();
    MainMemoryReplayStats CollectMemoryStats() const;
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
    // not reread GameModel pose after physics store authority has the body row.
    void RecordEditorTransformEvent( int modelIndex,
                                     uint32_t changedFlags,
                                     uint32_t replayBodyId,
                                     const Math::Vector::Vector3& position,
                                     const Math::Orientation::Quaternion& orientation,
                                     int modelCount,
                                     int scaleAxis,
                                     float scaleFactor );
    bool SaveSolverReplay( const char* path ) const;
    bool SavePresentationWithSolverHashes( const char* path, ReplayV2SaveResult* result = nullptr ) const;

  private:
    ReplayRecorder m_presentation;                                    // Bounded replay presentation recorder for recent-frame inspection.
    ReplaySolverRecorder m_solver;                                    // Same-tick solver-state recorder kept in tandem with presentation replay.
    ReplayEventRecorder m_events;                                     // Bounded intent/event stream kept beside v2 replay tracks.
    ReplayBranchInfo m_branch;                                        // Current live replay branch provenance.
    RunLoadedReplayPresentationState m_loadedPresentation;
    RunReplayScrubberState m_scrubber;
    RunReplayCameraState m_camera;
    RunReplayPathVisualizerState m_pathVisualizer;
    RunReplayPredictionState m_prediction;
    RunReplayCauseTreeState m_causeTree;
    RunReplayVelocityEditState m_velocityEdit;
    std::vector<ReplayPredictionGhostDrawRequest> m_predictionGhostDrawRequests;
    std::vector<uint8_t> m_focusModelMask;
    ReplayLauncherVisualSample m_launcherVisualBackup;
    // Invariant: replay render pose matching is a per-frame mark table capped by
    // the live model budget. It must not allocate while scrub/prediction views
    // are applied during rendering.
    std::array<uint8_t, MAX_GAME_MODELS> m_renderPoseBodyMatched = {};
    bool m_launcherVisualBackupActive = false;
};
} // namespace Basics
} // namespace SkullbonezCore
