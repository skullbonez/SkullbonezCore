/*
File: SkullbonezSource/Runtime/Replay/ReplayPredictionTopologyPublication.cpp
Purpose:
  Publishes causal topology and retained prediction markers from completed frames.

Summary:
  This publication slice derives child relationships, reveal windows, and fixed
  marker values after frame rows become acquire-visible.

Glossary:
  Causal topology: Root-to-child relationships inferred from contacts and motion.

Invariants:
  - Topology never reads beyond the acquire-visible prediction prefix.
  - Published marker and topology versions change only on coherent replacement.

Related:
  - ReplayPredictionPublication.cpp
  - ReplayPredictionPublicationOperations.h
*/
#include "ReplayPredictionPublicationOperations.h"
#include "../Scene/SceneEntityStore.h"
#include "../Editor/EditorHullAssets.h"
#include "ReplayOverlayLayout.h"
#include "ReplayScrubber.h"
#include "../../Core/Config.h"
#include "../../Core/SceneCapacity.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations;
using namespace SkullbonezCore::Runtime::ReplayScrubberOperations;
namespace Gameplay = SkullbonezCore::Gameplay;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
namespace Physics = SkullbonezCore::Physics;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Math::Vector::Vector3;
namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations
{
namespace
{
constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;
constexpr float REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ = 8.0f * 8.0f;
constexpr float REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE = 0.05f;
constexpr float REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE_SQ = REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE *
                                                                 REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE;
constexpr double REPLAY_PREDICTION_REST_GRACE_SECONDS = 0.4;
constexpr ReplayFrameIndex REPLAY_PREDICTION_REST_GRACE_FRAMES = static_cast<ReplayFrameIndex>( REPLAY_PREDICTION_REST_GRACE_SECONDS / PHYSICS_FIXED_DT );
constexpr float REPLAY_PREDICTION_REST_POSITION_EPSILON_SQ = 0.5f * 0.5f;

// Concept: topology publication and drawing sample the same revealed prefix.
// The Prediction-owned stride operation keeps both consumers at one bounded
// density without giving either consumer ownership of the sampling policy.
struct ReplayPredictionDrawFrameWindow
{
    ReplayFrameIndex lastFrame = 0;
    ReplayFrameIndex revealFrame = 0;
    std::size_t sampleStride = 1;
};


ReplayPredictionDrawFrameWindow
PrepareReplayPredictionDrawFrameWindow( RunReplayPredictionState& prediction,
                                        const std::vector<RunReplayPredictionFrame>& frames,
                                        std::size_t frameCount )
{
    ReplayPredictionDrawFrameWindow window;
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount == 0 )
    {
        return window;
    }

    // Invariant: root, child, marker, affected-body, and ragdoll lanes all draw
    // against this one reveal clamp for the selected prediction prefix.
    window.lastFrame = frames[frameCount - 1].frameIndex;
    window.revealFrame = ReplayPredictionRevealFrameIndex( prediction, window.lastFrame );
    window.sampleStride = ReplayPredictionPathStrideForSampleCount( frameCount );
    return window;
}
} // namespace

const ColliderRecord* ReplayColliderRecordForModelIndex( const ColliderStore* colliderStore, int modelIndex );

// Concept: cold baseline drawing deliberately reuses the smooth replay ribbon
// path. It should read as the old future's ghost, never as jaggy debug wire.

// Concept: prediction future-node discovery can replace a motion-inferred child
// with a contact-derived child. These helpers keep that policy at the wrapper
// edge instead of duplicating the contact traversal.
template <typename NodeRange>
bool TryGetReplayFutureDepthInNodes( const NodeRange& nodes,
                                     Physics::PhysicsSceneObjectId rootId,
                                     ReplayFrameIndex rootFrame,
                                     bool requireRootFrame,
                                     Physics::PhysicsSceneObjectId id,
                                     ReplayFrameIndex frame,
                                     int& outDepth )
{
    if ( id.value == 0 )
    {
        return false;
    }

    if ( id.value == rootId.value )
    {
        outDepth = 0;
        return !requireRootFrame || frame >= rootFrame;
    }

    for ( const RunReplayPathTraceNode& node : nodes )
    {
        if ( node.id.value == id.value && frame >= node.firstFrame )
        {
            outDepth = node.depth;
            return true;
        }
    }

    return false;
}

template <typename NodeRange>
RunReplayPathTraceNode* FindReplayFutureNodeInNodes( NodeRange& nodes, Physics::PhysicsSceneObjectId id )
{
    for ( RunReplayPathTraceNode& node : nodes )
    {
        if ( node.id.value == id.value )
        {
            return &node;
        }
    }

    return nullptr;
}

void AssignReplayFutureNode( RunReplayPathTraceNode& node,
                             Physics::PhysicsSceneObjectId parentId,
                             int parentModelIndex,
                             Physics::PhysicsSceneObjectId id,
                             int modelIndex,
                             ReplayFrameIndex firstFrame,
                             const Vector3& contactPoint,
                             const Vector3& contactNormal,
                             int depth,
                             bool contactDerived )
{
    node.id = id;
    node.parentId = parentId;
    node.modelRow.value = modelIndex;
    node.parentModelRow.value = parentModelIndex;
    node.firstFrame = firstFrame;
    node.contactPoint = contactPoint;
    node.contactNormal = contactNormal;
    node.depth = depth;
    node.contactDerived = contactDerived;
}

