/*
File: SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp
Purpose:
  Implements replay cause-row construction, window input, and focus behavior.

Summary:
  The cause tree is an explanatory replay UI over retained solver contacts and
  predicted movement. ReplayAuthoring builds bounded rows from immutable owner
  views, owns window placement and drag/resize state, derives row hover from a
  disposable shared surface, and resolves camera focus from explicit
  prediction, solver, and live-store views.

Glossary:
  Cause tree: Contact, solver-row, and predicted-motion graph explaining replay
    body influence.
  Focus row: Cause-tree row selected for replay inspection camera targeting.

Invariants:
  - Window drag and resize gestures must release pointer capture on mouse up.
  - A higher-priority UI block suppresses cause-window actions and draw hover.
  - Focus changes hold live replay advance so selected historical rows remain visible.
  - Row construction never grows storage on the input/render path.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#include "ReplayAuthoring.h"
#include "ReplayPrediction.h"
#include "ReplayPresentation.h"
#include "../../Assets/AssetKeys.h"
#include "../CameraCollection.h"
#include "../InputController.h"
#include "../InputRouter.h"
#include "../../Core/Profiler.h"
#include "../../Core/FatalError.h"
#include "ReplayOverlayLayout.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::ReplayOverlay;

namespace
{
bool IsReplayCauseTreeToolOwner( WorldInteractionOwner owner )
{
    return owner == WorldInteractionOwner::ReplayScrub || owner == WorldInteractionOwner::ReplayVelocityEdit ||
           owner == WorldInteractionOwner::ReplayPrediction || owner == WorldInteractionOwner::ReplayBranchTarget ||
           owner == WorldInteractionOwner::ReplayCauseTree;
}


Vector3 ReplayCauseTreeNormalizeOr( Vector3 value, const Vector3& fallback )
{
    const float magSq = VectorMagSquared( value );
    if ( magSq <= TOLERANCE * TOLERANCE )
    {
        return fallback;
    }
    value /= sqrtf( magSq );
    return value;
}

float ReplayCauseTreeColliderRadius( const ColliderRecord& collider )
{
    return (std::max)( collider.boundingRadius, 1.0f );
}

float ReplayCauseTreeColliderRadiusForModelRow( const ColliderStore& colliderStore, int modelRow )
{
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForModelIndex( modelRow );
    if ( const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle ) )
    {
        return ReplayCauseTreeColliderRadius( *collider );
    }

    const auto colliders = colliderStore.Records();
    if ( modelRow < 0 || modelRow >= static_cast<int>( colliders.size() ) )
    {
        return 1.0f;
    }
    return ReplayCauseTreeColliderRadius( colliders[static_cast<std::size_t>( modelRow )] );
}

float ReplayCauseTreeColliderRadiusForBody( const ColliderStore& colliderStore,
                                            const PhysicsBodyRecord& body,
                                            int fallbackModelRow )
{
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( body.handle );
    if ( const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle ) )
    {
        return ReplayCauseTreeColliderRadius( *collider );
    }
    return ReplayCauseTreeColliderRadiusForModelRow( colliderStore, fallbackModelRow );
}

// Concept: focus pose lookup is authoring interpretation over immutable replay
// publications plus live physics rows. Dense model rows remain radius hints;
// ReplayBodyId is the identity check in every source.
// Invariant: this helper stores no view and never repairs topology while input
// is active. The frame boundary prepared paired body/collider stores first.
bool ResolveReplayCauseTreeBodyPosition( ReplayBodyId id,
                                         bool predictionEnabled,
                                         ReplayBodyId predictionTargetId,
                                         ReplayBodyId pathTargetId,
                                         std::span<const RunReplayPredictionFrame> activePredictionFrames,
                                         const ReplaySolverFrameSample* solverSample,
                                         const PhysicsBodyStore& bodyStore,
                                         const ColliderStore& colliderStore,
                                         Vector3& outPosition,
                                         float* outRadius )
{
    if ( id.value == 0 )
    {
        return false;
    }

    if ( outRadius )
    {
        *outRadius = 1.0f;
    }

    const auto publishSampleRadius = [&]( ModelRowHint modelRow )
    {
        if ( !outRadius )
        {
            return;
        }
        const PhysicsBodyHandle liveBody = bodyStore.HandleForReplayBodyId( id.value, modelRow.value );
        const PhysicsBodyRecord* liveBodyRecord = bodyStore.RecordForHandle( liveBody );
        *outRadius = liveBodyRecord
                         ? ReplayCauseTreeColliderRadiusForBody( colliderStore, *liveBodyRecord, modelRow.value )
                         : ReplayCauseTreeColliderRadiusForModelRow( colliderStore, modelRow.value );
    };

    if ( predictionEnabled && !activePredictionFrames.empty() && predictionTargetId.value == pathTargetId.value )
    {
        for ( const RunReplayPredictionBodySample& body : activePredictionFrames.front().bodies )
        {
            if ( body.id.value == id.value )
            {
                outPosition = body.position;
                publishSampleRadius( body.modelRow );
                return true;
            }
        }
    }

    if ( solverSample )
    {
        for ( const ReplaySolverBodySample& body : solverSample->bodies )
        {
            if ( body.id.value == id.value )
            {
                outPosition = body.position;
                publishSampleRadius( body.modelRow );
                return true;
            }
        }
    }

    const auto bodies = bodyStore.Records();
    const auto hotFields = bodyStore.HotFields();
    for ( std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex )
    {
        const PhysicsBodyRecord& body = bodies[bodyIndex];
        if ( body.replayBodyId == id.value )
        {
            outPosition = PhysicsBodyPosition( hotFields, bodyIndex );
            if ( outRadius )
            {
                const int fallbackModelRow = bodyStore.ModelIndexForHandle( body.handle );
                *outRadius = ReplayCauseTreeColliderRadiusForBody( colliderStore, body, fallbackModelRow );
            }
            return true;
        }
    }
    return false;
}
// Concept: replay body lookup is sample-shaped, not subsystem-shaped.
//
// Solver samples and prediction frames both expose body rows keyed by
// ReplayBodyId, while modelIndex remains only a fast hint into each row array.
// Invariant: wrappers keep the old negative-modelIndex behavior for callers
// that still distinguish solver samples from prediction samples.
template <typename FrameSample, typename BodySample>
const BodySample* FindReplayBodyByIdInSample( const FrameSample& sample, ReplayBodyId id );

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
const BodySample* FindReplayBodyByModelIndexInSample( const FrameSample& sample, int modelIndex );

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
ReplayBodyId ReplayBodyIdForModelIndexInSample( const FrameSample& sample, int modelIndex );

const ReplaySolverBodySample* FindReplayBodyById( const ReplaySolverFrameSample& sample, ReplayBodyId id )
{
    return FindReplayBodyByIdInSample<ReplaySolverFrameSample, ReplaySolverBodySample>( sample, id );
}

template <typename FrameSample, typename BodySample>
const BodySample* FindReplayBodyByIdInSample( const FrameSample& sample, ReplayBodyId id )
{
    for ( const BodySample& body : sample.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }
    return nullptr;
}

ReplayBodyId ReplayBodyIdForModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    return ReplayBodyIdForModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, false>( sample,
                                                                                                      modelIndex );
}

Vector3 ReplayNormalizeOr( Vector3 value, const Vector3& fallback )
{
    const float magSq = VectorMagSquared( value );
    if ( magSq <= TOLERANCE * TOLERANCE )
    {
        return fallback;
    }
    value /= sqrtf( magSq );
    return value;
}

const ReplaySolverBodySample* FindReplayBodyByModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    return FindReplayBodyByModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, true>( sample,
                                                                                                      modelIndex );
}

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
const BodySample* FindReplayBodyByModelIndexInSample( const FrameSample& sample, int modelIndex )
{
    if constexpr ( !AllowNegativeModelIndex )
    {
        if ( modelIndex < 0 )
        {
            return nullptr;
        }
    }

    if ( modelIndex >= 0 && modelIndex < static_cast<int>( sample.bodies.size() ) )
    {
        const BodySample& body = sample.bodies[static_cast<std::size_t>( modelIndex )];
        if ( body.modelRow.value == modelIndex )
        {
            return &body;
        }
    }

    for ( const BodySample& body : sample.bodies )
    {
        if ( body.modelRow.value == modelIndex )
        {
            return &body;
        }
    }
    return nullptr;
}

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
ReplayBodyId ReplayBodyIdForModelIndexInSample( const FrameSample& sample, int modelIndex )
{
    if ( const BodySample* body =
             FindReplayBodyByModelIndexInSample<FrameSample, BodySample, AllowNegativeModelIndex>( sample,
                                                                                                   modelIndex ) )
    {
        return body->id;
    }
    return ReplayBodyId{};
}

bool ReplayContactHasModelIndex( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                 int modelIndex )
{
    return modelIndex >= 0 && ( contact.bodyA == modelIndex || contact.bodyB == modelIndex );
}

int ReplayContactOtherModelIndex( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                  int modelIndex )
{
    if ( contact.bodyA == modelIndex )
    {
        return contact.bodyB;
    }
    if ( contact.bodyB == modelIndex )
    {
        return contact.bodyA;
    }
    return -1;
}

Vector3 ReplayContactPoint( const ReplaySolverFrameSample& sample,
                            const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact )
{
    if ( const ReplaySolverBodySample* bodyA = FindReplayBodyByModelIndex( sample, contact.bodyA ) )
    {
        return bodyA->position + contact.rA;
    }
    if ( const ReplaySolverBodySample* bodyB = FindReplayBodyByModelIndex( sample, contact.bodyB ) )
    {
        return bodyB->position + contact.rB;
    }
    return SkullbonezCore::Math::Vector::ZERO_VECTOR;
}

Vector3 ReplayContactNormalForModel( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                     int modelIndex )
{
    Vector3 normal = contact.normal;
    if ( contact.isTerrain && VectorMagSquared( contact.terrainNormal ) > TOLERANCE * TOLERANCE )
    {
        normal = contact.terrainNormal;
    }
    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        normal = normal * -1.0f;
    }
    return ReplayNormalizeOr( normal, Vector3( 0.0f, 1.0f, 0.0f ) );
}

Vector3 ReplayContactImpulseForModel( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                      int modelIndex )
{
    const Vector3 rowImpulse =
        contact.normal * contact.accN + contact.tangent1 * contact.accT1 + contact.tangent2 * contact.accT2;
    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        return rowImpulse;
    }
    return rowImpulse * -1.0f;
}

int ReplayFindPipelineIndexForContact( const SkullbonezCore::Physics::PhysicsSolverSnapshot& snapshot,
                                       const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact )
{
    for ( int i = 0; i < static_cast<int>( snapshot.pipelineTrace.size() ); ++i )
    {
        const PhysicsPipelineRecord& record = snapshot.pipelineTrace[static_cast<std::size_t>( i )];
        if ( record.featureId == contact.featureId &&
             ( ( record.bodyA == contact.bodyA && record.bodyB == contact.bodyB ) ||
               ( record.bodyA == contact.bodyB && record.bodyB == contact.bodyA ) ) )
        {
            return i;
        }
    }
    return -1;
}
} // namespace

bool ReplayAuthoring::BuildCauseTreeRows(
    const RunReplayPathVisualizerState& path,
    const RunReplayPredictionState& prediction,
    std::span<const RunReplayPredictionFrame> activePredictionFrames,
    const ReplaySolverFrameSample* solverSample,
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
    const PhysicsBodyStore& bodyStore,
    const RunReplayCameraState& camera,
    int& outCameraFocusedRow )
{
    PROFILE_SCOPED( m_profiler, "Frame/Replay/CauseTree/BuildRows" );
    outCameraFocusedRow = -1;
    BeginCauseTreeRowBuild();

    if ( !path.hasTarget || path.targetId.value == 0 )
    {
        return false;
    }

    // Why: ActivePredictionFrames() waits for a coherent full buffer, while the
    // prediction overlay exposes a populated build prefix so long jobs are
    // visible immediately. The cause tree must use the same readiness rule.
    const bool predictionPrefixVisible = activePredictionFrames.size() >= 2 ||
                                         prediction.HasPublishedBuildFramePrefix() ||
                                         !prediction.futureNodeCache.futureNodes.empty();
    const bool usePrediction =
        prediction.enabled && predictionPrefixVisible && prediction.simulation.targetId.value == path.targetId.value;
    const std::vector<RunReplayPathTraceNode>& nodes =
        usePrediction ? prediction.futureNodeCache.futureNodes : path.futureNodes;
    const std::size_t solverContactCount =
        solverSample ? solverSample->worldSnapshot.physics.persistentContacts.size() : static_cast<std::size_t>( 0 );
    const std::size_t estimatedRows = 1 + nodes.size() + solverContactCount * 3;
    if ( !CauseTreeRowCapacityCovers( estimatedRows ) )
    {
        // Hazard: this path runs from input/render. If a future scene exceeds
        // the preallocated explanation budget, hide the overlay for the frame
        // instead of growing row storage on the hot path.
        SetCauseTreeSelectedRow( -1 );
        return false;
    }
    bool rowOverflow = false;
    auto appendCauseTreeRow = [&]( const RunReplayCauseTreeRow& row ) -> bool
    {
        if ( !AppendCauseTreeRow( row ) )
        {
            rowOverflow = true;
            return false;
        }
        return true;
    };

    // Invariant: cause-tree rows keep model indices only for UI row selection
    // and solver-artifact contact matching. ReplayBodyId identity resolves
    // through body-store handles first; solver samples are historical fallback.
    auto modelIndexForId = [&]( ReplayBodyId id, int preferredModelIndex ) -> int
    {
        const PhysicsBodyHandle body = bodyStore.HandleForReplayBodyId( id.value, preferredModelIndex );
        const int liveIndex = bodyStore.ModelIndexForHandle( body );
        if ( liveIndex >= 0 )
        {
            return liveIndex;
        }
        if ( solverSample )
        {
            if ( const ReplaySolverBodySample* sampleBody = FindReplayBodyById( *solverSample, id ) )
            {
                return sampleBody->modelRow.value;
            }
        }
        return -1;
    };

    auto idForModelIndex = [&]( int modelIndex ) -> ReplayBodyId
    {
        ReplayBodyId id;
        if ( modelIndex < 0 )
        {
            return id;
        }
        if ( solverSample )
        {
            id = ReplayBodyIdForModelIndex( *solverSample, modelIndex );
            if ( id.value != 0 )
            {
                return id;
            }
        }
        if ( const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex ) )
        {
            id.value = body->replayBodyId;
        }
        return id;
    };

    auto writeName =
        [&]( ReplayBodyId id, int modelIndex, const char* fallback, char* out, std::size_t outSize ) -> void
    {
        out[0] = '\0';
        if ( fallback && fallback[0] != '\0' )
        {
            strncpy_s( out, outSize, fallback, _TRUNCATE );
            return;
        }
        if ( modelIndex >= 0 && modelIndex < static_cast<int>( presentationRecords.size() ) )
        {
            const char* modelName = presentationRecords[static_cast<std::size_t>( modelIndex )].displayName;
            if ( modelName && modelName[0] != '\0' )
            {
                strncpy_s( out, outSize, modelName, _TRUNCATE );
                return;
            }
        }
        if ( solverSample )
        {
            if ( const ReplaySolverBodySample* body = FindReplayBodyById( *solverSample, id ) )
            {
                if ( body->name[0] != '\0' )
                {
                    strncpy_s( out, outSize, body->name, _TRUNCATE );
                    return;
                }
            }
        }
        sprintf_s( out, outSize, "body_%u", id.value );
    };

    auto appendSolverRowsForBody = [&]( RunReplayCauseTreeRow bodyRow ) -> void
    {
        if ( usePrediction )
        {
            for ( int i = 0; i < static_cast<int>( nodes.size() ); ++i )
            {
                const RunReplayPathTraceNode& node = nodes[static_cast<std::size_t>( i )];
                if ( node.id.value != bodyRow.id.value )
                {
                    continue;
                }
                RunReplayCauseTreeRow contactRow;
                contactRow.kind = node.contactDerived ? RunReplayCauseTreeRowKind::PredictionContact
                                                      : RunReplayCauseTreeRowKind::PredictionMotion;
                contactRow.id = bodyRow.id;
                contactRow.parentId = node.parentId;
                contactRow.counterpartId = node.parentId;
                contactRow.firstFrame = node.firstFrame;
                contactRow.depth = bodyRow.depth + 1;
                contactRow.modelRow.value = bodyRow.modelRow.value;
                contactRow.counterpartModelRow.value = node.parentModelRow.value;
                contactRow.contactIndex = i;
                contactRow.prediction = true;
                contactRow.point = node.contactPoint;
                contactRow.normal = ReplayNormalizeOr( node.contactNormal, Vector3( 0.0f, 1.0f, 0.0f ) );
                if ( node.contactDerived )
                {
                    sprintf_s( contactRow.name, sizeof( contactRow.name ), "Predicted contact" );
                    sprintf_s( contactRow.detail,
                               sizeof( contactRow.detail ),
                               "first frame %llu  normal %.2f %.2f %.2f",
                               static_cast<unsigned long long>( node.firstFrame ),
                               contactRow.normal.x,
                               contactRow.normal.y,
                               contactRow.normal.z );
                }
                else
                {
                    sprintf_s( contactRow.name, sizeof( contactRow.name ), "Predicted movement" );
                    sprintf_s( contactRow.detail,
                               sizeof( contactRow.detail ),
                               "first affected frame %llu  direction %.2f %.2f %.2f",
                               static_cast<unsigned long long>( node.firstFrame ),
                               contactRow.normal.x,
                               contactRow.normal.y,
                               contactRow.normal.z );
                }
                if ( !appendCauseTreeRow( contactRow ) )
                {
                    return;
                }
            }
            return;
        }

        if ( !solverSample || bodyRow.modelRow.value < 0 )
        {
            return;
        }

        struct ManifoldGroup
        {
            int otherModelIndex = -1;
            bool terrain = false;
        };
        std::array<ManifoldGroup, REPLAY_CAUSE_TREE_CONTACT_CAPACITY> groups = {};
        std::size_t groupCount = 0;
        for ( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact :
              solverSample->worldSnapshot.physics.persistentContacts )
        {
            if ( !ReplayContactHasModelIndex( contact, bodyRow.modelRow.value ) )
            {
                continue;
            }
            const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelRow.value );
            const bool terrain = contact.isTerrain || otherModelIndex < 0;
            bool exists = false;
            for ( std::size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex )
            {
                const ManifoldGroup& group = groups[groupIndex];
                if ( group.otherModelIndex == otherModelIndex && group.terrain == terrain )
                {
                    exists = true;
                    break;
                }
            }
            if ( !exists )
            {
                if ( groupCount >= groups.size() )
                {
                    rowOverflow = true;
                    return;
                }
                groups[groupCount] = { otherModelIndex, terrain };
                ++groupCount;
            }
        }

        for ( std::size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const ManifoldGroup& group = groups[groupIndex];
            Vector3 centroid = SkullbonezCore::Math::Vector::ZERO_VECTOR;
            Vector3 normalSum = SkullbonezCore::Math::Vector::ZERO_VECTOR;
            float maxPenetration = 0.0f;
            int pointCount = 0;
            int firstContactIndex = -1;
            uint32_t firstFeatureId = 0;
            for ( int i = 0; i < static_cast<int>( solverSample->worldSnapshot.physics.persistentContacts.size() );
                  ++i )
            {
                const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact =
                    solverSample->worldSnapshot.physics.persistentContacts[static_cast<std::size_t>( i )];
                if ( !ReplayContactHasModelIndex( contact, bodyRow.modelRow.value ) )
                {
                    continue;
                }
                const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelRow.value );
                const bool terrain = contact.isTerrain || otherModelIndex < 0;
                if ( otherModelIndex != group.otherModelIndex || terrain != group.terrain )
                {
                    continue;
                }
                const Vector3 point = ReplayContactPoint( *solverSample, contact );
                centroid += point;
                normalSum += ReplayContactNormalForModel( contact, bodyRow.modelRow.value );
                maxPenetration = (std::max)( maxPenetration, contact.penetration );
                pointCount += 1;
                if ( firstContactIndex < 0 )
                {
                    firstContactIndex = i;
                    firstFeatureId = contact.featureId;
                }
            }
            if ( pointCount <= 0 )
            {
                continue;
            }
            centroid /= static_cast<float>( pointCount );
            const ReplayBodyId otherId = idForModelIndex( group.otherModelIndex );

            char otherName[64] = {};
            if ( group.terrain )
            {
                strncpy_s( otherName, sizeof( otherName ), "terrain", _TRUNCATE );
            }
            else
            {
                writeName( otherId, group.otherModelIndex, nullptr, otherName, sizeof( otherName ) );
            }

            RunReplayCauseTreeRow manifoldRow;
            manifoldRow.kind = RunReplayCauseTreeRowKind::Manifold;
            manifoldRow.id = bodyRow.id;
            manifoldRow.parentId = bodyRow.parentId;
            manifoldRow.counterpartId = otherId;
            manifoldRow.depth = bodyRow.depth + 1;
            manifoldRow.modelRow.value = bodyRow.modelRow.value;
            manifoldRow.counterpartModelRow.value = group.otherModelIndex;
            manifoldRow.contactIndex = firstContactIndex;
            manifoldRow.featureId = static_cast<int>( firstFeatureId );
            manifoldRow.manifoldPointCount = pointCount;
            manifoldRow.penetration = maxPenetration;
            manifoldRow.point = centroid;
            manifoldRow.normal = ReplayNormalizeOr( normalSum, Vector3( 0.0f, 1.0f, 0.0f ) );
            manifoldRow.terrain = group.terrain;
            sprintf_s( manifoldRow.name, sizeof( manifoldRow.name ), "Manifold vs %s", otherName );
            sprintf_s( manifoldRow.detail,
                       sizeof( manifoldRow.detail ),
                       "%d point%s  max pen %.3f",
                       pointCount,
                       pointCount == 1 ? "" : "s",
                       maxPenetration );
            if ( !appendCauseTreeRow( manifoldRow ) )
            {
                return;
            }

            for ( int i = 0; i < static_cast<int>( solverSample->worldSnapshot.physics.persistentContacts.size() );
                  ++i )
            {
                const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact =
                    solverSample->worldSnapshot.physics.persistentContacts[static_cast<std::size_t>( i )];
                if ( !ReplayContactHasModelIndex( contact, bodyRow.modelRow.value ) )
                {
                    continue;
                }
                const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelRow.value );
                const bool terrain = contact.isTerrain || otherModelIndex < 0;
                if ( otherModelIndex != group.otherModelIndex || terrain != group.terrain )
                {
                    continue;
                }

                RunReplayCauseTreeRow solverRow;
                solverRow.kind = RunReplayCauseTreeRowKind::SolverRow;
                solverRow.id = bodyRow.id;
                solverRow.parentId = bodyRow.parentId;
                solverRow.counterpartId = otherId;
                solverRow.depth = bodyRow.depth + 2;
                solverRow.modelRow.value = bodyRow.modelRow.value;
                solverRow.counterpartModelRow.value = group.otherModelIndex;
                solverRow.contactIndex = i;
                solverRow.solverRowIndex = i;
                solverRow.pipelineIndex =
                    ReplayFindPipelineIndexForContact( solverSample->worldSnapshot.physics, contact );
                solverRow.featureId = static_cast<int>( contact.featureId );
                solverRow.manifoldPointCount = contact.manifoldPointCount;
                solverRow.penetration = contact.penetration;
                solverRow.normalImpulse = contact.accN;
                solverRow.tangentImpulse = sqrtf( contact.accT1 * contact.accT1 + contact.accT2 * contact.accT2 );
                solverRow.warmStartImpulse = contact.terrainWarmStart;
                solverRow.bias = contact.bias;
                solverRow.effectiveMass = contact.normalMass;
                solverRow.frictionLimit = contact.frictionLimit;
                solverRow.point = ReplayContactPoint( *solverSample, contact );
                solverRow.normal = ReplayContactNormalForModel( contact, bodyRow.modelRow.value );
                solverRow.impulse = ReplayContactImpulseForModel( contact, bodyRow.modelRow.value );
                solverRow.terrain = terrain;
                solverRow.warmStarted = contact.warmStarted;
                sprintf_s( solverRow.name, sizeof( solverRow.name ), "Solver row %d", i );
                const char* traceStage = "";
                if ( solverRow.pipelineIndex >= 0 )
                {
                    const PhysicsPipelineRecord& record =
                        solverSample->worldSnapshot.physics
                            .pipelineTrace[static_cast<std::size_t>( solverRow.pipelineIndex )];
                    traceStage = PhysicsPipelineStageName( record.stage );
                }
                sprintf_s( solverRow.detail,
                           sizeof( solverRow.detail ),
                           "feature %u  n %.3f  t %.3f  bias %.3f  mass %.3f  limit %.3f  %s%s%s",
                           contact.featureId,
                           solverRow.normalImpulse,
                           solverRow.tangentImpulse,
                           solverRow.bias,
                           solverRow.effectiveMass,
                           solverRow.frictionLimit,
                           contact.warmStarted ? "warm" : "cold",
                           solverRow.pipelineIndex >= 0 ? "  " : "",
                           traceStage );
                if ( !appendCauseTreeRow( solverRow ) )
                {
                    return;
                }
            }
        }
    };

    auto addBodyRow = [&]( ReplayBodyId id,
                           ReplayBodyId parentId,
                           ReplayFrameIndex firstFrame,
                           int depth,
                           int modelIndex,
                           const char* fallbackName ) -> bool
    {
        if ( id.value == 0 )
        {
            return false;
        }

        RunReplayCauseTreeRow row;
        row.kind = RunReplayCauseTreeRowKind::Body;
        row.id = id;
        row.parentId = parentId;
        row.firstFrame = firstFrame;
        row.depth = depth;
        row.modelRow.value = modelIndexForId( id, modelIndex );
        row.prediction = usePrediction;
        writeName( id, row.modelRow.value, fallbackName, row.name, sizeof( row.name ) );
        if ( usePrediction && firstFrame > 0 )
        {
            sprintf_s( row.detail,
                       sizeof( row.detail ),
                       "first affected frame %llu",
                       static_cast<unsigned long long>( firstFrame ) );
        }
        else if ( row.modelRow.value >= 0 && solverSample )
        {
            if ( const ReplaySolverBodySample* body = FindReplayBodyByModelIndex( *solverSample, row.modelRow.value ) )
            {
                sprintf_s( row.detail,
                           sizeof( row.detail ),
                           "contacts %u  max pen %.3f  impulse %.3f",
                           static_cast<unsigned int>( body->contactCount ),
                           body->maxPenetration,
                           body->normalImpulseSum );
            }
        }
        else if ( firstFrame > 0 )
        {
            sprintf_s( row.detail,
                       sizeof( row.detail ),
                       "first affected frame %llu",
                       static_cast<unsigned long long>( firstFrame ) );
        }
        if ( !appendCauseTreeRow( row ) )
        {
            return false;
        }
        appendSolverRowsForBody( row );
        return !rowOverflow;
    };

    if ( !addBodyRow( path.targetId, ReplayBodyId{}, 0, 0, path.targetModelRow.value, path.targetName ) )
    {
        FailCauseTreeRowBuild();
        return false;
    }

    auto addChildren = [&]( auto&& self, ReplayBodyId parentId, int fallbackDepth ) -> void
    {
        for ( const RunReplayPathTraceNode& node : nodes )
        {
            if ( node.parentId.value != parentId.value )
            {
                continue;
            }
            const int depth = node.depth > 0 ? node.depth : fallbackDepth;
            if ( addBodyRow( node.id,
                             parentId,
                             node.firstFrame,
                             depth,
                             modelIndexForId( node.id, node.modelRow.value ),
                             nullptr ) )
            {
                self( self, node.id, depth + 1 );
            }
        }
    };
    addChildren( addChildren, path.targetId, 1 );
    if ( rowOverflow )
    {
        FailCauseTreeRowBuild();
        return false;
    }

    SetCauseTreeSelectedRow( -1 );
    const RunReplayCauseTreeState& causeTree = CauseTree();
    if ( camera.focusKind != RunReplayCameraFocusKind::None )
    {
        for ( int i = 0; i < static_cast<int>( causeTree.rows.size() ); ++i )
        {
            const RunReplayCauseTreeRow& row = causeTree.rows[static_cast<std::size_t>( i )];
            if ( row.kind != camera.focusRowKind || row.id.value != camera.focusedId.value ||
                 row.modelRow.value != camera.focusModelRow.value || row.terrain != camera.focusTerrain )
            {
                continue;
            }
            if ( row.kind == RunReplayCauseTreeRowKind::Body ||
                 ( row.counterpartId.value == camera.counterpartId.value &&
                   row.counterpartModelRow.value == camera.focusCounterpartModelRow.value &&
                   ( row.kind != RunReplayCauseTreeRowKind::SolverRow ||
                     ( row.featureId == camera.focusFeatureId &&
                       row.solverRowIndex == camera.focusSolverRowIndex ) ) ) )
            {
                SetCauseTreeSelectedRow( i );
                outCameraFocusedRow = i;
                break;
            }
        }
    }
    if ( CauseTree().selectedRow >= static_cast<int>( CauseTree().rows.size() ) )
    {
        SetCauseTreeSelectedRow( -1 );
    }
    return !CauseTree().rows.empty();
}


void ReplayAuthoring::BeginCauseTreeInputFrame() noexcept
{
    m_causeTree.pointerBlocked = true;
}


void ReplayAuthoring::EnsureCauseTreeWindowPlacement( int screenWidth, int screenHeight ) noexcept
{
    EnsureReplayCauseWindowPlacement( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::SetCauseTreePointer( int mouseX, int mouseY, bool blocked ) noexcept
{
    m_causeTree.mouseX = mouseX;
    m_causeTree.mouseY = mouseY;
    m_causeTree.pointerBlocked = blocked;
}


void ReplayAuthoring::MoveCauseTreeWindow( int mouseX, int mouseY, int screenWidth, int screenHeight ) noexcept
{
    m_causeTree.x = mouseX - m_causeTree.dragOffsetX;
    m_causeTree.y = mouseY - m_causeTree.dragOffsetY;
    ClampReplayCauseWindow( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::ResizeCauseTreeWindow( int mouseX, int mouseY, int screenWidth, int screenHeight ) noexcept
{
    m_causeTree.width = m_causeTree.resizeStartWidth + ( mouseX - m_causeTree.resizeStartMouseX );
    m_causeTree.height = m_causeTree.resizeStartHeight + ( mouseY - m_causeTree.resizeStartMouseY );
    ClampReplayCauseWindow( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::ScrollCauseTreeWindow( float delta, int screenWidth, int screenHeight ) noexcept
{
    m_causeTree.scrollY += delta;
    ClampReplayCauseWindow( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::BeginCauseTreeResize( int mouseX, int mouseY ) noexcept
{
    m_causeTree.resizeStartMouseX = mouseX;
    m_causeTree.resizeStartMouseY = mouseY;
    m_causeTree.resizeStartWidth = m_causeTree.width;
    m_causeTree.resizeStartHeight = m_causeTree.height;
}


void ReplayAuthoring::BeginCauseTreeMove( int mouseX, int mouseY ) noexcept
{
    m_causeTree.dragOffsetX = mouseX - m_causeTree.x;
    m_causeTree.dragOffsetY = mouseY - m_causeTree.y;
}


bool ReplayAuthoring::TryGetCauseTreeRow( int rowIndex, RunReplayCauseTreeRow& outRow ) const noexcept
{
    if ( rowIndex < 0 || rowIndex >= static_cast<int>( m_causeTree.rows.size() ) )
    {
        return false;
    }
    outRow = m_causeTree.rows[static_cast<std::size_t>( rowIndex )];
    return true;
}


void ReplayAuthoring::SetCauseTreeFocus( int rowIndex, ReplayBodyId focusedId ) noexcept
{
    m_causeTree.selectedRow = rowIndex;
    m_causeTree.focusedId = focusedId;
}


bool ReplayAuthoring::TickCauseTreeInput( ReplayPresentation& presentationOwner,
                                          ReplayScrubber& scrubberOwner,
                                          const RunReplayPredictionState& prediction,
                                          std::span<const RunReplayPredictionFrame> activePredictionFrames,
                                          const ReplaySolverFrameSample* currentSolverSample,
                                          bool uiBlocksMouse,
                                          int wheelDelta,
                                          InputRouter& inputRouter,
                                          RuntimeInteractionController& interaction,
                                          const PhysicsBodyStore& bodyStore,
                                          const ColliderStore& colliderStore,
                                          std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                          Environment::CameraCollection* cameras,
                                          Geometry::Terrain* terrain,
                                          RunCameraState& camera,
                                          RunMousePickupState& mousePickup,
                                          RunCameraMode normalizedCurrentMode,
                                          RunCameraMode normalizedRestoreMode,
                                          bool attachedFollow,
                                          bool directorGrabbed,
                                          bool editorModeEnabled,
                                          int screenWidth,
                                          int screenHeight,
                                          bool& outEnterInteractive )
{
    InputRouter& m_inputRouter = inputRouter;
    RuntimeInteractionController& m_interaction = interaction;
    const auto enterInspectionCamera = [&]()
    {
        ReplayPresentationOperations::EnterInspectionCamera( presentationOwner,
                                                             cameras,
                                                             camera,
                                                             normalizedCurrentMode,
                                                             m_interaction,
                                                             m_inputRouter,
                                                             mousePickup );
    };
    const auto exitInspectionCamera = [&]()
    {
        ReplayPresentationOperations::ExitInspectionCamera( presentationOwner,
                                                            *this,
                                                            cameras,
                                                            terrain,
                                                            camera,
                                                            normalizedRestoreMode,
                                                            attachedFollow,
                                                            directorGrabbed,
                                                            m_interaction,
                                                            m_inputRouter );
    };
    PROFILE_SCOPED( m_profiler, "Frame/Replay/CauseTree/Input" );
    // Concept: Cause-tree input owns the explanatory replay window state while
    // body focus resolves from explicit prediction, solver, and live-store
    // views captured for this input turn.
    const RuntimeMouseEdges& pointer = m_inputRouter.UiSnapshot().mouse;
    const bool leftPressed = pointer.leftPressed;
    const bool leftReleased = pointer.leftReleased;
    BeginCauseTreeInputFrame();
    const ReplayBodyId pathTargetId = presentationOwner.PathVisualizer().targetId;
    const auto resolveCauseTreeBody = [&]( ReplayBodyId id, Vector3& outPosition, float* outRadius )
    {
        return ResolveReplayCauseTreeBodyPosition( id,
                                                   prediction.enabled,
                                                   prediction.simulation.targetId,
                                                   pathTargetId,
                                                   activePredictionFrames,
                                                   currentSolverSample,
                                                   bodyStore,
                                                   colliderStore,
                                                   outPosition,
                                                   outRadius );
    };

    const auto activateReplayCameraForCauseRow = [&]( const RunReplayCauseTreeRow& row, int rowIndex )
    {
        PROFILE_SCOPED( m_profiler, "Frame/Replay/CauseTree/Focus" );
        Vector3 targetPosition = row.point;
        float targetRadius = 2.0f;
        RunReplayCameraFocusKind focusKind = RunReplayCameraFocusKind::Body;
        // Lifetime: replay focus borrows already-prepared physics store views
        // for one UI action. Topology repair belongs to the runtime/frame
        // boundary, not this read-only cause-tree lookup.
        switch ( row.kind )
        {
        case RunReplayCauseTreeRowKind::Body:
        {
            const bool bodyResolved = resolveCauseTreeBody( row.id, targetPosition, &targetRadius );
            if ( !bodyResolved )
            {
                return;
            }
            focusKind = RunReplayCameraFocusKind::Body;
            break;
        }
        case RunReplayCauseTreeRowKind::Manifold:
            resolveCauseTreeBody( row.id, targetPosition, &targetRadius );
            targetPosition = row.point;
            targetRadius = (std::max)( targetRadius * 0.55f, 2.0f );
            focusKind = RunReplayCameraFocusKind::Manifold;
            break;
        case RunReplayCauseTreeRowKind::SolverRow:
            resolveCauseTreeBody( row.id, targetPosition, &targetRadius );
            targetPosition = row.point;
            targetRadius = (std::max)( targetRadius * 0.45f, 1.5f );
            focusKind = RunReplayCameraFocusKind::SolverRow;
            break;
        case RunReplayCauseTreeRowKind::PredictionContact:
        case RunReplayCauseTreeRowKind::PredictionMotion:
            resolveCauseTreeBody( row.id, targetPosition, &targetRadius );
            targetPosition = row.point;
            targetRadius = (std::max)( targetRadius * 0.45f, 1.5f );
            focusKind = row.kind == RunReplayCauseTreeRowKind::PredictionContact
                            ? RunReplayCameraFocusKind::PredictionContact
                            : RunReplayCameraFocusKind::PredictionMotion;
            break;
        default:
            return;
        }

        if ( VectorMagSquared( targetPosition ) <= TOLERANCE * TOLERANCE &&
             row.kind != RunReplayCauseTreeRowKind::Body )
        {
            return;
        }

        outEnterInteractive = true;
        const bool hadReplayCameraFocus = presentationOwner.CameraView().focusKind != RunReplayCameraFocusKind::None;
        if ( !scrubberOwner.View().liveAdvanceHeld )
        {
            if ( scrubberOwner.SetLiveAdvanceHeld( true ) && !IsReplayCauseTreeToolOwner( m_interaction.Owner() ) )
            {
                interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                 WorldInteractionOwner::ReplayScrub,
                                                                 InteractionExitReason::EnterReplay );
            }
            presentationOwner.SetCameraPauseOwnership( true );
        }
        else if ( !hadReplayCameraFocus )
        {
            presentationOwner.SetCameraPauseOwnership( false );
        }
        enterInspectionCamera();

        ReplayCameraFocusRequest focus;
        focus.focusKind = focusKind;
        focus.focusedId = row.id;
        focus.counterpartId = row.counterpartId;
        focus.focusedRow = rowIndex;
        focus.focusRowKind = row.kind;
        focus.focusModelRow = row.modelRow;
        focus.focusCounterpartModelRow = row.counterpartModelRow;
        focus.focusContactIndex = row.contactIndex;
        focus.focusSolverRowIndex = row.solverRowIndex;
        focus.focusFeatureId = row.featureId;
        focus.focusTerrain = row.terrain;
        focus.targetPoint = targetPosition;
        focus.targetNormal = ReplayCauseTreeNormalizeOr( row.normal, Vector3( 0.0f, 1.0f, 0.0f ) );
        focus.impulseVector = row.impulse;
        focus.targetRadius = targetRadius;
        presentationOwner.ApplyCameraFocus( focus );
        SetCauseTreeFocus( rowIndex, row.id );

        if ( cameras )
        {
            const Vector3 eye = cameras->GetRenderCameraTranslation();
            Vector3 direction = ReplayCauseTreeNormalizeOr( eye - targetPosition, Vector3( 0.45f, 0.28f, 0.85f ) );
            direction = ReplayCauseTreeNormalizeOr( direction, Vector3( 0.45f, 0.28f, 0.85f ) );
            const float distance = (std::max)( 12.0f, targetRadius * 5.5f );
            const Vector3 newEye = targetPosition + direction * distance + Vector3( 0.0f, targetRadius * 0.35f, 0.0f );
            cameras->TweenPrimaryToPose( newEye, targetPosition, cameras->GetRenderCameraUp() );
            cameras->ResetRelativity();
        }
        InputController::ResetMouseLook( camera );
        m_inputRouter.RequestCursorVisible( true );
    };

    const int screenW = screenWidth;
    const int screenH = screenHeight;
    const auto causeTreeDragMode = [&]()
    {
        const RuntimeInteractionGesture& gesture = m_interaction.Gesture();
        return gesture.kind == RuntimeInteractionGestureKind::ReplayCauseTreeDrag ? gesture.axis : -1;
    };
    const auto endCauseTreeDragIfReleased = [&]()
    {
        if ( leftReleased && causeTreeDragMode() >= 0 )
        {
            m_inputRouter.ReleaseNativeCapture();
            m_interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }
    };
    if ( editorModeEnabled || screenW <= 0 || screenH <= 0 )
    {
        endCauseTreeDragIfReleased();
        return false;
    }

    int focusedCameraRow = -1;
    if ( !BuildCauseTreeRows( presentationOwner.PathVisualizer(),
                              prediction,
                              activePredictionFrames,
                              currentSolverSample,
                              presentation,
                              bodyStore,
                              presentationOwner.CameraView(),
                              focusedCameraRow ) )
    {
        endCauseTreeDragIfReleased();
        return false;
    }
    if ( focusedCameraRow >= 0 )
    {
        presentationOwner.SetCameraFocusedRow( focusedCameraRow );
    }

    EnsureCauseTreeWindowPlacement( screenW, screenH );
    const RuntimePointerEvent& runtimePointer = m_inputRouter.RuntimeSnapshot().pointer;
    if ( !runtimePointer.hasClientPosition )
    {
        endCauseTreeDragIfReleased();
        return false;
    }
    const POINT mouse{ runtimePointer.clientX, runtimePointer.clientY };
    SetCauseTreePointer( mouse.x, mouse.y, uiBlocksMouse );
    ReplayCauseWindowSurface surface;
    BuildReplayCauseWindowSurface( CauseTree(), surface );
    surface.ResolvePointer( mouse.x, mouse.y, uiBlocksMouse );
    const auto isHotControl = [&]( ReplayCauseWindowControl control )
    { return surface.hasHotControl && surface.hotControl == ReplayCauseWindowControlId( control ); };
    const RuntimeUiControl* contentControl =
        surface.Find( ReplayCauseWindowControlId( ReplayCauseWindowControl::Content ) );
    if ( !contentControl )
    {
        SB_FATAL( "ReplayCauseWindowSurface", "Content control is missing from the cause-window surface." );
    }
    const UI::UIRect content = contentControl->drawRect;

    if ( causeTreeDragMode() == 0 )
    {
        MoveCauseTreeWindow( mouse.x, mouse.y, screenW, screenH );
        if ( leftReleased )
        {
            m_inputRouter.ReleaseNativeCapture();
            m_interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }
        return true;
    }

    if ( causeTreeDragMode() == 1 )
    {
        ResizeCauseTreeWindow( mouse.x, mouse.y, screenW, screenH );
        if ( leftReleased )
        {
            m_inputRouter.ReleaseNativeCapture();
            m_interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }
        return true;
    }

    if ( uiBlocksMouse || !surface.consumesPointer )
    {
        return false;
    }

    if ( wheelDelta != 0 )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         WorldInteractionOwner::ReplayCauseTree,
                                                         InteractionExitReason::EnterReplay );
        const float wheelRows = static_cast<float>( wheelDelta ) / 120.0f;
        ScrollCauseTreeWindow( -wheelRows * REPLAY_CAUSE_WINDOW_ROW_HEIGHT * 3.0f, screenW, screenH );
        return true;
    }

    if ( leftPressed && isHotControl( ReplayCauseWindowControl::Resize ) )
    {
        RuntimeInteractionGesture gesture;
        gesture.kind = RuntimeInteractionGestureKind::ReplayCauseTreeDrag;
        gesture.button = RuntimePointerButton::Left;
        gesture.startX = mouse.x;
        gesture.startY = mouse.y;
        gesture.axis = 1;
        if ( !m_interaction.BeginOwnedToolGesture( RuntimeWorkspace::Replay,
                                                   WorldInteractionOwner::ReplayCauseTree,
                                                   gesture ) )
        {
            return false;
        }
        BeginCauseTreeResize( mouse.x, mouse.y );
        m_inputRouter.RequestNativeCapture();
        return true;
    }

    if ( leftPressed && isHotControl( ReplayCauseWindowControl::Title ) )
    {
        RuntimeInteractionGesture gesture;
        gesture.kind = RuntimeInteractionGestureKind::ReplayCauseTreeDrag;
        gesture.button = RuntimePointerButton::Left;
        gesture.startX = mouse.x;
        gesture.startY = mouse.y;
        gesture.axis = 0;
        if ( !m_interaction.BeginOwnedToolGesture( RuntimeWorkspace::Replay,
                                                   WorldInteractionOwner::ReplayCauseTree,
                                                   gesture ) )
        {
            return false;
        }
        BeginCauseTreeMove( mouse.x, mouse.y );
        m_inputRouter.RequestNativeCapture();
        return true;
    }

    if ( isHotControl( ReplayCauseWindowControl::Content ) )
    {
        const float localY = static_cast<float>( mouse.y ) - content.y + CauseTree().scrollY;
        const int rowIndex = static_cast<int>( floorf( localY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );
        RunReplayCauseTreeRow selectedRow;
        if ( TryGetCauseTreeRow( rowIndex, selectedRow ) )
        {
            if ( leftPressed )
            {
                interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                 WorldInteractionOwner::ReplayCauseTree,
                                                                 InteractionExitReason::EnterReplay );
                activateReplayCameraForCauseRow( selectedRow, rowIndex );
            }
        }
        else if ( leftPressed )
        {
            const bool ownedSimulationPause = presentationOwner.ClearCameraFocus();
            ClearCauseTreeFocus();
            const ReplayScrubberView scrubber = scrubberOwner.View();
            if ( ownedSimulationPause && scrubber.liveAdvanceHeld && !scrubber.historicalSamplePaused )
            {
                scrubberOwner.SetLiveAdvanceHeld( false );
            }
            presentationOwner.ClearPathState();
            ResetCauseTreeRows();
            QueuePredictionCacheReset();
            exitInspectionCamera();
        }
    }

    return true;
}
