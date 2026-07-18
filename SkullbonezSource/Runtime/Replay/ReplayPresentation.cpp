/*
File: SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp
Purpose:
  Initializes and owns replay presentation storage independently of ReplayRuntime.

Summary:
  Replay prediction and retained timelines publish values; this owner keeps the
  mutable camera, path, mask, ghost, and packet state used to turn those values
  into the exact renderer submission for one frame.

Glossary:
  Presentation storage: Replay-only buffers and fixed masks used while building
    the visible path, markers, ghosts, and immutable visual packet.
  Visual packet: Frame-local read-only description of the exact buffers sent to
    the renderer and compared by the mega replay oracle.

Invariants:
  - Path selection and focus masks reserve before steady replay interaction.
  - The render-pose match table is fixed at the scene model capacity.
  - Recording-only ghost storage reserves only after replay is configured.

Related:
  - ReplayPresentation.h
  - ReplayVisualPacket.h
  - ReplayPredictionDrawing.cpp
*/
#include "ReplayPresentation.h"
#include "ReplayPredictionView.h"
#include "../RuntimePickService.h"
#include "../Scene/SceneEntityStore.h"
#include "../Tools/RuntimeTools.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace SkullbonezCore::Runtime
{

const char* ReplayPathColorModeName( ReplayPathColorMode mode ) noexcept
{
    switch ( mode )
    {
    case ReplayPathColorMode::LaneFlat:
        return "Lane flat";
    case ReplayPathColorMode::VelocityHeat:
        return "Velocity heat";
    case ReplayPathColorMode::TimeGradient:
        return "Time gradient";
    case ReplayPathColorMode::PerObjectHue:
        return "Per-object hue";
    case ReplayPathColorMode::CausalDepth:
        return "Causal depth";
    default:
        return "Lane flat";
    }
}

ReplayPastTrajectoryView ReplayPresentation::PastTrajectoryView() const noexcept
{
    ReplayPastTrajectoryView view;
    view.targetId = m_pathVisualizer.targetId;
    view.retainedTargetId = m_pathVisualizer.pastTrajectory.targetId;
    view.targetModelRow = m_pathVisualizer.targetModelRow;
    view.firstFrame = m_pathVisualizer.pastTrajectory.firstFrame;
    view.builtThroughFrame = m_pathVisualizer.pastTrajectory.builtThroughFrame;
    view.totalFramesEvicted = m_pathVisualizer.pastTrajectory.totalFramesEvicted;
    view.fullRebuildCount = m_pathVisualizer.pastTrajectory.fullRebuildCount;
    view.incrementalTrimCount = m_pathVisualizer.pastTrajectory.incrementalTrimCount;
    view.hasTarget = m_pathVisualizer.hasTarget;
    view.valid = m_pathVisualizer.pastTrajectory.valid;
    return view;
}

void ReplayPresentation::PopulateLauncherVisualCapture( ReplayCaptureInput& input, RuntimeTools& runtimeTools )
{
    runtimeTools.BuildReplayLauncherVisualSample( m_launcherVisualCaptureScratch );
    input.launcherVisual = &m_launcherVisualCaptureScratch;
}

void ReplayPresentation::ReserveLauncherVisualCaptureBuffers()
{
    constexpr std::size_t launcherLaserShotCapacity = 32;
    m_launcherVisualCaptureScratch.rayLines.reserve( RunRayCastTestState::MAX_LINES );
    m_launcherVisualCaptureScratch.laserShots.reserve( launcherLaserShotCapacity );
}

void ReplayPresentation::StoreLauncherVisualBackupFrom( RuntimeTools& runtimeTools )
{
    runtimeTools.BuildReplayLauncherVisualSample( m_launcherVisualBackup );
    m_launcherVisualBackupActive = true;
}

void ReplayPresentation::RestoreAndClearLauncherVisualBackup( RuntimeTools& runtimeTools )
{
    if ( !m_launcherVisualBackupActive )
    {
        return;
    }
    runtimeTools.RestoreReplayLauncherVisualSample( m_launcherVisualBackup );
    ClearLauncherVisualBackup();
}

namespace
{
constexpr int REPLAY_TRAJECTORY_SUBMISSION_STEADY_FRAME_TARGET = 120;

template <typename T> uint64_t ReplayPresentationVectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() * sizeof( T ) );
}

float ReplayQueryColliderRadiusForModelIndex( const Physics::ColliderStore& colliderStore, int modelIndex )
{
    const Physics::PhysicsColliderHandle colliderHandle = colliderStore.HandleForModelIndex( modelIndex );
    const Physics::ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
    if ( !collider || colliderStore.ModelIndexForHandle( colliderHandle ) != modelIndex )
    {
        return 1.0f;
    }
    return (std::max)( collider->boundingRadius > 0.0f
                           ? collider->boundingRadius
                           : Math::CollisionDetection::GetShapeBoundingRadius( collider->shape ),
                       1.0f );
}

ReplayBodyId ReplayQueryBodyIdForModelIndex( const Physics::PhysicsBodyStore& bodyStore, int modelIndex )
{
    ReplayBodyId id;
    if ( const Physics::PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex ) )
    {
        id.value = body->replayBodyId;
    }
    return id;
}

bool ReplayQueryIntersectRaySphere( const Math::Vector::Vector3& rayOrigin,
                                    const Math::Vector::Vector3& rayDirection,
                                    const Math::Vector::Vector3& center,
                                    float radius,
                                    float& outT )
{
    const Math::Vector::Vector3 offset = rayOrigin - center;
    const float rayProjection = offset * rayDirection;
    const float radialDistance = ( offset * offset ) - radius * radius;
    if ( radialDistance > 0.0f && rayProjection > 0.0f )
    {
        return false;
    }
    const float discriminant = rayProjection * rayProjection - radialDistance;
    if ( discriminant < 0.0f )
    {
        return false;
    }
    outT = -rayProjection - sqrtf( discriminant );
    if ( outT < 0.0f )
    {
        outT = 0.0f;
    }
    return true;
}