bool ReplayFutureNodeTopologyEquals( const RunReplayPathTraceNode& a, const RunReplayPathTraceNode& b )
{
    return a.id.value == b.id.value && a.parentId.value == b.parentId.value && a.modelRow.value == b.modelRow.value &&
           a.parentModelRow.value == b.parentModelRow.value && a.firstFrame == b.firstFrame && a.depth == b.depth &&
           a.contactDerived == b.contactDerived;
}


bool ReplayFutureNodeTopologyEquals( const std::vector<RunReplayPathTraceNode>& a,
                                     const std::vector<RunReplayPathTraceNode>& b )
{
    if ( a.size() != b.size() )
    {
        return false;
    }

    for ( std::size_t i = 0; i < a.size(); ++i )
    {
        if ( !ReplayFutureNodeTopologyEquals( a[i], b[i] ) )
        {
            return false;
        }
    }

    return true;
}


uint32_t AllocateReplayFutureNodeTopologyVersion( RunReplayPredictionFutureNodeCache& cache )
{
    uint32_t version = cache.nextFutureNodesTopologyVersion;
    ++cache.nextFutureNodesTopologyVersion;
    if ( cache.nextFutureNodesTopologyVersion == 0 )
    {
        cache.nextFutureNodesTopologyVersion = 1;
    }

    if ( version == 0 )
    {
        version = cache.nextFutureNodesTopologyVersion;
        ++cache.nextFutureNodesTopologyVersion;
    }

    return version;
}

template <typename ContactRange,
          typename BodyIdResolver,
          typename DepthResolver,
          typename NodeAdder,
          typename BudgetExpired>
bool BuildReplayFutureNodesFromContacts( const ContactRange& contacts,
                                         ReplayFrameIndex frameIndex,
                                         std::size_t startContactIndex,
                                         const SceneEntityStore* collection,
                                         bool includeRagdollVisuals,
                                         BodyIdResolver bodyIdForModelIndex,
                                         DepthResolver tryGetDepth,
                                         NodeAdder addNode,
                                         BudgetExpired budgetExpired,
                                         std::size_t& outNextContactIndex )
{
    outNextContactIndex = (std::min)( startContactIndex, contacts.size() );
    for ( std::size_t contactIndex = outNextContactIndex; contactIndex < contacts.size(); ++contactIndex )
    {
        // Invariant: callers that slice a frame on budget exhaustion must resume
        // from this contact index before advancing the frame cursor.
        if ( budgetExpired() )
        {
            return false;
        }

        const auto& contact = contacts[contactIndex];
        const bool ragdollA = collection && ReplayModelIndexIsRagdollPart( *collection, contact.bodyA );
        const bool ragdollB = collection && ReplayModelIndexIsRagdollPart( *collection, contact.bodyB );
        const int modelIndexA = collection ? ReplayRagdollTorsoModelIndexForPart( *collection, contact.bodyA )
                                           : contact.bodyA;

        const int modelIndexB = collection ? ReplayRagdollTorsoModelIndexForPart( *collection, contact.bodyB )
                                           : contact.bodyB;

        const Physics::PhysicsSceneObjectId idA = bodyIdForModelIndex( modelIndexA );
        const Physics::PhysicsSceneObjectId idB = bodyIdForModelIndex( modelIndexB );
        int depthA = -1;
        int depthB = -1;
        const bool activeA = tryGetDepth( idA, frameIndex, depthA );
        const bool activeB = tryGetDepth( idB, frameIndex, depthB );
        if ( activeA && !activeB && ( includeRagdollVisuals || !ragdollB ) )
        {
            addNode( idA, modelIndexA, idB, modelIndexB, frameIndex, contact.point, contact.normal, depthA + 1, true );
        }
        else if ( activeB && !activeA && ( includeRagdollVisuals || !ragdollA ) )
        {
            addNode( idB,
                     modelIndexB,
                     idA,
                     modelIndexA,
                     frameIndex,
                     contact.point,
                     contact.normal * -1.0f,
                     depthB + 1,
                     true );
        }

        outNextContactIndex = contactIndex + 1;
    }

    outNextContactIndex = 0;
    return true;
}

// Invariant: path thinning is anchored to solver frame indices, not visitor
// ordinal. Partial scans may resume at different offsets, but the same replay
// tick must always keep or drop the same visual segment.

struct ReplayPathChildDrawState
{
    RunReplayPathTraceNode node;
    bool active = false;
    // Concept: the two-box causal story. Entry is the body's IN-PLACE pose
    // from prediction frame 0 - the wall exactly as the live scene knows it.
    // It is drawn yellow the moment the body visibly moves and never slides.
    // lastMotionFrame times when the grey resting box may pop in.
    bool hasEntryPose = false;
    int entryModelIndex = -1;
    ReplayFrameIndex lastMotionFrame = 0;
    Vector3 entryPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion entryOrientation = IDENTITY_QUATERNION;
};

struct ReplayPathChildDrawContext
{
    std::array<ReplayPathChildDrawState, REPLAY_PATH_MAX_FUTURE_NODES> nodes = {};
    std::size_t nodeCount = 0;
};

