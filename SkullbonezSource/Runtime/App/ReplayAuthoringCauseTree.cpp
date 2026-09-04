/*
File: SkullbonezSource/Runtime/App/ReplayAuthoringCauseTree.cpp
Purpose:
  Implements Prediction-owned cause-row composition and focus activation.

Summary:
  The cause tree is an explanatory replay UI over exact recorded or predicted
  event frames. ReplayPrediction builds bounded rows into ReplayAuthoring
  storage, then activates a selected row through explicit solver, prediction,
  and live-store values without retaining a lower Replay owner. High detail
  adapts either source to the same body/manifold/solver-row grouping and stamps
  prediction rows with their immutable evidence identity. Low detail keeps the
  synthetic contact/motion projection. Body expansion visits each causal body
  once, producing a cycle-free spanning hierarchy rather than repeating both
  directions of the contact graph.

Glossary:
  Focus row: Cause-tree row selected for replay inspection camera targeting.

Invariants:
  - Focus changes hold live replay advance so selected historical rows remain visible.
  - Row composition never grows storage on the input/render path.
  - Recorded body expansion visits each model row at most once.
  - High prediction detail expands only from exact sealed evidence belonging
    to the coherent prefix currently presented by ReplayPrediction.
  - Low prediction rows never expose Manifold or SolverRow evidence.
  - Replay path, solver, camera, and store borrows expire before each command returns.

Related:
  - SkullbonezSource/Runtime/App/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTreeInput.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "../Replay/ReplayAuthoring.h"
#include "../Replay/ReplayCoordination.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "ReplayAuthoringCauseTree.h"
#include "../Prediction/ReplayPrediction.h"
#include "../Prediction/ReplayPredictionPublicationOperations.h"
#include "../Replay/ReplayPresentation.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../../Assets/AssetKeys.h"
#include "../../Core/Profiler.h"
#include "../../Core/SceneCapacity.h"
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
namespace Rendering = SkullbonezCore::Rendering;
using namespace SkullbonezCore::Runtime::ReplayOverlay;
using namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations;

namespace
{
constexpr uint64_t REPLAY_CAUSE_TREE_FINGERPRINT_OFFSET = 14695981039346656037ull;
constexpr uint64_t REPLAY_CAUSE_TREE_FINGERPRINT_PRIME = 1099511628211ull;

void MixReplayCauseTreeFingerprint( uint64_t& fingerprint, uint64_t value ) noexcept
{
    for ( int byteIndex = 0; byteIndex < 8; ++byteIndex )
    {
        fingerprint ^= static_cast<uint8_t>( value >> ( byteIndex * 8 ) );
        fingerprint *= REPLAY_CAUSE_TREE_FINGERPRINT_PRIME;
    }
}

void MixReplayCauseTreeTextFingerprint( uint64_t& fingerprint, const char* text, std::size_t capacity ) noexcept
{
    std::size_t index = 0;

    while ( index < capacity && text[index] != '\0' )
    {
        fingerprint ^= static_cast<uint8_t>( text[index] );
        fingerprint *= REPLAY_CAUSE_TREE_FINGERPRINT_PRIME;
        ++index;
    }

    MixReplayCauseTreeFingerprint( fingerprint, index );
}

uint64_t ReplayCauseTreeSourceFingerprint( const ReplayPrediction& predictionOwner, const RunReplayPathVisualizerState& path,
                                           const ReplaySolverFrameSample* solverSample,
                                           std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                           const PhysicsBodyStore& bodyStore ) noexcept
{
    const RunReplayPredictionState& prediction = predictionOwner.State();
    const ReplayPredictionPresentationView predictionView = predictionOwner.PresentationView();
    const std::span<const RunReplayPredictionFrame> presentedFrames = predictionView.timeline.frames;
    uint64_t fingerprint = REPLAY_CAUSE_TREE_FINGERPRINT_OFFSET;
    MixReplayCauseTreeFingerprint( fingerprint, path.hasTarget ? 1u : 0u );
    MixReplayCauseTreeFingerprint( fingerprint, path.targetId.value );
    MixReplayCauseTreeFingerprint( fingerprint, static_cast<uint32_t>( path.targetModelRow.value ) );
    MixReplayCauseTreeTextFingerprint( fingerprint, path.targetName, sizeof( path.targetName ) );
    MixReplayCauseTreeFingerprint( fingerprint, prediction.enabled ? 1u : 0u );
    MixReplayCauseTreeFingerprint( fingerprint, predictionView.timeline.complete ? 1u : 0u );
    MixReplayCauseTreeFingerprint( fingerprint, predictionView.topology.targetId.value );
    MixReplayCauseTreeFingerprint( fingerprint, predictionView.timeline.generation );
    MixReplayCauseTreeFingerprint( fingerprint, predictionView.topology.ragdollVisualsEnabled ? 1u : 0u );
    // Why: the presented frame tail and trajectory publication advance on
    // every prefix update, but cause rows change only when immutable causal
    // nodes are appended. Hashing the tail would rebuild the full hierarchy
    // every prediction frame in large scenes.
    MixReplayCauseTreeFingerprint( fingerprint, predictionView.topology.futureNodes.size() );
    MixReplayCauseTreeFingerprint( fingerprint, static_cast<uint8_t>( predictionOwner.DetailMode() ) );

    if ( prediction.enabled && !presentedFrames.empty() )
    {
        MixReplayCauseTreeFingerprint( fingerprint, presentedFrames.front().frameIndex );
    }
    else if ( !prediction.enabled && solverSample )
    {
        MixReplayCauseTreeFingerprint( fingerprint, solverSample->frameIndex );
        MixReplayCauseTreeFingerprint( fingerprint, solverSample->solverHash );
        MixReplayCauseTreeFingerprint( fingerprint, solverSample->presentationHash );
    }

    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    MixReplayCauseTreeFingerprint( fingerprint, bodyRecords.size() );

    for ( const PhysicsBodyRecord& body : bodyRecords )
    {
        MixReplayCauseTreeFingerprint( fingerprint, body.sceneObjectId.value );
    }

    MixReplayCauseTreeFingerprint( fingerprint, presentationRecords.size() );

    for ( const Rendering::RenderInstancePresentationRecord& presentation : presentationRecords )
    {
        MixReplayCauseTreeTextFingerprint( fingerprint, presentation.displayName, sizeof( presentation.displayName ) );
    }

    return fingerprint;
}

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

const RunReplayPredictionBodySample* FindPredictionBodyByModelIndex( const RunReplayPredictionFrame& frame, int modelIndex )
{
    const auto found = std::find_if( frame.bodies.begin(), frame.bodies.end(),
                                     [&]( const RunReplayPredictionBodySample& body )
                                     { return body.modelRow.value == modelIndex; } );
    return found != frame.bodies.end() ? &*found : nullptr;
}

Vector3 ReplayPredictionContactPoint( const RunReplayPredictionFrame& frame,
                                      const Physics::PhysicsSolverPersistentContactSample& contact )
{
    if ( const RunReplayPredictionBodySample* bodyA = FindPredictionBodyByModelIndex( frame, contact.bodyA ) )
    {
        return bodyA->position + contact.rA;
    }

    if ( const RunReplayPredictionBodySample* bodyB = FindPredictionBodyByModelIndex( frame, contact.bodyB ) )
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

bool ReplayPipelineRecordAnchorsContact( const PhysicsPipelineRecord& record,
                                         const Physics::PhysicsSolverPersistentContactSample& contact )
{
    switch ( record.stage )
    {
    case PhysicsPipelineStage::ManifoldRow:
    case PhysicsPipelineStage::WarmStart:
    case PhysicsPipelineStage::SolverIteration:
    case PhysicsPipelineStage::PositionCorrection:
    case PhysicsPipelineStage::CacheStore:
        break;
    default:
        return false;
    }

    return record.featureId == contact.featureId && ( ( record.bodyA == contact.bodyA && record.bodyB == contact.bodyB ) ||
                                                      ( record.bodyA == contact.bodyB && record.bodyB == contact.bodyA ) );
}

bool ResolveReplayCauseTreeCameraFocus( ReplayAuthoring& authoring, const RunReplayCameraState& camera,
                                        int& outCameraFocusedRow ) noexcept
{
    outCameraFocusedRow = -1;
    const RunReplayCauseTreeState& causeTree = authoring.CauseTree();

    if ( causeTree.rows.empty() )
    {
        return false;
    }

    authoring.SetCauseTreeSelectedRow( -1 );

    if ( camera.focusKind != RunReplayCameraFocusKind::None )
    {
        for ( int rowIndex = 0; rowIndex < static_cast<int>( causeTree.rows.size() ); ++rowIndex )
        {
            const RunReplayCauseTreeRow& row = causeTree.rows[static_cast<std::size_t>( rowIndex )];

            if ( row.kind != camera.focusRowKind || row.id.value != camera.focusedId.value ||
                 row.modelRow.value != camera.focusModelRow.value || row.terrain != camera.focusTerrain )
            {
                continue;
            }

            if ( row.kind == RunReplayCauseTreeRowKind::Body ||
                 ( row.counterpartId.value == camera.counterpartId.value &&
                   row.counterpartModelRow.value == camera.focusCounterpartModelRow.value &&
                   ( row.kind != RunReplayCauseTreeRowKind::SolverRow ||
                     ( row.featureId == camera.focusFeatureId && row.solverRowIndex == camera.focusSolverRowIndex ) ) &&
                   ( !row.prediction || !row.sourceHighDetail ||
                     ( camera.focusSourceHighDetail && row.sourceGeneration == camera.focusSourceGeneration &&
                       row.sourceBankEpoch == camera.focusSourceBankEpoch &&
                       row.sourceTopologyVersion == camera.focusSourceTopologyVersion &&
                       row.sourcePublicationVersion == camera.focusSourcePublicationVersion ) ) ) )
            {
                authoring.SetCauseTreeSelectedRow( rowIndex );
                outCameraFocusedRow = rowIndex;
                break;
            }
        }
    }

    return true;
}

class ReplayCauseTreeRowBuilder final
{
  public:
    ReplayCauseTreeRowBuilder( const ReplayPrediction& predictionOwner, ReplayAuthoring& authoring,
                               const ReplaySolverFrameSample* solverSample,
                               std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                               const PhysicsBodyStore& bodyStore,
                               std::span<const RunReplayPredictionFrame> activePredictionFrames,
                               std::span<const RunReplayPathTraceNode> nodes, bool usePrediction ) noexcept
        : predictionOwner( predictionOwner ), authoring( authoring ), solverSample( solverSample ),
          presentationRecords( presentationRecords ), bodyStore( bodyStore ),
          activePredictionFrames( activePredictionFrames ), nodes( nodes ), usePrediction( usePrediction )
    {
    }

    bool Build( const RunReplayPathVisualizerState& path )
    {
        const ReplayFrameIndex rootFrame = usePrediction ? ( activePredictionFrames.empty()
                                                                 ? 0
                                                                 : activePredictionFrames.front().frameIndex )
                                                         : ( solverSample ? solverSample->frameIndex : 0 );
        const int rootModelIndex = modelIndexForId( path.targetId, path.targetModelRow.value );

        if ( !usePrediction && rootModelIndex >= 0 && rootModelIndex < static_cast<int>( recordedBodyQueued.size() ) )
        {
            recordedBodyQueued[static_cast<std::size_t>( rootModelIndex )] = true;
        }

        if ( !addBodyRow( path.targetId, Physics::PhysicsSceneObjectId {}, rootFrame, 0, rootModelIndex, path.targetName ) )
        {
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

        while ( !usePrediction && recordedBodyWorkCursor < recordedBodyWorkCount && !rowOverflow )
        {
            const RecordedBodyWork work = recordedBodyWork[recordedBodyWorkCursor++];
            (void)addBodyRow( work.id, work.parentId, rootFrame, work.depth, work.modelIndex, nullptr );
        }

        return !rowOverflow;
    }

  private:
    struct RecordedBodyWork
    {
        Physics::PhysicsSceneObjectId id;
        Physics::PhysicsSceneObjectId parentId;
        int modelIndex = -1;
        int depth = 0;
    };

    bool appendCauseTreeRow( const RunReplayCauseTreeRow& row )
    {
        if ( authoring.AppendCauseTreeRow( row ) )
        {
            return true;
        }

        rowOverflow = true;
        return false;
    }

    int modelIndexForId( Physics::PhysicsSceneObjectId id, int preferredModelIndex ) const
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
    }

    Physics::PhysicsSceneObjectId idForModelIndex( int modelIndex ) const
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
    }

    void writeName( Physics::PhysicsSceneObjectId id, int modelIndex, const char* fallback, char* out,
                    std::size_t outSize ) const
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
    }

    bool appendPredictionSummaryRow( const RunReplayCauseTreeRow& bodyRow )
    {
        if ( !usePrediction )
        {
            return false;
        }

        const auto found = std::find_if( nodes.begin(), nodes.end(), [&]( const RunReplayPathTraceNode& node )
                                         { return node.id.value == bodyRow.id.value; } );
        const RunReplayPathTraceNode* predictionNode = found != nodes.end() ? &*found : nullptr;

        if ( predictionOwner.DetailMode() != ReplayPredictionDetailMode::Low &&
             ( !predictionNode || predictionNode->contactDerived ) )
        {
            return false;
        }

        if ( !predictionNode )
        {
            return true;
        }

        RunReplayCauseTreeRow contactRow;
        contactRow.kind = predictionNode->contactDerived ? RunReplayCauseTreeRowKind::PredictionContact
                                                         : RunReplayCauseTreeRowKind::PredictionMotion;
        contactRow.id = bodyRow.id;
        contactRow.parentId = predictionNode->parentId;
        contactRow.counterpartId = predictionNode->parentId;
        contactRow.firstFrame = predictionNode->firstFrame;
        contactRow.depth = bodyRow.depth + 1;
        contactRow.modelRow.value = bodyRow.modelRow.value;
        contactRow.counterpartModelRow.value = predictionNode->parentModelRow.value;
        contactRow.contactIndex = static_cast<int>( found - nodes.begin() );
        contactRow.prediction = true;
        contactRow.point = predictionNode->contactPoint;
        contactRow.normal = ReplayNormalizeOr( predictionNode->contactNormal, Vector3( 0.0f, 1.0f, 0.0f ) );

        const char* label = predictionNode->contactDerived ? "Predicted contact" : "Predicted movement";
        const char* detail = predictionNode->contactDerived ? "first frame %llu  normal %.2f %.2f %.2f"
                                                            : "first affected frame %llu  direction %.2f %.2f %.2f";
        sprintf_s( contactRow.name, sizeof( contactRow.name ), "%s", label );
        sprintf_s( contactRow.detail, sizeof( contactRow.detail ), detail,
                   static_cast<unsigned long long>( predictionNode->firstFrame ), contactRow.normal.x, contactRow.normal.y,
                   contactRow.normal.z );
        (void)appendCauseTreeRow( contactRow );
        return true;
    }

    void appendSolverRowsForBody( RunReplayCauseTreeRow bodyRow )
    {
        if ( appendPredictionSummaryRow( bodyRow ) )
        {
            return;
        }

        if ( bodyRow.modelRow.value < 0 )
        {
            return;
        }

        const RunReplayPredictionFrame* predictionFrame = nullptr;
        ReplayPredictionSolverEvidenceFrameView predictionEvidence;

        if ( usePrediction )
        {
            const auto foundFrame = std::find_if( activePredictionFrames.begin(), activePredictionFrames.end(),
                                                  [&]( const RunReplayPredictionFrame& frame )
                                                  { return frame.frameIndex == bodyRow.firstFrame; } );

            if ( foundFrame == activePredictionFrames.end() )
            {
                return;
            }

            predictionFrame = &*foundFrame;
            predictionEvidence = predictionOwner.SolverEvidenceForPresentedFrame( bodyRow.firstFrame );

            if ( !predictionEvidence.Valid() )
            {
                return;
            }
        }
        else if ( !solverSample )
        {
            return;
        }

        const std::size_t sourceContactCount = usePrediction ? predictionEvidence.ContactCount()
                                                             : solverSample->worldSnapshot.physics.persistentContacts.size();
        const std::size_t sourcePipelineCount = usePrediction ? predictionEvidence.PipelineCount()
                                                              : solverSample->worldSnapshot.physics.pipelineTrace.size();
        const auto sourceContactAt = [&]( std::size_t index ) -> const Physics::PhysicsSolverPersistentContactSample*
        {
            if ( usePrediction )
            {
                return predictionEvidence.Contact( index );
            }

            return index < solverSample->worldSnapshot.physics.persistentContacts.size()
                       ? &solverSample->worldSnapshot.physics.persistentContacts[index]
                       : nullptr;
        };
        const auto sourcePipelineAt = [&]( std::size_t index ) -> const PhysicsPipelineRecord*
        {
            if ( usePrediction )
            {
                return predictionEvidence.Pipeline( index );
            }

            return index < solverSample->worldSnapshot.physics.pipelineTrace.size()
                       ? &solverSample->worldSnapshot.physics.pipelineTrace[index]
                       : nullptr;
        };
        const auto sourceContactPoint = [&]( const Physics::PhysicsSolverPersistentContactSample& contact )
        {
            return usePrediction ? ReplayPredictionContactPoint( *predictionFrame, contact )
                                 : ReplayContactPoint( *solverSample, contact );
        };
        const auto sourceIdForModelIndex = [&]( int modelIndex ) -> Physics::PhysicsSceneObjectId
        {
            if ( usePrediction )
            {
                const RunReplayPredictionBodySample* body = FindPredictionBodyByModelIndex( *predictionFrame, modelIndex );
                return body ? body->id : Physics::PhysicsSceneObjectId {};
            }

            return idForModelIndex( modelIndex );
        };
        const auto sourceModelIndexForId = [&]( Physics::PhysicsSceneObjectId id )
        {
            if ( usePrediction )
            {
                const auto body = std::find_if( predictionFrame->bodies.begin(), predictionFrame->bodies.end(),
                                                [&]( const RunReplayPredictionBodySample& candidate )
                                                { return candidate.id == id; } );
                return body != predictionFrame->bodies.end() ? body->modelRow.value : -1;
            }

            return modelIndexForId( id, -1 );
        };
        const auto sourcePipelineIndexForContact = [&]( const Physics::PhysicsSolverPersistentContactSample& contact )
        {
            for ( std::size_t index = 0; index < sourcePipelineCount; ++index )
            {
                const PhysicsPipelineRecord* record = sourcePipelineAt( index );

                if ( record && ReplayPipelineRecordAnchorsContact( *record, contact ) )
                {
                    return static_cast<int>( index );
                }
            }

            return -1;
        };

        struct ManifoldGroup
        {
            int otherModelIndex = -1;
            bool terrain = false;
        };

        std::array<ManifoldGroup, REPLAY_CAUSE_TREE_CONTACT_CAPACITY> groups = {};

        std::size_t groupCount = 0;

        for ( std::size_t contactIndex = 0; contactIndex < sourceContactCount; ++contactIndex )
        {
            const Physics::PhysicsSolverPersistentContactSample* contact = sourceContactAt( contactIndex );

            if ( !contact || !ReplayContactHasModelIndex( *contact, bodyRow.modelRow.value ) )
            {
                continue;
            }

            const int otherModelIndex = ReplayContactOtherModelIndex( *contact, bodyRow.modelRow.value );
            const bool terrain = contact->isTerrain || otherModelIndex < 0;
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

        // Invariant: the manifold on the causal incoming edge is the first
        // explanation below a predicted child. Stable encounter order is
        // preserved for every simultaneous secondary manifold.
        const int causalParentModelIndex = sourceModelIndexForId( bodyRow.parentId );

        for ( std::size_t groupIndex = 1; groupIndex < groupCount; ++groupIndex )
        {
            if ( groups[groupIndex].otherModelIndex == causalParentModelIndex && !groups[groupIndex].terrain )
            {
                std::swap( groups[0], groups[groupIndex] );
                break;
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

            for ( std::size_t contactIndex = 0; contactIndex < sourceContactCount; ++contactIndex )
            {
                const Physics::PhysicsSolverPersistentContactSample* contact = sourceContactAt( contactIndex );

                if ( !contact || !ReplayContactHasModelIndex( *contact, bodyRow.modelRow.value ) )
                {
                    continue;
                }

                const int otherModelIndex = ReplayContactOtherModelIndex( *contact, bodyRow.modelRow.value );
                const bool terrain = contact->isTerrain || otherModelIndex < 0;

                if ( otherModelIndex != group.otherModelIndex || terrain != group.terrain )
                {
                    continue;
                }

                const Vector3 point = sourceContactPoint( *contact );
                centroid += point;
                normalSum += ReplayContactNormalForModel( *contact, bodyRow.modelRow.value );
                maxPenetration = (std::max)( maxPenetration, contact->penetration );
                pointCount += 1;

                if ( firstContactIndex < 0 )
                {
                    firstContactIndex = static_cast<int>( contactIndex );
                    firstFeatureId = contact->featureId;
                }
            }

            if ( pointCount <= 0 )
            {
                continue;
            }

            centroid /= static_cast<float>( pointCount );
            const Physics::PhysicsSceneObjectId otherId = sourceIdForModelIndex( group.otherModelIndex );

            if ( !group.terrain && group.otherModelIndex >= 0 &&
                 group.otherModelIndex < static_cast<int>( recordedBodyQueued.size() ) &&
                 recordedBodyQueued[static_cast<std::size_t>( group.otherModelIndex )] )
            {
                continue;
            }

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
            manifoldRow.firstFrame = bodyRow.firstFrame;
            manifoldRow.depth = bodyRow.depth + 1;
            manifoldRow.modelRow.value = bodyRow.modelRow.value;
            manifoldRow.counterpartModelRow.value = group.otherModelIndex;
            manifoldRow.contactIndex = firstContactIndex;
            const Physics::PhysicsSolverPersistentContactSample* firstContact = sourceContactAt(
                static_cast<std::size_t>( firstContactIndex ) );
            manifoldRow.pipelineIndex = firstContact ? sourcePipelineIndexForContact( *firstContact ) : -1;

            if ( usePrediction && manifoldRow.pipelineIndex < 0 )
            {
                continue;
            }

            manifoldRow.featureId = static_cast<int>( firstFeatureId );
            manifoldRow.manifoldPointCount = pointCount;
            manifoldRow.penetration = maxPenetration;
            manifoldRow.point = centroid;
            manifoldRow.normal = ReplayNormalizeOr( normalSum, Vector3( 0.0f, 1.0f, 0.0f ) );
            manifoldRow.prediction = usePrediction;
            manifoldRow.terrain = group.terrain;

            if ( usePrediction )
            {
                const ReplayPredictionEvidenceIdentity& identity = predictionEvidence.frame->identity;
                manifoldRow.sourceGeneration = identity.generation;
                manifoldRow.sourceBankEpoch = identity.bankEpoch;
                manifoldRow.sourceTopologyVersion = identity.topologyVersion;
                manifoldRow.sourcePublicationVersion = identity.publicationVersion;
                manifoldRow.sourceHighDetail = true;
            }

            sprintf_s( manifoldRow.name, sizeof( manifoldRow.name ), "Manifold vs %s", otherName );
            sprintf_s( manifoldRow.detail, sizeof( manifoldRow.detail ), "%d point%s  max pen %.3f", pointCount,
                       pointCount == 1 ? "" : "s", maxPenetration );

            if ( !appendCauseTreeRow( manifoldRow ) )
            {
                return;
            }

            for ( std::size_t contactIndex = 0; contactIndex < sourceContactCount; ++contactIndex )
            {
                const Physics::PhysicsSolverPersistentContactSample* contact = sourceContactAt( contactIndex );

                if ( !contact || !ReplayContactHasModelIndex( *contact, bodyRow.modelRow.value ) )
                {
                    continue;
                }

                const int otherModelIndex = ReplayContactOtherModelIndex( *contact, bodyRow.modelRow.value );
                const bool terrain = contact->isTerrain || otherModelIndex < 0;

                if ( otherModelIndex != group.otherModelIndex || terrain != group.terrain )
                {
                    continue;
                }

                RunReplayCauseTreeRow solverRow;
                solverRow.kind = RunReplayCauseTreeRowKind::SolverRow;
                solverRow.id = bodyRow.id;
                solverRow.parentId = bodyRow.parentId;
                solverRow.counterpartId = otherId;
                solverRow.firstFrame = bodyRow.firstFrame;
                solverRow.depth = bodyRow.depth + 2;
                solverRow.modelRow.value = bodyRow.modelRow.value;
                solverRow.counterpartModelRow.value = group.otherModelIndex;
                solverRow.contactIndex = static_cast<int>( contactIndex );
                solverRow.solverRowIndex = static_cast<int>( contactIndex );
                solverRow.pipelineIndex = sourcePipelineIndexForContact( *contact );

                solverRow.featureId = static_cast<int>( contact->featureId );
                solverRow.manifoldPointCount = contact->manifoldPointCount;
                solverRow.penetration = contact->penetration;
                solverRow.normalImpulse = contact->accN;
                solverRow.tangentImpulse = sqrtf( contact->accT1 * contact->accT1 + contact->accT2 * contact->accT2 );
                solverRow.warmStartImpulse = contact->terrainWarmStart;
                solverRow.bias = contact->bias;
                solverRow.effectiveMass = contact->normalMass;
                solverRow.frictionLimit = contact->frictionLimit;
                solverRow.point = sourceContactPoint( *contact );
                solverRow.normal = ReplayContactNormalForModel( *contact, bodyRow.modelRow.value );
                solverRow.impulse = ReplayContactImpulseForModel( *contact, bodyRow.modelRow.value );
                solverRow.prediction = usePrediction;
                solverRow.terrain = terrain;
                solverRow.warmStarted = contact->warmStarted;

                if ( usePrediction )
                {
                    const ReplayPredictionEvidenceIdentity& identity = predictionEvidence.frame->identity;
                    solverRow.sourceGeneration = identity.generation;
                    solverRow.sourceBankEpoch = identity.bankEpoch;
                    solverRow.sourceTopologyVersion = identity.topologyVersion;
                    solverRow.sourcePublicationVersion = identity.publicationVersion;
                    solverRow.sourceHighDetail = true;
                }

                sprintf_s( solverRow.name, sizeof( solverRow.name ), "Solver row %zu", contactIndex );
                const char* traceStage = "";

                if ( solverRow.pipelineIndex >= 0 )
                {
                    const PhysicsPipelineRecord* record = sourcePipelineAt(
                        static_cast<std::size_t>( solverRow.pipelineIndex ) );

                    traceStage = record ? PhysicsPipelineStageName( record->stage ) : "";
                }

                sprintf_s( solverRow.detail, sizeof( solverRow.detail ),
                           "feature %u  n %.3f  t %.3f  bias %.3f  mass %.3f  limit %.3f  %s%s%s", contact->featureId,
                           solverRow.normalImpulse, solverRow.tangentImpulse, solverRow.bias, solverRow.effectiveMass,
                           solverRow.frictionLimit, contact->warmStarted ? "warm" : "cold",
                           solverRow.pipelineIndex >= 0 ? "  " : "", traceStage );

                if ( !appendCauseTreeRow( solverRow ) )
                {
                    return;
                }
            }

            if ( !usePrediction && !group.terrain && otherId.value != 0 && group.otherModelIndex >= 0 &&
                 group.otherModelIndex < static_cast<int>( recordedBodyQueued.size() ) )
            {
                if ( recordedBodyWorkCount >= recordedBodyWork.size() )
                {
                    rowOverflow = true;
                    return;
                }

                // Invariant: a recorded contact graph may contain cycles.
                // Queue each model row once so counterpart expansion produces
                // a bounded spanning hierarchy instead of A -> B -> A loops.
                recordedBodyQueued[static_cast<std::size_t>( group.otherModelIndex )] = true;
                recordedBodyWork[recordedBodyWorkCount++] = {
                    otherId,
                    bodyRow.id,
                    group.otherModelIndex,
                    bodyRow.depth + 2,
                };
            }
        }
    }

    bool addBodyRow( Physics::PhysicsSceneObjectId id, Physics::PhysicsSceneObjectId parentId, ReplayFrameIndex firstFrame,
                     int depth, int modelIndex, const char* fallbackName )
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
    }

    const ReplayPrediction& predictionOwner;
    ReplayAuthoring& authoring;
    const ReplaySolverFrameSample* solverSample;
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords;
    const PhysicsBodyStore& bodyStore;
    std::span<const RunReplayPredictionFrame> activePredictionFrames;
    std::span<const RunReplayPathTraceNode> nodes;
    bool usePrediction = false;
    bool rowOverflow = false;
    std::array<RecordedBodyWork, static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )>
        recordedBodyWork = {};
    std::array<bool, static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )> recordedBodyQueued = {};
    std::size_t recordedBodyWorkCount = 0;
    std::size_t recordedBodyWorkCursor = 0;
};

} // namespace

bool SkullbonezCore::Runtime::BuildReplayCauseTreeRows(
    const ReplayPrediction& predictionOwner, ReplayAuthoring& authoring, const RunReplayPathVisualizerState& path,
    const ReplaySolverFrameSample* solverSample,
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords, const PhysicsBodyStore& bodyStore,
    const RunReplayCameraState& camera, int& outCameraFocusedRow )
{
    const uint64_t sourceFingerprint = ReplayCauseTreeSourceFingerprint( predictionOwner, path, solverSample,
                                                                         presentationRecords, bodyStore );

    if ( authoring.CauseTreeRowsMatchSource( sourceFingerprint ) )
    {
        return ResolveReplayCauseTreeCameraFocus( authoring, camera, outCameraFocusedRow );
    }

    const RunReplayPredictionState& prediction = predictionOwner.State();
    const ReplayPredictionPresentationView predictionView = predictionOwner.PresentationView();
    const std::span<const RunReplayPredictionFrame> presentedPredictionFrames = predictionView.timeline.frames;
    PROFILE_SCOPED( "Frame/Replay/CauseTree/BuildRows" );
    outCameraFocusedRow = -1;
    authoring.BeginCauseTreeRowBuild( sourceFingerprint );

    if ( !path.hasTarget || path.targetId.value == 0 )
    {
        return false;
    }

    const bool usePrediction = prediction.enabled;

    if ( usePrediction && !ReplayPredictionCauseWindowAvailable( predictionOwner.DetailMode(), true ) )
    {
        return false;
    }

    const bool predictionPrefixMatchesPath = predictionView.topology.targetId.value == path.targetId.value;
    const std::span<const RunReplayPredictionFrame>
        activePredictionFrames = predictionPrefixMatchesPath ? presentedPredictionFrames
                                                             : std::span<const RunReplayPredictionFrame> {};
    const std::span<const RunReplayPathTraceNode> nodes = usePrediction && predictionPrefixMatchesPath &&
                                                                  predictionView.topology.cacheValid
                                                              ? predictionView.topology.futureNodes
                                                              : std::span<const RunReplayPathTraceNode> {};
    const std::size_t solverContactCount = solverSample ? solverSample->worldSnapshot.physics.persistentContacts.size()
                                                        : static_cast<std::size_t>( 0 );
    const std::size_t estimatedRows = usePrediction && predictionOwner.DetailMode() == ReplayPredictionDetailMode::High
                                          ? REPLAY_CAUSE_TREE_ROW_CAPACITY
                                          : 1 + nodes.size() + solverContactCount * 3;

    if ( !authoring.CauseTreeRowCapacityCovers( estimatedRows ) )
    {
        authoring.SetCauseTreeSelectedRow( -1 );
        return false;
    }

    ReplayCauseTreeRowBuilder builder( predictionOwner, authoring, solverSample, presentationRecords, bodyStore,
                                       activePredictionFrames, nodes, usePrediction );

    if ( !builder.Build( path ) )
    {
        authoring.FailCauseTreeRowBuild();
        return false;
    }

    return ResolveReplayCauseTreeCameraFocus( authoring, camera, outCameraFocusedRow );
}


bool SkullbonezCore::Runtime::ActivateReplayCauseTreeRow(
    const ReplayPrediction& predictionOwner, ReplayAuthoring& authoring, int rowIndex, ReplayPresentation& presentationOwner,
    ReplayScrubber& scrubberOwner, const ReplaySolverFrameSample* currentSolverSample, const PhysicsBodyStore& bodyStore,
    const ColliderStore& colliderStore, RuntimeInteractionController& interaction, Vector3& outTargetPosition,
    float& outTargetRadius )
{
    const RunReplayPredictionState& prediction = predictionOwner.State();
    const ReplayPredictionPresentationView predictionView = predictionOwner.PresentationView();
    const std::span<const RunReplayPredictionFrame> activePredictionFrames = predictionView.timeline.frames;
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
        return ResolveReplayCauseTreeBodyPosition( id, prediction.enabled, predictionView.topology.targetId, pathTargetId,
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