bool ReplayPresentationModelIsRagdollPart(
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
    int modelIndex )
{
    // SimpleRagdoll children share replay visuals with their collection root.
    // This helper keeps that policy beside the ghost requests it filters.
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( presentationRecords.size() ) )
    {
        return false;
    }
    return presentationRecords[static_cast<std::size_t>( modelIndex )].simpleRagdollPart;
}

const Physics::PhysicsBodyRecord* ReplayPresentationResolveReplayBody( const Physics::PhysicsBodyStore& bodyStore,
                                                                       ReplayBodyId id,
                                                                       int modelIndexHint,
                                                                       int modelCount,
                                                                       int& outModelIndex )
{
    outModelIndex = -1;
    if ( id.value == 0 )
    {
        return nullptr;
    }

    // Invariant: replay artifacts carry model indices only as staleable hints.
    // Stable identity is the replay id resolved through the live body handle map.
    const Physics::PhysicsBodyHandle body = bodyStore.HandleForReplayBodyId( id.value, modelIndexHint );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );
    if ( !record || record->replayBodyId != id.value || modelIndex < 0 || modelIndex >= modelCount )
    {
        return nullptr;
    }

    outModelIndex = modelIndex;
    return record;
}

const Physics::PhysicsBodyRecord* ReplayPresentationBodyRecordForModelIndex( const Physics::PhysicsBodyStore& bodyStore,
                                                                             int modelIndex )
{
    const Physics::PhysicsBodyHandle body = bodyStore.HandleForModelIndex( modelIndex );
    const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );
    if ( !record || bodyStore.ModelIndexForHandle( body ) != modelIndex || record->replayBodyId == 0 )
    {
        return nullptr;
    }
    return record;
}

bool ReplayPresentationQueueRenderPoseOverride( Rendering::RenderInstanceStore& renderInstances,
                                                const Physics::PhysicsBodyStore& bodyStore,
                                                const Physics::ColliderStore& colliderStore,
                                                ReplayBodyId replayBodyId,
                                                const Math::Vector::Vector3& position,
                                                const Math::Orientation::Quaternion& orientation )
{
    const Physics::PhysicsBodyHandle body = bodyStore.HandleForReplayBodyId( replayBodyId.value );
    const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    if ( !record || record->replayBodyId != replayBodyId.value || modelIndex < 0 )
    {
        return false;
    }
    return renderInstances.OverridePose( modelIndex, replayBodyId.value, position, orientation, colliderStore );
}

int ReplayPresentationRenderPoseModelHint( const ReplayBodyPresentationSample& ) noexcept
{
    return -1;
}

int ReplayPresentationRenderPoseModelHint( const ReplaySolverBodySample& ) noexcept
{
    return -1;
}

int ReplayPresentationRenderPoseModelHint( const RunReplayPredictionBodySample& body ) noexcept
{
    return body.modelRow.value;
}

Math::Orientation::Quaternion ReplayPresentationRenderPoseOrientation( const ReplayBodyPresentationSample& body )
{
    return Math::Orientation::Quaternion( body.orientation[0],
                                          body.orientation[1],
                                          body.orientation[2],
                                          body.orientation[3] );
}

Math::Orientation::Quaternion ReplayPresentationRenderPoseOrientation( const ReplaySolverBodySample& body )
{
    return Math::Orientation::Quaternion( body.orientation[0],
                                          body.orientation[1],
                                          body.orientation[2],
                                          body.orientation[3] );
}

Math::Orientation::Quaternion ReplayPresentationRenderPoseOrientation( const RunReplayPredictionBodySample& body )
{
    return body.orientation;
}

// Concept: all retained replay body rows apply the same renderer override
// protocol. Compile-time overloads adapt only the model-row hint and orientation
// storage; no callback or owner indirection enters this per-frame loop.
//
// Invariant: body order, identity resolution, quaternion normalization, override
// submission, and match marking stay in this exact order for every sample type.
template <typename BodySample>
bool ReplayPresentationApplyBodyRenderPoses( Rendering::RenderInstanceStore& renderInstances,
                                             const Physics::PhysicsBodyStore& bodyStore,
                                             const Physics::ColliderStore& colliderStore,
                                             std::span<const BodySample> bodies,
                                             std::span<uint8_t> matchedBodies,
                                             int modelCount )
{
    bool queuedAny = false;
    for ( const BodySample& body : bodies )
    {
        int resolvedModelIndex = -1;
        if ( !ReplayPresentationResolveReplayBody( bodyStore,
                                                   body.id,
                                                   ReplayPresentationRenderPoseModelHint( body ),
                                                   modelCount,
                                                   resolvedModelIndex ) )
        {
            continue;
        }

        Math::Orientation::Quaternion orientation = ReplayPresentationRenderPoseOrientation( body );
        orientation.Normalise();
        if ( ReplayPresentationQueueRenderPoseOverride( renderInstances,
                                                        bodyStore,
                                                        colliderStore,
                                                        body.id,
                                                        body.position,
                                                        orientation ) )
        {
            matchedBodies[static_cast<std::size_t>( resolvedModelIndex )] = 1;
            queuedAny = true;
        }
    }
    return queuedAny;
}

bool ReplayPresentationHideUnmatchedRenderBodies( Rendering::RenderInstanceStore& renderInstances,
                                                  const Physics::PhysicsBodyStore& bodyStore,
                                                  const Physics::ColliderStore& colliderStore,
                                                  std::span<const uint8_t> matchedBodies,
                                                  int modelCount )
{
    bool queuedAny = false;
    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );
    for ( int modelIndex = 0; modelIndex < modelCount; ++modelIndex )
    {
        if ( matchedBodies[static_cast<std::size_t>( modelIndex )] != 0 )
        {
            continue;
        }

        const Physics::PhysicsBodyRecord* bodyRecord =
            ReplayPresentationBodyRecordForModelIndex( bodyStore, modelIndex );
        if ( !bodyRecord )
        {
            continue;
        }

        // Why: loaded artifacts may not contain every live body. Move unmatched
        // bodies out of view instead of letting unrelated live geometry appear
        // inside the scrubbed replay frame.
        const ReplayBodyId replayBodyId{ bodyRecord->replayBodyId };
        if ( ReplayPresentationQueueRenderPoseOverride( renderInstances,
                                                        bodyStore,
                                                        colliderStore,
                                                        replayBodyId,
                                                        hiddenReplayPosition,
                                                        Math::Orientation::IDENTITY_QUATERNION ) )
        {
            queuedAny = true;
        }
    }
    return queuedAny;
}

RunReplayPathTarget* FindReplayQueryPathTarget( RunReplayPathVisualizerState& visualizer, ReplayBodyId id )
{
    for ( RunReplayPathTarget& target : visualizer.targets )
    {
        if ( target.id.value == id.value )
        {
            return &target;
        }
    }
    return nullptr;
}

void ApplyReplayQueryPrimaryPathTarget( RunReplayPathVisualizerState& visualizer,
                                        ReplayBodyId id,
                                        int modelIndex,
                                        const char* name )
{
    visualizer.hasTarget = true;
    visualizer.targetId = id;
    visualizer.targetModelRow.value = modelIndex;
    visualizer.targetName[0] = '\0';
    if ( name && name[0] != '\0' )
    {
        strncpy_s( visualizer.targetName, sizeof( visualizer.targetName ), name, _TRUNCATE );
    }
}
} // namespace

ReplayPresentation::ReplayPresentation( Core::Profiler* profiler ) : m_profiler( profiler )
{
    // Runtime allocation policy: path target selection is a live replay UI
    // action, so it rotates entries within a fixed pre-gameplay vector budget.
    m_pathVisualizer.targets.reserve( REPLAY_PATH_MAX_ROOT_TARGETS );
    // Runtime allocation policy: focus masks are rewritten during replay render
    // passes, so the byte vector owns its full model-capacity storage up front.
    m_focusModelMask.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_renderPoseBodyMatched.fill( uint8_t{ 0 } );
}


RunReplayCameraState ReplayPresentation::CameraView() const noexcept
{
    return m_camera;
}


SkullbonezCore::Core::MainMemoryReplayTrajectoryStats ReplayPresentation::TrajectoryVisualStatsSnapshot() const noexcept
{
    return m_trajectoryVisualStats;
}


ReplayTrajectorySubmissionProbeStats ReplayPresentation::TrajectorySubmissionProbeSnapshot() const noexcept
{
    return m_trajectorySubmissionProbe;
}


const ReplayVisualPacket& ReplayPresentation::PublishedVisualPacketView() const noexcept
{
    return m_publishedVisualPacket;
}


std::span<const ReplayPredictionGhostDrawRequest> ReplayPresentation::PredictionGhostDrawRequestsView() const noexcept
{
    return m_predictionGhostDrawRequests;
}


const std::vector<uint8_t>& ReplayPresentation::FocusModelMaskView() const noexcept
{
    return m_focusModelMask;
}


ReplayPresentationMemoryStats ReplayPresentation::CollectMemoryStats() const noexcept
{
    ReplayPresentationMemoryStats stats;
    stats.pathOwnerBytes = static_cast<uint64_t>( sizeof( m_pathVisualizer ) );
    stats.pathTargetCapacityBytes = ReplayPresentationVectorCapacityBytes( m_pathVisualizer.targets );
    stats.pathFutureNodeCapacityBytes = ReplayPresentationVectorCapacityBytes( m_pathVisualizer.futureNodes );
    stats.ghostRequestCapacityBytes = ReplayPresentationVectorCapacityBytes( m_predictionGhostDrawRequests );
    stats.focusModelMaskCapacityBytes = ReplayPresentationVectorCapacityBytes( m_focusModelMask );
    stats.launcherVisualBytes = static_cast<uint64_t>( sizeof( m_launcherVisualBackup ) ) +
                                ReplayPresentationVectorCapacityBytes( m_launcherVisualBackup.rayLines ) +
                                ReplayPresentationVectorCapacityBytes( m_launcherVisualBackup.laserShots );
    stats.pathNodeCount = static_cast<uint64_t>( m_pathVisualizer.futureNodes.size() );
    stats.ghostRequestCount = static_cast<uint64_t>( m_predictionGhostDrawRequests.size() );
    stats.trajectory = m_trajectoryVisualStats;
    return stats;
}


bool ReplayPresentation::HasLauncherVisualBackup() const noexcept
{
    return m_launcherVisualBackupActive;
}


void ReplayPresentation::ReserveRecordingBuffers()
{
    // Runtime allocation policy: ghost requests are replay-only overlay data.
    // Reserve during startup replay configuration, before steady interaction.
    m_predictionGhostDrawRequests.reserve( REPLAY_PREDICTION_GHOST_REQUEST_CAPACITY );
}


void ReplayPresentation::BeginCameraInspection( RunCameraMode restoreMode,
                                                uint32_t restoreCameraHash,
                                                const Math::Vector::Vector3& restoreEye,
                                                const Math::Vector::Vector3& restoreView,
                                                const Math::Vector::Vector3& restoreUp ) noexcept
{
    m_camera.restoreCameraMode = restoreMode;
    m_camera.restoreCameraHash = restoreCameraHash;
    m_camera.restoreEye = restoreEye;
    m_camera.restoreView = restoreView;
    m_camera.restoreUp = restoreUp;
    m_camera.hasRestorePose = true;
    m_camera.active = true;
}


void ReplayPresentation::EndCameraInspection() noexcept
{
    m_camera.active = false;
    m_camera.focusKind = RunReplayCameraFocusKind::None;
    m_camera.focusedRow = -1;
    m_camera.hasRestorePose = false;
    m_camera.ownsSimulationPause = false;
    m_camera.restoreCameraMode = RunCameraMode::Demo;
}


void ReplayPresentation::SetCameraPauseOwnership( bool ownsPause ) noexcept
{
    m_camera.ownsSimulationPause = ownsPause;
}


void ReplayPresentation::ApplyCameraFocus( const ReplayCameraFocusRequest& request ) noexcept
{
    m_camera.focusKind = request.focusKind;
    m_camera.focusedId = request.focusedId;
    m_camera.counterpartId = request.counterpartId;
    m_camera.focusedRow = request.focusedRow;
    m_camera.focusRowKind = request.focusRowKind;
    m_camera.focusModelRow = request.focusModelRow;
    m_camera.focusCounterpartModelRow = request.focusCounterpartModelRow;
    m_camera.focusContactIndex = request.focusContactIndex;
    m_camera.focusSolverRowIndex = request.focusSolverRowIndex;
    m_camera.focusFeatureId = request.focusFeatureId;
    m_camera.focusTerrain = request.focusTerrain;
    m_camera.targetPoint = request.targetPoint;
    m_camera.targetNormal = request.targetNormal;
    m_camera.impulseVector = request.impulseVector;
    m_camera.targetRadius = request.targetRadius;
}


