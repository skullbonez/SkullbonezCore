/*
File: SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp
Purpose:
  Implements Prediction-owned cause-row composition and focus activation.

Summary:
  The cause tree is an explanatory replay UI over retained solver contacts and
  predicted movement. ReplayPrediction builds bounded rows into ReplayAuthoring
  storage, then activates a selected row through explicit solver, prediction,
  and live-store values without retaining a lower Replay owner.

Glossary:
  Focus row: Cause-tree row selected for replay inspection camera targeting.

Invariants:
  - Focus changes hold live replay advance so selected historical rows remain visible.
  - Row composition never grows storage on the input/render path.
  - Replay path, solver, camera, and store borrows expire before each command returns.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTreeInput.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "../Replay/ReplayAuthoring.h"
#include "../Replay/ReplayCoordination.h"
#include "ReplayPrediction.h"
#include "ReplayPredictionPublicationOperations.h"
#include "../Replay/ReplayPresentation.h"
#include "../../Assets/AssetKeys.h"
#include "../../Core/Profiler.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;
namespace Physics = SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::ReplayOverlay;
using namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations;

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

float ReplayCauseTreeColliderRadiusForBody( const ColliderStore& colliderStore, const PhysicsBodyRecord& body,
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
// Physics::PhysicsSceneObjectId is the identity check in every source.
// Invariant: this helper stores no view and never repairs topology while input
// is active. The frame boundary prepared paired body/collider stores first.
bool ResolveReplayCauseTreeBodyPosition( Physics::PhysicsSceneObjectId id, bool predictionEnabled,
                                         Physics::PhysicsSceneObjectId predictionTargetId,
                                         Physics::PhysicsSceneObjectId pathTargetId,
                                         std::span<const RunReplayPredictionFrame> activePredictionFrames,
                                         const ReplaySolverFrameSample* solverSample, const PhysicsBodyStore& bodyStore,
                                         const ColliderStore& colliderStore, Vector3& outPosition, float* outRadius )
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

        const PhysicsBodyHandle liveBody = bodyStore.HandleForSceneObjectId( id, modelRow.value );
        const PhysicsBodyRecord* liveBodyRecord = bodyStore.RecordForHandle( liveBody );
        *outRadius = liveBodyRecord ? ReplayCauseTreeColliderRadiusForBody( colliderStore, *liveBodyRecord, modelRow.value )
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

        if ( body.sceneObjectId == id )
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
Physics::PhysicsSceneObjectId SceneObjectIdForModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    return SceneObjectIdForModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, false>( sample, modelIndex );
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
    const Vector3 rowImpulse = contact.normal * contact.accN + contact.tangent1 * contact.accT1 +
                               contact.tangent2 * contact.accT2;

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

bool ReplayPrediction::BuildCauseTreeRows( ReplayAuthoring& authoring, const RunReplayPathVisualizerState& path,
                                           const ReplaySolverFrameSample* solverSample,
                                           std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                           const PhysicsBodyStore& bodyStore, const RunReplayCameraState& camera,
                                           int& outCameraFocusedRow )
{
    const RunReplayPredictionState& prediction = m_state;
    const std::span<const RunReplayPredictionFrame> activePredictionFrames = ActiveFrames();
    PROFILE_SCOPED( "Frame/Replay/CauseTree/BuildRows" );
    outCameraFocusedRow = -1;
    authoring.BeginCauseTreeRowBuild();

    if ( !path.hasTarget || path.targetId.value == 0 )
    {
        return false;
    }

    // Why: ActivePredictionFrames() waits for a coherent full buffer, while the
    // prediction overlay exposes a populated build prefix so long jobs are
    // visible immediately. The cause tree must use the same readiness rule.
    const bool predictionPrefixVisible = activePredictionFrames.size() >= 2 || prediction.HasPublishedBuildFramePrefix() ||
                                         !prediction.futureNodeCache.futureNodes.empty();

    const bool usePrediction = prediction.enabled && predictionPrefixVisible &&
                               prediction.simulation.targetId.value == path.targetId.value;

    static const std::vector<RunReplayPathTraceNode> EMPTY_PREDICTION_NODES;
    const std::vector<RunReplayPathTraceNode>& nodes = usePrediction ? prediction.futureNodeCache.futureNodes
                                                                     : EMPTY_PREDICTION_NODES;

    const std::size_t solverContactCount = solverSample ? solverSample->worldSnapshot.physics.persistentContacts.size()
                                                        : static_cast<std::size_t>( 0 );

    const std::size_t estimatedRows = 1 + nodes.size() + solverContactCount * 3;

    if ( !authoring.CauseTreeRowCapacityCovers( estimatedRows ) )
    {
        // Hazard: this path runs from input/render. If a future scene exceeds
        // the preallocated explanation budget, hide the overlay for the frame
        // instead of growing row storage on the hot path.
        authoring.SetCauseTreeSelectedRow( -1 );
        return false;
    }

    bool rowOverflow = false;
    auto appendCauseTreeRow = [&]( const RunReplayCauseTreeRow& row ) -> bool
    {
        if ( !authoring.AppendCauseTreeRow( row ) )
        {
            rowOverflow = true;

            return false;
        }

        return true;
    };

    // Invariant: cause-tree rows keep model indices only for UI row selection
    // and solver-artifact contact matching. Physics::PhysicsSceneObjectId identity resolves
    // through body-store handles first; solver samples are historical fallback.
    auto modelIndexForId = [&]( Physics::PhysicsSceneObjectId id, int preferredModelIndex ) -> int
    {
        const PhysicsBodyHandle body = bodyStore.HandleForSceneObjectId( id, preferredModelIndex );

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

    auto idForModelIndex = [&]( int modelIndex ) -> Physics::PhysicsSceneObjectId
    {
        Physics::PhysicsSceneObjectId id;

        if ( modelIndex < 0 )
        {
            return id;
        }

        if ( solverSample )
        {
            id = SceneObjectIdForModelIndex( *solverSample, modelIndex );

            if ( id.value != 0 )
            {
                return id;
            }
        }

        if ( const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex ) )
        {
            id = body->sceneObjectId;
        }

        return id;
    };

    auto writeName = [&]( Physics::PhysicsSceneObjectId id, int modelIndex, const char* fallback, char* out,
                          std::size_t outSize ) -> void
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
                    sprintf_s( contactRow.detail, sizeof( contactRow.detail ), "first frame %llu  normal %.2f %.2f %.2f",
                               static_cast<unsigned long long>( node.firstFrame ), contactRow.normal.x, contactRow.normal.y,
                               contactRow.normal.z );
                }
                else
                {
                    sprintf_s( contactRow.name, sizeof( contactRow.name ), "Predicted movement" );
                    sprintf_s( contactRow.detail, sizeof( contactRow.detail ),
                               "first affected frame %llu  direction %.2f %.2f %.2f",
                               static_cast<unsigned long long>( node.firstFrame ), contactRow.normal.x, contactRow.normal.y,
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

            for ( int i = 0; i < static_cast<int>( solverSample->worldSnapshot.physics.persistentContacts.size() ); ++i )
            {
                const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample&
                    contact = solverSample->worldSnapshot.physics.persistentContacts[static_cast<std::size_t>( i )];

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
            const Physics::PhysicsSceneObjectId otherId = idForModelIndex( group.otherModelIndex );

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
            sprintf_s( manifoldRow.detail, sizeof( manifoldRow.detail ), "%d point%s  max pen %.3f", pointCount,
                       pointCount == 1 ? "" : "s", maxPenetration );

            if ( !appendCauseTreeRow( manifoldRow ) )
            {
                return;
            }

            for ( int i = 0; i < static_cast<int>( solverSample->worldSnapshot.physics.persistentContacts.size() ); ++i )
            {
                const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample&
                    contact = solverSample->worldSnapshot.physics.persistentContacts[static_cast<std::size_t>( i )];

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
                solverRow.pipelineIndex = ReplayFindPipelineIndexForContact( solverSample->worldSnapshot.physics, contact );

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
                    const PhysicsPipelineRecord&
                        record = solverSample->worldSnapshot.physics
                                     .pipelineTrace[static_cast<std::size_t>( solverRow.pipelineIndex )];

                    traceStage = PhysicsPipelineStageName( record.stage );
                }

                sprintf_s( solverRow.detail, sizeof( solverRow.detail ),
                           "feature %u  n %.3f  t %.3f  bias %.3f  mass %.3f  limit %.3f  %s%s%s", contact.featureId,
                           solverRow.normalImpulse, solverRow.tangentImpulse, solverRow.bias, solverRow.effectiveMass,
                           solverRow.frictionLimit, contact.warmStarted ? "warm" : "cold",
                           solverRow.pipelineIndex >= 0 ? "  " : "", traceStage );

                if ( !appendCauseTreeRow( solverRow ) )
                {
                    return;
                }
            }
        }
    };

    auto addBodyRow = [&]( Physics::PhysicsSceneObjectId id, Physics::PhysicsSceneObjectId parentId,
                           ReplayFrameIndex firstFrame, int depth, int modelIndex, const char* fallbackName ) -> bool
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
            sprintf_s( row.detail, sizeof( row.detail ), "first affected frame %llu",
                       static_cast<unsigned long long>( firstFrame ) );
        }
        else if ( row.modelRow.value >= 0 && solverSample )
        {
            if ( const ReplaySolverBodySample* body = FindReplayBodyByModelIndex( *solverSample, row.modelRow.value ) )
            {
                sprintf_s( row.detail, sizeof( row.detail ), "contacts %u  max pen %.3f  impulse %.3f",
                           static_cast<unsigned int>( body->contactCount ), body->maxPenetration, body->normalImpulseSum );
            }
        }
        else if ( firstFrame > 0 )
        {
            sprintf_s( row.detail, sizeof( row.detail ), "first affected frame %llu",
                       static_cast<unsigned long long>( firstFrame ) );
        }

        if ( !appendCauseTreeRow( row ) )
        {
            return false;
        }

        appendSolverRowsForBody( row );
        return !rowOverflow;
    };

    if ( !addBodyRow( path.targetId, Physics::PhysicsSceneObjectId {}, 0, 0, path.targetModelRow.value, path.targetName ) )
    {
        authoring.FailCauseTreeRowBuild();
        return false;
    }

    auto addChildren = [&]( auto&& self, Physics::PhysicsSceneObjectId parentId, int fallbackDepth ) -> void
    {
        for ( const RunReplayPathTraceNode& node : nodes )
        {
            if ( node.parentId.value != parentId.value )
            {
                continue;
            }

            const int depth = node.depth > 0 ? node.depth : fallbackDepth;

            if ( addBodyRow( node.id, parentId, node.firstFrame, depth, modelIndexForId( node.id, node.modelRow.value ),
                             nullptr ) )
            {
                self( self, node.id, depth + 1 );
            }
        }
    };

    addChildren( addChildren, path.targetId, 1 );

    if ( rowOverflow )
    {
        authoring.FailCauseTreeRowBuild();
        return false;
    }

    authoring.SetCauseTreeSelectedRow( -1 );
    const RunReplayCauseTreeState& causeTree = authoring.CauseTree();

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
                     ( row.featureId == camera.focusFeatureId && row.solverRowIndex == camera.focusSolverRowIndex ) ) ) )
            {
                authoring.SetCauseTreeSelectedRow( i );
                outCameraFocusedRow = i;
                break;
            }
        }
    }

    if ( authoring.CauseTree().selectedRow >= static_cast<int>( authoring.CauseTree().rows.size() ) )
    {
        authoring.SetCauseTreeSelectedRow( -1 );
    }

    return !authoring.CauseTree().rows.empty();
}


bool ReplayPrediction::ActivateCauseTreeRow( ReplayAuthoring& authoring, int rowIndex, ReplayPresentation& presentationOwner,
                                             ReplayScrubber& scrubberOwner,
                                             const ReplaySolverFrameSample* currentSolverSample,
                                             const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                             RuntimeInteractionController& interaction, Vector3& outTargetPosition,
                                             float& outTargetRadius )
{
    const RunReplayPredictionState& prediction = m_state;
    const std::span<const RunReplayPredictionFrame> activePredictionFrames = ActiveFrames();
    RunReplayCauseTreeRow row;

    if ( !authoring.TryGetCauseTreeRow( rowIndex, row ) )
    {
        return false;
    }

    PROFILE_SCOPED( "Frame/Replay/CauseTree/Focus" );
    Vector3 targetPosition = row.point;
    float targetRadius = 2.0f;
    RunReplayCameraFocusKind focusKind = RunReplayCameraFocusKind::Body;
    const Physics::PhysicsSceneObjectId pathTargetId = presentationOwner.PathVisualizer().targetId;
    const auto resolveBody = [&]( Physics::PhysicsSceneObjectId id, Vector3& outPosition, float* outRadius )
    {
        return ResolveReplayCauseTreeBodyPosition( id, prediction.enabled, prediction.simulation.targetId, pathTargetId,
                                                   activePredictionFrames, currentSolverSample, bodyStore, colliderStore,
                                                   outPosition, outRadius );
    };

    // Lifetime: replay focus borrows already-prepared physics store views for
    // this one selection. No source reference is retained by ReplayAuthoring.
    switch ( row.kind )
    {
    case RunReplayCauseTreeRowKind::Body:

        if ( !resolveBody( row.id, targetPosition, &targetRadius ) )
        {
            return false;
        }

        focusKind = RunReplayCameraFocusKind::Body;
        break;
    case RunReplayCauseTreeRowKind::Manifold:
        resolveBody( row.id, targetPosition, &targetRadius );
        targetPosition = row.point;
        targetRadius = (std::max)( targetRadius * 0.55f, 2.0f );
        focusKind = RunReplayCameraFocusKind::Manifold;
        break;
    case RunReplayCauseTreeRowKind::SolverRow:
        resolveBody( row.id, targetPosition, &targetRadius );
        targetPosition = row.point;
        targetRadius = (std::max)( targetRadius * 0.45f, 1.5f );
        focusKind = RunReplayCameraFocusKind::SolverRow;
        break;
    case RunReplayCauseTreeRowKind::PredictionContact:
    case RunReplayCauseTreeRowKind::PredictionMotion:
        resolveBody( row.id, targetPosition, &targetRadius );
        targetPosition = row.point;
        targetRadius = (std::max)( targetRadius * 0.45f, 1.5f );
        focusKind = row.kind == RunReplayCauseTreeRowKind::PredictionContact ? RunReplayCameraFocusKind::PredictionContact
                                                                             : RunReplayCameraFocusKind::PredictionMotion;

        break;
    default:
        return false;
    }

    if ( VectorMagSquared( targetPosition ) <= TOLERANCE * TOLERANCE && row.kind != RunReplayCauseTreeRowKind::Body )
    {
        return false;
    }

    const bool hadReplayCameraFocus = presentationOwner.CameraView().focusKind != RunReplayCameraFocusKind::None;

    if ( !scrubberOwner.View().liveAdvanceHeld )
    {
        if ( scrubberOwner.SetLiveAdvanceHeld( true ) && !IsReplayCauseTreeToolOwner( interaction.Owner() ) )
        {
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub,
                                                             InteractionExitReason::EnterReplay );
        }

        presentationOwner.SetCameraPauseOwnership( true );
    }
    else if ( !hadReplayCameraFocus )
    {
        presentationOwner.SetCameraPauseOwnership( false );
    }

    presentationOwner.ApplyCameraFocus( row, rowIndex, focusKind, targetPosition,
                                        ReplayCauseTreeNormalizeOr( row.normal, Vector3( 0.0f, 1.0f, 0.0f ) ),
                                        targetRadius );

    authoring.SetCauseTreeFocus( rowIndex, row.id );
    outTargetPosition = targetPosition;
    outTargetRadius = targetRadius;
    return true;
}