// Why: downstream replay markers should show the collider's real authored
// shape, not the broadphase radius used for cheap collision culling.
const ColliderRecord* ReplayColliderRecordForModelIndex( const ColliderStore* colliderStore, int modelIndex )
{
    if ( !colliderStore )
    {
        return nullptr;
    }

    // Why: retained prediction markers store historical model-index samples, not
    // live body handles. Use this only for presentation fallback; store-edit
    // paths resolve through PhysicsBodyHandle before reading collider rows.
    const PhysicsColliderHandle colliderHandle = colliderStore->HandleForModelIndex( modelIndex );
    const ColliderRecord* collider = colliderStore->RecordForHandle( colliderHandle );
    if ( !collider || colliderStore->ModelIndexForHandle( colliderHandle ) != modelIndex )
    {
        return nullptr;
    }

    return collider;
}

ReplayPredictionRetainedMarker* FindOrAddReplayPredictionRetainedMarker( RunReplayPredictionState& prediction,
                                                                         Physics::PhysicsSceneObjectId id,
                                                                         int modelIndex )
{
    if ( id.value == 0 )
    {
        return nullptr;
    }

    for ( std::size_t i = 0; i < prediction.futureNodeCache.retainedMarkerCount; ++i )
    {
        ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[i];
        if ( marker.id.value == id.value )
        {
            if ( modelIndex >= 0 )
            {
                marker.modelRow.value = modelIndex;
            }

            return &marker;
        }
    }

    if ( prediction.futureNodeCache.retainedMarkerCount >= prediction.futureNodeCache.retainedMarkers.size() )
    {
        return nullptr;
    }

    ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache
                                                 .retainedMarkers[prediction.futureNodeCache.retainedMarkerCount++];

    marker = ReplayPredictionRetainedMarker {};

    marker.id = id;
    marker.modelRow.value = modelIndex;
    return &marker;
}

void RetainReplayPredictionEntryMarker( RunReplayPredictionState& prediction,
                                        Physics::PhysicsSceneObjectId id,
                                        int modelIndex,
                                        const Vector3& position,
                                        Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker = FindOrAddReplayPredictionRetainedMarker( prediction,
                                                                                           id,
                                                                                           modelIndex ) )
    {
        marker->hasEntryPose = true;
        marker->entryPosition = position;
        marker->entryOrientation = orientation;
        marker->entryOrientation.Normalise();
    }
}

void RetainReplayPredictionRestMarker( RunReplayPredictionState& prediction,
                                       Physics::PhysicsSceneObjectId id,
                                       int modelIndex,
                                       const Vector3& position,
                                       Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker = FindOrAddReplayPredictionRetainedMarker( prediction,
                                                                                           id,
                                                                                           modelIndex ) )
    {
        marker->hasRestPose = true;
        marker->hasHorizonPose = false;
        marker->restPosition = position;
        marker->restOrientation = orientation;
        marker->restOrientation.Normalise();
    }
}

void RetainReplayPredictionHorizonMarker( RunReplayPredictionState& prediction,
                                          Physics::PhysicsSceneObjectId id,
                                          int modelIndex,
                                          const Vector3& position,
                                          Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker = FindOrAddReplayPredictionRetainedMarker( prediction,
                                                                                           id,
                                                                                           modelIndex ) )
    {
        if ( marker->hasRestPose )
        {
            return;
        }

        marker->hasHorizonPose = true;
        marker->horizonPosition = position;
        marker->horizonOrientation = orientation;
        marker->horizonOrientation.Normalise();
    }
}