void ReplayPresentation::SetCameraFocusedRow( int row ) noexcept
{
    m_camera.focusedRow = row;
}


bool ReplayPresentation::ClearCameraFocus() noexcept
{
    const bool ownedPause = m_camera.ownsSimulationPause;
    m_camera.focusKind = RunReplayCameraFocusKind::None;
    m_camera.focusedId = ReplayBodyId{};
    m_camera.counterpartId = ReplayBodyId{};
    m_camera.focusedRow = -1;
    m_camera.focusRowKind = RunReplayCauseTreeRowKind::Body;
    m_camera.focusModelRow.value = -1;
    m_camera.focusCounterpartModelRow.value = -1;
    m_camera.focusContactIndex = -1;
    m_camera.focusSolverRowIndex = -1;
    m_camera.focusFeatureId = 0;
    m_camera.focusTerrain = false;
    m_camera.targetPoint = Math::Vector::ZERO_VECTOR;
    m_camera.targetNormal = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    m_camera.impulseVector = Math::Vector::ZERO_VECTOR;
    m_camera.ownsSimulationPause = false;
    return ownedPause;
}


void ReplayPresentation::ClearPathState()
{
    m_pathVisualizer.hasTarget = false;
    m_pathVisualizer.targetId = ReplayBodyId{};
    m_pathVisualizer.targetModelRow.value = -1;
    m_pathVisualizer.targetName[0] = '\0';
    m_pathVisualizer.futureNodes.clear();
    m_pathVisualizer.targets.clear();
    m_pathVisualizer.pastTrajectory = RunReplayPastTrajectoryBuildState{};
}


void ReplayPresentation::PreparePathDrawing( const Physics::PhysicsBodyStore& bodyStore )
{
    // The legacy report vector is intentionally empty: the prediction owner
    // publishes its immutable future-node view, while the presentation owner
    // retains only selected past-path targets.
    m_pathVisualizer.futureNodes.clear();
    if ( !m_pathVisualizer.hasTarget || !m_pathVisualizer.pastPathVisible || m_pathVisualizer.targetId.value == 0 )
    {
        return;
    }

    if ( m_pathVisualizer.targets.empty() )
    {
        RunReplayPathTarget target;
        target.id = m_pathVisualizer.targetId;
        target.modelRow = m_pathVisualizer.targetModelRow;
        if ( m_pathVisualizer.targetName[0] != '\0' )
        {
            strncpy_s( target.name, sizeof( target.name ), m_pathVisualizer.targetName, _TRUNCATE );
        }
        m_pathVisualizer.targets.push_back( target );
    }

    for ( RunReplayPathTarget& target : m_pathVisualizer.targets )
    {
        const Physics::PhysicsBodyHandle handle =
            bodyStore.HandleForReplayBodyId( target.id.value, target.modelRow.value );
        const int modelIndex = bodyStore.ModelIndexForHandle( handle );
        if ( modelIndex < 0 )
        {
            continue;
        }
        target.modelRow.value = modelIndex;
        if ( target.id.value == m_pathVisualizer.targetId.value )
        {
            m_pathVisualizer.targetModelRow.value = modelIndex;
        }
    }
}


void ReplayPresentation::SetPathTargetModelRow( Physics::ModelRowHint modelRow ) noexcept
{
    m_pathVisualizer.targetModelRow = modelRow;
}


void ReplayPresentation::ApplyArchivePathState( const RunReplayPathVisualizerState& archiveState )
{
    m_pathVisualizer.hasTarget = archiveState.hasTarget;
    m_pathVisualizer.pastPathVisible = archiveState.pastPathVisible;
    m_pathVisualizer.targetId = archiveState.targetId;
    m_pathVisualizer.targetModelRow = archiveState.targetModelRow;
    strncpy_s( m_pathVisualizer.targetName, sizeof( m_pathVisualizer.targetName ), archiveState.targetName, _TRUNCATE );
    m_pathVisualizer.futureNodes.clear();
    m_pathVisualizer.targets.clear();
    m_pathVisualizer.pastTrajectory = RunReplayPastTrajectoryBuildState{};
}


void ReplayPresentation::ApplyPastTrajectoryUpdate( ReplayBodyId targetId,
                                                    ReplayFrameIndex firstFrame,
                                                    ReplayFrameIndex builtThroughFrame,
                                                    uint64_t totalFramesEvicted,
                                                    uint64_t fullRebuildCount,
                                                    uint64_t incrementalTrimCount,
                                                    bool valid,
                                                    Physics::ModelRowHint targetModelRow,
                                                    bool targetModelRowRepaired )
{
    m_pathVisualizer.pastTrajectory.targetId = targetId;
    m_pathVisualizer.pastTrajectory.firstFrame = firstFrame;
    m_pathVisualizer.pastTrajectory.builtThroughFrame = builtThroughFrame;
    m_pathVisualizer.pastTrajectory.totalFramesEvicted = totalFramesEvicted;
    m_pathVisualizer.pastTrajectory.fullRebuildCount = fullRebuildCount;
    m_pathVisualizer.pastTrajectory.incrementalTrimCount = incrementalTrimCount;
    m_pathVisualizer.pastTrajectory.valid = valid;
    if ( targetModelRowRepaired )
    {
        m_pathVisualizer.targetModelRow = targetModelRow;
    }
}


void ReplayPresentation::TogglePastPathVisible()
{
    m_pathVisualizer.pastPathVisible = !m_pathVisualizer.pastPathVisible;
    if ( !m_pathVisualizer.pastPathVisible )
    {
        m_pathVisualizer.futureNodes.clear();
    }
}


