/*
File: SkullbonezSource/Runtime/Replay/ReplayPresentation.h
Purpose:
  Owns replay path, camera, overlay, render-pose, and published visual state.

Mental model:
  ReplayPresentation is the mutable authority for everything replay renders.
  ReplayRuntime sequences the owner but does not retain parallel visual state.

Glossary:
  Path target: Stable replay body selected for visualization.
  Path color mode: Value-only rule that recolors published trajectory segments
    at draw time without changing replay capture or prediction storage.
  HUD (Heads-Up Display): Value-only replay diagnostics sampled once for the
    late UI/text pass.

Invariants:
  - ReplayBodyId is identity; ModelRowHint is only a dense-row hint.
  - Published packet spans are frame-local borrows into the submitted tracer.
  - Render-pose matching uses a fixed model-capacity mask and never allocates.
  - ReplayHudStatus borrows no owner and is coherent for one UI frame.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayIdentity.h"
#include "ReplayRecorder.h"
#include "ReplayVisualPacket.h"
#include "../RuntimeCameraMode.h"
#include "../RuntimeInteractionController.h"
#include "../../Assets/AssetKeys.h"
#include "../../Core/Common.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Physics/PhysicsHandles.h"
#include "../Scene/SceneCapacity.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
} // namespace Physics
namespace Rendering
{
class RenderInstanceStore;
struct RenderInstancePresentationRecord;
} // namespace Rendering
namespace Environment
{
class CameraCollection;
}
namespace Geometry
{
class Terrain;
}
namespace Runtime
{
class SceneEntityStore;
class InputRouter;
class ReplayAuthoring;
class ReplayPrediction;
class ReplayPresentation;
class ReplayScrubber;
class RuntimeTools;
class RunEditorTracer;
struct RunCameraState;
struct RunMousePickupState;
struct RunReplayCauseTreeState;
struct RunReplayPredictionFrame;
struct ReplayPastTrajectoryView;
struct ReplayPredictionPresentationView;

// Read-only publication consumed by world render passes after Run has prepared
// replay render poses, overlay records, ghosts, and focus masks. The pointers
// and spans borrow owner storage for this render call only; passes cannot reach
// replay mutation or scheduling authority through this value.
struct ReplayRenderFrameView
{
    const ReplayPresentationSample* presentationSample = nullptr;
    const ReplaySolverFrameSample* solverSample = nullptr;
    const RunReplayPredictionFrame* predictionFrame = nullptr;
    const ReplayVisualPacket* visualPacket = nullptr;
    const std::vector<uint8_t>* focusModelMask = nullptr;
    bool predictionEnabled = false;
    bool liveAdvanceHeld = false;
    bool focusFadeActive = false;
};

// Lifetime: selected replay rows are frame-local borrows. The render
// composition shell must consume them before replay input or prediction update
// mutates the owning timeline.
struct ReplayRenderSelectionView
{
    const ReplayPresentationSample* presentationSample = nullptr;
    const ReplaySolverFrameSample* solverSample = nullptr;
    const RunReplayPredictionFrame* predictionFrame = nullptr;
};

// Value-only per-frame publication for replay diagnostics drawn by the late
// UI/text pass. It borrows no owner, and memoryStats is populated only while
// the Memory tab explicitly requests replay accounting.
struct ReplayHudStatus
{
    SkullbonezCore::Core::MainMemoryReplayStats memoryStats;
    int memoryPreset = 0;
    int requestedRetentionSeconds = 0;
    int requestedBudgetMiB = 0;
    int presentationRetentionSeconds = 0;
    int solverRetentionSeconds = 0;
    float divergenceUnits = 0.0f;
    bool memoryBudgetClamped = false;
    bool solverWindowReduced = false;
    bool divergenceValid = false;
    bool memoryStatsValid = false;
};

struct ReplayOverlayBuildInput
{
    bool editorModeEnabled = false;
    RuntimeInteractionGesture gesture;
    int sceneFrame = 0;
};

struct ReplayPathPickInput
{
    Math::Vector::Vector3 rayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rayDirection = Math::Vector::ZERO_VECTOR;
    bool hasWorldRay = false;
    bool additive = false;
    bool clearOnMiss = false;
};

struct ReplayPathPickResult
{
    bool picked = false;
    bool exitInspectionCamera = false;
};

namespace ReplayPresentationOperations
{
// Stateless host-camera transitions shared by scrubber and authoring tools.
// Every owner reference is a synchronous borrow; neither operation stores host
// or replay authority after returning.
void EnterInspectionCamera( ReplayPresentation& presentation,
                            Environment::CameraCollection* cameras,
                            RunCameraState& camera,
                            RunCameraMode normalizedCurrentMode,
                            RuntimeInteractionController& interaction,
                            InputRouter& inputRouter,
                            RunMousePickupState& mousePickup );
void ExitInspectionCamera( ReplayPresentation& presentation,
                           const ReplayAuthoring& authoring,
                           Environment::CameraCollection* cameras,
                           Geometry::Terrain* terrain,
                           RunCameraState& camera,
                           RunCameraMode normalizedRestoreMode,
                           bool attachedFollow,
                           bool directorGrabbed,
                           RuntimeInteractionController& interaction,
                           InputRouter& inputRouter );

// Applies the replay-owner and host-camera reaction after ReplayTimeline has
// committed a presentation artifact. Production startup and Debug probes share
// this operation so validation cannot drift from the operator-visible path.
bool ActivateLoadedPresentation( bool hasLoadedPresentation,
                                 float normalized,
                                 double now,
                                 ReplayScrubber& scrubber,
                                 ReplayPresentation& presentation,
                                 ReplayAuthoring& authoring,
                                 ReplayPrediction& prediction,
                                 Environment::CameraCollection* cameras,
                                 Geometry::Terrain* terrain,
                                 RunCameraState& camera,
                                 RunMousePickupState& mousePickup,
                                 RunCameraMode normalizedCurrentMode,
                                 RunCameraMode normalizedRestoreMode,
                                 bool attachedFollow,
                                 bool directorGrabbed,
                                 RuntimeInteractionController& interaction,
                                 InputRouter& inputRouter );
} // namespace ReplayPresentationOperations

struct ReplayWorldPointerInput
{
    // Value-only facts for one routed pointer gesture. Mutable and store owners
    // are explicit operands on ReplayRuntime::RouteWorldPointer.
    bool leftPressed = false;
    bool suppressWorldAction = false;
    bool editorMode = false;
    bool uiWantsNativeCursor = false;
    bool controlDown = false;
    bool launcherMode = false;
    ReplayPathPickInput pick;
    RunCameraMode restoreCameraMode = RunCameraMode::Inspect;
    bool attachedCameraFollow = false;
    bool directorGrabbed = false;
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

enum class ReplayPathColorMode : uint8_t
{
    LaneFlat,
    VelocityHeat,
    TimeGradient,
    PerObjectHue,
    CausalDepth
};

const char* ReplayPathColorModeName( ReplayPathColorMode mode ) noexcept;

struct RunReplayPathVisualizerState
{
    // Concept: the retained/past lane is an operator-visible overlay choice.
    // A selected target remains the authority for *what* could draw; this flag
    // only answers whether the solver-history lane should be emitted this
    // frame.
    bool hasTarget = false;
    bool pastPathVisible = true;
    // Invariant: the presentation owner retains a deterministic value-only
    // mode. Draw code reads it without mutating trajectory capture or storage.
    ReplayPathColorMode colorMode = ReplayPathColorMode::LaneFlat;
    ReplayBodyId targetId;
    Physics::ModelRowHint targetModelRow;
    char targetName[64] = {};
    std::vector<RunReplayPathTraceNode> futureNodes;
    std::vector<RunReplayPathTarget> targets;
    RunReplayPastTrajectoryBuildState pastTrajectory;
};

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

// Value-only selection applied by the presentation owner after cause-tree hit
// testing. Restore-camera state remains private and cannot be overwritten by a
// focus command.
struct ReplayCameraFocusRequest
{
    RunReplayCameraFocusKind focusKind = RunReplayCameraFocusKind::None;
    ReplayBodyId focusedId;
    ReplayBodyId counterpartId;
    int focusedRow = -1;
    RunReplayCauseTreeRowKind focusRowKind = RunReplayCauseTreeRowKind::Body;
    Physics::ModelRowHint focusModelRow;
    Physics::ModelRowHint focusCounterpartModelRow;
    int focusContactIndex = -1;
    int focusSolverRowIndex = -1;
    int focusFeatureId = 0;
    bool focusTerrain = false;
    Math::Vector::Vector3 targetPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 targetNormal = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 impulseVector = Math::Vector::ZERO_VECTOR;
    float targetRadius = 1.0f;
};

struct ReplayPresentationMemoryStats
{
    uint64_t pathOwnerBytes = 0;
    uint64_t pathTargetCapacityBytes = 0;
    uint64_t pathFutureNodeCapacityBytes = 0;
    uint64_t ghostRequestCapacityBytes = 0;
    uint64_t focusModelMaskCapacityBytes = 0;
    uint64_t launcherVisualBytes = 0;
    uint64_t pathNodeCount = 0;
    uint64_t ghostRequestCount = 0;
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats trajectory;
};

// Concept: presentation is a concrete owner, not a collection of fields on
// ReplayRuntime. Mutation is expressed as bounded commands; consumers receive
// only value snapshots or read-only frame spans.
class ReplayPresentation
{
  public:
    ReplayPresentation();

    RunReplayCameraState CameraView() const noexcept;
    const RunReplayPathVisualizerState& PathVisualizer() const noexcept
    {
        return m_pathVisualizer;
    }
    ReplayPastTrajectoryView PastTrajectoryView() const noexcept;
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats TrajectoryVisualStatsSnapshot() const noexcept;
    ReplayTrajectorySubmissionProbeStats TrajectorySubmissionProbeSnapshot() const noexcept;
    const ReplayVisualPacket& PublishedVisualPacketView() const noexcept;
    std::span<const ReplayPredictionGhostDrawRequest> PredictionGhostDrawRequestsView() const noexcept;
    const std::vector<uint8_t>& FocusModelMaskView() const noexcept;
    ReplayPresentationMemoryStats CollectMemoryStats() const noexcept;
    bool HasLauncherVisualBackup() const noexcept;
    void ReserveRecordingBuffers();
    void ReserveLauncherVisualCaptureBuffers();
    void PopulateLauncherVisualCapture( ReplayCaptureInput& input, RuntimeTools& runtimeTools );
    void StoreLauncherVisualBackupFrom( RuntimeTools& runtimeTools );
    void RestoreAndClearLauncherVisualBackup( RuntimeTools& runtimeTools );
    void BeginCameraInspection( RunCameraMode restoreMode,
                                uint32_t restoreCameraHash,
                                const Math::Vector::Vector3& restoreEye,
                                const Math::Vector::Vector3& restoreView,
                                const Math::Vector::Vector3& restoreUp ) noexcept;
    void EndCameraInspection() noexcept;
    void SetCameraPauseOwnership( bool ownsPause ) noexcept;
    void ApplyCameraFocus( const ReplayCameraFocusRequest& request ) noexcept;
    void SetCameraFocusedRow( int row ) noexcept;
    bool ClearCameraFocus() noexcept;
    void ClearPathState();
    // Publishes the selected-target rows needed by read-only path drawing.
    // Model rows are repairable hints; stable ReplayBodyId remains authority.
    void PreparePathDrawing( const Physics::PhysicsBodyStore& bodyStore );
    void SetPathTargetModelRow( Physics::ModelRowHint modelRow ) noexcept;
    void ApplyArchivePathState( const RunReplayPathVisualizerState& archiveState );
    void ApplyPastTrajectoryUpdate( ReplayBodyId targetId,
                                    ReplayFrameIndex firstFrame,
                                    ReplayFrameIndex builtThroughFrame,
                                    uint64_t totalFramesEvicted,
                                    uint64_t fullRebuildCount,
                                    uint64_t incrementalTrimCount,
                                    bool valid,
                                    Physics::ModelRowHint targetModelRow,
                                    bool targetModelRowRepaired );
    void TogglePastPathVisible();
    // Advances the value-only path palette in its stable UI order. Existing
    // trajectory records remain unchanged and are recolored on the next draw.
    ReplayPathColorMode CyclePathColorMode() noexcept;
    bool SetPathTarget( const char* name, int modelIndex, const Physics::PhysicsBodyStore& bodyStore );
    bool SetPathTarget( ReplayBodyId id, Physics::ModelRowHint modelRow, const char* name );
    ReplayPathPickResult
    TryPickPathTarget( const ReplayPathPickInput& input,
                       const SceneEntityStore& entities,
                       const Physics::PhysicsBodyStore& bodyStore,
                       const Physics::ColliderStore& colliderStore,
                       std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                       const ReplaySolverFrameSample* currentSolverSample );
    bool BuildFocusModelMask( const Physics::PhysicsBodyStore& bodyStore,
                              int modelCount,
                              std::span<const RunReplayPathTraceNode> futureNodes );
    void ClearPredictionGhostDrawRequests() noexcept;
    bool CanAppendPredictionGhostDrawRequests( std::size_t count ) const noexcept;
    void AppendPredictionGhostDrawRequest( const ReplayPredictionGhostDrawRequest& request );
    bool HasPredictionGhostDrawRequests() const noexcept;
    bool PrepareRenderPoseBodyMatch( int modelCount ) noexcept;
    void ClearLauncherVisualBackup();
    void ResetTrajectoryVisualStats() noexcept;
    void RecordTrajectoryFrameStats( const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& frameStats );
    void PublishVisualPacket( ReplayVisualPacket packet );
    void RecordTrajectorySubmissionFrame(
        const SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats& submissionStats,
        int frameNumber,
        uint64_t reserveGrowthEventCount );
    void RecordTrajectoryBudgetExpiry( SkullbonezCore::Core::MainMemoryReplayBudgetPass pass );
    void RecordTrajectoryRebuildCause( SkullbonezCore::Core::MainMemoryReplayRebuildCause cause );
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
    bool
    BuildPredictionGhostDrawRequests( const ReplayPredictionPresentationView& prediction,
                                      std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                      const Physics::PhysicsBodyStore& bodyStore );
    void PublishVisualPacket( ReplayVisualPacket packet,
                              const ReplayPredictionPresentationView& prediction,
                              const ReplaySolverFrameSample* latestSolver,
                              uint64_t replayReserveGrowthEvents );
    void RenderPathVisualizer( const ReplayPredictionPresentationView& prediction,
                               const ReplaySolverFrameSample* presentSample,
                               Physics::PhysicsEngine& physics,
                               const SceneEntityStore& entities,
                               RunEditorTracer& tracer );
    void RenderCauseFocusOverlay( const RunReplayCauseTreeState& causeTree,
                                  const ReplayPredictionPresentationView& prediction,
                                  const ReplaySolverFrameSample* currentSolverSample,
                                  const Physics::PhysicsBodyStore& bodyStore,
                                  const Physics::ColliderStore& colliderStore,
                                  const SceneEntityStore& entities,
                                  RunEditorTracer& tracer );

  private:
    RunReplayCameraState m_camera;
    RunReplayPathVisualizerState m_pathVisualizer;
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats m_trajectoryVisualStats;
    ReplayTrajectorySubmissionProbeStats m_trajectorySubmissionProbe;
    ReplayVisualPacket m_publishedVisualPacket;
    std::vector<ReplayPredictionGhostDrawRequest> m_predictionGhostDrawRequests;
    std::vector<uint8_t> m_focusModelMask;
    ReplayLauncherVisualSample m_launcherVisualBackup;
    ReplayLauncherVisualSample m_launcherVisualCaptureScratch;
    // Invariant: replay render pose matching is a per-frame mark table capped
    // by the live model budget, so scrub/prediction rendering never allocates.
    std::array<uint8_t, SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS> m_renderPoseBodyMatched = {};
    bool m_launcherVisualBackupActive = false;
};

} // namespace Runtime
} // namespace SkullbonezCore
