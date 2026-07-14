/*
File: SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp
Purpose:
  Initializes and owns replay presentation storage independently of ReplayRuntime.

Mental model:
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
  - RunReplayTools.cpp
*/
#include "ReplayPresentation.h"
#include "../RuntimePickService.h"
#include "../Scene/SceneEntityStore.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr int REPLAY_TRAJECTORY_SUBMISSION_STEADY_FRAME_TARGET = 120;

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

ReplayPresentation::ReplayPresentation()
{
    // Runtime allocation policy: path target selection is a live replay UI
    // action, so it rotates entries within a fixed pre-gameplay vector budget.
    m_pathVisualizer.targets.reserve( REPLAY_PATH_MAX_ROOT_TARGETS );
    // Runtime allocation policy: focus masks are rewritten during replay render
    // passes, so the byte vector owns its full model-capacity storage up front.
    m_focusModelMask.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_renderPoseBodyMatched.fill( uint8_t{ 0 } );
}


void ReplayPresentation::ReserveRecordingBuffers()
{
    // Runtime allocation policy: ghost requests are replay-only overlay data.
    // Reserve during startup replay configuration, before steady interaction.
    m_predictionGhostDrawRequests.reserve( REPLAY_PREDICTION_GHOST_REQUEST_CAPACITY );
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

    m_pathVisualizer.hasTarget = true;
    m_pathVisualizer.targetId.value = body->replayBodyId;
    m_pathVisualizer.targetModelRow.value = modelIndex;
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
         modelCount > SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS )
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


void ReplayPresentation::StoreLauncherVisualBackup( const ReplayLauncherVisualSample& sample )
{
    m_launcherVisualBackup = sample;
    m_launcherVisualBackupActive = true;
}


void ReplayPresentation::ClearLauncherVisualBackup()
{
    m_launcherVisualBackup = ReplayLauncherVisualSample{};
    m_launcherVisualBackupActive = false;
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