ReplayPathColorMode ReplayPresentation::CyclePathColorMode() noexcept
{
    constexpr uint8_t modeCount = static_cast<uint8_t>( ReplayPathColorMode::CausalDepth ) + 1u;
    const uint8_t next = ( static_cast<uint8_t>( m_pathVisualizer.colorMode ) + 1u ) % modeCount;
    m_pathVisualizer.colorMode = static_cast<ReplayPathColorMode>( next );
    return m_pathVisualizer.colorMode;
}


bool ReplayPresentation::SetPathTarget( const char* name, int modelIndex, const Physics::PhysicsBodyStore& bodyStore )
{
    if ( modelIndex < 0 )
    {
        return false;
    }
    const Physics::PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex );
    if ( !body || body->replayBodyId == 0 )
    {
        return false;
    }

    return SetPathTarget( ReplayBodyId{ body->replayBodyId }, Physics::ModelRowHint{ modelIndex }, name );
}


bool ReplayPresentation::SetPathTarget( ReplayBodyId id, Physics::ModelRowHint modelRow, const char* name )
{
    if ( id.value == 0 || modelRow.value < 0 )
    {
        return false;
    }

    m_pathVisualizer.hasTarget = true;
    m_pathVisualizer.targetId = id;
    m_pathVisualizer.targetModelRow = modelRow;
    m_pathVisualizer.targetName[0] = '\0';
    if ( name && name[0] != '\0' )
    {
        strncpy_s( m_pathVisualizer.targetName, sizeof( m_pathVisualizer.targetName ), name, _TRUNCATE );
    }
    m_pathVisualizer.futureNodes.clear();
    return true;
}


ReplayPathPickResult
ReplayPresentation::TryPickPathTarget( const ReplayPathPickInput& input,
                                       const SceneEntityStore& entities,
                                       const Physics::PhysicsBodyStore& bodyStore,
                                       const Physics::ColliderStore& colliderStore,
                                       std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                       const ReplaySolverFrameSample* currentSolverSample )
{
    ReplayPathPickResult result;
    if ( !input.hasWorldRay )
    {
        if ( input.clearOnMiss )
        {
            ClearPathState();
            result.exitInspectionCamera = true;
        }
        return result;
    }

    const int modelCount = (std::min)( bodyStore.Count(), colliderStore.Count() );
    const auto copyPresentationName = [&]( int modelIndex, char* outName, std::size_t outSize )
    {
        if ( !outName || outSize == 0 )
        {
            return;
        }
        outName[0] = '\0';
        if ( modelIndex >= 0 && modelIndex < static_cast<int>( presentationRecords.size() ) )
        {
            const char* displayName = presentationRecords[static_cast<std::size_t>( modelIndex )].displayName;
            if ( displayName[0] != '\0' )
            {
                strncpy_s( outName, outSize, displayName, _TRUNCATE );
            }
        }
    };

    ReplayBodyId pickedId;
    int pickedIndex = -1;
    char pickedName[64] = {};
    if ( currentSolverSample )
    {
        float bestT = FLT_MAX;
        for ( const ReplaySolverBodySample& body : currentSolverSample->bodies )
        {
            float radius = 1.0f;
            if ( body.modelRow.value >= 0 && body.modelRow.value < modelCount )
            {
                radius = ReplayQueryColliderRadiusForModelIndex( colliderStore, body.modelRow.value ) + 1.0f;
            }
            float rayT = 0.0f;
            if ( ReplayQueryIntersectRaySphere( input.rayOrigin, input.rayDirection, body.position, radius, rayT ) &&
                 rayT < bestT )
            {
                bestT = rayT;
                pickedId = body.id;
                pickedIndex = body.modelRow.value;
                if ( body.name[0] != '\0' )
                {
                    strncpy_s( pickedName, sizeof( pickedName ), body.name, _TRUNCATE );
                }
            }
        }
    }
    else
    {
        RuntimePickRequest request;
        request.purpose = RuntimePickPurpose::ReplayPathTarget;
        request.bodyStore = &bodyStore;
        request.colliderStore = &colliderStore;
        request.rayOrigin = input.rayOrigin;
        request.rayDirection = input.rayDirection;

        RuntimePickResult pick;
        if ( RuntimePickService::TryPickModel( request, pick ) )
        {
            pickedIndex = pick.modelRow.value;
            pickedId = ReplayQueryBodyIdForModelIndex( bodyStore, pickedIndex );
            copyPresentationName( pickedIndex, pickedName, sizeof( pickedName ) );
        }
    }

    if ( pickedIndex >= 0 && pickedIndex < modelCount )
    {
        const SceneEntityRecord* pickedEntity = entities.TryGet( pickedIndex );
        const int collectionIndex =
            pickedEntity && pickedEntity->behaviorGroup.kind == SceneBehaviorGroupKind::SimpleRagdoll
                ? entities.FindBySceneObjectId( pickedEntity->behaviorGroup.rootObjectId )
                : pickedIndex;
        if ( collectionIndex >= 0 && collectionIndex < modelCount && collectionIndex != pickedIndex )
        {
            pickedIndex = collectionIndex;
            pickedId = ReplayQueryBodyIdForModelIndex( bodyStore, collectionIndex );
            copyPresentationName( collectionIndex, pickedName, sizeof( pickedName ) );
        }
    }

    if ( pickedId.value != 0 )
    {
        if ( !input.additive )
        {
            m_pathVisualizer.targets.clear();
        }
        RunReplayPathTarget* target = FindReplayQueryPathTarget( m_pathVisualizer, pickedId );
        if ( !target )
        {
            // Invariant: constructor-reserved target storage rotates entries;
            // live picking never grows the vector.
            if ( m_pathVisualizer.targets.capacity() < REPLAY_PATH_MAX_ROOT_TARGETS )
            {
                return result;
            }
            if ( m_pathVisualizer.targets.size() >= REPLAY_PATH_MAX_ROOT_TARGETS )
            {
                m_pathVisualizer.targets.erase( m_pathVisualizer.targets.begin() );
            }
            if ( m_pathVisualizer.targets.size() >= m_pathVisualizer.targets.capacity() )
            {
                return result;
            }
            RunReplayPathTarget nextTarget;
            nextTarget.id = pickedId;
            m_pathVisualizer.targets.push_back( nextTarget );
            target = &m_pathVisualizer.targets.back();
        }

        target->modelRow.value = pickedIndex;
        target->name[0] = '\0';
        if ( pickedName[0] != '\0' )
        {
            strncpy_s( target->name, sizeof( target->name ), pickedName, _TRUNCATE );
        }
        ApplyReplayQueryPrimaryPathTarget( m_pathVisualizer, pickedId, pickedIndex, target->name );
        m_pathVisualizer.futureNodes.clear();
        result.picked = true;
        return result;
    }

    if ( input.clearOnMiss )
    {
        ClearPathState();
        result.exitInspectionCamera = true;
    }
    return result;
}


