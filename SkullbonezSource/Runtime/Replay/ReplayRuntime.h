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
  Cause tree: Replay contact graph used by the tool UI to explain which body or
    contact caused another replay body to matter.
  Velocity edit: Replay tool that displays and edits linear/angular velocity on
    the current path target.
  Runtime state: UI and tool state that belongs to replay but is still consumed
    by Run while the subsystem is being separated.
  Prediction cache: Incremental future-path data built from predicted solver
    frames under a render-frame budget.

Invariants:
  - Stored indices are hints; ReplayBodyId remains the identity check.
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

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
class GameModelCollection;
} // namespace GameObjects

namespace Basics
{
struct ReplayV2SaveResult;

inline constexpr std::size_t REPLAY_PREDICTION_GHOST_MAX_FRAMES = 24;

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
    float position = 1.0f;         // 0 = oldest retained sample, 1 = live edge.
    float presentationPosition = 1.0f;
    float solverPosition = 1.0f;
    int mouseX = 0;
    int mouseY = 0;
    double visibleUntil = 0.0;
    double saveMessageUntil = 0.0;
    char saveMessage[96] = {};
};

struct RunReplayPathTraceNode
{
    ReplayBodyId id;
    ReplayBodyId parentId;
    int modelIndex = -1;           // Fast lookup hint; ReplayBodyId remains authority.
    int parentModelIndex = -1;     // Fast lookup hint for contact-chain parents.
    ReplayFrameIndex firstFrame = 0;
    Math::Vector::Vector3 contactPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 contactNormal = Math::Vector::ZERO_VECTOR;
    int depth = 0;
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
    PredictionContact
};

enum class RunReplayCauseTreeRowKind
{
    Body,
    Manifold,
    SolverRow,
    PredictionContact
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
    float fixedContactHighlightSeconds = 0.0f;
    bool fixed = false;
};

struct RunReplayPredictionBodySample
{
    ReplayBodyId id;
    int modelIndex = -1;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
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

struct RunReplayPredictionState
{
    bool enabled = false;
    bool checkboxHovered = false;
    bool ragdollVisualsEnabled = true;
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
    std::vector<RunReplayPredictionFrame> buildFrames;
    std::vector<RunReplayPathTraceNode> futureNodes;
    // Incremental tree cursors. Prediction can contain thousands of frames, so
    // futureNodes is built over multiple render frames under the visualizer
    // budget instead of rebuilding the whole tree every frame.
    std::size_t futureNodesBuiltFrameCount = 0;
    std::size_t futureNodesBuiltContactIndex = 0;
    ReplayBodyId futureNodesBuiltTargetId;
    bool futureNodesBuiltRagdollVisuals = true;
    bool futureNodesBuiltFromBuildFrames = false;
    bool futureNodesCacheValid = false;
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
    bool SetLiveAdvanceHeld( bool held );
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

    RecordingConfigResult ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath );
    void FlushHashLogs();
    void ResetBranch();
    void ResetTimeline( const char* sceneLabel );
    bool IsPresentationEnabled() const;
    bool IsCaptureEnabled() const;
    ReplayRecorderStats PresentationStats() const;
    ReplayRecorderStats SolverStats() const;
    ReplayEventRecorderStats EventStats() const;
    ReplayFrameIndex NextEventFrameIndex() const;
    void CaptureFrame( ReplayCaptureInput input );
    bool ApplyPresentationSampleForRender( GameObjects::GameModelCollection& models,
                                           const ReplayPresentationSample& sample );
    bool ApplySolverSampleForRender( GameObjects::GameModelCollection& models, const ReplaySolverFrameSample& sample );
    bool ApplyPredictionFrameForRender( GameObjects::GameModelCollection& models,
                                        const RunReplayPredictionFrame& frame );
    void RestoreRenderPose( GameObjects::GameModelCollection& models );
    bool HasLoadedPresentation() const;
    const ReplayPresentationSample* LoadedPresentationSampleAtNormalized( float normalized ) const;
    const ReplayPresentationSample* LoadedPresentationLatestSample() const;
    bool IsScrubPaused() const;
    const ReplayPresentationSample* CurrentScrubSample() const;
    const ReplaySolverFrameSample* CurrentSolverScrubSample() const;
    const RunReplayPredictionFrame* CurrentPredictionScrubFrame() const;
    bool ResolveCauseTreeBodyPosition( ReplayBodyId id,
                                       const std::vector<GameObjects::GameModel>& models,
                                       Math::Vector::Vector3& outPosition,
                                       float* outRadius ) const;
    int ResolveVelocityEditModelIndex( const std::vector<GameObjects::GameModel>& models ) const;
    bool BuildCauseTreeRows( const std::vector<GameObjects::GameModel>& models );
    bool BuildPredictionGhostDrawRequests( const std::vector<GameObjects::GameModel>& models );
    const std::vector<ReplayPredictionGhostDrawRequest>& PredictionGhostDrawRequests() const;
    bool BuildFocusModelMask( const GameObjects::GameModelCollection& models );
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
    void RecordEditorTransformEvent( int modelIndex,
                                     uint32_t changedFlags,
                                     const GameObjects::GameModel& model,
                                     int modelCount,
                                     int scaleAxis,
                                     float scaleFactor );
    bool SaveSolverReplay( const char* path ) const;
    bool SavePresentationWithSolverHashes( const char* path, ReplayV2SaveResult* result = nullptr ) const;

  private:
    struct RenderPoseBackup
    {
        int modelIndex = -1;
        Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
        Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    };

    ReplayRecorder m_presentation; // Bounded replay presentation recorder for recent-frame inspection.
    ReplaySolverRecorder m_solver; // Same-tick solver-state recorder kept in tandem with presentation replay.
    ReplayEventRecorder m_events;  // Bounded intent/event stream kept beside v2 replay tracks.
    ReplayBranchInfo m_branch;     // Current live replay branch provenance.
    RunLoadedReplayPresentationState m_loadedPresentation;
    RunReplayScrubberState m_scrubber;
    RunReplayCameraState m_camera;
    RunReplayPathVisualizerState m_pathVisualizer;
    RunReplayPredictionState m_prediction;
    RunReplayCauseTreeState m_causeTree;
    RunReplayVelocityEditState m_velocityEdit;
    std::vector<RenderPoseBackup> m_renderPoseBackups;
    std::vector<ReplayPredictionGhostDrawRequest> m_predictionGhostDrawRequests;
    std::vector<uint8_t> m_focusModelMask;
    ReplayLauncherVisualSample m_launcherVisualBackup;
    bool m_launcherVisualBackupActive = false;
};
} // namespace Basics
} // namespace SkullbonezCore