void RetainReplayPredictionEndStateMarkers( RunReplayPredictionState& prediction,
                                            ReplayFrameIndex revealFrame,
                                            const std::vector<RunReplayPredictionFrame>& completeFrames,
                                            std::size_t completeFrameCount )
{
    completeFrameCount = (std::min)( completeFrameCount, completeFrames.size() );
    if ( completeFrameCount < 2 || revealFrame < completeFrames[completeFrameCount - 1].frameIndex )
    {
        return;
    }

    // Why: late in the 200-brick wall prediction, ownership can move from the
    // affected-body fallback into the future-node tree faster than the budgeted
    // line scan can rediscover every brick. The stable end state is cheap to
    // prove from the final and grace frames. Resting bodies get grey boxes;
    // bodies still moving when the event horizon ends get a ghost endpoint.
    for ( std::size_t i = 0; i < prediction.futureNodeCache.retainedMarkerCount; ++i )
    {
        ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[i];
        if ( !marker.hasEntryPose || marker.hasRestPose || marker.hasHorizonPose )
        {
            continue;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( ReplayPredictionBodyRestingPose( completeFrames,
                                              completeFrameCount,
                                              marker.id,
                                              marker.modelRow.value,
                                              restPosition,
                                              restOrientation ) )
        {
            RetainReplayPredictionRestMarker( prediction,
                                              marker.id,
                                              marker.modelRow.value,
                                              restPosition,
                                              restOrientation );

            continue;
        }

        const RunReplayPredictionBodySample* finalBody = FindReplayPredictionBodyByIdWithHint(
            completeFrames[completeFrameCount - 1],
            marker.id,
            marker.modelRow.value );

        if ( !finalBody )
        {
            continue;
        }

        if ( VectorMagSquared( finalBody->position - marker.entryPosition ) <= REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ &&
             !ReplayPredictionBodyHasVisibleLinearMotion( *finalBody ) )
        {
            continue;
        }

        RetainReplayPredictionHorizonMarker( prediction,
                                             marker.id,
                                             finalBody->modelRow.value,
                                             finalBody->position,
                                             finalBody->orientation );
    }
}

// Concept: causal markers are the two-box story of each affected body.
//
// Yellow is fixed at the body's last still pose before it visibly moved - for
// a wall brick, its perfect-formation pose. Grey pops in ONLY at the body's
// final resting pose, and only when the completed prediction actually ends
// with it at rest; a body still moving at the horizon end gets a travel line
// and nothing else. Neither box ever slides.
void RetainReplayPredictionCausalMarkers( RunReplayPredictionState& prediction,
                                          ReplayPathChildDrawContext& context,
                                          ReplayFrameIndex revealFrame,
                                          const std::vector<RunReplayPredictionFrame>* completeFrames,
                                          std::size_t completeFrameCount )
{
    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        const ReplayPathChildDrawState& drawState = context.nodes[i];
        if ( drawState.hasEntryPose )
        {
            RetainReplayPredictionEntryMarker( prediction,
                                               drawState.node.id,
                                               drawState.entryModelIndex,
                                               drawState.entryPosition,
                                               drawState.entryOrientation );
        }

        // Why: completeFrames is null while the job is still building - a
        // growing prefix has no authoritative ending, so no grey box may
        // exist yet. The reveal timing check keeps the grey pop causal: it
        // appears only after the cursor has watched the body stop.
        if ( !drawState.active || !completeFrames )
        {
            continue;
        }

        if ( revealFrame < drawState.lastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
        {
            continue;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( !ReplayPredictionBodyRestingPose( *completeFrames,
                                               completeFrameCount,
                                               drawState.node.id,
                                               drawState.node.modelRow.value,
                                               restPosition,
                                               restOrientation ) )
        {
            continue;
        }

        RetainReplayPredictionRestMarker( prediction,
                                          drawState.node.id,
                                          drawState.node.modelRow.value,
                                          restPosition,
                                          restOrientation );
    }
}

void BuildReplayPredictionChildMarkerContext( ReplayPathChildDrawContext& context,
                                              const RunReplayPredictionState& prediction,
                                              const std::vector<RunReplayPredictionFrame>& frames,
                                              std::size_t frameCount,
                                              ReplayFrameIndex revealFrame )
{
    frameCount = (std::min)( frameCount, frames.size() );
    context = ReplayPathChildDrawContext {};
    context.nodeCount = (std::min)( prediction.futureNodeCache.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        context.nodes[i].node = prediction.futureNodeCache.futureNodes[i];
    }

    if ( frameCount < 2 || context.nodeCount == 0 )
    {
        return;
    }

    for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];
        if ( frame.frameIndex > revealFrame )
        {
            break;
        }

        for ( std::size_t i = 0; i < context.nodeCount; ++i )
        {
            ReplayPathChildDrawState& drawState = context.nodes[i];
            if ( frame.frameIndex < drawState.node.firstFrame )
            {
                continue;
            }

            const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint(
                frame,
                drawState.node.id,
                drawState.node.modelRow.value );

            if ( !body )
            {
                continue;
            }

            if ( !drawState.active )
            {
                if ( !ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
                {
                    continue;
                }

                const RunReplayPredictionBodySample* initialSample = FindReplayPredictionBodyByIdWithHint(
                    frames[0],
                    drawState.node.id,
                    body->modelRow.value );

                drawState.active = true;
                drawState.hasEntryPose = true;
                drawState.entryModelIndex = body->modelRow.value;
                drawState.entryPosition = initialSample ? initialSample->position : body->position;
                drawState.entryOrientation = initialSample ? initialSample->orientation : body->orientation;
                drawState.entryOrientation.Normalise();
                drawState.lastMotionFrame = frame.frameIndex;
                continue;
            }

            if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                drawState.lastMotionFrame = frame.frameIndex;
            }
        }
    }
}

ReplayFrameIndex ReplayPredictionVisibleRootMotionFrame( const std::vector<RunReplayPredictionFrame>& frames,
                                                         std::size_t frameCount,
                                                         ReplayFrameIndex revealFrame,
                                                         Physics::PhysicsSceneObjectId rootId,
                                                         int rootModelIndex )
{
    frameCount = (std::min)( frameCount, frames.size() );
    ReplayFrameIndex rootLastMotionFrame = 0;
    for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];
        if ( frame.frameIndex > revealFrame )
        {
            break;
        }

        const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint( frame,
                                                                                          rootId,
                                                                                          rootModelIndex );

        if ( body && ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
        {
            rootLastMotionFrame = frame.frameIndex;
        }
    }

    return rootLastMotionFrame;
}

