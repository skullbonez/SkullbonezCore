/*
File: SkullbonezSource/Runtime/Replay/ReplayPresentation.h
Purpose:
  Owns replay path, camera, overlay, render-pose, and published visual state.

Summary:
  ReplayPresentation is the mutable authority for everything replay renders.
  ReplayRuntime sequences the owner but does not retain parallel visual state.

Glossary:
  Path target: Stable replay body selected for visualization.
  Path color mode: Value-only rule that recolors published trajectory segments
    at draw time without changing replay capture or prediction storage.
  HUD (Heads-Up Display): Value-only replay diagnostics sampled once for the
    late UI/text pass.

Invariants:
  - Physics::PhysicsSceneObjectId is identity; ModelRowHint is only a dense-row hint.
  - Published packet spans are frame-local borrows into the submitted tracer.
  - Render-pose matching uses a fixed model-capacity mask and never allocates.
  - ReplayHudStatus borrows no owner and is coherent for one UI frame.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayIdentity.h"
#include "ReplayPathPackets.h"
#include "ReplayPresentationPackets.h"
#include "ReplayRecorder.h"
#include "ReplayVisualPacket.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../../Assets/AssetKeys.h"
#include "../../Core/Common.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../Core/SceneCapacity.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
} // namespace Core
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
class EditorTracer;
struct CameraControlState;
struct RunMousePickupState;
struct RunReplayCauseTreeState;
struct RunReplayPredictionFrame;
struct ReplayPastTrajectoryView;
struct ReplayPredictionPresentationView;

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

// Host-camera effect emitted by replay interaction phases. The action carries
// no camera owner or frame data and is applied synchronously by ReplayRuntime.
enum class ReplayInspectionCameraAction : uint8_t
{
    None,
    Enter,
    Exit
};

namespace ReplayPresentationOperations
{
// Stateless host-camera transitions shared by scrubber and authoring tools.
// Every owner reference is a synchronous borrow; neither operation stores host
// or replay authority after returning.
void EnterInspectionCamera( ReplayPresentation& presentation,
                            Environment::CameraCollection* cameras,
                            CameraControlState& camera,
                            RunCameraMode normalizedCurrentMode,
                            RuntimeInteractionController& interaction,
                            InputRouter& inputRouter,
                            RunMousePickupState& mousePickup );
void ExitInspectionCamera( ReplayPresentation& presentation,
                           const ReplayAuthoring& authoring,
                           Environment::CameraCollection* cameras,
                           Geometry::Terrain* terrain,
                           CameraControlState& camera,
                           RunCameraMode normalizedRestoreMode,
                           bool attachedFollow,
                           bool directorGrabbed,
                           RuntimeInteractionController& interaction,
                           InputRouter& inputRouter );

// A committed load first releases gesture/camera ownership, then the caller
// exits the host camera before arming the new scrub position. Keeping these
// phases explicit prevents the load transaction from becoming a parameter bag.
bool BeginLoadedPresentationActivation( bool hasLoadedPresentation,
                                        ReplayScrubber& scrubber,
                                        ReplayPresentation& presentation,
                                        ReplayAuthoring& authoring,
                                        RuntimeInteractionController& interaction,
                                        InputRouter& inputRouter );
void ArmLoadedPresentation( float normalized,
                            double now,
                            ReplayScrubber& scrubber,
                            ReplayPresentation& presentation,
                            ReplayAuthoring& authoring,
                            ReplayPrediction& prediction,
                            RuntimeInteractionController& interaction );
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
    Physics::PhysicsSceneObjectId focusedId;
    Physics::PhysicsSceneObjectId counterpartId;
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
    // Live-publication coherence probe: once a prediction's child tree is
    // render-ready, it must remain ready for every later frame of that run.
    uint64_t presentationTargetId = 0;
    ReplayFrameIndex presentationSourceFrame = 0;
    uint32_t futureTreeReadinessDropCount = 0;
    bool presentationKeyValid = false;
    bool futureTreeReadySeen = false;
    bool futureTreeReadyLastFrame = false;
};

// Value-only selection applied by the presentation owner after cause-tree hit
// testing. Restore-camera state remains private and cannot be overwritten by a
// focus command.
struct ReplayCameraFocusRequest
{
    RunReplayCameraFocusKind focusKind = RunReplayCameraFocusKind::None;
    Physics::PhysicsSceneObjectId focusedId;
    Physics::PhysicsSceneObjectId counterpartId;
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
    explicit ReplayPresentation( Core::Profiler* profiler = nullptr );

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
    // Model rows are repairable hints; stable Physics::PhysicsSceneObjectId remains authority.
    void PreparePathDrawing( const Physics::PhysicsBodyStore& bodyStore );
    void SetPathTargetModelRow( Physics::ModelRowHint modelRow ) noexcept;
    void ApplyArchivePathState( const RunReplayPathVisualizerState& archiveState );
    void ApplyPastTrajectoryUpdate( Physics::PhysicsSceneObjectId targetId,
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
    bool SetPathTarget( Physics::PhysicsSceneObjectId id, Physics::ModelRowHint modelRow, const char* name );
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
                               EditorTracer& tracer );
    void RenderCauseFocusOverlay( const RunReplayCauseTreeState& causeTree,
                                  const ReplayPredictionPresentationView& prediction,
                                  const ReplaySolverFrameSample* currentSolverSample,
                                  const Physics::PhysicsBodyStore& bodyStore,
                                  const Physics::ColliderStore& colliderStore,
                                  const SceneEntityStore& entities,
                                  EditorTracer& tracer );

  private:
    // Lifetime: startup-bound diagnostics borrow; never retained beyond Run.
    Core::Profiler* m_profiler;
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
    std::array<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_renderPoseBodyMatched = {};
    bool m_launcherVisualBackupActive = false;
};

} // namespace Runtime
} // namespace SkullbonezCore
