/*
File: SkullbonezSource/Runtime/Replay/RunReplayQueryTools.cpp
Purpose:
  Contains replay path-target picking and query helpers.

Summary:
  Replay path queries translate a mouse pick into a stable ReplayBodyId target.
  The visualizer and prediction layers can then follow retained or future solver
  contacts without depending on transient model indices alone.

Glossary:
  ReplayBodyId: Stable body id retained across replay samples.
  Path target: Body selected for retained/future trajectory visualization.

Invariants:
  - Picking must prefer stable replay ids and only use model indices as hints.
  - Changing path targets invalidates prediction caches.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/RuntimePickService.h
*/
#include "ReplayRuntime.h"
#include "../Scene/SceneEntityStore.h"
#include "../RuntimePickService.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;

namespace
{
float ReplayQueryColliderRadiusForModelIndex( const ColliderStore& colliderStore, int modelIndex )
{
    // Why: retained replay rows may only carry a model-index sample. This helper
    // is a display-radius fallback; live target markers resolve collider rows
    // through PhysicsBodyHandle before drawing authored shapes.
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForModelIndex( modelIndex );
    const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
    if ( !collider || colliderStore.ModelIndexForHandle( colliderHandle ) != modelIndex )
    {
        return 1.0f;
    }

    return (std::max)( collider->boundingRadius > 0.0f ? collider->boundingRadius
                                                       : GetShapeBoundingRadius( collider->shape ),
                       1.0f );
}


ReplayBodyId ReplayQueryBodyIdForModelIndex( const PhysicsBodyStore& bodyStore, int modelIndex )
{
    ReplayBodyId id;
    if ( const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex ) )
    {
        id.value = body->replayBodyId;
    }
    return id;
}


bool ReplayQueryIntersectRaySphere( const Vector3& rayOrigin,
                                    const Vector3& rayDirection,
                                    const Vector3& center,
                                    float radius,
                                    float& outT )
{
    const Vector3 m = rayOrigin - center;
    const float b = m * rayDirection;
    const float c = ( m * m ) - radius * radius;
    if ( c > 0.0f && b > 0.0f )
    {
        return false;
    }

    const float discriminant = b * b - c;
    if ( discriminant < 0.0f )
    {
        return false;
    }

    outT = -b - sqrtf( discriminant );
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


ReplayRuntime::PathPickResult
ReplayRuntime::TryPickPathTarget( const PathPickInput& input,
                                  const SceneEntityStore& entities,
                                  const PhysicsBodyStore& bodyStore,
                                  const ColliderStore& colliderStore,
                                  const std::vector<Rendering::RenderInstancePresentationRecord>& presentationRecords )
{
    PathPickResult pickResult;
    // Concept: A path pick converts volatile mouse/model hits into stable
    // ReplayBodyId targets before prediction and retained-path caches observe it.
    if ( !input.hasWorldRay )
    {
        if ( input.clearOnMiss )
        {
            ClearCameraFocusForRestore();
            ClearPathVisualizerState();
            pickResult.exitInspectionCamera = true;
        }
        return pickResult;
    }

    const int modelCount = bodyStore.Count() < colliderStore.Count() ? bodyStore.Count() : colliderStore.Count();
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
    if ( const ReplaySolverFrameSample* sample = CurrentSolverScrubSample() )
    {
        float bestT = FLT_MAX;
        for ( const ReplaySolverBodySample& body : sample->bodies )
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
                pickedName[0] = '\0';
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

        RuntimePickResult result;
        if ( RuntimePickService::TryPickModel( request, result ) )
        {
            pickedIndex = result.modelRow.value;
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
        RunReplayPathVisualizerState& visualizer = PathVisualizer();
        if ( !input.additive )
        {
            visualizer.targets.clear();
        }

        RunReplayPathTarget* target = FindReplayQueryPathTarget( visualizer, pickedId );
        if ( !target )
        {
            // Invariant: path target storage is pre-reserved by ReplayRuntime.
            // Mouse picking may rotate entries inside that fixed capacity, but
            // it must not request replay growth while the scene is live.
            if ( visualizer.targets.capacity() < REPLAY_PATH_MAX_ROOT_TARGETS )
            {
                return pickResult;
            }
            if ( visualizer.targets.size() >= REPLAY_PATH_MAX_ROOT_TARGETS )
            {
                visualizer.targets.erase( visualizer.targets.begin() );
            }
            if ( visualizer.targets.size() >= visualizer.targets.capacity() )
            {
                return pickResult;
            }
            RunReplayPathTarget nextTarget;
            nextTarget.id = pickedId;
            visualizer.targets.push_back( nextTarget );
            target = &visualizer.targets.back();
        }

        target->modelRow.value = pickedIndex;
        target->name[0] = '\0';
        if ( pickedName[0] != '\0' )
        {
            strncpy_s( target->name, sizeof( target->name ), pickedName, _TRUNCATE );
        }
        ApplyReplayQueryPrimaryPathTarget( visualizer, pickedId, pickedIndex, target->name );
        visualizer.futureNodes.clear();
        ClearPredictionCache();
        MarkPredictionDirty();
        pickResult.picked = true;
        return pickResult;
    }

    if ( input.clearOnMiss )
    {
        ClearCameraFocusForRestore();
        ClearPathVisualizerState();
        pickResult.exitInspectionCamera = true;
    }
    return pickResult;
}


bool ReplayRuntime::RouteWorldPointer( const WorldPointerInput& input )
{
    if ( !input.leftPressed || input.suppressWorldAction || input.editorMode || input.uiWantsNativeCursor ||
         ( !input.controlDown && input.launcherMode ) )
    {
        return false;
    }

    const PathPickResult pickResult =
        TryPickPathTarget( input.pick, input.entities, input.bodyStore, input.colliderStore, input.presentation );
    if ( pickResult.exitInspectionCamera )
    {
        ExitInspectionCamera( input.cameras,
                              input.terrain,
                              input.camera,
                              input.restoreCameraMode,
                              input.attachedCameraFollow,
                              input.directorGrabbed,
                              input.interaction,
                              input.inputRouter );
    }
    return true;
}
