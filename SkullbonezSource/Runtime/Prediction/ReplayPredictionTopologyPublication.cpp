/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp
Purpose:
  Publishes causal topology and retained prediction markers from completed frames.

Summary:
  This publication slice derives child relationships, reveal windows, and fixed
  marker values after frame rows become acquire-visible.

Glossary:
  Causal topology: Root-to-child relationships inferred from solver contacts.

Invariants:
  - Topology never reads beyond the acquire-visible prediction prefix.
  - Published marker and topology versions change only on coherent replacement.
  - The captured visible bank remains unchanged until its hidden topology and
    trajectory replacement reaches one generation-matched ready transition.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h
*/
#include "ReplayPredictionPublicationOperations.h"
#include "ReplayPredictionPublication.MarkerScan.inl"
#include "../../Assets/EditorHullAssets.h"
#include "../../Core/Config.h"
#include "../../Core/Profiler.h"
#include "../../Core/SceneCapacity.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsApi.h"
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


ReplayPredictionDrawFrameWindow PrepareReplayPredictionDrawFrameWindow( RunReplayPredictionState& prediction,
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
bool TryGetReplayFutureDepthInNodes( const NodeRange& nodes, Physics::PhysicsSceneObjectId rootId,
                                     ReplayFrameIndex rootFrame, bool requireRootFrame, Physics::PhysicsSceneObjectId id,
                                     ReplayFrameIndex frame, int& outDepth )
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

struct ReplayPredictionFutureNodeCandidate
{
    Physics::PhysicsSceneObjectId parentId;
    int parentModelIndex = -1;
    Physics::PhysicsSceneObjectId id;
    int modelIndex = -1;
    ReplayFrameIndex firstFrame;
    Vector3 contactPoint;
    Vector3 contactNormal;
    int depth = 0;
    bool contactDerived = false;
};

void AssignReplayFutureNode( RunReplayPathTraceNode& node, const ReplayPredictionFutureNodeCandidate& candidate )
{
    node.id = candidate.id;
    node.parentId = candidate.parentId;
    node.modelRow.value = candidate.modelIndex;
    node.parentModelRow.value = candidate.parentModelIndex;
    node.firstFrame = candidate.firstFrame;
    node.contactPoint = candidate.contactPoint;
    node.contactNormal = candidate.contactNormal;
    node.depth = candidate.depth;
    node.contactDerived = candidate.contactDerived;
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

void RetainReplayPredictionEndStateMarkers( RunReplayPredictionState& prediction, ReplayFrameIndex revealFrame,
                                            const std::vector<RunReplayPredictionFrame>& completeFrames,
                                            std::size_t completeFrameCount )
{
    completeFrameCount = (std::min)( completeFrameCount, completeFrames.size() );

    if ( completeFrameCount < 2 || revealFrame < completeFrames[completeFrameCount - 1].frameIndex )
    {
        return;
    }

    bool markersChanged = false;

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

        if ( ReplayPredictionBodyRestingPose( completeFrames, completeFrameCount, marker.id, marker.modelRow.value,
                                              restPosition, restOrientation ) )
        {
            RetainReplayPredictionRestMarker( prediction, marker.id, marker.modelRow.value, restPosition, restOrientation );
            markersChanged = true;

            continue;
        }

        const RunReplayPredictionBodySample*
            finalBody = FindReplayPredictionBodyByIdWithHint( completeFrames[completeFrameCount - 1], marker.id,
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

        RetainReplayPredictionHorizonMarker( prediction, marker.id, finalBody->modelRow.value, finalBody->position,
                                             finalBody->orientation );
        markersChanged = true;
    }

    if ( markersChanged )
    {
        prediction.futureNodeCache.MarkRetainedMarkersChanged();
    }
}

ReplayFrameIndex ReplayPredictionVisibleRootMotionFrame( const std::vector<RunReplayPredictionFrame>& frames,
                                                         std::size_t frameCount, ReplayFrameIndex revealFrame,
                                                         Physics::PhysicsSceneObjectId rootId, int rootModelIndex )
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

        const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint( frame, rootId, rootModelIndex );

        if ( body && ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
        {
            rootLastMotionFrame = frame.frameIndex;
        }
    }

    return rootLastMotionFrame;
}

void RetainReplayPredictionRootEndMarker( RunReplayPredictionState& prediction,
                                          const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount,
                                          ReplayFrameIndex revealFrame, Physics::PhysicsSceneObjectId rootId,
                                          int rootModelIndex, const ColliderStore& colliderStore )
{
    frameCount = (std::min)( frameCount, frames.size() );

    if ( frameCount == 0u || !ReplayColliderRecordForModelIndex( &colliderStore, rootModelIndex ) )
    {
        return;
    }

    const auto rootEndMarkerPublished = [&prediction, rootId]()
    {
        for ( std::size_t markerIndex = 0u; markerIndex < prediction.futureNodeCache.retainedMarkerCount; ++markerIndex )
        {
            const ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[markerIndex];

            if ( marker.id.value == rootId.value )
            {
                return marker.hasRestPose || marker.hasHorizonPose;
            }
        }

        return false;
    };

    if ( rootEndMarkerPublished() )
    {
        return;
    }

    const ReplayFrameIndex finalFrame = frames[frameCount - 1u].frameIndex;
    const bool finalReveal = revealFrame >= finalFrame;
    const ReplayFrameIndex rootLastMotionFrame = ReplayPredictionVisibleRootMotionFrame( frames, frameCount, revealFrame,
                                                                                         rootId, rootModelIndex );

    if ( !finalReveal && revealFrame < rootLastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
    {
        return;
    }

    Vector3 rootRestPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion rootRestOrientation = IDENTITY_QUATERNION;

    if ( ReplayPredictionBodyRestingPose( frames, frameCount, rootId, rootModelIndex, rootRestPosition,
                                          rootRestOrientation ) )
    {
        RetainReplayPredictionRestMarker( prediction, rootId, rootModelIndex, rootRestPosition, rootRestOrientation );

        if ( rootEndMarkerPublished() )
        {
            prediction.futureNodeCache.MarkRetainedMarkersChanged();
        }

        return;
    }

    if ( !finalReveal )
    {
        return;
    }

    const RunReplayPredictionBodySample* finalBody = FindReplayPredictionBodyByIdWithHint( frames[frameCount - 1u], rootId,
                                                                                           rootModelIndex );

    if ( finalBody )
    {
        // The selected root has no collision-entry marker of its own. A cyan
        // horizon wireframe keeps its moving endpoint explicit at the final reveal.
        RetainReplayPredictionHorizonMarker( prediction, rootId, finalBody->modelRow.value, finalBody->position,
                                             finalBody->orientation );

        if ( rootEndMarkerPublished() )
        {
            prediction.futureNodeCache.MarkRetainedMarkersChanged();
        }
    }
}


class ReplayPredictionFutureTopologyBuilder
{
  public:
    ReplayPredictionFutureTopologyBuilder( std::vector<RunReplayPathTraceNode>& nodes, ReplayPredictionSceneView scene,
                                           Physics::PhysicsSceneObjectId rootId, bool includeRagdollVisuals )
        : m_nodes( nodes ), m_scene( scene ), m_rootId( rootId ), m_includeRagdollVisuals( includeRagdollVisuals )
    {
    }

    bool BuildFrameContacts( const RunReplayPredictionFrame& frame, std::size_t startContactIndex,
                             const std::chrono::steady_clock::time_point& budgetStart, double budgetMilliseconds,
                             std::size_t& outNextContactIndex );
    bool Full() const noexcept
    {
        return m_nodes.size() >= REPLAY_PATH_MAX_FUTURE_NODES;
    }

  private:
    bool TryGetDepth( Physics::PhysicsSceneObjectId id, ReplayFrameIndex frame, int& outDepth ) const;
    void Add( const ReplayPredictionFutureNodeCandidate& candidate );

    // Lifetime: the builder is scoped to one cache-update call. The cache owns
    // the mutable vector, while the scene value borrows the caller's entity span.
    std::vector<RunReplayPathTraceNode>& m_nodes;
    ReplayPredictionSceneView m_scene;
    Physics::PhysicsSceneObjectId m_rootId;
    bool m_includeRagdollVisuals = true;
};

bool ReplayPredictionFutureTopologyBuilder::TryGetDepth( Physics::PhysicsSceneObjectId id, ReplayFrameIndex frame,
                                                         int& outDepth ) const
{
    return TryGetReplayFutureDepthInNodes( m_nodes, m_rootId, 0, false, id, frame, outDepth );
}

void ReplayPredictionFutureTopologyBuilder::Add( const ReplayPredictionFutureNodeCandidate& candidate )
{
    // Invariant: selected-causal presentation admits a body only through a
    // recorded solver contact with an already active body.
    if ( !candidate.contactDerived || candidate.id.value == 0 || candidate.id.value == m_rootId.value )
    {
        return;
    }

    if ( RunReplayPathTraceNode* existing = FindReplayFutureNodeInNodes( m_nodes, candidate.id ) )
    {
        if ( !existing->contactDerived )
        {
            AssignReplayFutureNode( *existing, candidate );
        }

        return;
    }

    if ( Full() )
    {
        return;
    }

    RunReplayPathTraceNode node;
    AssignReplayFutureNode( node, candidate );

    m_nodes.push_back( node );
}

bool ReplayPredictionFutureTopologyBuilder::BuildFrameContacts( const RunReplayPredictionFrame& frame,
                                                                std::size_t startContactIndex,
                                                                const std::chrono::steady_clock::time_point& budgetStart,
                                                                double budgetMilliseconds, std::size_t& outNextContactIndex )
{
    outNextContactIndex = (std::min)( startContactIndex, frame.debugContacts.size() );

    for ( std::size_t contactIndex = outNextContactIndex; contactIndex < frame.debugContacts.size(); ++contactIndex )
    {
        // Invariant: budget slicing resumes from this exact contact before the
        // caller advances its frame cursor.
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return false;
        }

        const auto& contact = frame.debugContacts[contactIndex];
        const bool ragdollA = ReplayModelIndexIsRagdollPart( m_scene, contact.bodyA );
        const bool ragdollB = ReplayModelIndexIsRagdollPart( m_scene, contact.bodyB );
        const int modelIndexA = ReplayRagdollTorsoModelIndexForPart( m_scene, contact.bodyA );
        const int modelIndexB = ReplayRagdollTorsoModelIndexForPart( m_scene, contact.bodyB );
        const Physics::PhysicsSceneObjectId idA = ReplayPredictionBodyIdForModelIndex( frame, modelIndexA );
        const Physics::PhysicsSceneObjectId idB = ReplayPredictionBodyIdForModelIndex( frame, modelIndexB );
        int depthA = -1;
        int depthB = -1;
        const bool activeA = TryGetDepth( idA, frame.frameIndex, depthA );
        const bool activeB = TryGetDepth( idB, frame.frameIndex, depthB );

        if ( activeA && !activeB && ( m_includeRagdollVisuals || !ragdollB ) )
        {
            Add( ReplayPredictionFutureNodeCandidate { idA, modelIndexA, idB, modelIndexB, frame.frameIndex, contact.point,
                                                       contact.normal, depthA + 1, true } );
        }
        else if ( activeB && !activeA && ( m_includeRagdollVisuals || !ragdollA ) )
        {
            Add( ReplayPredictionFutureNodeCandidate { idB, modelIndexB, idA, modelIndexA, frame.frameIndex, contact.point,
                                                       contact.normal * -1.0f, depthB + 1, true } );
        }

        outNextContactIndex = contactIndex + 1;
    }

    outNextContactIndex = 0;
    return true;
}

void UpdateReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction,
                                            const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount,
                                            bool usingBuildFrames, ReplayPredictionSceneView scene,
                                            Physics::PhysicsSceneObjectId rootId,
                                            const std::chrono::steady_clock::time_point& budgetStart,
                                            double budgetMilliseconds )
{
    if ( prediction.archivePresentationRestored )
    {
        // Invariant: the archive's visible topology is already canonical. Its
        // live-build scratch vector is deliberately absent, so publishing that
        // empty scratch would erase the restored nodes on the first reveal.
        return;
    }

    // Invariant: frameCount is the populated prefix of frames. buildFrames is
    // pre-sized for the whole prediction horizon, so using frames.size() while
    // building would scan empty rows and mark the future-node cache complete
    // before contacts have been captured.
    frameCount = (std::min)( frameCount, frames.size() );
    const bool completingBuildFrames = !usingBuildFrames && prediction.futureNodeCache.futureNodesBuiltFromBuildFrames &&
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
        const bool topologyChanged = !ReplayFutureNodeTopologyEquals( prediction.futureNodeCache.futureNodes,
                                                                      prediction.futureNodeCache.futureNodeBuildScratch );

        prediction.futureNodeCache.futureNodes = prediction.futureNodeCache.futureNodeBuildScratch;

        if ( topologyChanged )
        {
            if ( prediction.committedPublication.pending )
            {
                // Invariant: this cache is hidden behind the completed-build
                // snapshot. Every budget slice shares one replacement token;
                // only the coherent-ready flip makes it reader-visible.
                prediction.futureNodeCache
                    .futureNodesTopologyVersion = prediction.committedPublication.AcquireReplacementTopologyVersion(
                    [&prediction]() { return AllocateReplayFutureNodeTopologyVersion( prediction.futureNodeCache ); } );
            }
            else
            {
                prediction.futureNodeCache.futureNodesTopologyVersion = AllocateReplayFutureNodeTopologyVersion(
                    prediction.futureNodeCache );
            }
        }
    };

    if ( prediction.futureNodeCache.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        prediction.futureNodeCache.futureNodesBuiltFrameCount = frameCount;
        prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
        publishScratch();
        return;
    }

    ReplayPredictionFutureTopologyBuilder topologyBuilder( prediction.futureNodeCache.futureNodeBuildScratch, scene, rootId,
                                                           prediction.ragdollVisualsEnabled );

    while ( prediction.futureNodeCache.futureNodesBuiltFrameCount < frameCount )
    {
        const std::size_t frameIndex = prediction.futureNodeCache.futureNodesBuiltFrameCount;
        std::size_t nextContactIndex = prediction.futureNodeCache.futureNodesBuiltContactIndex;

        if ( !topologyBuilder.BuildFrameContacts( frames[frameIndex],
                                                  prediction.futureNodeCache.futureNodesBuiltContactIndex, budgetStart,
                                                  budgetMilliseconds, nextContactIndex ) )
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


void PrepareReplayPredictionOverlay( RunReplayPredictionState& prediction, ReplayPredictionSceneView scene,
                                     const ColliderStore& colliderStore, const ReplayPredictionOverlayRequest& request,
                                     ReplayPredictionUpdateResult& result )
{
    if ( prediction.committedPublication.pending &&
         ( prediction.committedPublication.generation != prediction.build.generationBeginCount ||
           prediction.committedPublication.sourceFrameCount != prediction.CommittedFrameCount() ) )
    {
        // Hazard: an explicit restart may supersede a pending duplicate. Its
        // visible-bank facts belong to the prior generation and must never be
        // used to authorize a later branch flip.
        prediction.committedPublication.Reset();
    }

    const bool presentingBuildPrefix = prediction.BuildPrefixShouldBePresented();
    const bool committedPublicationPending = prediction.committedPublication.pending;

    // Lifetime: a target click is allowed to queue the next generation while
    // the promoted prediction is still being duplicated. Every hidden phase
    // must nevertheless use the promoted snapshot's source through the flip.
    const Physics::PhysicsSceneObjectId publicationTargetId = prediction.committedPublication.PublicationTargetId(
        request.targetId );
    const ModelRowHint publicationTargetModelRow = prediction.committedPublication.PublicationTargetModelRow(
        request.targetModelRow );
    const bool publicationTargetAvailable = prediction.committedPublication.PublicationTargetAvailable(
        request.targetAvailable );
    const bool publicationUsingBuildFrames = committedPublicationPending
                                                 ? prediction.committedPublication.ReplacementTrajectoryBank() ==
                                                       ReplayPredictionTrajectoryBank::Build
                                                 : presentingBuildPrefix;
    const std::vector<RunReplayPredictionFrame>& activePredictionFrames = presentingBuildPrefix
                                                                              ? prediction.build.buildFrames
                                                                              : prediction.simulation.frames;

    std::size_t activePredictionFrameCount = presentingBuildPrefix ? activePredictionFrames.size()
                                                                   : prediction.CommittedFrameCount();

    if ( presentingBuildPrefix )
    {
        // Invariant: the frame thread owns this latch. A worker may release more
        // rows after preparation, but topology, trajectories, markers, ghosts,
        // and packet headers must all keep this one coherent prefix until the
        // next preparation pass.
        const std::size_t workerPublishedCount = prediction.PublishedBuildFrameCount();
        prediction.build.presentationPublication.Prepare( workerPublishedCount, activePredictionFrames.size() );
        activePredictionFrameCount = prediction.build.presentationPublication
                                         .PresentedCount( workerPublishedCount, activePredictionFrames.size() );
    }

    if ( activePredictionFrameCount < 2 )
    {
        return;
    }

    // Invariant: reveal advancement and derived-cache publication happen
    // before rendering. The overlay receives one immutable visible prefix and
    // cannot change which causal evidence later passes observe in this frame.
    // Invariant: one clock bounds the whole overlay pass, not just the future
    // node build. Phases run in priority order and each remaining phase is
    // skipped once the shared budget is spent, so the pass shows as much as it
    // can within budgetMilliseconds and finishes the rest on later frames.
    // Hazard: the check is between phases, so a pass can overrun by at most the
    // duration of the one phase already in flight. No phase is interrupted
    // internally, which is what keeps every retained marker set self-consistent.
    const auto overlayBudgetStart = std::chrono::steady_clock::now();

    // Concept: these child markers exist to separate the overlay's idle cost from
    // its rebuild cost. Once a horizon is complete the pass still runs every
    // frame, and only some phases are cached behind a scan/version match, so a
    // single PrepareOverlay total cannot say which phase is paying.
    ReplayPredictionDrawFrameWindow drawWindow;

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/PrepareOverlay/DrawWindow" );
        drawWindow = PrepareReplayPredictionDrawFrameWindow( prediction, activePredictionFrames,
                                                             activePredictionFrameCount );
    }

    const bool bufferComplete = !presentingBuildPrefix;

    if ( !publicationTargetAvailable || publicationTargetId.value == 0 )
    {
        return;
    }

    if ( presentingBuildPrefix && !prediction.revealClock.deterministicFrameEnabled &&
         !PublishReplayPredictionBuildRootTrajectoryPrefix( prediction, activePredictionFrameCount ) )
    {
        return;
    }

    if ( bufferComplete )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/PrepareOverlay/RootEndMarker" );
        RetainReplayPredictionRootEndMarker( prediction, activePredictionFrames, activePredictionFrameCount,
                                             drawWindow.revealFrame, publicationTargetId, publicationTargetModelRow.value,
                                             colliderStore );
    }

    if ( prediction.enabled )
    {
        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/PrepareOverlay/FutureNodeCache" );
            UpdateReplayPredictionFutureNodeCache( prediction, activePredictionFrames, activePredictionFrameCount,
                                                   publicationUsingBuildFrames, scene, publicationTargetId,
                                                   overlayBudgetStart, request.budgetMilliseconds );
        }

        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/PrepareOverlay/TrajectoryStore" );
            UpdateReplayPredictionTrajectoryStore( prediction, activePredictionFrames, activePredictionFrameCount,
                                                   publicationUsingBuildFrames, publicationTargetId, overlayBudgetStart,
                                                   request.budgetMilliseconds );
        }

        (void)ReplayPredictionBudgetExpiredForPass( result,
                                                    SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBuildTree,
                                                    overlayBudgetStart, request.budgetMilliseconds );
    }

    const bool futureTreeReady = prediction.FutureTreeReadyForDraw( publicationTargetId, publicationUsingBuildFrames,
                                                                    activePredictionFrameCount );

    // Why: the scan cache is the resume token. Skipping the block leaves
    // markerScan.Matches false, so the next frame retries the whole rebuild
    // rather than committing a half-built marker set.
    if ( futureTreeReady && !ReplayPredictionBudgetExpired( overlayBudgetStart, request.budgetMilliseconds ) )
    {
        ReplayPredictionChildMarkerScanState& markerScan = prediction.futureNodeCache.childMarkerScan;
        const uint32_t generation = prediction.build.generationBeginCount;
        const uint32_t topologyVersion = prediction.futureNodeCache.futureNodesTopologyVersion;
        const std::size_t markerNodeCount = (std::min)( prediction.futureNodeCache.futureNodes.size(),
                                                        static_cast<std::size_t>( REPLAY_PATH_MAX_FUTURE_NODES ) );

        if ( !markerScan.Matches( generation, topologyVersion, markerNodeCount, publicationTargetId,
                                  activePredictionFrameCount, drawWindow.revealFrame, publicationUsingBuildFrames,
                                  bufferComplete ) )
        {
            bool markerScanComplete = false;

            {
                PROFILE_SCOPED( "Frame/Replay/Prediction/PrepareOverlay/BuildChildMarkerContext" );
                markerScanComplete = AdvanceReplayPredictionChildMarkerScan( markerScan, prediction, activePredictionFrames,
                                                                             activePredictionFrameCount,
                                                                             drawWindow.revealFrame, generation,
                                                                             publicationTargetId,
                                                                             publicationUsingBuildFrames, bufferComplete,
                                                                             overlayBudgetStart,
                                                                             request.budgetMilliseconds );
            }

            // Why: expiry defers only this coherent marker replacement. The
            // overlay still reaches its remaining budget checks so frame-level
            // presentation bookkeeping cannot be skipped by marker discovery.
            if ( markerScanComplete )
            {
                {
                    PROFILE_SCOPED( "Frame/Replay/Prediction/PrepareOverlay/RetainCausalMarkers" );
                    RetainReplayPredictionCausalMarkers( prediction, markerScan, drawWindow.revealFrame,
                                                         bufferComplete ? &activePredictionFrames : nullptr,
                                                         bufferComplete ? activePredictionFrameCount : 0 );
                    prediction.futureNodeCache.MarkRetainedMarkersChanged();
                }
            }
        }
    }

    if ( bufferComplete && !ReplayPredictionBudgetExpired( overlayBudgetStart, request.budgetMilliseconds ) )
    {
        // Hazard: this uncached phase runs only once the buffer is complete, so
        // it is present exactly on the idle frames an operator is accounting for.
        PROFILE_SCOPED( "Frame/Replay/Prediction/PrepareOverlay/EndStateMarkers" );
        RetainReplayPredictionEndStateMarkers( prediction, drawWindow.revealFrame, activePredictionFrames,
                                               activePredictionFrameCount );
    }

    if ( committedPublicationPending )
    {
        (void)TryFlipReplayPredictionCommittedPublication( prediction, publicationTargetId, activePredictionFrameCount,
                                                           drawWindow.revealFrame, overlayBudgetStart,
                                                           request.budgetMilliseconds );
    }
}


} // namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations
