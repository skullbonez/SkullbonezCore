/*
File: SkullbonezSource/Runtime/App/ReplayPredictionPresentation.h
Purpose:
  Owns prediction pose, ghost, focus-mask, trajectory, and visual-packet presentation state.

Summary:
  ReplayPredictionPresentation is the App-owned visual composition state over
  immutable Prediction publication and synchronous Replay/Scene/Tools borrows.

Glossary:
  Presentation packet: Frame-local immutable spans submitted to rendering.
  Submission probe: Stable-window evidence for trajectory bytes and reserve growth.

Invariants:
  - App presentation may consume sibling values but no sibling retains this owner.
  - Every Replay path or camera borrow expires before the presentation command returns.
  - Pose matching and focus masks are capped by MAX_SCENE_OBJECTS.
  - Ghost requests reserve their complete steady-state capacity before gameplay.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.h
  - SkullbonezSource/Runtime/App/ReplayPredictionRetainedGeometry.h
  - SkullbonezSource/Runtime/Replay/ReplayPresentation.h
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Automation/ReplayAutomationPackets.h"
#include "ReplayPredictionDrawing.h"
#include "../Prediction/ReplayPredictionView.h"
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
namespace Core
{
class SbDiagnosticStore;
}
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
    ReplayPredictionPresentation( Core::SbDiagnosticStore& resultDiagnostics, Core::Profiler* profiler = nullptr );

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
    bool BuildFocusModelMask( const RunReplayPathVisualizerState& path, const Physics::PhysicsBodyStore& bodyStore,
                              int modelCount, std::span<const RunReplayPathTraceNode> futureNodes );
    bool ApplyFrameForRender( Rendering::RenderInstanceStore& renderInstances, const Physics::PhysicsBodyStore& bodyStore,
                              const Physics::ColliderStore& colliderStore, const RunReplayPredictionFrame& frame );
    bool BuildGhostDrawRequests( const ReplayPredictionPresentationView& prediction,
                                 std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                 const Physics::PhysicsBodyStore& bodyStore );

    // Owns the retained append-only trajectory list and its publication cursor.
    // The frame tracer receives provisional tails only; no draw-list state
    // escapes back to Runtime/App.
    bool PrepareRetainedGeometryDrawList( const ReplayPredictionPresentationView& prediction,
                                          const RunReplayPathVisualizerState& path, const SceneEntityStore& entities,
                                          const Physics::ColliderStore& colliderStore, EditorTracer& frameTracer,
                                          const Core::ReplayTrajectoryAppearanceConfig& trajectoryAppearance );
    void AttachRetainedPredictionGeometry( ReplayVisualPacket& packet, const Math::Vector::Vector3& cameraEye,
                                           const Math::Vector::Vector3& cameraUp );
    void PublishVisualPacket( ReplayVisualPacket packet, const ReplayPredictionPresentationView& prediction,
                              Physics::PhysicsSceneObjectId pathTargetId, const ReplaySolverFrameSample* latestSolver,
                              uint64_t replayReserveGrowthEvents );
    void RenderPathVisualizer( const ReplayPredictionPresentationView& prediction, const RunReplayPathVisualizerState& path,
                               const ReplaySolverFrameSample* presentSample, Physics::PhysicsEngine& physics,
                               const SceneEntityStore& entities, EditorTracer& tracer, bool drawPredictionOverlay = true );
    void RenderCauseFocusOverlay( const RunReplayCameraState& camera, const RunReplayCauseTreeState& causeTree,
                                  const ReplayPredictionPresentationView& prediction,
                                  const ReplaySolverFrameSample* currentSolverSample,
                                  const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                                  const SceneEntityStore& entities, EditorTracer& tracer );
    void ResetTrajectoryVisualStats() noexcept;
    void RecordTrajectoryFrameStats( const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& frameStats );
    void
    RecordTrajectorySubmissionFrame( const SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats& submissionStats,
                                     int frameNumber, uint64_t reserveGrowthEventCount );
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
    ReplayOverlay::ReplayPredictionRetainedGeometry m_retainedGeometry;
    EditorTracer m_retainedMarkerDrawList;
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
