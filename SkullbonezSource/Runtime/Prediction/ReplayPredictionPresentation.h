/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.h
Purpose:
  Owns prediction pose, ghost, focus-mask, trajectory, and visual-packet presentation state.

Summary:
  ReplayPredictionPresentation is the bounded visual sub-owner inside
  ReplayPrediction. Runtime/App supplies synchronous Replay path and camera
  values when sequencing a frame; the owner never retains a Replay owner.

Glossary:
  Presentation packet: Frame-local immutable spans submitted to rendering.
  Focus mask: Dense frame-local rows faded around the selected path family.
  Submission probe: Stable-window evidence for trajectory bytes and reserve growth.

Invariants:
  - Prediction presentation may consume Replay values but Replay never names this owner.
  - Every Replay path or camera borrow expires before the presentation command returns.
  - Pose matching and focus masks are capped by MAX_SCENE_OBJECTS.
  - Ghost requests reserve their complete steady-state capacity before gameplay.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.h
  - SkullbonezSource/Runtime/Replay/ReplayPresentation.h
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
*/
#pragma once

#include "ReplayPredictionDrawing.h"
#include "ReplayPredictionView.h"
#include "../Replay/ReplayPresentation.h"
#include "../Replay/ReplayVisualPacket.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Core/SceneCapacity.h"

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
class PhysicsEngine;
} // namespace Physics
namespace Rendering
{
class RenderInstanceStore;
struct RenderInstancePresentationRecord;
} // namespace Rendering
namespace Runtime
{
class EditorTracer;
class SceneEntityStore;

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
    // Invariant: readiness may become true during a publication but cannot
    // regress while the target/source publication key remains unchanged.
    uint64_t presentationTargetId = 0;
    ReplayFrameIndex presentationSourceFrame = 0;
    uint32_t futureTreeReadinessDropCount = 0;
    bool presentationKeyValid = false;
    bool futureTreeReadySeen = false;
    bool futureTreeReadyLastFrame = false;
};

struct ReplayPredictionPresentationMemoryStats
{
    uint64_t ghostRequestCapacityBytes = 0;
    uint64_t focusModelMaskCapacityBytes = 0;
    uint64_t ghostRequestCount = 0;
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats trajectory;
};

class ReplayPredictionPresentation
{
  public:
    explicit ReplayPredictionPresentation( Core::Profiler* profiler = nullptr );

    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats TrajectoryVisualStatsSnapshot() const noexcept;
    ReplayTrajectorySubmissionProbeStats TrajectorySubmissionProbeSnapshot() const noexcept;
    const ReplayVisualPacket& PublishedVisualPacketView() const noexcept;
    std::span<const ReplayPredictionGhostDrawRequest> GhostDrawRequestsView() const noexcept;
    const std::vector<uint8_t>& FocusModelMaskView() const noexcept;
    ReplayPredictionPresentationMemoryStats CollectMemoryStats() const noexcept;
    uint64_t AppearanceInvalidationCount() const noexcept
    {
        return m_retainedAppearanceInvalidationCount;
    }

