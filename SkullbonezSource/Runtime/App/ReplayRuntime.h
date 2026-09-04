/*
Purpose:
  Composes Replay, Prediction, and Planning sibling owners at the application boundary.

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
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include "../Replay/ReplayAuthoring.h"
#include "../Replay/ReplayCoordination.h"
#include "../Automation/ReplayAutomationView.h"
#include "../Planning/ReplayPlanningRuntime.h"
#include "../Replay/ReplayIdentity.h"
#include "../Replay/ReplayRuntimePackets.h"
#include "../Prediction/ReplayPrediction.h"
#include "ReplayPredictionPresentation.h"
#include "../Replay/ReplayPresentation.h"
#include "../Planning/ReplayOverlayRenderer.h"
#include "../Replay/ReplayScrubber.h"
#include "../Replay/ReplayTimeline.h"

#include "../Replay/ReplayRecorder.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Replay/ReplayV2Artifact.h"
#include "../Replay/ReplayVisualPacket.h"
#include "../Prediction/ReplayPredictionScheduling.h"
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
#ifdef _DEBUG
#include "../../Core/FatalError.h"
#endif
#include "../../Maths/Quaternion.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../Physics/PhysicsWorldForces.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class Profiler;
class SbDiagnosticStore;
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
struct ReplayRuntimeTestAccess;
class InputRouter;
class RuntimeTools;
class EditorToolsOwner;
class SceneController;
class SceneWorld;
class AttachedCameraController;

namespace ReplayLiveRestoreOperations
{
// Builds the detached result published after one restore transaction reaches a
// success or recoverable-failure terminal phase.
ReplayLiveRestoreOutcome BuildOutcome( const ReplayRestoreTransaction& transaction, ReplayLiveRestoreKind kind,
                                       bool restored );
} // namespace ReplayLiveRestoreOperations

class DiagnosticsRuntime;
class SimulationSystem;
enum class GeneratedObjectTypeOverride;
struct CameraControlState;
struct OverlayDebugState;
struct RunMousePickupState;
class RuntimeRenderer;
struct SceneSessionState;
struct ReplayV2SaveResult;
struct ReplayV2SolverHashSample;

// Immutable artifact rows needed while App advances one restore target. The
// checkpoint-selection tables remain in the loading phase and cannot be
// recovered through this stepping view.
struct ReplayRestoreStepView
{
    std::span<const ReplayV2SolverHashSample> hashes;
    std::span<const ReplayEventSample> events;
    std::span<const ReplayPresentationSample> presentationSamples;
};
#ifdef _DEBUG
struct ReplayScrubProbeDiagnostic;
struct ReplayStartupProbeContinuationTestAccess;

// Invariant: restore probes apply camera mode and attached-follow state as one
// synchronous camera transition; callers cannot supply only part of it.
struct ReplayProbeRestoreCameraState
{
    CameraControlState& camera;
    RunCameraMode restoreMode;
    bool attachedFollow;
};

// Invariant: startup probe scene description and every target restore observe
// the same UI and generated-object overrides for the whole continuation step.
struct ReplayStartupProbeSceneOverrides
{
    SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides;
    GeneratedObjectTypeOverride& generatedObjectType;
};
#endif

#ifdef _DEBUG
// Invariant: a Debug startup may pause only while App services one typed
// camera/input or restored-branch action. This cursor retains detached restore
// evidence and internally addressed values, never a borrowed reference to a
// process owner.
class ReplayStartupProbeContinuation
{
  public:
    enum class Phase : uint8_t
    {
        Idle,
        Running,
        AwaitingApplication,
        ApplicationApplied,
        Complete,
        Failed
    };

    enum class PendingAction : uint8_t
    {
        None,
        ActivateLoadedPresentation,
        ApplyRestoredBranchTimeline
    };

    explicit ReplayStartupProbeContinuation( double applicationTimeSeconds )
        : m_applicationTimeSeconds( applicationTimeSeconds )
    {
    }

    ReplayStartupProbeContinuation( const ReplayStartupProbeContinuation& ) = delete;
    ReplayStartupProbeContinuation& operator=( const ReplayStartupProbeContinuation& ) = delete;
    ReplayStartupProbeContinuation( ReplayStartupProbeContinuation&& ) = delete;
    ReplayStartupProbeContinuation& operator=( ReplayStartupProbeContinuation&& ) = delete;

    static constexpr bool IsLegalTransition( Phase current, Phase next )
    {
        return ( current == Phase::Idle && next == Phase::Running ) ||
               ( current == Phase::Running && next == Phase::AwaitingApplication ) ||
               ( current == Phase::AwaitingApplication && next == Phase::ApplicationApplied ) ||
               ( current == Phase::AwaitingApplication && next == Phase::Failed ) ||
               ( current == Phase::ApplicationApplied && next == Phase::Running ) ||
               ( current == Phase::Running && ( next == Phase::Complete || next == Phase::Failed ) );
    }

    static void RequireLegalTransitionOrFatal( Phase current, Phase next, const char* operation )
    {
        if ( !IsLegalTransition( current, next ) )
        {
            SB_FATAL( "Runtime/ReplayStartupProbeContinuation",
                      "Illegal startup-probe continuation transition. operation=%s current=%u next=%u", operation,
                      static_cast<unsigned int>( current ), static_cast<unsigned int>( next ) );
        }
    }

    static constexpr bool IsApplicationStateCoherent( Phase phase, PendingAction action, bool hasRestore )
    {
        if ( phase != Phase::AwaitingApplication )
        {
            return action == PendingAction::None;
        }

        return action != PendingAction::None && ( action != PendingAction::ApplyRestoredBranchTimeline || hasRestore );
    }

    static void RequireApplicationStateOrFatal( Phase phase, PendingAction action, bool hasRestore, const char* operation )
    {
        if ( !IsApplicationStateCoherent( phase, action, hasRestore ) )
        {
            SB_FATAL( "Runtime/ReplayStartupProbeContinuation",
                      "Startup-probe application state is incoherent. operation=%s phase=%u action=%u restore=%u", operation,
                      static_cast<unsigned int>( phase ), static_cast<unsigned int>( action ), hasRestore ? 1u : 0u );
        }
    }

    Phase CurrentPhase() const
    {
        return m_phase;
    }

    PendingAction ApplicationAction() const
    {
        RequireApplicationStateOrFatal( m_phase, m_pendingAction, m_restore.has_value(), "ReadApplicationAction" );
        return m_pendingAction;
    }

    bool IsTerminal() const
    {
        return m_phase == Phase::Complete || m_phase == Phase::Failed;
    }

  private:
    friend class ReplayRuntime;
    friend struct ReplayStartupProbeContinuationTestAccess;

    enum class Step : uint8_t
    {
        Load,
        Checkpoint,
        Target,
        Branch,
        Failure,
        Complete
    };

    enum class Resume : uint8_t
    {
        None,
        CompleteLoad,
        CompleteCheckpoint,
        CompleteBranchPreparation,
        CompleteBranchRestore
    };

    void AdvanceOrFatal( Phase next, const char* operation );

    void RejectPendingApplicationOrFatal( const char* operation )
    {
        // Invariant: a terminal cursor cannot retain an action that App could
        // service after failure. Clear the returned value before closing the
        // finite-state edge, then prove the resulting state is self-consistent.
        m_pendingAction = PendingAction::None;
        RequireLegalTransitionOrFatal( m_phase, Phase::Failed, operation );
        m_phase = Phase::Failed;
        RequireApplicationStateOrFatal( m_phase, m_pendingAction, m_restore.has_value(), operation );
    }

    Phase m_phase = Phase::Idle;
    Step m_step = Step::Load;
    Resume m_resume = Resume::None;
    PendingAction m_pendingAction = PendingAction::None;
    double m_applicationTimeSeconds = 0.0;
    char m_sceneLabel[260] = {};

    // Invariant: emplace gives the transaction one stable address across the
    // App-serviced action; its self-referential diagnostic strings are never
    // moved, and its scene-label view points only into this value cursor.
    std::optional<ReplayRestoreTransaction> m_restore;
    ReplayLiveRestoreRequest m_restoreRequest;
    ReplayLiveRestoreOutcome m_restoreOutcome;
    ReplaySolverFrameSample m_checkpoint;
    ReplayV2SolverCheckpointLoadResult m_checkpointLoadResult;

    // Invariant: these counts are detached artifact evidence captured before
    // activation and reported only after App services the pending action.
    std::size_t m_loadedVisualPacketCount = 0;
    std::size_t m_loadedVisualPredictionBytes = 0;
};
#endif

namespace ReplayPresentationOperations
{
void EnterInspectionCamera( ReplayPresentation& presentation, Environment::CameraCollection* cameras,
                            CameraControlState& camera, RunCameraMode normalizedCurrentMode,
                            RuntimeInteractionController& interaction, InputRouter& inputRouter,
                            RunMousePickupState& mousePickup, uint32_t inspectionCameraHash = CAMERA_FREE );
void ExitInspectionCamera( ReplayPresentation& presentation, const ReplayAuthoring& authoring,
                           Environment::CameraCollection* cameras, Geometry::Terrain* terrain, CameraControlState& camera,
                           RunCameraMode normalizedRestoreMode, bool attachedFollow, bool directorGrabbed,
                           RuntimeInteractionController& interaction, InputRouter& inputRouter );
bool BeginLoadedPresentationActivation( bool hasLoadedPresentation, ReplayScrubber& scrubber,
                                        ReplayPresentation& presentation, ReplayAuthoring& authoring,
                                        RuntimeInteractionController& interaction, InputRouter& inputRouter );

// App-level activation closes both lower Replay presentation state and the
// sibling Prediction owner before arming the loaded scrub position.
void ArmLoadedPresentation( float normalized, double now, ReplayScrubber& scrubber, ReplayPresentation& presentation,
                            ReplayAuthoring& authoring, ReplayPrediction& prediction,
                            RuntimeInteractionController& interaction );
} // namespace ReplayPresentationOperations

inline ReplayToolGestureView ProjectReplayToolGesture( const RuntimeInteractionGesture& gesture ) noexcept
{
    ReplayToolGestureView view;
    view.body = gesture.body;
    view.axis = gesture.axis;
    view.angular = gesture.angular;

    switch ( gesture.kind )
    {
    case RuntimeInteractionGestureKind::ReplayScrubDrag:
        view.kind = ReplayToolGestureKind::ScrubDrag;
        break;
    case RuntimeInteractionGestureKind::ReplayVelocityDrag:
        view.kind = ReplayToolGestureKind::VelocityDrag;
        break;
    case RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag:
        view.kind = ReplayToolGestureKind::PredictionHorizonDrag;
        break;
    case RuntimeInteractionGestureKind::ReplayCauseTreeDrag:
        view.kind = ReplayToolGestureKind::CauseTreeDrag;
        break;
    default:
        break;
    }

    return view;
}

// App owns the probe workflow because debug verification composes lower Replay
// state with the sibling Prediction owner. ReplayProbeState.h contains only the
// bounded values and therefore never exposes this upper-package authority.
class ReplayProbeRunner
{
  public:
    explicit ReplayProbeRunner( Core::SbDiagnosticStore& resultDiagnostics ) : m_resultDiagnostics( resultDiagnostics )
    {
    }

    // Returns whether live prediction generation remains permitted after the
    // startup capability request is installed.
    bool Configure( const ReplayStartupRequest& request );
    const ReplayStartupWorkflowState& Startup() const noexcept
    {
        return m_startup;
    }
#ifdef _DEBUG
    // Installs Debug-only CLI probe state after Configure has copied the
    // product load request and capability bit.
    void ConfigureDebug( const ReplayStartupRequest& request );

    // Lifetime: outDiagnostic borrows the selected body's bounded name only
    // until TickProbes publishes the row synchronously.
    SkullbonezCore::Core::SbResult TickScrubProbe( SceneWorld& world, const ReplayTimeline& timeline,
                                                   ReplayPresentation& presentation,
                                                   ReplayScrubProbeDiagnostic* outDiagnostic );
    ReplayProbeRestoreRequest PrepareRestoreProbe( const ReplayTimeline& timeline );
    SkullbonezCore::Core::SbResult CompleteRestoreProbe( const ReplayProbeRestoreRequest& request, bool restored,
                                                         const char* reason );
    ReplayProbeSaveRequest PrepareSaveProbe( const ReplayTimeline& timeline );
    void CompleteSaveProbe( const ReplayProbeSaveRequest& request, const SkullbonezCore::Core::SbResult& result );
    SkullbonezCore::Core::SbResult CurrentFailure() const;
    void RecordFailure( const SkullbonezCore::Core::SbResult& result );
    SkullbonezCore::Core::SbResult VerifyLoadedPresentationBeforeActivation(
        ReplayTimeline& timeline, ReplayScrubber& scrubber, ReplayPresentation& presentation, ReplayAuthoring& authoring,
        ReplayPrediction& prediction, ReplayPredictionPresentation& predictionPresentation, SceneWorld& world,
        EditorToolsOwner& editorTools, RuntimeTools& runtimeTools, std::size_t& outVisualPacketCount,
        std::size_t& outVisualPredictionBytes );
    SkullbonezCore::Core::SbResult VerifyLoadedPresentationAfterActivation( ReplayTimeline& timeline,
                                                                            ReplayScrubber& scrubber,
                                                                            ReplayPresentation& presentation,
                                                                            SceneWorld& world, std::size_t visualPacketCount,
                                                                            std::size_t visualPredictionBytes );
    SkullbonezCore::Core::SbResult PrepareCheckpointFileProbe( const char* path, ReplaySolverFrameSample& outCheckpoint,
                                                               ReplayV2SolverCheckpointLoadResult& outLoadResult );
    SkullbonezCore::Core::SbResult CompleteCheckpointFileProbe( const char* path, const ReplaySolverFrameSample& checkpoint,
                                                                const ReplayV2SolverCheckpointLoadResult& loadResult,
                                                                bool restored, const char* reason );
    SkullbonezCore::Core::SbResult CompleteTargetFileProbe( const char* path, const RunReplayV2TargetRestoreResult& result,
                                                            bool restored, const char* reason );
    ReplayFailureProbeRequest BeginFailureFileProbe( const char* path );
    ReplayFailureProbeRequest AdvanceFailureFileProbe( const ReplayFailureProbeRequest& request,
                                                       const ReplayFailureProbeStepResult& result );
    SkullbonezCore::Core::SbResult BeginBranchFileProbe( ReplayTimeline& timeline, const char* path );
    SkullbonezCore::Core::SbResult CompleteBranchFileProbePreparation( ReplayTimeline& timeline, ReplayScrubber& scrubber,
                                                                       double now, ReplayLiveRestoreRequest& outRequest );
    SkullbonezCore::Core::SbResult CompleteBranchFileProbe( const char* path, const ReplayLiveRestoreOutcome& outcome );
#endif

  private:
    SkullbonezCore::Core::SbResult ReplayProbeFailure( const char* message ) const;
    Core::SbDiagnosticStore& m_resultDiagnostics;
    ReplayStartupWorkflowState m_startup;
#ifdef _DEBUG
    ReplayProbeState m_probes;
    ReplayFailureFileProbeState m_failureFile;
#endif
};

// Detached replay-inspection values that are not part of the replay artifact.
// ReplayRuntime translates this recording baseline back through the concrete
// Planning, Replay presentation, and Camera owners before turn zero is routed.
struct ReplayInteractionRecordingCauseState
{
    ReplayCauseInspectionMode mode = ReplayCauseInspectionMode::Inactive;
    ReplayCauseInspectorTab activeTab = ReplayCauseInspectorTab::Summary;
    int selectedRow = -1;
    int selectedDetailContactRow = -1;
    int solverDetailFirstRow = 0;
    int rawRecordFirstRow = 0;
    int iterationsFirstRow = 0;
    ReplayFrameIndex sourceFrame = 0;
    ReplayFrameIndex targetFrame = 0;
    ReplayFrameIndex presentedFrame = 0;
    bool detailVisible = false;
    bool ownsPause = false;
    bool transportPending = false;
    bool transportInFlight = false;
    bool returnIssued = false;
    float easedProgress = 0.0f;
    float drawerProgress = 0.0f;
};

#if defined( SKULLBONEZ_SKARNESS )
struct ReplaySkarnessState
{
    ReplayInputView input;
    bool predictionBuilding = false;
    bool predictionComplete = false;
    bool predictionDirty = false;
    bool predictionRestartPending = false;
    bool predictionGenerationPermitted = false;
    bool predictionHighDetail = false;
    bool ragdollVisualsEnabled = false;
    bool pastPathVisible = false;
    float predictionHorizonSeconds = 0.0f;
    uint32_t predictionGeneration = 0;
    uint64_t predictionSourceTargetId = 0;
    ReplayFrameIndex predictionSourceFrame = 0;
    uint64_t predictionSourceSolverHash = 0;
    uint32_t committedPredictionFrames = 0;
    uint32_t incompleteContactFrameCount = 0;
    uint64_t publishedPredictionTargetId = 0;
    uint32_t publishedPredictionFrames = 0;
    uint32_t trajectoryRecordCount = 0;
    uint32_t selectedPastRootPointCount = 0;
    uint32_t selectedFutureRootPointCount = 0;
    uint32_t contactChildIncomingCount = 0;
    uint32_t contactChildOutgoingCount = 0;
    uint32_t childOutgoingPreEntryPointCount = 0;
    uint32_t retainedEntryMarkerCount = 0;
    uint32_t retainedEndMarkerCount = 0;
    uint32_t drawnCollisionWireframeCount = 0;
    uint32_t drawnEndingWireframeCount = 0;
    uint32_t collisionWireframePathMismatchCount = 0;
    uint32_t endingWireframePathMismatchCount = 0;
    uint32_t futureNodeCount = 0;
    uint32_t retainedLineFloatCount = 0;
    uint32_t retainedRibbonVertexFloatCount = 0;
    uint32_t causeTreeRowCount = 0;
    uint64_t causeTreeRowBuildCount = 0;
    uint64_t causeTreeRowCacheHitCount = 0;
    bool causeWindowAvailable = false;
    bool causeInspectorOpen = false;
    float causeInspectorDrawerProgress = 0.0f;
    int selectedCauseRow = -1;
    int causeInspectionMode = 0;
    float causeTransitionProgress = 0.0f;
    int inspectionCameraFocusKind = 0;
    bool inspectionFocusFadeActive = false;
    uint32_t inspectionFocusObjectCount = 0;
    uint64_t selectedCausePrimaryId = 0;
    uint64_t selectedCauseCounterpartId = 0;
    uint32_t causeContactPointCount = 0;
    uint32_t submittedCauseContactPointCount = 0;
    uint32_t submittedCauseContactBodyCount = 0;
    uint64_t inspectionPathFocusPrimaryId = 0;
    uint64_t inspectionPathFocusCounterpartId = 0;
    uint32_t inspectionFocusedPathRangeCount = 0;
    uint32_t inspectionContextPathRangeCount = 0;
    uint32_t inspectionFocusedPathSegmentCount = 0;
    uint32_t inspectionContextPathSegmentCount = 0;
    uint32_t inspectionPathOpacityMismatchCount = 0;
    bool inspectionPathFocusActive = false;
    bool retainedPathGeometrySaturated = false;
    bool visualPacketHasGeometry = false;
    ReplayTrajectorySubmissionProbeStats trajectorySubmission;
};
#endif

class ReplayRuntime
{
  public:
    ReplayRuntime( Core::SbDiagnosticStore& resultDiagnostics, Core::Profiler* profiler );

    // Publishes scalar input decisions without exposing replay owner storage.
    ReplayInputView BuildInputView() const noexcept;
    const RunReplayCauseTreeState& CauseTree() const noexcept;
    ReplayCauseInspectionView CauseInspectionView() const noexcept;
#if defined( SKULLBONEZ_SKARNESS )
    ReplaySkarnessState BuildSkarnessState() const noexcept;
#endif
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    // Lifetime: returned references/spans are synchronous validation evidence;
    // callers must rebuild the view after any replay mutation. The method is
    // absent from ordinary builds so diagnostics cannot enter the frame path.
    ReplayAutomationView BuildAutomationView() const;
#endif

    // Publishes replay-selected samples and const tool state for one late UI
    // pass. Window/UI facts remain caller-owned values.
    ReplayOverlay::ReplayOverlayStateView
    BuildOverlayStateView( bool editorModeEnabled, bool uiVisible, bool uiMinimized, RuntimeInteractionGestureKind gesture,
                           std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                           const Physics::PhysicsBodyStore& bodyStore );
    const UI::UIDrawList& ComposeOverlayDrawList( const ReplayOverlay::ReplayOverlayStateView& replay,
                                                  bool gameUiSurfaceActive, bool scenePhysicsEnabled,
                                                  RuntimeInteractionGestureKind gesture,
                                                  ReplayOverlay::ReplayOverlayViewport viewport, double nowSeconds );

    // Selects at most one historical track plus the prediction preview for the
    // current render turn; returned sample pointers are frame-local borrows.
    ReplayFrameSelection BuildPresentationSelection() const;

    // Render preparation is deliberately phased: pose mutation, overlay/ghost
    // construction, packet publication, then focus-mask/view selection.
    ReplayFrameSelection ApplyRenderPose( Rendering::RenderInstanceStore& renderInstances, Physics::PhysicsEngine& physics,
                                          RuntimeTools& runtimeTools );
    void PrepareRenderOverlay( Physics::PhysicsEngine& physics, const SceneEntityStore& entities, EditorTracer& tracer,
                               const Core::ReplayTrajectoryAppearanceConfig& trajectoryAppearance, bool editorModeEnabled,
                               const RuntimeInteractionGesture& gesture, int sceneFrame,
                               std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords );
    void PublishRenderPacket( EditorTracer& tracer, const Math::Vector::Vector3& cameraTranslation,
                              const Math::Vector::Vector3& cameraUp, uint64_t replayReserveGrowthEvents );
    ReplayRenderFrameViews BuildRenderFrameViews( const ReplayFrameSelection& selection, Physics::PhysicsEngine& physics,
                                                  int modelCount, bool collisionVisualizer, bool debugTransparentBodyPass );
    void CompleteRenderFrame( bool submissionRendered, int sceneFrame, uint64_t replayReserveGrowthEvents,
                              RuntimeTools& runtimeTools );
    void CancelRenderFrame( RuntimeTools& runtimeTools );

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
    ReplayRecordingActivationResult ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath,
                                                        int runtimeBodyCapacity );

    // Cold recording boundary: writes the Replay-owned presentation/solver/event
    // baseline without exposing recorder storage to App.
    bool SaveInteractionRecordingBaseline( const char* path ) const;

    // Applies a UI or tool policy request. A true return means recorder windows
    // changed or queued policy state changed before recording was configured.
    bool ApplyMemoryPolicyRequest( const ReplayMemoryPolicyRequest& request );

    // Sets the causal-unfold pacing the operator surface authors. Presentation
    // only: it changes how fast an already-computed horizon is drawn, never what
    // was simulated.
    void ApplyPredictionRevealRate( float revealRate );

    // Exposes the resolved policy for diagnostics/UI; callers must not infer
    // recorder capacity from raw requested fields.
    ReplayShutdownReport FinishShutdown();
    ReplaySceneTimelineResetResult BeginSceneTimelineReset( const ReplaySceneTimelineResetInput& input );
    ReplaySceneTimelineResetResult FinishSceneTimelineReset( const ReplaySceneTimelineResetInput& input );
    void ResetSceneTimeline( const ReplaySceneTimelineResetInput& input, InputRouter& inputRouter,
                             RuntimeInteractionController& interaction, Environment::CameraCollection* cameras,
                             Geometry::Terrain* terrain, CameraControlState& camera, RunCameraMode normalizedRestoreMode,
                             bool attachedFollow, bool directorGrabbed );
    void ObserveSceneLifecycleAfterClear( const SceneLifecyclePacket& packet, RuntimeInteractionController& interaction,
                                          InputRouter& inputRouter );
    void ObserveSceneLifecycleAfterActivation( const SceneLifecyclePacket& packet,
                                               const ReplaySceneTimelineResetInput& input, InputRouter& inputRouter,
                                               RuntimeInteractionController& interaction,
                                               Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                                               CameraControlState& camera, RunCameraMode normalizedRestoreMode,
                                               bool attachedFollow, bool directorGrabbed );
    bool RestoreSolverSampleAsLive( ReplayRestoreTransaction& transaction, SceneWorld& world, SceneSessionState& scene,
                                    OverlayDebugState& debug, RuntimeTools& runtimeTools,
                                    const ReplaySolverFrameSample& sample );

    // Restores one selected artifact target through the transaction's phase
    // invariant. SceneController is borrowed as the concrete scene/session
    // owner; the focused restore phases retain no participant pointer.
    bool RestoreV2ArtifactTargetState( ReplayRestoreTransaction& transaction, SceneController& sceneController,
                                       OverlayDebugState& debug, EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                       SimulationSystem& simulation, const SkullbonezCore::Core::EngineConfig& config,
                                       Assets::AssetSystem& assets, Threading::WorkerPool& workerPool,
                                       SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                       GeneratedObjectTypeOverride& generatedObjectTypeOverride );

    // Applies branch provenance and advances a verified restore to Complete.
    // This phase must run before CompleteLiveRestoreScrubber.
    void ApplyRestoredBranchTimeline( ReplayRestoreTransaction& transaction, const ReplayLiveRestoreOutcome& outcome,
                                      SceneController& sceneController, InputRouter& inputRouter,
                                      RuntimeInteractionController& interaction, CameraControlState& camera,
                                      RunCameraMode normalizedRestoreMode, bool attachedFollow, bool directorGrabbed );

    // Publishes the terminal result to the scrubber. The implementation fails
    // fatally if success has not reached Complete or failure has not reached
    // Failed/RolledBack.
    void CompleteLiveRestoreScrubber( const ReplayRestoreTransaction& transaction, const ReplayLiveRestoreRequest& request,
                                      ReplayLiveRestoreOutcome& outcome );
    void CompletePlanningTransition( uint64_t token, bool succeeded ) noexcept;
#ifdef _DEBUG
    // Debug probes use the production phase transaction and receive concrete
    // owners only for the synchronous operation that needs them.
    ReplayProbeTickResult TickProbes( SceneController& sceneController, OverlayDebugState& debug,
                                      EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                      const SkullbonezCore::Core::EngineConfig& config, Assets::AssetSystem& assets,
                                      const ReplaySceneTimelineResetInput& timelineReset,
                                      DiagnosticsRuntime& diagnosticsRuntime, InputRouter& inputRouter,
                                      RuntimeInteractionController& interaction,
                                      const ReplayProbeRestoreCameraState& restoreCamera );

    // Publishes detached diagnostic values without retaining diagnostics or
    // scene authority in Replay or its transaction.
    void PublishRestoreDiagnostic( const ReplayRestoreTransaction& transaction, DiagnosticsRuntime& diagnosticsRuntime,
                                   const SceneSessionState& scene ) const;

#endif
    void CaptureFrame( int sceneFrame, float physicsDt, const ReplayWorldPresentationSample& world,
                       const ReplayCameraSample& camera, Physics::PhysicsEngine& physics,
                       const Gameplay::TornadoGameplay& tornadoGameplay, const SceneEntityStore& entities,
                       RuntimeTools& runtimeTools );
    SkullbonezCore::Core::MainMemoryReplayStats CollectMemoryStats() const;

    // Publishes the value-only replay facts consumed by the late HUD pass.
    // Memory accounting is sampled only when explicitly requested for the tab.
    ReplayHudStatus BuildHudStatus( bool includeMemoryStats ) const;

    // Attaches branch/frame provenance and submits one already encoded event
    // value. Hashing and payload construction belong to ReplayRecorder domain
    // builders, leaving this boundary as composition only.
    void SubmitEvent( const ReplayEventCommand& command );
    void TickWorkspace( const ReplayWorkspaceFrameInput& input, InputRouter& inputRouter,
                        RuntimeInteractionController& interaction, SceneWorld& world, CameraControlState& camera,
                        AttachedCameraController& attachedCamera, RunMousePickupState& mousePickup,
                        ReplayWorkspaceOutput& output );

    // Each overload names only the host owners needed by that action. App
    // visits the closed command variant and performs the composition-root dispatch.
    void ApplyTransportCommand( const ReplaySetRecordingEnabledCommand& command, double now );
    void ApplyTransportCommand( const ReplayJumpToStartCommand&, RuntimeInteractionController& interaction, double now,
                                ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplayJumpToEndCommand&, RuntimeInteractionController& interaction, double now,
                                ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplayStepBackwardCommand&, RuntimeInteractionController& interaction, double now,
                                ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplayStepForwardCommand&, RuntimeInteractionController& interaction, double now,
                                ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplayTogglePlayPauseCommand&, InputRouter& inputRouter,
                                RuntimeInteractionController& interaction, CameraControlState& camera, double now,
                                ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplaySetRevealSpeedCommand& command, double now );
    void ApplyTransportCommand( const ReplayScrubCommand& command, RuntimeInteractionController& interaction, double now,
                                ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplayTogglePredictionCommand&, RuntimeInteractionController& interaction, double now,
                                ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplaySetPredictionDetailModeCommand& command, Environment::CameraCollection* cameras,
                                Geometry::Terrain* terrain, CameraControlState& camera, RunCameraMode normalizedRestoreMode,
                                bool attachedFollow, bool directorGrabbed, RuntimeInteractionController& interaction,
                                InputRouter& inputRouter, double now );
    void ApplyTransportCommand( const ReplaySetPredictionHorizonCommand& command, double now );
    void ApplyTransportCommand( const ReplaySetVelocityEditEnabledCommand& command, InputRouter& inputRouter,
                                RuntimeInteractionController& interaction, CameraControlState& camera, double now,
                                ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplaySetRagdollVisualsEnabledCommand& command, double now );
    void ApplyTransportCommand( const ReplaySetPastPathVisibleCommand& command, double now );
    void ApplyTransportCommand( const ReplayRestoreBranchCommand&, RuntimeInteractionController& interaction, double now,
                                ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplaySaveCommand&, double now, ReplayWorkspaceOutput& output );
    ReplayTransportLoadResult BeginTransportLoad( const ReplayLoadCommand&, HWND window, double now );
    void ActivateLoadedTransport( Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                                  CameraControlState& camera, RunCameraMode normalizedCurrentMode,
                                  RunCameraMode normalizedRestoreMode, bool attachedFollow, bool directorGrabbed,
                                  RuntimeInteractionController& interaction, InputRouter& inputRouter,
                                  RunMousePickupState& mousePickup, double now );
    void ApplyTransportCommand( const ReplayReturnToLiveCommand&, Environment::CameraCollection* cameras,
                                Geometry::Terrain* terrain, CameraControlState& camera, RunCameraMode normalizedRestoreMode,
                                bool attachedFollow, bool directorGrabbed, RuntimeInteractionController& interaction,
                                InputRouter& inputRouter, double now, ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplaySelectCauseRowCommand& command, RuntimeInteractionController& interaction,
                                double now, ReplayWorkspaceOutput& output );
    void ApplyTransportCommand( const ReplaySetCauseInspectorOpenCommand& command, double now );
    void ConfigureStartupWorkflows( const ReplayStartupRequest& request );
    ReplayFrameIntentResult ApplyFrameIntent( const ReplayFrameIntent& intent );

    // Restores only Replay-owned transport values from an interaction manifest
    // after the referenced v2 artifact has been loaded.
    void RestoreInteractionRecordingBaseline( RunReplayTrack track, float presentationTrackPosition,
                                              float solverTrackPosition, bool historicalPaused, bool liveAdvanceHeld );

    // Rebuilds the selected cause row from the loaded replay artifact and then
    // restores the detached inspector transition values through their owners.
    bool RestoreInteractionRecordingCauseBaseline( const ReplayInteractionRecordingCauseState& baseline, double now,
                                                   const ReplayWorkspaceFrameInput& input, InputRouter& inputRouter,
                                                   RuntimeInteractionController& interaction, SceneWorld& world,
                                                   AttachedCameraController& attachedCamera, CameraControlState& camera,
                                                   RunMousePickupState& mousePickup );
    ReplayStartupResult RunStartupWorkflows( double applicationTimeSeconds );
    bool ApplyStartupApplicationAction( const ReplayStartupResult& result, SceneController& sceneController,
                                        CameraControlState& camera, RunCameraMode normalizedCurrentMode,
                                        RunCameraMode normalizedRestoreMode, bool attachedFollow, bool directorGrabbed,
                                        RuntimeInteractionController& interaction, InputRouter& inputRouter,
                                        RunMousePickupState& mousePickup );
#ifdef _DEBUG
    ReplayStartupResult AdvanceStartupProbeWorkflows( ReplayStartupProbeContinuation& continuation,
                                                      SceneController& sceneController,
                                                      DiagnosticsRuntime& diagnosticsRuntime, OverlayDebugState& debug,
                                                      EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                                      SimulationSystem& simulation,
                                                      const SkullbonezCore::Core::EngineConfig& config,
                                                      Assets::AssetSystem& assets, Threading::WorkerPool& workerPool,
                                                      const ReplayStartupProbeSceneOverrides& sceneOverrides );
    ReplayStartupResult ApplyStartupProbeApplicationAction( ReplayStartupProbeContinuation& continuation,
                                                            SceneController& sceneController, CameraControlState& camera,
                                                            RunCameraMode normalizedCurrentMode,
                                                            RunCameraMode normalizedRestoreMode, bool attachedFollow,
                                                            bool directorGrabbed, RuntimeInteractionController& interaction,
                                                            InputRouter& inputRouter, RunMousePickupState& mousePickup );
#endif

    // Advances and publishes the private prediction during frame update.
    // Callers must complete this before any replay overlay traversal begins.
    void UpdatePrediction( Physics::PhysicsEngine& physics, const Gameplay::TornadoGameplay& tornadoGameplay,
                           const SceneEntityStore& entities, const SkullbonezCore::Core::EngineConfig& config,
                           const Physics::PhysicsWorldForces& worldForces, ReplayPredictionPathPresentation pathPresentation,
                           Threading::WorkerPool& workerPool, bool scenePhysicsEnabled, double simulationTimeSinceLastStart,
                           double simulationTotalTime );

    // Appends replay-owned records after RuntimeTools has rebuilt the shared
    // fixed-capacity tracer. RuntimeRenderer only submits the completed buffer.
    void AppendOverlayTrace( Physics::PhysicsEngine& physics, const SceneEntityStore& entities, EditorTracer& tracer,
                             const ReplayPredictionPresentationView& prediction, const ReplayOverlayBuildInput& input,
                             bool drawPredictionOverlay = true );

    // Routes value-only pointer facts through replay path selection. Store and
    // camera owners are explicit one-call borrows, not fields in the command.
    bool RouteWorldPointer( const ReplayWorldPointerInput& input, const SceneEntityStore& entities,
                            const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                            std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                            Environment::CameraCollection* cameras, Geometry::Terrain* terrain, CameraControlState& camera,
                            RuntimeInteractionController& interaction, InputRouter& inputRouter );
    bool HasActiveInteractionState() const;

    // Applies one typed leave-replay command. External camera/input owners are
    // synchronous operands and are never retained by ReplayRuntime.
    bool ApplyInteractionExit( const ReplayInteractionExitInput& input, Physics::PhysicsEngine& physics,
                               Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                               CameraControlState& camera, RuntimeInteractionController& interaction,
                               InputRouter& inputRouter );

    // Clears replay gesture, scrubber, inspection-camera, and velocity-key
    // state as one focus-loss transition before generic input resets itself.
    void ApplyInputFocusLoss( Environment::CameraCollection* cameras, Geometry::Terrain* terrain, CameraControlState& camera,
                              RunCameraMode normalizedRestoreMode, bool attachedFollow, bool directorGrabbed,
                              RuntimeInteractionController& interaction, InputRouter& inputRouter );

    // Clears replay gesture/camera state as one replay-owned scene transition.
    // The owner bundle is borrowed for this synchronous operation only.
    void ClearInteractionForSceneLoad( RuntimeInteractionController& interaction, InputRouter& inputRouter );

    // Clears replay-owned transient state and reports whether the camera owner
    // must execute an inspection-camera exit after the state transition.
    bool ClearInteractionForRuntimeTransition( RuntimeInteractionController& interaction, InputRouter& inputRouter );

    // Application-shell camera composition. The root supplies its private
    // presentation/authoring owners to stateless presentation operations; host
    // camera and input owners remain synchronous operands and are not retained.
    void EnterInspectionCamera( Environment::CameraCollection* cameras, CameraControlState& camera,
                                RunCameraMode normalizedCurrentMode, RuntimeInteractionController& interaction,
                                InputRouter& inputRouter, RunMousePickupState& mousePickup,
                                uint32_t inspectionCameraHash = CAMERA_FREE );
    void ExitInspectionCamera( Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                               CameraControlState& camera, RunCameraMode normalizedRestoreMode, bool attachedFollow,
                               bool directorGrabbed, RuntimeInteractionController& interaction, InputRouter& inputRouter );

  private:
    friend struct ReplayRuntimeTestAccess;

    // Advances one selected restore target through event application, fixed
    // stepping, and hash validation while the transaction owns progress.
    bool StepRestoreTarget( ReplayRestoreTransaction& transaction, SceneController& sceneController,
                            OverlayDebugState& debug, EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                            Assets::AssetSystem& assets, Threading::WorkerPool& workerPool, int sceneObjectCapacity,
                            const ReplayRestoreStepView& restoreView, const ReplaySolverFrameSample& checkpoint,
                            const ReplayV2SolverHashSample& target );

    // Writes the current presentation, solver hashes/checkpoints, and event
    // stream to an explicit cold-I/O binary v2 path.
    bool SavePresentationWithSolverHashes( const char* path, ReplayV2SaveResult* result = nullptr,
                                           std::span<const ReplayVisualArchiveSample> visualPackets = {},
                                           std::span<const uint8_t> visualPredictionState = {} ) const;

    // Owns scrubber save sequencing and status publication; file decode and
    // loaded-track state belong to ReplayTimeline.
    bool SavePresentationFromScrubber( double now );
    bool BeginLoadedPresentationActivationScrubber( bool hasLoadedPresentation, InputRouter& inputRouter,
                                                    RuntimeInteractionController& interaction );
    void ArmLoadedPresentationScrubber( float normalized, double now, RuntimeInteractionController& interaction );
    void ClearCameraFocusForRestore();
    ReplayPathPickResult ApplyPathPick( const ReplayPathPickInput& input, const SceneEntityStore& entities,
                                        const Physics::PhysicsBodyStore& bodyStore,
                                        const Physics::ColliderStore& colliderStore,
                                        std::span<const Rendering::RenderInstancePresentationRecord> presentation );
    ReplayPathPickResult ApplyInterceptTargetPick( const ReplayPathPickInput& input,
                                                   const Physics::PhysicsBodyStore& bodyStore,
                                                   const Physics::ColliderStore& colliderStore );
    ReplayInspectionCameraAction TickScrubberInput( const ReplayWorkspaceFrameInput& input, bool uiBlocksMouse,
                                                    InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                                    CameraControlState& camera, ReplayWorkspaceOutput& output );

    // App applies the published camera/restore actions synchronously; Planning
    // retains only causal selection, generation, and pause policy.
    void ApplyCauseTreeSelection( int requestedRow, const ReplayWorkspaceFrameInput& input, InputRouter& inputRouter,
                                  RuntimeInteractionController& interaction, SceneWorld& world,
                                  AttachedCameraController& attachedCamera, CameraControlState& camera,
                                  RunMousePickupState& mousePickup, ReplayWorkspaceOutput& output );
    void ApplyCauseInspectionTransition( const ReplayWorkspaceFrameInput& input, bool pointerBlocked, SceneWorld& world,
                                         AttachedCameraController& attachedCamera, CameraControlState& camera,
                                         ReplayWorkspaceOutput& output );
    void ApplyCauseInspectionLifecycle( int requestedRow, bool exitCauseTreeInspection,
                                        ReplayInspectionCameraAction scrubberHostAction, bool causeInteractionActive,
                                        const ReplayWorkspaceFrameInput& input, InputRouter& inputRouter,
                                        RuntimeInteractionController& interaction, Environment::CameraCollection* cameras,
                                        Geometry::Terrain* terrain, CameraControlState& camera,
                                        AttachedCameraController& attachedCamera );

  private:
    float SolverPresentTrackPosition() const;
    bool ShouldRenderScrubber( bool editorModeEnabled, bool uiVisible, bool uiMinimized,
                               RuntimeInteractionGestureKind gesture ) const;
    bool HasLoadedPresentation() const;
    const ReplayPresentationSample* LoadedPresentationSampleAtNormalized( float normalized ) const;
    const ReplayPresentationSample* LoadedPresentationLatestSample() const;
    bool IsScrubPaused() const;
    const ReplayPresentationSample* CurrentScrubSample() const;
    const ReplaySolverFrameSample* CurrentSolverScrubSample() const;
    const RunReplayPredictionFrame* CurrentPredictionScrubFrame() const;

    ReplayPredictionDetailTransitionAction ApplyPredictionDetailModeCommand( ReplayPredictionDetailMode requestedMode );
    bool ClearPredictionCauseWindowForDetailTransition( ReplayPredictionDetailTransitionAction actions );
    void PublishTransportFeedback( const char* message, double now );
    void EnterReplayTransportWorkspace( RuntimeInteractionController& interaction, ReplayWorkspaceOutput& output );
    bool SetTransportCursor( float normalized, RuntimeInteractionController& interaction, double now,
                             ReplayWorkspaceOutput& output );
    void ApplyAuthoringPredictionRequest();
    void ApplyPredictionUpdateResult( const ReplayPredictionUpdateResult& result );
    void ApplyPastTrajectoryUpdate( const ReplayPastTrajectoryUpdate& update );
    void AppendSolverTrajectorySampleToStore( const ReplaySolverFrameSample& sample );
    bool ApplyPlanningVelocityMutation( Physics::PhysicsEngine& physics, const ReplayTripPlannerVelocityMutation& mutation );
    const ReplayPredictionCauseEvidencePacket& CopyPredictionCauseEvidence( const RunReplayCauseTreeRow& row );

    // Lifetime: startup-bound diagnostics borrow shared only with concrete replay owners.
    Core::SbDiagnosticStore& m_resultDiagnostics;
    ReplayTimeline m_timeline;
    ReplayProbeRunner m_probeRunner;
    ReplayScrubber m_scrubberOwner;
    ReplayPresentation m_visualPresentation;
    ReplayLauncherVisualSample m_launcherVisualCaptureScratch;
    std::array<const char*, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_captureEntityNamesScratch = {};
    ReplayAuthoring m_authoring;
    ReplayPrediction m_predictionOwner;
    ReplayPredictionPresentation m_predictionPresentation;
    std::array<ReplayPredictionSceneEntityFact, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>
        m_predictionSceneFactsScratch = {};
    ReplayPlanningRuntime m_planningOwner;
    ReplayPredictionCauseEvidencePacket m_predictionCauseEvidenceScratch;

#if defined( SKULLBONEZ_SKARNESS )
    uint32_t m_lastSubmittedCauseContactPointCount = 0;
    uint32_t m_lastSubmittedCauseContactBodyCount = 0;
#endif

    // Lifetime: typed UI/automation commands arrive after the replay workspace
    // tick. Retain one semantic row request until the next tick can run the
    // same selection, transport, camera, and pause path as a pointer click.
    int m_pendingCauseSelectionRow = -1;

    // Invariant: App records both complete replay aggregates around the exact
    // synchronous evidence release; Prediction cannot observe sibling owners.
    uint64_t m_predictionEvidenceReleaseBeforeReplayTotalBytes = 0;
    uint64_t m_predictionEvidenceReleaseAfterReplayTotalBytes = 0;
    uint64_t m_predictionEvidenceReleaseBeforeCategoryTotalBytes = 0;
    uint64_t m_predictionEvidenceReleaseAfterCategoryTotalBytes = 0;
    int m_presentationSaveSequence = 0;
    SceneLifecycleGenerationObserver m_sceneClearObserver;
    SceneLifecycleGenerationObserver m_sceneActivationObserver;
};
} // namespace Runtime
} // namespace SkullbonezCore