void RetainReplayPredictionRootRestMarker( RunReplayPredictionState& prediction,
                                           const std::vector<RunReplayPredictionFrame>& frames,
                                           std::size_t frameCount,
                                           ReplayFrameIndex revealFrame,
                                           Physics::PhysicsSceneObjectId rootId,
                                           int rootModelIndex,
                                           const ColliderStore& colliderStore )
{
    const ReplayFrameIndex rootLastMotionFrame = ReplayPredictionVisibleRootMotionFrame( frames,
                                                                                         frameCount,
                                                                                         revealFrame,
                                                                                         rootId,
                                                                                         rootModelIndex );

    if ( revealFrame < rootLastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
    {
        return;
    }

    Vector3 rootRestPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion rootRestOrientation = IDENTITY_QUATERNION;
    if ( ReplayPredictionBodyRestingPose( frames,
                                          frameCount,
                                          rootId,
                                          rootModelIndex,
                                          rootRestPosition,
                                          rootRestOrientation ) &&
         ReplayColliderRecordForModelIndex( &colliderStore, rootModelIndex ) )
    {
        RetainReplayPredictionRestMarker( prediction, rootId, rootModelIndex, rootRestPosition, rootRestOrientation );
    }
}


void RetainReplayPredictionAffectedBodyMarkers( const std::vector<RunReplayPredictionFrame>& frames,
                                                std::size_t frameCount,
                                                RunReplayPredictionState& prediction,
                                                ReplayFrameIndex revealFrame,
                                                bool bufferComplete,
                                                Physics::PhysicsSceneObjectId rootId,
                                                int rootModelIndex,
                                                const std::vector<RunReplayPathTraceNode>& futureNodes,
                                                const SceneEntityStore& collection,
                                                const ColliderStore& colliderStore )
{
    std::array<ReplayPredictionAffectedBodyTrail, REPLAY_PATH_MAX_FUTURE_NODES> trails = {};
    const std::size_t trailCount = BuildReplayPredictionAffectedBodyTrails( frames,
                                                                            frameCount,
                                                                            revealFrame,
                                                                            rootId,
                                                                            rootModelIndex,
                                                                            futureNodes,
                                                                            collection,
                                                                            trails );

    frameCount = (std::min)( frameCount, frames.size() );
    // Why: marker publication is bounded and independent of render traversal.
    // Once revealed, a causal box stays in the prediction owner's published
    // cache even when the draw quota later degrades line work.
    for ( std::size_t trailIndex = 0; trailIndex < trailCount; ++trailIndex )
    {
        const ReplayPredictionAffectedBodyTrail& trail = trails[trailIndex];
        if ( !ReplayColliderRecordForModelIndex( &colliderStore, trail.modelRow.value ) )
        {
            continue;
        }

        RetainReplayPredictionEntryMarker( prediction,
                                           trail.id,
                                           trail.modelRow.value,
                                           trail.entryPosition,
                                           trail.entryOrientation );

        // Why: grey exists only for stories that end at rest inside the
        // completed horizon - see RetainReplayPredictionCausalMarkers.
        if ( !bufferComplete || revealFrame < trail.lastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
        {
            continue;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( ReplayPredictionBodyRestingPose( frames,
                                              frameCount,
                                              trail.id,
                                              trail.modelRow.value,
                                              restPosition,
                                              restOrientation ) )
        {
            RetainReplayPredictionRestMarker( prediction,
                                              trail.id,
                                              trail.modelRow.value,
                                              restPosition,
                                              restOrientation );
        }
    }
}

struct ReplayPredictionFutureContext
{
    RunReplayPredictionState* prediction = nullptr;
    std::vector<RunReplayPathTraceNode>* nodes = nullptr;
    const SceneEntityStore* collection = nullptr;
    Physics::PhysicsSceneObjectId rootId;
    bool includeRagdollVisuals = true;
};

bool TryGetReplayPredictionFutureDepth( const ReplayPredictionFutureContext& context,
                                        Physics::PhysicsSceneObjectId id,
                                        ReplayFrameIndex frame,
                                        int& outDepth )
{
    const std::vector<RunReplayPathTraceNode>& nodes = context.nodes
                                                           ? *context.nodes
                                                           : context.prediction->futureNodeCache.futureNodeBuildScratch;

    return TryGetReplayFutureDepthInNodes( nodes, context.rootId, 0, false, id, frame, outDepth );
}

void AddReplayPredictionFutureNode( ReplayPredictionFutureContext& context,
                                    Physics::PhysicsSceneObjectId parentId,
                                    int parentModelIndex,
                                    Physics::PhysicsSceneObjectId id,
                                    int modelIndex,
                                    ReplayFrameIndex firstFrame,
                                    const Vector3& contactPoint,
                                    const Vector3& contactNormal,
                                    int depth,
                                    bool contactDerived )
{
    if ( id.value == 0 || id.value == context.rootId.value || !context.nodes )
    {
        return;
    }

    std::vector<RunReplayPathTraceNode>& nodes = *context.nodes;
    if ( RunReplayPathTraceNode* existing = FindReplayFutureNodeInNodes( nodes, id ) )
    {
        // Why: a real contact edge carries stronger causal evidence than an
        // earlier motion-only fallback for the same future body.
        if ( contactDerived && !existing->contactDerived )
        {
            AssignReplayFutureNode( *existing,
                                    parentId,
                                    parentModelIndex,
                                    id,
                                    modelIndex,
                                    firstFrame,
                                    contactPoint,
                                    contactNormal,
                                    depth,
                                    true );
        }

        return;
    }

    if ( nodes.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        return;
    }

    RunReplayPathTraceNode node;
    AssignReplayFutureNode( node,
                            parentId,
                            parentModelIndex,
                            id,
                            modelIndex,
                            firstFrame,
                            contactPoint,
                            contactNormal,
                            depth,
                            contactDerived );

    nodes.push_back( node );
}

bool BuildReplayPredictionFutureNodes( const RunReplayPredictionFrame& frame,
                                       ReplayPredictionFutureContext& context,
                                       std::size_t startContactIndex,
                                       const std::chrono::steady_clock::time_point& budgetStart,
                                       double budgetMilliseconds,
                                       std::size_t& outNextContactIndex )
{
    return BuildReplayFutureNodesFromContacts(
        frame.debugContacts,
        frame.frameIndex,
        startContactIndex,
        context.collection,
        context.includeRagdollVisuals,
        [&]( int modelIndex ) { return ReplayPredictionBodyIdForModelIndex( frame, modelIndex ); },
        [&]( Physics::PhysicsSceneObjectId id, ReplayFrameIndex frameIndex, int& outDepth )
        { return TryGetReplayPredictionFutureDepth( context, id, frameIndex, outDepth ); },
        [&]( Physics::PhysicsSceneObjectId parentId,
             int parentModelIndex,
             Physics::PhysicsSceneObjectId id,
             int modelIndex,
             ReplayFrameIndex firstFrame,
             const Vector3& contactPoint,
             const Vector3& contactNormal,
             int depth,
             bool contactDerived )
        {
            AddReplayPredictionFutureNode( context,
                                           parentId,
                                           parentModelIndex,
                                           id,
                                           modelIndex,
                                           firstFrame,
                                           contactPoint,
                                           contactNormal,
                                           depth,
                                           contactDerived );
        },
        [&]() { return ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ); },
        outNextContactIndex );
}

bool ReplayPredictionBodyReachedActivationDisplacement( const RunReplayPredictionBodySample& initialBody,
                                                        const RunReplayPredictionBodySample& body,
                                                        Vector3& previousPosition,
                                                        float& accumulatedDisplacement,
                                                        Vector3& outActivationDelta )
{
    const Vector3 frameDelta = body.position - previousPosition;
    previousPosition = body.position;
    accumulatedDisplacement += VectorMag( frameDelta );
    outActivationDelta = body.position - initialBody.position;

    return accumulatedDisplacement >= REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE ||
           VectorMagSquared( outActivationDelta ) >= REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE_SQ;
}

bool BuildReplayPredictionAffectedFutureNodes( const std::vector<RunReplayPredictionFrame>& frames,
                                               std::size_t frameCount,
                                               ReplayPredictionFutureContext& context,
                                               const std::chrono::steady_clock::time_point& budgetStart,
                                               double budgetMilliseconds )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || context.rootId.value == 0 || !context.nodes )
    {
        return true;
    }

    const RunReplayPredictionFrame& firstFrame = frames.front();
    const RunReplayPredictionBodySample* rootBody = FindReplayPredictionBodyById( firstFrame, context.rootId );
    const int rootModelIndex = rootBody ? rootBody->modelRow.value : -1;

    // Concept: contact-derived nodes own the authoritative firstFrame whenever
    // the solver captured a contact tick. This sparse-contact fallback waits
    // for measured displacement from the first prediction sample instead of a
    // one-frame speed spike, so slow-pushed bodies join on the tick they
    // actually begin to move.
    for ( const RunReplayPredictionBodySample& initialBody : firstFrame.bodies )
    {
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return false;
        }

        if ( context.nodes->size() >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            return true;
        }

        if ( initialBody.id.value == 0 || initialBody.id.value == context.rootId.value ||
             ( rootModelIndex >= 0 && initialBody.modelRow.value == rootModelIndex ) )
        {
            continue;
        }

        if ( context.collection && ReplayModelIndexIsRagdollPart( *context.collection, initialBody.modelRow.value ) )
        {
            continue;
        }

        Vector3 previousPosition = initialBody.position;
        float accumulatedDisplacement = 0.0f;
        for ( std::size_t frameSlot = 1; frameSlot < frameCount; ++frameSlot )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return false;
            }

            const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint(
                frames[frameSlot],
                initialBody.id,
                initialBody.modelRow.value );

            if ( !body )
            {
                continue;
            }

            Vector3 activationDelta;
            if ( !ReplayPredictionBodyReachedActivationDisplacement( initialBody,
                                                                     *body,
                                                                     previousPosition,
                                                                     accumulatedDisplacement,
                                                                     activationDelta ) )
            {
                continue;
            }

            AddReplayPredictionFutureNode(
                context,
                context.rootId,
                rootModelIndex,
                initialBody.id,
                body->modelRow.value,
                frames[frameSlot].frameIndex,
                body->position,
                ReplayNormalizeOr( activationDelta,
                                   ReplayNormalizeOr( body->linearVelocity, Vector3( 0.0f, 1.0f, 0.0f ) ) ),
                1,
                false );

            break;
        }
    }

    return true;
}

void UpdateReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction,
                                            const std::vector<RunReplayPredictionFrame>& frames,
                                            std::size_t frameCount,
                                            bool usingBuildFrames,
                                            const SceneEntityStore& collection,
                                            Physics::PhysicsSceneObjectId rootId,
                                            const std::chrono::steady_clock::time_point& budgetStart,
                                            double budgetMilliseconds )
{
    // Invariant: frameCount is the populated prefix of frames. buildFrames is
    // pre-sized for the whole prediction horizon, so using frames.size() while
    // building would scan empty rows and mark the future-node cache complete
    // before contacts have been captured.
    frameCount = (std::min)( frameCount, frames.size() );
    const bool completingBuildFrames = !usingBuildFrames &&
                                       prediction.futureNodeCache.futureNodesBuiltFromBuildFrames &&
                                       prediction.futureNodeCache.futureNodesBuiltFrameCount <= frameCount;

    const bool sourceMismatch = prediction.futureNodeCache.futureNodesBuiltFromBuildFrames != usingBuildFrames &&
                                !completingBuildFrames;

    // Invariant: these inputs define the meaning of the cached tree. Any change
    // means old future nodes may point at the wrong root or include the wrong
    // ragdoll aggregation policy.
    const bool cacheMismatch = !prediction.futureNodeCache.futureNodesCacheValid ||
                               prediction.futureNodeCache.futureNodesBuiltTargetId.value != rootId.value ||
                               prediction.futureNodeCache.futureNodesBuiltRagdollVisuals !=
                                   prediction.ragdollVisualsEnabled ||
                               sourceMismatch || prediction.futureNodeCache.futureNodesBuiltFrameCount > frameCount;

    if ( cacheMismatch )
    {
        ClearReplayPredictionFutureNodeCache( prediction );
        prediction.futureNodeCache.futureNodesBuiltTargetId = rootId;
        prediction.futureNodeCache.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
        prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = usingBuildFrames;
        prediction.futureNodeCache.futureNodesCacheValid = rootId.value != 0;
    }
    else if ( completingBuildFrames )
    {
        prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    }

    if ( rootId.value == 0 || frameCount == 0 || !prediction.futureNodeCache.futureNodesCacheValid )
    {
        return;
    }

    auto publishScratch = [&]()
    {
        // Why: the renderer reads futureNodes only after this builder returns.
        // Copying the scratch prefix here lets cause/effect paths grow over
        // frames without exposing a vector while it is being mutated.
        const bool topologyChanged = !ReplayFutureNodeTopologyEquals(
            prediction.futureNodeCache.futureNodes,
            prediction.futureNodeCache.futureNodeBuildScratch );

        prediction.futureNodeCache.futureNodes = prediction.futureNodeCache.futureNodeBuildScratch;
        if ( topologyChanged )
        {
            prediction.futureNodeCache.futureNodesTopologyVersion = AllocateReplayFutureNodeTopologyVersion( prediction.futureNodeCache );
        }
    };

    if ( prediction.futureNodeCache.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        prediction.futureNodeCache.futureNodesBuiltFrameCount = frameCount;
        prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
        publishScratch();
        return;
    }

    ReplayPredictionFutureContext futureContext;
    futureContext.prediction = &prediction;
    futureContext.nodes = &prediction.futureNodeCache.futureNodeBuildScratch;
    futureContext.collection = &collection;
    futureContext.rootId = rootId;
    futureContext.includeRagdollVisuals = prediction.ragdollVisualsEnabled;

    while ( prediction.futureNodeCache.futureNodesBuiltFrameCount < frameCount )
    {
        const std::size_t frameIndex = prediction.futureNodeCache.futureNodesBuiltFrameCount;
        std::size_t nextContactIndex = prediction.futureNodeCache.futureNodesBuiltContactIndex;
        if ( !BuildReplayPredictionFutureNodes( frames[frameIndex],
                                                futureContext,
                                                prediction.futureNodeCache.futureNodesBuiltContactIndex,
                                                budgetStart,
                                                budgetMilliseconds,
                                                nextContactIndex ) )
        {
            prediction.futureNodeCache.futureNodesBuiltContactIndex = nextContactIndex;
            publishScratch();
            return;
        }

        prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
        ++prediction.futureNodeCache.futureNodesBuiltFrameCount;

        if ( prediction.futureNodeCache.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            prediction.futureNodeCache.futureNodesBuiltFrameCount = frameCount;
            prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
            break;
        }

        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            publishScratch();
            return;
        }
    }

    if ( prediction.futureNodeCache.futureNodeBuildScratch.size() < REPLAY_PATH_MAX_FUTURE_NODES &&
         !BuildReplayPredictionAffectedFutureNodes( frames,
                                                    frameCount,
                                                    futureContext,
                                                    budgetStart,
                                                    budgetMilliseconds ) )
    {
        publishScratch();
        return;
    }

    if ( prediction.futureNodeCache.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        // Invariant: once the fixed topology cap is saturated, later frames
        // cannot publish additional nodes. Mark the cache complete for the
        // visible prefix so reports do not encode the frame-budget slice that
        // happened to discover the final retained node.
        prediction.futureNodeCache.futureNodesBuiltFrameCount = frameCount;
        prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
    }

    publishScratch();
}


