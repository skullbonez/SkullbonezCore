/*
File: SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp
Purpose:
  Initializes and owns replay presentation storage independently of ReplayRuntime.

Summary:
  Recorded timelines publish values; this lower owner keeps mutable camera,
  path-selection, launcher-backup, and recorded-pose state. Prediction visual
  storage remains in ReplayPredictionPresentation. Temporary cause focus also
  keeps the selected evidence identity so a same-frame prediction replacement
  cannot silently retarget the camera to a different solver row.

Glossary:
  Presentation storage: Replay-only path and launcher buffers used while
    selecting and rendering recorded state.

Invariants:
  - Path selection reserves before steady replay interaction.
  - The render-pose match table is fixed at the scene model capacity.
  - Exact predicted cause focus rematches every evidence stamp before reusing a
    row after cause-tree rebuild.

Related:
  - ReplayPresentation.h
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
*/
#include "ReplayPresentation.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <algorithm>
#include <cstring>

namespace SkullbonezCore::Runtime
{
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

void ReplayPresentation::ReserveLauncherVisualBackupBuffers()
{
    m_launcherVisualBackup.rayLines.reserve( REPLAY_LAUNCHER_RAY_LINE_CAPACITY );
    m_launcherVisualBackup.laserShots.reserve( REPLAY_LAUNCHER_LASER_SHOT_CAPACITY );
}

void ReplayPresentation::StoreLauncherVisualBackup( const ReplayLauncherVisualSample& sample )
{
    m_launcherVisualBackup = sample;
    m_launcherVisualBackupActive = true;
}

const ReplayLauncherVisualSample& ReplayPresentation::LauncherVisualBackup() const noexcept
{
    return m_launcherVisualBackup;
}

namespace
{
template <typename T> uint64_t ReplayPresentationVectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() * sizeof( T ) );
}

const Physics::PhysicsBodyRecord* ReplayPresentationResolveReplayBody( const Physics::PhysicsBodyStore& bodyStore,
                                                                       Physics::PhysicsSceneObjectId id, int modelIndexHint,
                                                                       int modelCount, int& outModelIndex )
{
    outModelIndex = -1;

    if ( id.value == 0 )
    {
        return nullptr;
    }

    // Invariant: replay artifacts carry model indices only as staleable hints.
    // Stable identity is the scene object id resolved through the live body handle map.
    const Physics::PhysicsBodyHandle body = bodyStore.HandleForSceneObjectId( id, modelIndexHint );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );

    if ( !record || record->sceneObjectId != id || modelIndex < 0 || modelIndex >= modelCount )
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

    if ( !record || bodyStore.ModelIndexForHandle( body ) != modelIndex || !record->sceneObjectId.IsValid() )
    {
        return nullptr;
    }

    return record;
}

bool ReplayPresentationQueueRenderPoseOverride( Rendering::RenderInstanceStore& renderInstances,
                                                const Physics::PhysicsBodyStore& bodyStore,
                                                const Physics::ColliderStore& colliderStore,
                                                Physics::PhysicsSceneObjectId sceneObjectId,
                                                const Math::Vector::Vector3& position,
                                                const Math::Orientation::Quaternion& orientation )
{
    const Physics::PhysicsBodyHandle body = bodyStore.HandleForSceneObjectId( sceneObjectId );
    const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );

    if ( !record || record->sceneObjectId != sceneObjectId || modelIndex < 0 )
    {
        return false;
    }

    return renderInstances.OverridePose( modelIndex, sceneObjectId, position, orientation, colliderStore );
}

int ReplayPresentationRenderPoseModelHint( const ReplayBodyPresentationSample& ) noexcept
{
    return -1;
}

int ReplayPresentationRenderPoseModelHint( const ReplaySolverBodySample& ) noexcept
{
    return -1;
}

Math::Orientation::Quaternion ReplayPresentationRenderPoseOrientation( const ReplayBodyPresentationSample& body )
{
    return Math::Orientation::Quaternion( body.orientation[0], body.orientation[1], body.orientation[2],
                                          body.orientation[3] );
}