    void ReserveRecordingBuffers();
    bool BuildFocusModelMask( const RunReplayPathVisualizerState& path,
                              const Physics::PhysicsBodyStore& bodyStore,
                              int modelCount,
                              std::span<const RunReplayPathTraceNode> futureNodes );
    bool ApplyFrameForRender( Rendering::RenderInstanceStore& renderInstances,
                              const Physics::PhysicsBodyStore& bodyStore,
                              const Physics::ColliderStore& colliderStore,
                              const RunReplayPredictionFrame& frame );
    bool BuildGhostDrawRequests( const ReplayPredictionPresentationView& prediction,
                                 std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                 const Physics::PhysicsBodyStore& bodyStore );
    // Owns the retained append-only trajectory list and its publication cursor.
    // The frame tracer receives provisional tails only; no draw-list state
    // escapes back to Runtime/App.
    bool PrepareRetainedTrajectoryDrawList( const ReplayPredictionPresentationView& prediction,
                                            const RunReplayPathVisualizerState& path,
                                            const SceneEntityStore& entities,
                                            const Physics::ColliderStore& colliderStore,
                                            EditorTracer& frameTracer,
                                            const Core::ReplayTrajectoryAppearanceConfig& trajectoryAppearance );
    void AttachRetainedPredictionGeometry( ReplayVisualPacket& packet,
                                           const Math::Vector::Vector3& cameraEye,
                                           const Math::Vector::Vector3& cameraUp );
    void PublishVisualPacket( ReplayVisualPacket packet,
                              const ReplayPredictionPresentationView& prediction,
                              Physics::PhysicsSceneObjectId pathTargetId,
                              const ReplaySolverFrameSample* latestSolver,
                              uint64_t replayReserveGrowthEvents );
    void RenderPathVisualizer( const ReplayPredictionPresentationView& prediction,
                               const RunReplayPathVisualizerState& path,
                               const ReplaySolverFrameSample* presentSample,
                               Physics::PhysicsEngine& physics,
                               const SceneEntityStore& entities,
                               EditorTracer& tracer,
                               bool drawPredictionOverlay = true );
    void RenderCauseFocusOverlay( const RunReplayCameraState& camera,
                                  const RunReplayCauseTreeState& causeTree,
                                  const ReplayPredictionPresentationView& prediction,
                                  const ReplaySolverFrameSample* currentSolverSample,
                                  const Physics::PhysicsBodyStore& bodyStore,
                                  const Physics::ColliderStore& colliderStore,
                                  const SceneEntityStore& entities,
                                  EditorTracer& tracer );
    void ResetTrajectoryVisualStats() noexcept;
    void RecordTrajectoryFrameStats( const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& frameStats );
    void RecordTrajectorySubmissionFrame(
        const SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats& submissionStats,
        int frameNumber,
        uint64_t reserveGrowthEventCount );
    void RecordTrajectoryBudgetExpiry( SkullbonezCore::Core::MainMemoryReplayBudgetPass pass );
    void RecordTrajectoryRebuildCause( SkullbonezCore::Core::MainMemoryReplayRebuildCause cause );

  private:
    void ClearGhostDrawRequests() noexcept;
    bool CanAppendGhostDrawRequests( std::size_t count ) const noexcept;
    void AppendGhostDrawRequest( const ReplayPredictionGhostDrawRequest& request );
    bool HasGhostDrawRequests() const noexcept;
    bool PrepareRenderPoseBodyMatch( int modelCount ) noexcept;
    void StorePublishedVisualPacket( ReplayVisualPacket packet );

    // Lifetime: startup-bound diagnostics borrow; never retained by worker work.
    Core::Profiler* m_profiler;
    SkullbonezCore::Core::MainMemoryReplayTrajectoryStats m_trajectoryVisualStats;
    ReplayTrajectorySubmissionProbeStats m_trajectorySubmissionProbe;
    ReplayVisualPacket m_publishedVisualPacket;
    std::vector<ReplayPredictionGhostDrawRequest> m_ghostDrawRequests;
    std::vector<uint8_t> m_focusModelMask;
    std::array<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_renderPoseBodyMatched = {};
    // Invariant: these fields are the sole retained Prediction trajectory
    // presentation state. App may sequence commands but cannot mutate cursors.
    EditorTracer m_retainedDrawList;
    ReplayOverlay::ReplayPredictionDrawListState m_retainedDrawListState;
    ReplayVisualPacket m_retainedDrawPacket;
    uint64_t m_retainedDrawStreamId = 1;
    uint64_t m_retainedDrawRevision = 0;
    uint64_t m_retainedAppearanceInvalidationCount = 0;
    bool m_retainedDrawPacketDirty = true;
    bool m_retainedRenderingActive = false;
};
} // namespace Runtime
} // namespace SkullbonezCore