bool ReplayPresentation::BuildFocusModelMask( const Physics::PhysicsBodyStore& bodyStore,
                                              int modelCount,
                                              std::span<const RunReplayPathTraceNode> futureNodes )
{
    if ( !m_pathVisualizer.hasTarget || m_pathVisualizer.targetId.value == 0 || modelCount <= 0 ||
         modelCount > SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        m_focusModelMask.clear();
        return false;
    }

    m_focusModelMask.assign( static_cast<std::size_t>( modelCount ), 0 );
    int markedCount = 0;
    const auto markByReplayId = [&]( ReplayBodyId id, int preferredModelIndex )
    {
        if ( id.value == 0 )
        {
            return;
        }

        const Physics::PhysicsBodyHandle body = bodyStore.HandleForReplayBodyId( id.value, preferredModelIndex );
        const int resolvedIndex = bodyStore.ModelIndexForHandle( body );
        if ( resolvedIndex >= 0 && resolvedIndex < modelCount )
        {
            uint8_t& mask = m_focusModelMask[static_cast<std::size_t>( resolvedIndex )];
            if ( mask == 0 )
            {
                mask = 1;
                ++markedCount;
            }
        }
    };

    if ( m_pathVisualizer.targets.empty() )
    {
        markByReplayId( m_pathVisualizer.targetId, m_pathVisualizer.targetModelRow.value );
    }
    else
    {
        for ( const RunReplayPathTarget& target : m_pathVisualizer.targets )
        {
            markByReplayId( target.id, target.modelRow.value );
        }
    }
    for ( const RunReplayPathTraceNode& node : futureNodes )
    {
        markByReplayId( node.id, node.modelRow.value );
    }

    if ( markedCount <= 0 || markedCount >= modelCount )
    {
        m_focusModelMask.clear();
        return false;
    }
    return true;
}


void ReplayPresentation::ClearPredictionGhostDrawRequests() noexcept
{
    m_predictionGhostDrawRequests.clear();
}


bool ReplayPresentation::CanAppendPredictionGhostDrawRequests( std::size_t count ) const noexcept
{
    return m_predictionGhostDrawRequests.size() + count <= m_predictionGhostDrawRequests.capacity();
}


void ReplayPresentation::AppendPredictionGhostDrawRequest( const ReplayPredictionGhostDrawRequest& request )
{
    // Invariant: callers prove capacity before the bounded presentation pass;
    // replay steady-state rendering must never grow this vector.
    if ( m_predictionGhostDrawRequests.size() < m_predictionGhostDrawRequests.capacity() )
    {
        m_predictionGhostDrawRequests.push_back( request );
    }
}


bool ReplayPresentation::HasPredictionGhostDrawRequests() const noexcept
{
    return !m_predictionGhostDrawRequests.empty();
}


// Concept: render replay poses are temporary render-instance overrides.
//
// Scrubbing should affect only the pixels drawn for this frame. These methods
// apply replay or prediction poses to a freshly prepared render-instance
// snapshot; live physics rows and authored presentation metadata are not
// mutated and therefore need no restore.
bool ReplayPresentation::ApplyPresentationSampleForRender( Rendering::RenderInstanceStore& renderInstances,
                                                           const Physics::PhysicsBodyStore& bodyStore,
                                                           const Physics::ColliderStore& colliderStore,
                                                           const ReplayPresentationSample& sample )
{
    const int modelCount = renderInstances.Count();
    if ( !PrepareRenderPoseBodyMatch( modelCount ) )
    {
        return false;
    }
    const std::span<uint8_t> matchedBodies( m_renderPoseBodyMatched.data(), static_cast<std::size_t>( modelCount ) );
    const bool queuedBodies = ReplayPresentationApplyBodyRenderPoses(
        renderInstances,
        bodyStore,
        colliderStore,
        std::span<const ReplayBodyPresentationSample>( sample.bodies.data(), sample.bodies.size() ),
        matchedBodies,
        modelCount );
    const bool queuedHidden = ReplayPresentationHideUnmatchedRenderBodies( renderInstances,
                                                                           bodyStore,
                                                                           colliderStore,
                                                                           matchedBodies,
                                                                           modelCount );
    return queuedBodies || queuedHidden;
}


bool ReplayPresentation::ApplySolverSampleForRender( Rendering::RenderInstanceStore& renderInstances,
                                                     const Physics::PhysicsBodyStore& bodyStore,
                                                     const Physics::ColliderStore& colliderStore,
                                                     const ReplaySolverFrameSample& sample )
{
    const int modelCount = renderInstances.Count();
    if ( !PrepareRenderPoseBodyMatch( modelCount ) )
    {
        return false;
    }
    const std::span<uint8_t> matchedBodies( m_renderPoseBodyMatched.data(), static_cast<std::size_t>( modelCount ) );
    const bool queuedBodies = ReplayPresentationApplyBodyRenderPoses(
        renderInstances,
        bodyStore,
        colliderStore,
        std::span<const ReplaySolverBodySample>( sample.bodies.data(), sample.bodies.size() ),
        matchedBodies,
        modelCount );
    const bool queuedHidden = ReplayPresentationHideUnmatchedRenderBodies( renderInstances,
                                                                           bodyStore,
                                                                           colliderStore,
                                                                           matchedBodies,
                                                                           modelCount );
    return queuedBodies || queuedHidden;
}