Math::Orientation::Quaternion ReplayPresentationRenderPoseOrientation( const ReplaySolverBodySample& body )
{
    return Math::Orientation::Quaternion( body.orientation[0], body.orientation[1], body.orientation[2],
                                          body.orientation[3] );
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
                                             const Physics::ColliderStore& colliderStore, std::span<const BodySample> bodies,
                                             std::span<uint8_t> matchedBodies, int modelCount )
{
    bool queuedAny = false;

    for ( const BodySample& body : bodies )
    {
        int resolvedModelIndex = -1;

        if ( !ReplayPresentationResolveReplayBody( bodyStore, body.id, ReplayPresentationRenderPoseModelHint( body ),
                                                   modelCount, resolvedModelIndex ) )
        {
            continue;
        }

        Math::Orientation::Quaternion orientation = ReplayPresentationRenderPoseOrientation( body );
        orientation.Normalise();

        if ( ReplayPresentationQueueRenderPoseOverride( renderInstances, bodyStore, colliderStore, body.id, body.position,
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
                                                  std::span<const uint8_t> matchedBodies, int modelCount )
{
    bool queuedAny = false;
    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );

    for ( int modelIndex = 0; modelIndex < modelCount; ++modelIndex )
    {
        if ( matchedBodies[static_cast<std::size_t>( modelIndex )] != 0 )
        {
            continue;
        }

        const Physics::PhysicsBodyRecord* bodyRecord = ReplayPresentationBodyRecordForModelIndex( bodyStore, modelIndex );

        if ( !bodyRecord )
        {
            continue;
        }

        // Why: loaded artifacts may not contain every live body. Move unmatched
        // bodies out of view instead of letting unrelated live geometry appear
        // inside the scrubbed replay frame.
        const Physics::PhysicsSceneObjectId sceneObjectId { bodyRecord->sceneObjectId };

        if ( ReplayPresentationQueueRenderPoseOverride( renderInstances, bodyStore, colliderStore, sceneObjectId,
                                                        hiddenReplayPosition, Math::Orientation::IDENTITY_QUATERNION ) )
        {
            queuedAny = true;
        }
    }

    return queuedAny;
}

RunReplayPathTarget* FindReplayQueryPathTarget( RunReplayPathVisualizerState& visualizer, Physics::PhysicsSceneObjectId id )
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

void ApplyReplayQueryPrimaryPathTarget( RunReplayPathVisualizerState& visualizer, Physics::PhysicsSceneObjectId id,
                                        int modelIndex, const char* name )
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

ReplayPresentation::ReplayPresentation( Core::Profiler* )
{
    // Runtime allocation policy: path target selection is a live replay UI
    // action, so it rotates entries within a fixed pre-gameplay vector budget.
    m_pathVisualizer.targets.reserve( REPLAY_PATH_MAX_ROOT_TARGETS );
    m_renderPoseBodyMatched.fill( uint8_t { 0 } );
}


RunReplayCameraState ReplayPresentation::CameraView() const noexcept
{
    return m_camera;
}


ReplayPresentationMemoryStats ReplayPresentation::CollectMemoryStats() const noexcept
{
    ReplayPresentationMemoryStats stats;
    stats.pathOwnerBytes = static_cast<uint64_t>( sizeof( m_pathVisualizer ) );
    stats.pathTargetCapacityBytes = ReplayPresentationVectorCapacityBytes( m_pathVisualizer.targets );
    stats.launcherVisualBytes = static_cast<uint64_t>( sizeof( m_launcherVisualBackup ) ) +
                                ReplayPresentationVectorCapacityBytes( m_launcherVisualBackup.rayLines ) +
                                ReplayPresentationVectorCapacityBytes( m_launcherVisualBackup.laserShots );

    return stats;
}


bool ReplayPresentation::HasLauncherVisualBackup() const noexcept
{
    return m_launcherVisualBackupActive;
}


void ReplayPresentation::BeginCameraInspection( RunCameraMode restoreMode, uint32_t restoreCameraHash,
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


void ReplayPresentation::ApplyCameraFocus( const RunReplayCauseTreeRow& row, int rowIndex,
                                           RunReplayCameraFocusKind focusKind, const Math::Vector::Vector3& resolvedPoint,
                                           const Math::Vector::Vector3& resolvedNormal, float resolvedRadius ) noexcept
{
    m_camera.focusKind = focusKind;
    m_camera.focusedId = row.id;
    m_camera.counterpartId = row.counterpartId;
    m_camera.focusedRow = rowIndex;
    m_camera.focusRowKind = row.kind;
    m_camera.focusModelRow = row.modelRow;
    m_camera.focusCounterpartModelRow = row.counterpartModelRow;
    m_camera.focusContactIndex = row.contactIndex;
    m_camera.focusSolverRowIndex = row.solverRowIndex;
    m_camera.focusFeatureId = row.featureId;
    m_camera.focusSourceGeneration = row.sourceGeneration;
    m_camera.focusSourceBankEpoch = row.sourceBankEpoch;
    m_camera.focusSourceTopologyVersion = row.sourceTopologyVersion;
    m_camera.focusSourcePublicationVersion = row.sourcePublicationVersion;
    m_camera.focusSourceHighDetail = row.sourceHighDetail;
    m_camera.focusTerrain = row.terrain;
    m_camera.targetPoint = resolvedPoint;
    m_camera.targetNormal = resolvedNormal;
    m_camera.impulseVector = row.impulse;
    m_camera.targetRadius = resolvedRadius;
}


void ReplayPresentation::SetCameraFocusedRow( int row ) noexcept
{
    m_camera.focusedRow = row;
}


bool ReplayPresentation::ClearCameraFocus() noexcept
{
    const bool ownedPause = m_camera.ownsSimulationPause;
    m_camera.focusKind = RunReplayCameraFocusKind::None;
    m_camera.focusedId = Physics::PhysicsSceneObjectId {};

    m_camera.counterpartId = Physics::PhysicsSceneObjectId {};

    m_camera.focusedRow = -1;
    m_camera.focusRowKind = RunReplayCauseTreeRowKind::Body;
    m_camera.focusModelRow.value = -1;
    m_camera.focusCounterpartModelRow.value = -1;
    m_camera.focusContactIndex = -1;
    m_camera.focusSolverRowIndex = -1;
    m_camera.focusFeatureId = 0;
    m_camera.focusSourceGeneration = 0;
    m_camera.focusSourceBankEpoch = 0;
    m_camera.focusSourceTopologyVersion = 0;
    m_camera.focusSourcePublicationVersion = 0;
    m_camera.focusSourceHighDetail = false;
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
    m_pathVisualizer.targetId = Physics::PhysicsSceneObjectId {};
    m_pathVisualizer.targetModelRow.value = -1;
    m_pathVisualizer.targetName[0] = '\0';
    m_pathVisualizer.targets.clear();
    m_pathVisualizer.pastTrajectory = RunReplayPastTrajectoryBuildState {};
}


void ReplayPresentation::PreparePathDrawing( const Physics::PhysicsBodyStore& bodyStore )
{
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
        const Physics::PhysicsBodyHandle handle = bodyStore.HandleForSceneObjectId( target.id, target.modelRow.value );
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
    m_pathVisualizer.targets.clear();
    m_pathVisualizer.pastTrajectory = RunReplayPastTrajectoryBuildState {};
}


void ReplayPresentation::ApplyPastTrajectoryUpdate( Physics::PhysicsSceneObjectId targetId, ReplayFrameIndex firstFrame,
                                                    ReplayFrameIndex builtThroughFrame, uint64_t totalFramesEvicted,
                                                    uint64_t fullRebuildCount, uint64_t incrementalTrimCount, bool valid,
                                                    Physics::ModelRowHint targetModelRow, bool targetModelRowRepaired )
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
}


ReplayPathColorMode ReplayPresentation::CyclePathColorMode() noexcept
{
    constexpr uint8_t modeCount = static_cast<uint8_t>( ReplayPathColorMode::CausalDepth ) + 1u;
    const uint8_t next = ( static_cast<uint8_t>( m_pathVisualizer.colorMode ) + 1u ) % modeCount;
    m_pathVisualizer.colorMode = static_cast<ReplayPathColorMode>( next );
    return m_pathVisualizer.colorMode;
}

void ReplayPresentation::SetPathColorMode( ReplayPathColorMode mode ) noexcept
{
    m_pathVisualizer.colorMode = mode;
}


bool ReplayPresentation::SetPathTarget( Physics::PhysicsSceneObjectId id, Physics::ModelRowHint modelRow, const char* name )
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

    return true;
}


ReplayPathPickResult ReplayPresentation::ApplyPathPickResult( const ReplayPathPickResult& resolved )
{
    ReplayPathPickResult result = resolved;

    if ( !resolved.picked )
    {
        if ( resolved.exitInspectionCamera )
        {
            ClearPathState();
        }

        return result;
    }

    const Physics::PhysicsSceneObjectId pickedId = resolved.targetId;
    const int pickedIndex = resolved.targetModelRow.value;
    const char* pickedName = resolved.targetName;

    if ( pickedId.value != 0 && pickedIndex >= 0 )
    {
        if ( !resolved.additive )
        {
            m_pathVisualizer.targets.clear();
        }

        RunReplayPathTarget* target = FindReplayQueryPathTarget( m_pathVisualizer, pickedId );

        if ( !target )
        {
            // Runtime allocation policy: constructor-reserved target storage
            // rotates entries; live picking never grows the vector.
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
        result.picked = true;
        return result;
    }

    result.picked = false;

    if ( result.exitInspectionCamera )
    {
        ClearPathState();
        result.exitInspectionCamera = true;
    }

    return result;
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
    const bool
        queuedBodies = ReplayPresentationApplyBodyRenderPoses( renderInstances, bodyStore, colliderStore,
                                                               std::span<const ReplayBodyPresentationSample>( sample.bodies
                                                                                                                  .data(),
                                                                                                              sample.bodies
                                                                                                                  .size() ),
                                                               matchedBodies, modelCount );

    const bool queuedHidden = ReplayPresentationHideUnmatchedRenderBodies( renderInstances, bodyStore, colliderStore,
                                                                           matchedBodies, modelCount );

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
    const bool queuedBodies = ReplayPresentationApplyBodyRenderPoses( renderInstances, bodyStore, colliderStore,
                                                                      std::span<const ReplaySolverBodySample>( sample.bodies
                                                                                                                   .data(),
                                                                                                               sample.bodies
                                                                                                                   .size() ),
                                                                      matchedBodies, modelCount );

    const bool queuedHidden = ReplayPresentationHideUnmatchedRenderBodies( renderInstances, bodyStore, colliderStore,
                                                                           matchedBodies, modelCount );

    return queuedBodies || queuedHidden;
}


bool ReplayPresentation::PrepareRenderPoseBodyMatch( int modelCount ) noexcept
{
    if ( modelCount < 0 || modelCount > SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        return false;
    }

    std::fill( m_renderPoseBodyMatched.begin(), m_renderPoseBodyMatched.begin() + static_cast<std::size_t>( modelCount ),
               uint8_t { 0 } );

    return true;
}


void ReplayPresentation::ClearLauncherVisualBackup() noexcept
{
    // Invariant: historical rendering borrows this scratch every frame. Clear
    // semantic values while retaining startup capacity so entering inspection
    // cannot allocate twice per presented solver sample.
    m_launcherVisualBackup.rayLines.clear();
    m_launcherVisualBackup.laserShots.clear();
    m_launcherVisualBackup.nextRayLine = 0;
    m_launcherVisualBackup.nextLaserShot = 0;
    m_launcherVisualBackup.fireMode = ReplayLauncherFireMode::Laser;
    m_launcherVisualBackup.visualizeRays = false;
    m_launcherVisualBackup.impulseStrength = 0.0f;
    m_launcherVisualBackup.projectileSpeed = 0.0f;
    m_launcherVisualBackupActive = false;
}


} // namespace SkullbonezCore::Runtime
