/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.h
Purpose:
  Composes replay's timeline, scrubber, presentation, prediction, and authoring owners.

Summary:
  ReplayRuntime sequences owner-to-owner work. The application shell exchanges
  typed commands and read-only published views; concrete replay owners retain
  their own state and implement their domain transitions.

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
  - Full timeline, scrubber, presentation, prediction, and authoring state is
    private; external callers cannot recover mutable owner authority.
  - Stored dense rows use ModelRowHint; ReplayBodyId remains the identity check.
  - Scrub/prediction draw poses are presentation-only value overrides; replay
    must not backup or mutate live legacy object record pose for rendering.
  - Prediction cache cursors must be reset whenever target, ragdoll mode, or
    sample storage changes.
  - Prediction worker tasks must be idle before build scratch, trajectory slots,
    or private-engine state are cleared.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp
  - SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include "ReplayAuthoring.h"
#include "ReplayCoordination.h"
#include "ReplayIdentity.h"
#include "ReplayPrediction.h"
#include "ReplayPresentation.h"
#include "ReplayOverlayRenderer.h"
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
#include "ReplayProbeState.h"
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
    ReplayRuntime();

    // Publishes scalar input decisions without exposing replay owner storage.
    ReplayInputView BuildInputView() const noexcept;
    // Lifetime: returned references/spans are synchronous validation evidence;
    // callers must rebuild the view after any replay mutation.
    ReplayAutomationView BuildAutomationView() const;
    // Publishes replay-selected samples and const tool state for one late UI
    // pass. Window/UI facts remain caller-owned values.
    ReplayOverlay::ReplayOverlayStateView BuildOverlayStateView( bool editorModeEnabled,
                                                                 bool uiVisible,
                                                                 bool uiMinimized,
                                                                 RuntimeInteractionGestureKind gesture ) const;
    // Selects at most one historical track plus the prediction preview for the
    // current render turn; returned sample pointers are frame-local borrows.
    ReplayRenderSelectionView BuildRenderSelectionView() const;
    ReplayRenderFrameView
    PrepareRenderFrame( Rendering::RenderInstanceStore& renderInstances,
                        std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                        Physics::PhysicsEngine& physics,
                        const SceneEntityStore& entities,
                        RuntimeTools& runtimeTools,
                        RunEditorTracer& tracer,
                        int modelCount,
                        bool editorModeEnabled,
                        const RuntimeInteractionGesture& gesture,
                        int sceneFrame,
                        bool collisionVisualizer,
                        bool debugTransparentBodyPass,
                        const Math::Vector::Vector3& cameraTranslation,
                        const Math::Vector::Vector3& cameraUp,
                        uint64_t replayReserveGrowthEvents );
    void CompleteRenderFrame( bool submissionRendered,
                              int sceneFrame,
                              uint64_t replayReserveGrowthEvents,
                              RuntimeTools& runtimeTools );
    void CancelRenderFrame( RuntimeTools& runtimeTools );
    ReplayVisualPacket BuildVisualProjectionForValidation(
        Physics::PhysicsEngine& physics,
        const SceneEntityStore& entities,
        std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
        const Physics::PhysicsBodyStore& bodyStore,
        RuntimeTools& runtimeTools,
        const Math::Vector::Vector3& cameraEye,
        const Math::Vector::Vector3& cameraUp,
        uint64_t replayReserveGrowthEvents );

    // Validation-only terminal transition: the sole engine process may decode
    // its frozen RVPD state and rebuild CPU presentation values after the last
    // rendered frame, but it can never schedule another prediction generation.
    void EnterOfflinePredictionVerification();
    // Cold artifact verification operations intentionally expose no mutable
    // prediction or presentation owner state to the probe translation unit.
    bool
    LoadPredictionArchiveForVerification( std::span<const uint8_t> bytes, char* outReason, std::size_t reasonSize );
    // Cold validation command that serializes the owner-coherent path and
    // prediction pair without publishing their private storage.
    bool BuildPredictionArchiveForValidation( std::vector<uint8_t>& outBytes ) const;
    void ResetPredictionPresentationVerification();
    // Publishes reveal, trajectory, and marker caches for callers that project
    // restored prediction values without running the normal frame scheduler.
    // RenderPathVisualizer remains read-only and must follow this command.
    void PreparePredictionPresentation( Physics::PhysicsEngine& physics, const SceneEntityStore& entities );
    void ClearPathVisualizerState();

    bool SetVelocityEditEnabled( bool enabled );
    ReplayKeyboardVelocityEditResult ApplyKeyboardVelocityEdit( const ReplayKeyboardVelocityEditInput& input );
    void SetTrackPosition( RunReplayTrack track, float position );
    void PinSolverScrubberToPresent();
    void SetAllTrackPositions( float position );
    bool ResetScrubberState();
    ReplayScrubberInputFrame BeginReplayScrubberInputFrame( bool leftPressed, bool leftReleased, bool restoreDown );
    ReplayScrubberUnavailableResult ResetUnavailableScrubberSurface( bool loadedPresentation );
    void ClearCauseTreeFocusSelection();
    bool SetLiveAdvanceHeld( bool held );
    bool ShouldUseInspectionCamera() const;
    bool InspectionActive() const;
    bool InspectionMouseLookActive( bool rightMouseDown, bool uiWantsNativeCursor, bool uiBlocksCameraMouse ) const;
    bool ArmLoadedPresentationScrubber( float normalized, double now );
    void ClearCameraFocusForRestore();

    // Configures bounded recorder storage. runtimeBodyCapacity must be the
    // scene/run body cap known before capture so replay frames do not allocate.
    ReplayRecordingConfigResult
    ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath, int runtimeBodyCapacity );
    // Applies a UI or tool policy request. A true return means recorder windows
    // changed or queued policy state changed before recording was configured.
    bool ApplyMemoryPolicyRequest( const ReplayMemoryPolicyRequest& request );
    // Exposes the resolved policy for diagnostics/UI; callers must not infer
    // recorder capacity from raw requested fields.
    const ReplayMemoryPolicy& MemoryPolicy() const;
    void FlushHashLogs();
    void ResetTimeline( const char* sceneLabel );
    ReplaySceneTimelineResetResult BeginSceneTimelineReset( const ReplaySceneTimelineResetInput& input );
    ReplaySceneTimelineResetResult FinishSceneTimelineReset( const ReplaySceneTimelineResetInput& input );
    void ResetSceneTimeline( const ReplaySceneTimelineResetInput& input, const ReplaySceneTimelineResetOwners& owners );
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
    // Debug probes compose the same restore transaction and topology operands
    // as production restore. No whole-runtime probe fixture or Run backdoor is accepted.
    ReplayProbeTickResult TickProbes( const ReplayRestoreTransaction& transaction,
                                      const ReplayArtifactTopologyOwners& topology );

  private:
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
    void CaptureFrame( ReplayCaptureInput input, RuntimeTools& runtimeTools );
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
    SkullbonezCore::Core::MainMemoryReplayStats CollectMemoryStats() const;
    // Publishes the value-only replay facts consumed by the late HUD pass.
    // Memory accounting is sampled only when explicitly requested for the tab.
    ReplayHudStatus BuildHudStatus( bool includeMemoryStats ) const;
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
    void TickWorkspace( const ReplayWorkspaceFrameInput& input,
                        InputRouter& inputRouter,
                        RuntimeInteractionController& interaction,
                        Physics::PhysicsEngine& physics,
                        const SceneEntityStore& entities,
                        std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                        Environment::CameraCollection* cameras,
                        Geometry::Terrain* terrain,
                        RunCameraState& camera,
                        RunMousePickupState& mousePickup,
                        ReplayWorkspaceOutput& output );
    void ConfigureStartupWorkflows( const ReplayStartupRequest& request );
    ReplayFrameIntentResult ApplyFrameIntent( const ReplayFrameIntent& intent );
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
    // Advances and publishes the private prediction during frame update.
    // Callers must complete this before any replay overlay traversal begins.
    void UpdatePrediction( Physics::PhysicsEngine& physics,
                           const SceneEntityStore& entities,
                           const SkullbonezCore::Core::EngineConfig& config,
                           const Physics::PhysicsWorldForces& worldForces,
                           Threading::WorkerPool& workerPool,
                           bool scenePhysicsEnabled,
                           double simulationTimeSinceLastStart,
                           double simulationTotalTime );
    // Appends replay-owned records after RuntimeTools has rebuilt the shared
    // fixed-capacity tracer. RuntimeRenderer only submits the completed buffer.
    void AppendOverlayTrace( Physics::PhysicsEngine& physics,
                             const SceneEntityStore& entities,
                             RunEditorTracer& tracer,
                             const ReplayOverlayBuildInput& input );
    void RenderVelocityEditOverlay( Physics::PhysicsEngine& physics,
                                    bool editorModeEnabled,
                                    const RuntimeInteractionGesture& gesture,
                                    RunEditorTracer& tracer );
    // Routes value-only pointer facts through replay path selection. Store and
    // camera owners are explicit one-call borrows, not fields in the command.
    bool RouteWorldPointer( const ReplayWorldPointerInput& input,
                            const SceneEntityStore& entities,
                            const Physics::PhysicsBodyStore& bodyStore,
                            const Physics::ColliderStore& colliderStore,
                            std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                            Environment::CameraCollection* cameras,
                            Geometry::Terrain* terrain,
                            RunCameraState& camera,
                            RuntimeInteractionController& interaction,
                            InputRouter& inputRouter );
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
    // Applies one typed leave-replay command. External camera/input owners are
    // synchronous operands and are never retained by ReplayRuntime.
    bool ApplyInteractionExit( const ReplayInteractionExitInput& input,
                               Environment::CameraCollection* cameras,
                               Geometry::Terrain* terrain,
                               RunCameraState& camera,
                               RuntimeInteractionController& interaction,
                               InputRouter& inputRouter );
    // Clears replay gesture, scrubber, inspection-camera, and velocity-key
    // state as one focus-loss transition before generic input resets itself.
    void ApplyInputFocusLoss( Environment::CameraCollection* cameras,
                              Geometry::Terrain* terrain,
                              RunCameraState& camera,
                              RunCameraMode normalizedRestoreMode,
                              bool attachedFollow,
                              bool directorGrabbed,
                              RuntimeInteractionController& interaction,
                              InputRouter& inputRouter );
    // Clears replay gesture/camera state as one replay-owned scene transition.
    // The owner bundle is borrowed for this synchronous operation only.
    void ClearInteractionForSceneLoad( const ReplaySceneTimelineResetOwners& owners );
    // Clears replay-owned transient state and reports whether the camera owner
    // must execute an inspection-camera exit after the state transition.
    bool ClearInteractionForRuntimeTransition( RuntimeInteractionController& interaction, InputRouter& inputRouter );

  private:
    ReplayPathPickResult ApplyPathPick( const ReplayPathPickInput& input,
                                        const SceneEntityStore& entities,
                                        const Physics::PhysicsBodyStore& bodyStore,
                                        const Physics::ColliderStore& colliderStore,
                                        std::span<const Rendering::RenderInstancePresentationRecord> presentation );
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
                                const ReplayPathPickInput& pointerRay,
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
    // Private owner reads support replay's own cross-owner composition. The
    // application shell receives only the published value/view APIs above.
    const ReplayRecorder& Presentation() const;
    const ReplaySolverRecorder& Solver() const;
    const ReplayEventRecorder& Events() const;
    const ReplayBranchInfo& Branch() const;
    const RunLoadedReplayPresentationState& LoadedPresentation() const;
    ReplayScrubberView ScrubberView() const noexcept;
    RunCameraMode ReplayRestoreCameraMode() const noexcept;
    bool ReplayCameraActive() const noexcept;
    const RunReplayPathVisualizerState& PathVisualizer() const;
    const RunReplayPredictionState& Prediction() const;
    ReplayPredictionPresentationView PredictionPresentationView() const;
    std::span<const RunReplayPredictionFrame> ActivePredictionFrames() const;
    const RunReplayCauseTreeState& CauseTree() const;
    const RunReplayVelocityEditState& VelocityEdit() const;
    float TrackPosition( RunReplayTrack track ) const;
    bool LiveAdvanceHeld() const;
    bool HasPathVisualizerTarget() const;
    bool HasCameraFocus() const;
    bool VelocityEditActive() const;
    float SolverPresentTrackPosition() const;
    bool ShouldRenderScrubber( bool editorModeEnabled,
                               bool uiVisible,
                               bool uiMinimized,
                               RuntimeInteractionGestureKind gesture ) const;
    bool HasLoadedPresentation() const;
    const ReplayPresentationSample* LoadedPresentationSampleAtNormalized( float normalized ) const;
    const ReplayPresentationSample* LoadedPresentationLatestSample() const;
    bool IsScrubPaused() const;
    const ReplayPresentationSample* CurrentScrubSample() const;
    const ReplaySolverFrameSample* CurrentSolverScrubSample() const;
    const RunReplayPredictionFrame* CurrentPredictionScrubFrame() const;

    void ApplyAuthoringPredictionRequest();
    void ApplyPredictionUpdateResult( const ReplayPredictionUpdateResult& result );
    void ApplyPastTrajectoryUpdate( const ReplayPastTrajectoryUpdate& update );
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
    ReplayTimeline m_timeline;
    ReplayProbeRunner m_probeRunner;
    ReplayScrubber m_scrubberOwner;
    ReplayPresentation m_visualPresentation;
    ReplayAuthoring m_authoring;
    ReplayPrediction m_predictionOwner;
};
} // namespace Runtime
} // namespace SkullbonezCore
