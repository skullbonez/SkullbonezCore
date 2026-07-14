/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.h
Purpose:
  Composes replay's timeline, scrubber, presentation, prediction, and authoring owners.

Summary:
  ReplayRuntime sequences owner-to-owner work. The application shell supplies
  frame-scoped live-owner views; concrete replay owners retain their own state
  and implement their domain transitions.

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

#include "ReplayAuthoring.h"
#include "ReplayIdentity.h"
#include "ReplayPrediction.h"
#include "ReplayPresentation.h"
#include "ReplayScrubber.h"
#include "ReplayTimeline.h"

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


class ReplayRuntime
{
  public:
    using ReplayOverlayBuildInput = Runtime::ReplayOverlayBuildInput;
    using PathPickInput = ReplayPathPickInput;
    using PathPickResult = ReplayPathPickResult;
    using WorldPointerInput = ReplayWorldPointerInput;

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
    using PointerButtonEdges = ReplayPointerButtonEdges;

    using ScrubberInputFrame = ReplayScrubberInputFrame;
    using ScrubberUnavailableResult = ReplayScrubberUnavailableResult;

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
    ReplayPredictionPresentationView PredictionPresentationView() const;
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
    void NotifyVelocityEditApplied();
    bool PredictionGenerationPermitted() const noexcept;
    // Validation-only terminal transition: the sole engine process may decode
    // its frozen RVPD state and rebuild CPU presentation values after the last
    // rendered frame, but it can never schedule another prediction generation.
    void EnterOfflinePredictionVerification();
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
                                                                  const ReplayArtifactTopologyOwners& topology,
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
    bool SavePresentationWithSolverHashes( const char* path,
                                           ReplayV2SaveResult* result = nullptr,
                                           std::span<const ReplayVisualArchiveSample> visualPackets = {},
                                           std::span<const uint8_t> visualPredictionState = {} ) const;
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
    void ApplyAuthoringPredictionRequest();
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

    ReplayTimeline m_timeline;
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
    RunReplayProbeState m_probes; // CLI-only replay validation state owned with the workflows it drives.
#endif
    ReplayScrubber m_scrubberOwner;
    ReplayPresentation m_visualPresentation;
    ReplayAuthoring m_authoring;
    ReplayPrediction m_predictionOwner;
};
} // namespace Runtime
} // namespace SkullbonezCore