bool ReplayPresentation::ApplyPredictionFrameForRender( Rendering::RenderInstanceStore& renderInstances,
                                                        const Physics::PhysicsBodyStore& bodyStore,
                                                        const Physics::ColliderStore& colliderStore,
                                                        const RunReplayPredictionFrame& frame )
{
    const int modelCount = renderInstances.Count();
    if ( !PrepareRenderPoseBodyMatch( modelCount ) )
    {
        return false;
    }
    const std::span<uint8_t> matchedBodies( m_renderPoseBodyMatched.data(), static_cast<std::size_t>( modelCount ) );
    const bool queuedBodies = ReplayPresentationApplyBodyRenderPoses(
        renderInstances,
        bodyStore,
        colliderStore,
        std::span<const RunReplayPredictionBodySample>( frame.bodies.data(), frame.bodies.size() ),
        matchedBodies,
        modelCount );
    const bool queuedHidden = ReplayPresentationHideUnmatchedRenderBodies( renderInstances,
                                                                           bodyStore,
                                                                           colliderStore,
                                                                           matchedBodies,
                                                                           modelCount );
    return queuedBodies || queuedHidden;
}


bool ReplayPresentation::BuildPredictionGhostDrawRequests(
    const ReplayPredictionPresentationView& prediction,
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
    const Physics::PhysicsBodyStore& bodyStore )
{
    ClearPredictionGhostDrawRequests();
    const std::span<const RunReplayPredictionFrame> frames = prediction.frames;
    const bool drawLivePrediction = prediction.enabled && prediction.ragdollVisualsEnabled && frames.size() >= 2;
    const bool drawBaseline = prediction.baselineValid && prediction.baselineComparisonActive &&
                              prediction.ragdollVisualsEnabled && !prediction.baselineBodyPoses.empty();

    bool hasRagdollPart = false;
    for ( int i = 0; i < static_cast<int>( presentationRecords.size() ); ++i )
    {
        if ( ReplayPresentationModelIsRagdollPart( presentationRecords, i ) )
        {
            hasRagdollPart = true;
            break;
        }
    }
    if ( !hasRagdollPart )
    {
        return false;
    }

    const std::size_t liveRequestCapacity =
        drawLivePrediction
            ? (std::min)( frames.size(), REPLAY_PREDICTION_GHOST_MAX_FRAMES + 1 ) * presentationRecords.size()
            : 0u;
    const std::size_t baselineRequestCapacity = drawBaseline ? prediction.baselineBodyPoses.size() : 0u;
    if ( !CanAppendPredictionGhostDrawRequests( liveRequestCapacity + baselineRequestCapacity ) )
    {
        return false;
    }

    if ( drawBaseline )
    {
        for ( const ReplayPredictionBaselineBodyPose& pose : prediction.baselineBodyPoses )
        {
            if ( !pose.hasRestPose || pose.modelRow.value < 0 ||
                 pose.modelRow.value >= static_cast<int>( presentationRecords.size() ) ||
                 !ReplayPresentationModelIsRagdollPart( presentationRecords, pose.modelRow.value ) )
            {
                continue;
            }

            ReplayPredictionGhostDrawRequest request;
            request.modelRow.value = pose.modelRow.value;
            request.position = pose.restPosition;
            request.orientation = pose.restOrientation;
            request.orientation.Normalise();
            request.alpha = 0.075f;
            request.tintR = 0.28f;
            request.tintG = 0.76f;
            request.tintB = 1.0f;
            request.tintStrength = 0.82f;
            AppendPredictionGhostDrawRequest( request );
        }
    }

    if ( !drawLivePrediction )
    {
        return HasPredictionGhostDrawRequests();
    }

    const std::size_t lastIndex = frames.size() - 1;
    const std::size_t stride =
        (std::max)( static_cast<std::size_t>( 1 ),
                    ( lastIndex + REPLAY_PREDICTION_GHOST_MAX_FRAMES - 1 ) / REPLAY_PREDICTION_GHOST_MAX_FRAMES );
    const ReplayFrameIndex lastFrame = frames.back().frameIndex;

    auto appendGhostFrame = [&]( std::size_t index )
    {
        const RunReplayPredictionFrame& predictionFrame = frames[index];
        if ( predictionFrame.frameIndex == 0 )
        {
            return;
        }

        const float t =
            lastFrame > 0
                ? std::clamp( static_cast<float>( predictionFrame.frameIndex ) / static_cast<float>( lastFrame ),
                              0.0f,
                              1.0f )
                : 1.0f;
        const float alpha = std::clamp( 0.055f + ( 1.0f - t ) * 0.105f, 0.045f, 0.18f );

        for ( const RunReplayPredictionBodySample& body : predictionFrame.bodies )
        {
            int resolvedModelIndex = -1;
            if ( !ReplayPresentationResolveReplayBody( bodyStore,
                                                       body.id,
                                                       body.modelRow.value,
                                                       static_cast<int>( presentationRecords.size() ),
                                                       resolvedModelIndex ) )
            {
                continue;
            }

            if ( !ReplayPresentationModelIsRagdollPart( presentationRecords, resolvedModelIndex ) )
            {
                continue;
            }

            ReplayPredictionGhostDrawRequest request;
            request.modelRow.value = resolvedModelIndex;
            request.position = body.position;
            request.orientation = body.orientation;
            request.orientation.Normalise();
            request.alpha = alpha;
            AppendPredictionGhostDrawRequest( request );
        }
    };

    std::size_t farIndex = lastIndex;
    if ( farIndex % stride != 0 )
    {
        appendGhostFrame( farIndex );
        farIndex = ( farIndex / stride ) * stride;
    }
    for ( std::size_t index = farIndex; index >= stride; index -= stride )
    {
        appendGhostFrame( index );
        if ( index == stride )
        {
            break;
        }
    }
    return HasPredictionGhostDrawRequests();
}


bool ReplayPresentation::PrepareRenderPoseBodyMatch( int modelCount ) noexcept
{
    if ( modelCount < 0 || modelCount > SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        return false;
    }
    std::fill( m_renderPoseBodyMatched.begin(),
               m_renderPoseBodyMatched.begin() + static_cast<std::size_t>( modelCount ),
               uint8_t{ 0 } );
    return true;
}


void ReplayPresentation::ClearLauncherVisualBackup()
{
    m_launcherVisualBackup = ReplayLauncherVisualSample{};
    m_launcherVisualBackupActive = false;
}


