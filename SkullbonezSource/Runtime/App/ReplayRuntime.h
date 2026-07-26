/*
File: SkullbonezSource/Runtime/App/ReplayRuntime.h
Purpose:
  Composes Replay, Prediction, and Planning sibling owners at the application boundary.

Summary:
  Runtime/App sequences typed work across concrete sibling owners and exposes
  published value views to the application shell. Replay remains the lower
  capture/timeline/scrub package; Prediction and Planning retain their own state.

Mental model:
  ReplayRuntime is an App composition root, not a Replay-package owner. It
  sequences owner-to-owner work while concrete package owners retain state and
  implement their domain transitions.

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
  Lifecycle generation: Scene-load identity used to separate clear-phase
    interaction cleanup from activated-scene timeline setup.

Invariants:
  - Replay, Prediction, and Planning are sibling fields; none retains a
    retained reference to this App composition root or to another lower owner.
  - Full timeline, scrubber, presentation, prediction, planning, and authoring
    state is private; external callers cannot recover mutable owner authority.
  - Stored dense rows use ModelRowHint; Physics::PhysicsSceneObjectId remains the identity check.
  - Scrub/prediction draw poses are presentation-only value overrides; replay
    must not backup or mutate live legacy object record pose for rendering.
  - Prediction cache cursors must be reset whenever target, ragdoll mode, or
    sample storage changes.
  - Prediction worker tasks must be idle before build scratch, trajectory slots,
    or private-engine state are cleared.
  - Clear-phase observation does not borrow replacement-scene cameras or terrain;
    only activation may apply replay presentation against the populated scene.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayCoordination.h
  - SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.h
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include "../Replay/ReplayAuthoring.h"
#include "../Replay/ReplayCoordination.h"
#include "ReplayRuntimePackets.h"
#include "../Planning/ReplayPlanningRuntime.h"
#include "../Replay/ReplayIdentity.h"
#include "../Prediction/ReplayPrediction.h"
#include "../Replay/ReplayPresentation.h"
#include "../Planning/ReplayOverlayRenderer.h"
#include "../Replay/ReplayScrubber.h"
#include "../Replay/ReplayTimeline.h"

#include "../Replay/ReplayRecorder.h"
#include "../Replay/ReplayVisualPacket.h"
#include "../Prediction/ReplayPredictionScheduling.h"
#include "../Prediction/ReplayPredictionDrawing.h"
#include "../../Assets/AssetKeys.h"
#include "../../Core/SceneCapacity.h"
#include "../Prediction/TrajectoryStore.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Scene/SceneLifecycle.h"
#include "../Tools/RuntimeTools.h"
#include "../Replay/ReplayProbeState.h"
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
class Profiler;
struct ReplayTrajectoryAppearanceConfig;
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
class RuntimeTools;
class SceneController;
class DiagnosticsRuntime;
class SimulationSystem;
enum class GeneratedObjectTypeOverride;
struct CameraControlState;
struct OverlayDebugState;
struct RunMousePickupState;
class RuntimeRenderer;
struct SceneSessionState;
struct ReplayV2SaveResult;
struct ReplaySolverSampleRestoreContext;

namespace ReplayPresentationOperations
{
// App-level activation closes both lower Replay presentation state and the
// sibling Prediction owner before arming the loaded scrub position.
void ArmLoadedPresentation( float normalized,
                            double now,
                            ReplayScrubber& scrubber,
                            ReplayPresentation& presentation,
                            ReplayAuthoring& authoring,
                            ReplayPrediction& prediction,
                            RuntimeInteractionController& interaction );
} // namespace ReplayPresentationOperations

class ReplayRuntime
{
  public:
    explicit ReplayRuntime( Core::Profiler* profiler );

    // Publishes scalar input decisions without exposing replay owner storage.
    ReplayInputView BuildInputView() const noexcept;
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    // Lifetime: returned references/spans are synchronous validation evidence;
    // callers must rebuild the view after any replay mutation. The method is
    // absent from ordinary builds so diagnostics cannot enter the frame path.
    ReplayAutomationView BuildAutomationView() const;
#endif
    // Publishes replay-selected samples and const tool state for one late UI
    // pass. Window/UI facts remain caller-owned values.
    ReplayOverlay::ReplayOverlayStateView
    BuildOverlayStateView( bool editorModeEnabled,
                           bool uiVisible,
                           bool uiMinimized,
                           RuntimeInteractionGestureKind gesture,
                           std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                           const Physics::PhysicsBodyStore& bodyStore );
    // Selects at most one historical track plus the prediction preview for the
    // current render turn; returned sample pointers are frame-local borrows.
    ReplayPresentationSelection BuildPresentationSelection() const;
    // Render preparation is deliberately phased: pose mutation, overlay/ghost
    // construction, packet publication, then focus-mask/view selection.
    ReplayPresentationSelection ApplyRenderPose( Rendering::RenderInstanceStore& renderInstances,
                                                 Physics::PhysicsEngine& physics,
                                                 RuntimeTools& runtimeTools );
    void PrepareRenderOverlay( Physics::PhysicsEngine& physics,
                               const SceneEntityStore& entities,
                               EditorTracer& tracer,
                               const Core::ReplayTrajectoryAppearanceConfig& trajectoryAppearance,
                               bool editorModeEnabled,
                               const RuntimeInteractionGesture& gesture,
                               int sceneFrame,
                               std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords );
    void PublishRenderPacket( EditorTracer& tracer,
                              const Math::Vector::Vector3& cameraTranslation,
                              const Math::Vector::Vector3& cameraUp,
                              uint64_t replayReserveGrowthEvents );
    ReplayRenderFrameView BuildRenderFrameView( const ReplayPresentationSelection& selection,
                                                Physics::PhysicsEngine& physics,
                                                int modelCount,
                                                bool collisionVisualizer,
                                                bool debugTransparentBodyPass );
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
    // Forwards the presentation-only palette command; prediction/capture state
    // and published trajectory records are not rebuilt.
    ReplayPathColorMode CyclePathColorMode() noexcept;
    void ToggleGuideArcs() noexcept;
    void SetGuideArcsEnabled( bool enabled ) noexcept;
    void TogglePorkchopPanel() noexcept;
    bool QueueTripPlannerCommand( const ReplayTripPlannerCommand& command ) noexcept;

    ReplayKeyboardVelocityEditResult ApplyKeyboardVelocityEdit( const ReplayKeyboardVelocityEditInput& input );

    // Configures bounded recorder storage. runtimeBodyCapacity must be the
    // scene/run body cap known before capture so replay frames do not allocate.
    ReplayRecordingActivationResult
    ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath, int runtimeBodyCapacity );
    // Applies a UI or tool policy request. A true return means recorder windows
    // changed or queued policy state changed before recording was configured.
    bool ApplyMemoryPolicyRequest( const ReplayMemoryPolicyRequest& request );
    // Exposes the resolved policy for diagnostics/UI; callers must not infer
    // recorder capacity from raw requested fields.
    ReplayShutdownReport FinishShutdown();
    ReplaySceneTimelineResetResult BeginSceneTimelineReset( const ReplaySceneTimelineResetInput& input );
    ReplaySceneTimelineResetResult FinishSceneTimelineReset( const ReplaySceneTimelineResetInput& input );
    void ResetSceneTimeline( const ReplaySceneTimelineResetInput& input, const ReplaySceneTimelineResetOwners& owners );
    void ObserveSceneLifecycleAfterClear( const SceneLifecyclePacket& packet,
                                          RuntimeInteractionController& interaction,
                                          InputRouter& inputRouter );
    void ObserveSceneLifecycleAfterActivation( const SceneLifecyclePacket& packet,
                                               const ReplaySceneTimelineResetInput& input,
                                               const ReplaySceneTimelineResetOwners& owners );
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

#endif
    void CaptureFrame( ReplayCaptureInput input, RuntimeTools& runtimeTools );
    SkullbonezCore::Core::MainMemoryReplayStats CollectMemoryStats() const;
    // Publishes the value-only replay facts consumed by the late HUD pass.
    // Memory accounting is sampled only when explicitly requested for the tab.
    ReplayHudStatus BuildHudStatus( bool includeMemoryStats ) const;
    // Attaches branch/frame provenance and submits one already encoded event
    // value. Hashing and payload construction belong to ReplayRecorder domain
    // builders, leaving this boundary as composition only.
    void SubmitEvent( const ReplayEventCommand& command );
    void TickWorkspace( const ReplayWorkspaceFrameInput& input,
                        InputRouter& inputRouter,
                        RuntimeInteractionController& interaction,
                        Physics::PhysicsEngine& physics,
                        const SceneEntityStore& entities,
                        std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                        Environment::CameraCollection* cameras,
                        Geometry::Terrain* terrain,
                        CameraControlState& camera,
                        RunMousePickupState& mousePickup,
                        ReplayWorkspaceOutput& output );
    // Applies one editor/legacy-independent transport value through the same
    // concrete replay owners used by pointer controls. Recoverable unavailable
    // states publish bounded scrubber feedback instead of failing the run.
    void ApplyTransportCommand( const ReplayTransportCommand& command,
                                const ReplayTransportHostContext& host,
                                InputRouter& inputRouter,
                                RuntimeInteractionController& interaction,
                                Environment::CameraCollection* cameras,
                                Geometry::Terrain* terrain,
                                CameraControlState& camera,
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
                           const Gameplay::TornadoGameplay& tornadoGameplay,
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
                             EditorTracer& tracer,
                             const ReplayPredictionPresentationView& prediction,
                             const ReplayOverlayBuildInput& input,
                             bool drawPredictionOverlay = true );
    // Routes value-only pointer facts through replay path selection. Store and
    // camera owners are explicit one-call borrows, not fields in the command.
    bool RouteWorldPointer( const ReplayWorldPointerInput& input,
                            const SceneEntityStore& entities,
                            const Physics::PhysicsBodyStore& bodyStore,
                            const Physics::ColliderStore& colliderStore,
                            std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                            Environment::CameraCollection* cameras,
                            Geometry::Terrain* terrain,
                            CameraControlState& camera,
                            RuntimeInteractionController& interaction,
                            InputRouter& inputRouter );
    bool HasActiveInteractionState() const;
    // Applies one typed leave-replay command. External camera/input owners are
    // synchronous operands and are never retained by ReplayRuntime.
    bool ApplyInteractionExit( const ReplayInteractionExitInput& input,
                               Physics::PhysicsEngine& physics,
                               Environment::CameraCollection* cameras,
                               Geometry::Terrain* terrain,
                               CameraControlState& camera,
                               RuntimeInteractionController& interaction,
                               InputRouter& inputRouter );
    // Clears replay gesture, scrubber, inspection-camera, and velocity-key
    // state as one focus-loss transition before generic input resets itself.
    void ApplyInputFocusLoss( Environment::CameraCollection* cameras,
                              Geometry::Terrain* terrain,
                              CameraControlState& camera,
                              RunCameraMode normalizedRestoreMode,
                              bool attachedFollow,
                              bool directorGrabbed,
                              RuntimeInteractionController& interaction,
                              InputRouter& inputRouter );
    // Clears replay gesture/camera state as one replay-owned scene transition.
    // The owner bundle is borrowed for this synchronous operation only.
    void ClearInteractionForSceneLoad( RuntimeInteractionController& interaction, InputRouter& inputRouter );
    // Clears replay-owned transient state and reports whether the camera owner
    // must execute an inspection-camera exit after the state transition.
    bool ClearInteractionForRuntimeTransition( RuntimeInteractionController& interaction, InputRouter& inputRouter );
    // Application-shell camera composition. The root supplies its private
    // presentation/authoring owners to stateless presentation operations; host
    // camera and input owners remain synchronous operands and are not retained.
    void EnterInspectionCamera( Environment::CameraCollection* cameras,
                                CameraControlState& camera,
                                RunCameraMode normalizedCurrentMode,
                                RuntimeInteractionController& interaction,
                                InputRouter& inputRouter,
                                RunMousePickupState& mousePickup );
    void ExitInspectionCamera( Environment::CameraCollection* cameras,
                               Geometry::Terrain* terrain,
                               CameraControlState& camera,
                               RunCameraMode normalizedRestoreMode,
                               bool attachedFollow,
                               bool directorGrabbed,
                               RuntimeInteractionController& interaction,
                               InputRouter& inputRouter );

  private:
    // Writes the current presentation, solver hashes/checkpoints, and event
    // stream to an explicit cold-I/O binary v2 path.
    bool SavePresentationWithSolverHashes( const char* path,
                                           ReplayV2SaveResult* result = nullptr,
                                           std::span<const ReplayVisualArchiveSample> visualPackets = {},
                                           std::span<const uint8_t> visualPredictionState = {} ) const;
    // Owns scrubber save sequencing and status publication; file decode and
    // loaded-track state belong to ReplayTimeline.
    bool SavePresentationFromScrubber( double now );
    bool BeginLoadedPresentationActivationScrubber( bool hasLoadedPresentation,
                                                    InputRouter& inputRouter,
                                                    RuntimeInteractionController& interaction );
    void ArmLoadedPresentationScrubber( float normalized, double now, RuntimeInteractionController& interaction );
    void ClearCameraFocusForRestore();
    ReplayPathPickResult ApplyPathPick( const ReplayPathPickInput& input,
                                        const SceneEntityStore& entities,
                                        const Physics::PhysicsBodyStore& bodyStore,
                                        const Physics::ColliderStore& colliderStore,
                                        std::span<const Rendering::RenderInstancePresentationRecord> presentation );
    ReplayPathPickResult ApplyInterceptTargetPick( const ReplayPathPickInput& input,
                                                   const Physics::PhysicsBodyStore& bodyStore,
                                                   const Physics::ColliderStore& colliderStore );
    ReplayInspectionCameraAction TickScrubberInput( bool uiBlocksMouse,
                                                    bool editorModeEnabled,
                                                    bool scenePhysicsEnabled,
                                                    bool uiVisible,
                                                    bool uiMinimized,
                                                    int screenWidth,
                                                    int screenHeight,
                                                    double now,
                                                    InputRouter& inputRouter,
                                                    RuntimeInteractionController& interaction,
                                                    CameraControlState& camera,
                                                    ReplayWorkspaceOutput& output );

  private:
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
    void RefreshRetainedPredictionGeometry( const Math::Vector::Vector3& cameraEye,
                                            const Math::Vector::Vector3& cameraUp );
    void AttachRetainedPredictionGeometry( ReplayVisualPacket& packet ) const;
#ifdef _DEBUG
    // Runs the configured Debug startup probes after product artifact loading
    // has completed; early probe failures are returned in the value result.
    ReplayStartupResult RunStartupProbeWorkflows( const ReplayStartupWorkflowState& startup,
                                                  ReplayStartupResult result,
                                                  const ReplayRestoreTransaction& probeTransaction,
                                                  const ReplayArtifactTopologyOwners& probeTopology,
                                                  RunMousePickupState& probeMousePickup,
                                                  RunCameraMode probeNormalizedCurrentMode,
                                                  double probeNow );
#endif
    bool RestoreV2ArtifactTargetStateImpl( const ReplayRestoreTransaction& transaction,
                                           const ReplayArtifactTopologyOwners& topologyOwners,
                                           const char* path,
                                           ReplayFrameIndex requestedFrame,
                                           bool makeLiveBranch,
                                           bool injectTargetHashMismatchForProbe,
                                           RunReplayV2TargetRestoreResult& outResult,
                                           char* outReason,
                                           std::size_t reasonSize );
    // Lifetime: startup-bound diagnostics borrow shared only with concrete replay owners.
    Core::Profiler* m_profiler;
    ReplayTimeline m_timeline;
    ReplayProbeRunner m_probeRunner;
    ReplayScrubber m_scrubberOwner;
    ReplayPresentation m_visualPresentation;
    ReplayAuthoring m_authoring;
    ReplayPrediction m_predictionOwner;
    ReplayPlanningRuntime m_planningOwner;
    // Concept: prediction ribbons are a retained append-only command list.
    // Frame-local tool/cause overlays keep using RuntimeTools::Tracer(), while
    // this tracer changes only when trajectory publication or reveal advances.
    EditorTracer m_predictionDrawList;
    ReplayOverlay::ReplayPredictionDrawListState m_predictionDrawListState;
    ReplayVisualPacket m_predictionDrawPacket;
    Math::Vector::Vector3 m_predictionDrawCameraEye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 m_predictionDrawCameraUp = Math::Vector::ZERO_VECTOR;
    uint64_t m_predictionDrawStreamId = 1;
    uint64_t m_predictionDrawRevision = 0;
    uint64_t m_predictionAppearanceInvalidationCount = 0;
    bool m_predictionDrawPacketDirty = true;
    bool m_predictionDrawCameraValid = false;
    bool m_predictionRetainedRenderingActive = false;
    SceneLifecycleGenerationObserver m_sceneClearObserver;
    SceneLifecycleGenerationObserver m_sceneActivationObserver;
};
} // namespace Runtime
} // namespace SkullbonezCore