void RebuildReplayPredictionCommittedTreeAfterWorkerCompletion( RunReplayPredictionState& prediction,
                                                                const SceneEntityStore& modelCollection,
                                                                Physics::PhysicsSceneObjectId rootId )
{
    if ( rootId.value == 0 || prediction.simulation.frames.size() < 2u )
    {
        return;
    }

    // Why: the worker publishes physics frames; the frame thread owns topology.
    // Build the committed child tree once from the full finished buffer so
    // automation and draw records do not depend on how many budgeted render
    // passes ran before the worker completed.
    ClearReplayPredictionFutureNodeCache( prediction );
    const auto rebuildStart = std::chrono::steady_clock::now();
    UpdateReplayPredictionFutureNodeCache( prediction,
                                           prediction.simulation.frames,
                                           prediction.simulation.frames.size(),
                                           false,
                                           modelCollection,
                                           rootId,
                                           rebuildStart,
                                           0.0 );

    UpdateReplayPredictionTrajectoryStore( prediction,
                                           prediction.simulation.frames,
                                           prediction.simulation.frames.size(),
                                           false,
                                           rootId );
}


void PrepareReplayPredictionOverlay( RunReplayPredictionState& prediction,
                                     const SceneEntityStore& modelCollection,
                                     const ColliderStore& colliderStore,
                                     Physics::PhysicsSceneObjectId targetId,
                                     ModelRowHint targetModelRow,
                                     bool targetAvailable,
                                     double budgetMilliseconds,
                                     ReplayPredictionUpdateResult& result )
{
    const bool usingBuildFrames = prediction.BuildPrefixShouldBePresented();
    const std::vector<RunReplayPredictionFrame>& activePredictionFrames = usingBuildFrames
                                                                              ? prediction.build.buildFrames
                                                                              : prediction.simulation.frames;

    std::size_t activePredictionFrameCount = activePredictionFrames.size();
    if ( usingBuildFrames )
    {
        // Invariant: the frame thread owns this latch. A worker may release more
        // rows after preparation, but topology, trajectories, markers, ghosts,
        // and packet headers must all keep this one coherent prefix until the
        // next preparation pass.
        const std::size_t workerPublishedCount = prediction.PublishedBuildFrameCount();
        prediction.build.presentationPublication.Prepare( workerPublishedCount, activePredictionFrames.size() );
        activePredictionFrameCount = prediction.build.presentationPublication.PresentedCount(
            workerPublishedCount,
            activePredictionFrames.size() );
    }

    if ( activePredictionFrameCount < 2 )
    {
        return;
    }

    // Invariant: reveal advancement and derived-cache publication happen
    // before rendering. The overlay receives one immutable visible prefix and
    // cannot change which causal evidence later passes observe in this frame.
    const ReplayPredictionDrawFrameWindow drawWindow = PrepareReplayPredictionDrawFrameWindow(
        prediction,
        activePredictionFrames,
        activePredictionFrameCount );

    const bool bufferComplete = !usingBuildFrames;
    if ( !targetAvailable || targetId.value == 0 )
    {
        return;
    }

    if ( usingBuildFrames && !prediction.revealClock.deterministicFrameEnabled &&
         !PublishReplayPredictionBuildRootTrajectoryPrefix( prediction, activePredictionFrameCount ) )
    {
        return;
    }

    if ( bufferComplete )
    {
        RetainReplayPredictionRootRestMarker( prediction,
                                              activePredictionFrames,
                                              activePredictionFrameCount,
                                              drawWindow.revealFrame,
                                              targetId,
                                              targetModelRow.value,
                                              colliderStore );
    }

    const auto buildBudgetStart = std::chrono::steady_clock::now();
    if ( prediction.enabled )
    {
        UpdateReplayPredictionFutureNodeCache( prediction,
                                               activePredictionFrames,
                                               activePredictionFrameCount,
                                               usingBuildFrames,
                                               modelCollection,
                                               targetId,
                                               buildBudgetStart,
                                               budgetMilliseconds );

        UpdateReplayPredictionTrajectoryStore( prediction,
                                               activePredictionFrames,
                                               activePredictionFrameCount,
                                               usingBuildFrames,
                                               targetId );

        (void)ReplayPredictionBudgetExpiredForPass(
            result,
            SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBuildTree,
            buildBudgetStart,
            budgetMilliseconds );
    }

    const bool futureTreeReady = prediction.FutureTreeReadyForDraw( targetId,
                                                                    usingBuildFrames,
                                                                    activePredictionFrameCount );

    if ( futureTreeReady )
    {
        ReplayPathChildDrawContext childDraw;
        BuildReplayPredictionChildMarkerContext( childDraw,
                                                 prediction,
                                                 activePredictionFrames,
                                                 activePredictionFrameCount,
                                                 drawWindow.revealFrame );

        RetainReplayPredictionCausalMarkers( prediction,
                                             childDraw,
                                             drawWindow.revealFrame,
                                             bufferComplete ? &activePredictionFrames : nullptr,
                                             bufferComplete ? activePredictionFrameCount : 0 );
    }

    RetainReplayPredictionAffectedBodyMarkers( activePredictionFrames,
                                               activePredictionFrameCount,
                                               prediction,
                                               drawWindow.revealFrame,
                                               bufferComplete,
                                               targetId,
                                               targetModelRow.value,
                                               prediction.futureNodeCache.futureNodes,
                                               modelCollection,
                                               colliderStore );

    if ( bufferComplete )
    {
        RetainReplayPredictionEndStateMarkers( prediction,
                                               drawWindow.revealFrame,
                                               activePredictionFrames,
                                               activePredictionFrameCount );
    }
}


} // namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations
