/*
File: SkullbonezSource/Runtime/Replay/ReplayPresentation.h
Purpose:
  Owns recorded replay path, camera, launcher backup, and render-pose state.

Summary:
  ReplayPresentation is the lower Replay visual owner. Prediction pose, ghost,
  trajectory, and packet state belong to ReplayPredictionPresentation above it;
  Runtime/App passes only synchronous path and camera values between siblings.
  One saved main-camera identity serves every temporary inspection slot. Exact
  predicted cause focus retains its evidence stamp so row rematching fails
  closed after same-frame bank replacement.

Glossary:
  Path target: Stable replay body selected for visualization.
  Path color mode: Value-only rule that recolors published trajectory segments
    at draw time without changing replay capture or prediction storage.

Invariants:
  - Physics::PhysicsSceneObjectId is identity; ModelRowHint is only a dense-row hint.
  - Render-pose matching uses a fixed model-capacity mask and never allocates.
  - ReplayHudStatus borrows no owner and is coherent for one UI frame.
  - Switching between inspection slots never replaces the saved return camera.
  - High-detail predicted focus is reusable only when generation, mode, epoch,
    topology, and publication stamps still match the rebuilt cause row.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "ReplayAuthoringPackets.h"
#include "ReplayIdentity.h"
#include "ReplayPathPackets.h"
#include "ReplayPresentationPackets.h"
#include "ReplayRecorder.h"
#include "../Camera/RuntimeCameraMode.h"
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
class InputRouter;
class ReplayAuthoring;
class ReplayPresentation;
class ReplayScrubber;
class EditorTracer;
struct CameraControlState;
struct RunMousePickupState;
struct RunReplayCauseTreeState;

struct ReplayOverlayBuildInput
{
    bool editorModeEnabled = false;
    ReplayToolGestureView gesture;
    int sceneFrame = 0;
};

// Host-camera effect emitted by replay interaction phases. The action carries
// no camera owner or frame data and is applied synchronously by ReplayRuntime.
enum class ReplayInspectionCameraAction : uint8_t
{
    None,
    Enter,
    Exit
};

struct ReplayWorldPointerInput
{
    // Value-only facts for one routed pointer gesture. Mutable and store owners
    // are explicit operands on ReplayRuntime::RouteWorldPointer. UI hit
    // suppression is authoritative; merely requesting a visible native cursor
    // does not grant the UI ownership of clicks over the world.
    bool leftPressed = false;
    bool suppressWorldAction = false;
    bool editorMode = false;
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

    // Invariant: these fields identify the exact prediction evidence bank that
    // produced the focused row; partial identity matches may not reuse focus.
    uint32_t focusSourceGeneration = 0;
    uint64_t focusSourceBankEpoch = 0;
    uint32_t focusSourceTopologyVersion = 0;
    uint64_t focusSourcePublicationVersion = 0;
    bool focusSourceHighDetail = false;
    bool focusTerrain = false;
};

struct ReplayPresentationMemoryStats
{
    uint64_t pathOwnerBytes = 0;
    uint64_t pathTargetCapacityBytes = 0;
    uint64_t launcherVisualBytes = 0;
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
    ReplayPresentationMemoryStats CollectMemoryStats() const noexcept;
    bool HasLauncherVisualBackup() const noexcept;
    void ReserveLauncherVisualBackupBuffers();
    void StoreLauncherVisualBackup( const ReplayLauncherVisualSample& sample );
    const ReplayLauncherVisualSample& LauncherVisualBackup() const noexcept;
    void ClearLauncherVisualBackup() noexcept;
    void BeginCameraInspection( RunCameraMode restoreMode, uint32_t restoreCameraHash,
                                const Math::Vector::Vector3& restoreEye, const Math::Vector::Vector3& restoreView,
                                const Math::Vector::Vector3& restoreUp ) noexcept;
    void EndCameraInspection() noexcept;
    void SetCameraPauseOwnership( bool ownsPause ) noexcept;

    // Applies the selected cause-tree values without exposing restore-camera
    // state, which remains private to the presentation owner.
    void ApplyCameraFocus( const RunReplayCauseTreeRow& row, int rowIndex, RunReplayCameraFocusKind focusKind,
                           const Math::Vector::Vector3& resolvedPoint, const Math::Vector::Vector3& resolvedNormal,
                           float resolvedRadius ) noexcept;
    void SetCameraFocusedRow( int row ) noexcept;
    bool ClearCameraFocus() noexcept;
    void ClearPathState();

    // Publishes the selected-target rows needed by read-only path drawing.
    // Model rows are repairable hints; stable Physics::PhysicsSceneObjectId remains authority.
    void PreparePathDrawing( const Physics::PhysicsBodyStore& bodyStore );
    void SetPathTargetModelRow( Physics::ModelRowHint modelRow ) noexcept;
    void ApplyArchivePathState( const RunReplayPathVisualizerState& archiveState );
    void ApplyPastTrajectoryUpdate( Physics::PhysicsSceneObjectId targetId, ReplayFrameIndex firstFrame,
                                    ReplayFrameIndex builtThroughFrame, uint64_t totalFramesEvicted,
                                    uint64_t fullRebuildCount, uint64_t incrementalTrimCount, bool valid,
                                    Physics::ModelRowHint targetModelRow, bool targetModelRowRepaired );
    void TogglePastPathVisible();

    // Advances the value-only path palette in its stable UI order. Existing
    // trajectory records remain unchanged and are recolored on the next draw.
    ReplayPathColorMode CyclePathColorMode() noexcept;
    bool SetPathTarget( Physics::PhysicsSceneObjectId id, Physics::ModelRowHint modelRow, const char* name );
    ReplayPathPickResult ApplyPathPickResult( const ReplayPathPickResult& resolved );
    bool PrepareRenderPoseBodyMatch( int modelCount ) noexcept;
    bool ApplyPresentationSampleForRender( Rendering::RenderInstanceStore& renderInstances,
                                           const Physics::PhysicsBodyStore& bodyStore,
                                           const Physics::ColliderStore& colliderStore,
                                           const ReplayPresentationSample& sample );
    bool ApplySolverSampleForRender( Rendering::RenderInstanceStore& renderInstances,
                                     const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                                     const ReplaySolverFrameSample& sample );

  private:
    RunReplayCameraState m_camera;
    RunReplayPathVisualizerState m_pathVisualizer;
    ReplayLauncherVisualSample m_launcherVisualBackup;

    // Invariant: replay render pose matching is a per-frame mark table capped
    // by the live model budget, so scrub/prediction rendering never allocates.
    std::array<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_renderPoseBodyMatched = {};
    bool m_launcherVisualBackupActive = false;
};

} // namespace Runtime
} // namespace SkullbonezCore
