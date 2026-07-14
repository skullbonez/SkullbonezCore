/*
File: SkullbonezSource/Runtime/Replay/ReplayPresentation.h
Purpose:
  Owns replay path, camera, overlay, render-pose, and published visual state.

Summary:
  ReplayPresentation is the mutable authority for everything replay renders.
  ReplayRuntime sequences the owner but does not retain parallel visual state.

Glossary:
  Path target: Stable replay body selected for visualization.
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
class PhysicsBodyStore;
} // namespace Physics
namespace Rendering
{
struct RenderInstancePresentationRecord;
}
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
struct RunCameraState;

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

struct ReplayWorldPointerInput
{
    // Lifetime: one routed pointer gesture. Every reference is borrowed for
    // the synchronous pick/optional camera-exit operation and is never stored.
    bool leftPressed = false;
    bool suppressWorldAction = false;
    bool editorMode = false;
    bool uiWantsNativeCursor = false;
    bool controlDown = false;
    bool launcherMode = false;
    ReplayPathPickInput pick;
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

// Concept: presentation is a concrete owner, not a collection of fields on
// ReplayRuntime. Its accessors are the temporary migration surface used while
// M3 moves drawing operations; consumers cannot substitute another state bag.
class ReplayPresentation
{
  public:
    ReplayPresentation();

    RunReplayCameraState& Camera() noexcept
    {
        return m_camera;
    }
    const RunReplayCameraState& Camera() const noexcept
    {
        return m_camera;
    }
    RunReplayPathVisualizerState& PathVisualizer() noexcept
    {
        return m_pathVisualizer;
    }
    const RunReplayPathVisualizerState& PathVisualizer() const noexcept
    {
        return m_pathVisualizer;
    }
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& TrajectoryVisualStats() noexcept
    {
        return m_trajectoryVisualStats;
    }
    const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& TrajectoryVisualStats() const noexcept
    {
        return m_trajectoryVisualStats;
    }
    ReplayTrajectorySubmissionProbeStats& TrajectorySubmissionProbe() noexcept
    {
        return m_trajectorySubmissionProbe;
    }
    const ReplayTrajectorySubmissionProbeStats& TrajectorySubmissionProbe() const noexcept
    {
        return m_trajectorySubmissionProbe;
    }
    ReplayVisualPacket& PublishedVisualPacket() noexcept
    {
        return m_publishedVisualPacket;
    }
    const ReplayVisualPacket& PublishedVisualPacket() const noexcept
    {
        return m_publishedVisualPacket;
    }
    std::vector<ReplayPredictionGhostDrawRequest>& PredictionGhostDrawRequests() noexcept
    {
        return m_predictionGhostDrawRequests;
    }
    const std::vector<ReplayPredictionGhostDrawRequest>& PredictionGhostDrawRequests() const noexcept
    {
        return m_predictionGhostDrawRequests;
    }
    std::vector<uint8_t>& FocusModelMask() noexcept
    {
        return m_focusModelMask;
    }
    const std::vector<uint8_t>& FocusModelMask() const noexcept
    {
        return m_focusModelMask;
    }
    ReplayLauncherVisualSample& LauncherVisualBackup() noexcept
    {
        return m_launcherVisualBackup;
    }
    const ReplayLauncherVisualSample& LauncherVisualBackup() const noexcept
    {
        return m_launcherVisualBackup;
    }
    ReplayLauncherVisualSample& LauncherVisualCaptureScratch() noexcept
    {
        return m_launcherVisualCaptureScratch;
    }
    std::array<uint8_t, SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS>& RenderPoseBodyMatched() noexcept
    {
        return m_renderPoseBodyMatched;
    }
    bool& LauncherVisualBackupActive() noexcept
    {
        return m_launcherVisualBackupActive;
    }
    bool LauncherVisualBackupActive() const noexcept
    {
        return m_launcherVisualBackupActive;
    }
    void ReserveRecordingBuffers();
    void ClearPathState();
    bool SetPathTarget( const char* name, int modelIndex, const Physics::PhysicsBodyStore& bodyStore );
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
    void StoreLauncherVisualBackup( const ReplayLauncherVisualSample& sample );
    void ClearLauncherVisualBackup();
    void RecordTrajectoryFrameStats( const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& frameStats );
    void PublishVisualPacket( ReplayVisualPacket packet );
    void RecordTrajectorySubmissionFrame(
        const SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats& submissionStats,
        int frameNumber,
        uint64_t reserveGrowthEventCount );
    void RecordTrajectoryBudgetExpiry( SkullbonezCore::Core::MainMemoryReplayBudgetPass pass );
    void RecordTrajectoryRebuildCause( SkullbonezCore::Core::MainMemoryReplayRebuildCause cause );

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