void ReplayPresentation::ResetTrajectoryVisualStats() noexcept
{
    m_trajectoryVisualStats = {};
}


void ReplayPresentation::RecordTrajectoryFrameStats(
    const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& frameStats )
{
    for ( std::size_t index = 0; index < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT; ++index )
    {
        m_trajectoryVisualStats.emittedSegments[index] += frameStats.emittedSegments[index];
        m_trajectoryVisualStats.droppedSegments[index] += frameStats.droppedSegments[index];
    }
}


void ReplayPresentation::PublishVisualPacket( ReplayVisualPacket packet )
{
    // Lifetime: spans point into the tracer's fixed reserves and remain valid
    // until the next frame clears that tracer. No packet survives frame order.
    m_publishedVisualPacket = packet;
}


void ReplayPresentation::PublishVisualPacket( ReplayVisualPacket packet,
                                              const ReplayPredictionPresentationView& prediction,
                                              const ReplaySolverFrameSample* latestSolver,
                                              uint64_t replayReserveGrowthEvents )
{
    packet.header.sourceFrame = prediction.sourceFrame;
    packet.header.revealFrame = prediction.revealFrame;
    packet.header.targetId = m_pathVisualizer.targetId;
    packet.header.branchId = latestSolver ? latestSolver->branch.branchId : 0u;
    packet.header.eventCursor = latestSolver ? latestSolver->eventCursor : 0u;
    packet.header.topologyVersion = prediction.topologyVersion;
    packet.header.publishedFrameCount = static_cast<uint32_t>( prediction.frames.size() );
    packet.header.futureNodeCount = static_cast<uint32_t>( prediction.futureNodes.size() );
    const std::span<const ReplayPredictionGhostDrawRequest> ghostRequests = PredictionGhostDrawRequestsView();
    packet.header.ghostRequestCount = static_cast<uint32_t>( ghostRequests.size() );
    packet.header.replayReserveGrowthEvents = replayReserveGrowthEvents;
    packet.header.predictionEnabled = prediction.enabled;
    packet.header.predictionBuilding = prediction.building;
    packet.header.predictionComplete = prediction.complete;
    packet.trajectoryRecords = prediction.trajectoryRecords;
    packet.futureNodes = prediction.futureNodes;
    packet.retainedMarkers = prediction.retainedMarkers;
    packet.ghostRequests = ghostRequests;
    packet.trajectoryDiagnostics = TrajectoryVisualStatsSnapshot();
    PublishVisualPacket( packet );
}


void ReplayPresentation::RecordTrajectorySubmissionFrame(
    const SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats& submissionStats,
    int frameNumber,
    uint64_t reserveGrowthEventCount )
{
    if ( !submissionStats.hasGeometry || submissionStats.vertexBytes == 0 || submissionStats.vertexCount == 0 )
    {
        return;
    }

    ++m_trajectorySubmissionProbe.observedFrameCount;
    m_trajectorySubmissionProbe.hasSubmission = true;
    m_trajectorySubmissionProbe.stableWindowTargetFrameCount = REPLAY_TRAJECTORY_SUBMISSION_STEADY_FRAME_TARGET;
    const bool sameSubmittedBytes = m_trajectorySubmissionProbe.stableFrameCount > 0 &&
                                    m_trajectorySubmissionProbe.stableHash == submissionStats.vertexHash &&
                                    m_trajectorySubmissionProbe.vertexBytes == submissionStats.vertexBytes &&
                                    m_trajectorySubmissionProbe.vertexCount == submissionStats.vertexCount &&
                                    m_trajectorySubmissionProbe.segmentCount == submissionStats.segmentCount;
    const bool sameReserveWindow = reserveGrowthEventCount == m_trajectorySubmissionProbe.reserveGrowthEventsAtEnd;

    if ( !sameSubmittedBytes || !sameReserveWindow )
    {
        // Invariant: reveal growth and prediction build reserves are allowed
        // before the steady window. Evidence begins only once bytes and reserve
        // counters hold steady.
        m_trajectorySubmissionProbe.stableFrameCount = 1;
        m_trajectorySubmissionProbe.firstFrame = frameNumber;
        m_trajectorySubmissionProbe.stableHash = submissionStats.vertexHash;
        m_trajectorySubmissionProbe.vertexBytes = submissionStats.vertexBytes;
        m_trajectorySubmissionProbe.vertexCount = submissionStats.vertexCount;
        m_trajectorySubmissionProbe.segmentCount = submissionStats.segmentCount;
        m_trajectorySubmissionProbe.reserveGrowthEventsAtStart = reserveGrowthEventCount;
    }
    else
    {
        ++m_trajectorySubmissionProbe.stableFrameCount;
    }

    m_trajectorySubmissionProbe.lastFrame = frameNumber;
    m_trajectorySubmissionProbe.reserveGrowthEventsAtEnd = reserveGrowthEventCount;
    m_trajectorySubmissionProbe.noReserveGrowth =
        m_trajectorySubmissionProbe.reserveGrowthEventsAtStart == m_trajectorySubmissionProbe.reserveGrowthEventsAtEnd;
    m_trajectorySubmissionProbe.stableWindowReady =
        m_trajectorySubmissionProbe.stableFrameCount >= REPLAY_TRAJECTORY_SUBMISSION_STEADY_FRAME_TARGET &&
        m_trajectorySubmissionProbe.noReserveGrowth;
}


void ReplayPresentation::RecordTrajectoryBudgetExpiry( SkullbonezCore::Core::MainMemoryReplayBudgetPass pass )
{
    const std::size_t passIndex = static_cast<std::size_t>( pass );
    if ( passIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_BUDGET_PASS_COUNT )
    {
        ++m_trajectoryVisualStats.budgetExpiries[passIndex];
    }
}


void ReplayPresentation::RecordTrajectoryRebuildCause( SkullbonezCore::Core::MainMemoryReplayRebuildCause cause )
{
    const std::size_t causeIndex = static_cast<std::size_t>( cause );
    if ( causeIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_REBUILD_CAUSE_COUNT )
    {
        ++m_trajectoryVisualStats.rebuildCauses[causeIndex];
    }
}

} // namespace SkullbonezCore::Runtime
