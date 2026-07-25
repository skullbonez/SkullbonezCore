/*
File: SkullbonezSource/Runtime/Replay/ReplayCauseFocusSubmission.cpp
Purpose:
  Submits markers for the replay body, manifold, prediction-contact, and solver-row focus chosen by Presentation.

Summary:
  Presentation selection is complete before this unit runs. It resolves stable
  scene ids at the Physics store boundary and translates the selected cause
  focus into bounded tracer values without changing timeline or prediction state.

Glossary:
  Cause focus: The selected body, contact, motion, or solver row highlighted by replay authoring.
  Submission: Conversion of already-selected values into tracer draw commands.

Invariants:
  - Stable PhysicsSceneObjectId values resolve through typed store handles.
  - Dense model rows are frame-local hints and are healed or invalidated on use.
  - Submission cannot mutate scrub, timeline, prediction, or cause-tree selection.

Related:
  - ReplayPresentationSubmission.h
  - ReplayPredictionDrawing.cpp
  - ReplayPresentation.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "ReplayPresentationSubmission.h"

#include "ReplayAuthoring.h"
#include "ReplayPrediction.h"
#include "ReplayPredictionPublicationOperations.h"
#include "ReplayPresentation.h"
#include "../Editor/EditorTools.h"
#include "../Scene/SceneEntityStore.h"
#include "../Tools/RuntimeTools.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
bool TryResolveReplayBodyModelIndex( const PhysicsBodyStore& bodyStore,
                                     PhysicsSceneObjectId id,
                                     int modelIndexHint,
                                     int modelCount,
                                     int& outModelIndex )
{
    if ( id.value == 0 )
    {
        return false;
    }

    const PhysicsBodyHandle body = bodyStore.HandleForSceneObjectId( id, modelIndexHint );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return false;
    }

    outModelIndex = modelIndex;
    return true;
}

const SkullbonezCore::Runtime::ReplaySolverBodySample*
FindReplayNonNegativeBodyByModelIndex( const SkullbonezCore::Runtime::ReplaySolverFrameSample& sample, int modelIndex )
{
    // Why: CauseFocus has always rejected the terrain/world negative sentinel,
    // while the shared solver wrapper retains the Prediction domain's legacy
    // negative-row scan. Select that policy explicitly instead of changing it.
    return FindReplayBodyByModelIndexInSample<SkullbonezCore::Runtime::ReplaySolverFrameSample,
                                              SkullbonezCore::Runtime::ReplaySolverBodySample,
                                              false>( sample, modelIndex );
}

bool ReplayContactHasModelIndex( const PhysicsSolverPersistentContactSample& contact, int modelIndex )
{
    return modelIndex >= 0 && ( contact.bodyA == modelIndex || contact.bodyB == modelIndex );
}

int ReplayContactOtherModelIndex( const PhysicsSolverPersistentContactSample& contact, int modelIndex )
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

Vector3 ReplayContactPoint( const SkullbonezCore::Runtime::ReplaySolverFrameSample& sample,
                            const PhysicsSolverPersistentContactSample& contact )
{
    if ( const SkullbonezCore::Runtime::ReplaySolverBodySample* bodyA = FindReplayNonNegativeBodyByModelIndex(
             sample,
             contact.bodyA ) )
    {
        return bodyA->position + contact.rA;
    }

    if ( const SkullbonezCore::Runtime::ReplaySolverBodySample* bodyB = FindReplayNonNegativeBodyByModelIndex(
             sample,
             contact.bodyB ) )
    {
        return bodyB->position + contact.rB;
    }

    return SkullbonezCore::Math::Vector::ZERO_VECTOR;
}

Vector3 ReplayContactNormalForModel( const PhysicsSolverPersistentContactSample& contact, int modelIndex )
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
} // namespace

namespace SkullbonezCore::Runtime::ReplayPresentationSubmissionOperations
{
bool TryResolveReplayBodyModelIndex( const PhysicsBodyStore& bodyStore,
                                     PhysicsSceneObjectId id,
                                     ModelRowHint& hint,
                                     int modelCount,
                                     int& outModelIndex )
{
    // Why: retained replay UI state carries a dense row only as an optimization.
    // The stable scene id remains authoritative while this resolver heals or
    // invalidates the frame-local hint.
    if ( !::TryResolveReplayBodyModelIndex( bodyStore, id, hint.value, modelCount, outModelIndex ) )
    {
        hint.value = -1;
        return false;
    }

    hint.value = outModelIndex;
    return true;
}

bool TryAddReplayTargetMarkerFromStores( EditorTracer& tracer,
                                         const PhysicsBodyStore& bodyStore,
                                         const ColliderStore& colliderStore,
                                         int modelIndex )
{
    const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( bodyHandle );
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
    if ( !body || !collider || bodyStore.ModelIndexForHandle( bodyHandle ) != modelIndex ||
         colliderStore.ModelIndexForHandle( colliderHandle ) != modelIndex || collider->body != bodyHandle )
    {
        return false;
    }

    // Invariant: submission resolves through typed handles before reading hot
    // store rows; a stale dense-row hint never becomes durable identity.
    const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
    const auto hotFields = bodyStore.HotFields();
    const float radius = (std::max)( 1.0f,
                                     (std::max)( hotFields.boundingRadius[bodyIndex], collider->boundingRadius ) ) *
                         1.18f;

    tracer.AddReplayTargetMarker( PhysicsBodyPosition( hotFields, bodyIndex ),
                                  PhysicsBodyOrientation( hotFields, bodyIndex ),
                                  collider->shape,
                                  radius );

    return true;
}
} // namespace SkullbonezCore::Runtime::ReplayPresentationSubmissionOperations

namespace SkullbonezCore::Runtime
{
void ReplayPresentation::RenderCauseFocusOverlay( const RunReplayCauseTreeState& causeTree,
                                                  const ReplayPredictionPresentationView& prediction,
                                                  const ReplaySolverFrameSample* currentSolverSample,
                                                  const PhysicsBodyStore& bodyStore,
                                                  const ColliderStore& colliderStore,
                                                  const SceneEntityStore& entities,
                                                  EditorTracer& tracer )
{
    using namespace ReplayPresentationSubmissionOperations;

    const RunReplayCameraState camera = CameraView();
    if ( camera.focusKind == RunReplayCameraFocusKind::None )
    {
        return;
    }

    if ( camera.focusKind == RunReplayCameraFocusKind::Body )
    {
        ModelRowHint focusHint;
        focusHint.value = camera.focusModelRow.value;
        int focusedModelIndex = -1;
        if ( TryResolveReplayBodyModelIndex( bodyStore,
                                             camera.focusedId,
                                             focusHint,
                                             bodyStore.Count(),
                                             focusedModelIndex ) )
        {
            TryAddReplayTargetMarkerFromStores( tracer, bodyStore, colliderStore, focusedModelIndex );
            return;
        }
    }

    if ( camera.focusKind == RunReplayCameraFocusKind::Manifold ||
         camera.focusKind == RunReplayCameraFocusKind::PredictionContact ||
         camera.focusKind == RunReplayCameraFocusKind::PredictionMotion )
    {
        if ( camera.focusKind == RunReplayCameraFocusKind::Manifold )
        {
            const ReplaySolverFrameSample* sample = currentSolverSample;

            if ( sample )
            {
                const ReplaySolverBodySample* focusedBody = FindReplayBodyById( *sample, camera.focusedId );
                const ReplaySolverBodySample* counterpartBody = FindReplayBodyById( *sample, camera.counterpartId );
                if ( focusedBody )
                {
                    bool drewContact = false;
                    for ( const PhysicsSolverPersistentContactSample& contact :
                          sample->worldSnapshot.physics.persistentContacts )
                    {
                        if ( !ReplayContactHasModelIndex( contact, focusedBody->modelRow.value ) )
                        {
                            continue;
                        }

                        const int otherModelIndex = ReplayContactOtherModelIndex( contact,
                                                                                  focusedBody->modelRow.value );

                        const bool terrain = contact.isTerrain || otherModelIndex < 0;
                        if ( camera.focusTerrain != terrain )
                        {
                            continue;
                        }

                        if ( !terrain && ( !counterpartBody || counterpartBody->modelRow.value != otherModelIndex ) )
                        {
                            continue;
                        }

                        tracer.AddReplayContactMarker(
                            ReplayContactPoint( *sample, contact ),
                            ReplayContactNormalForModel( contact, focusedBody->modelRow.value ),
                            0.1f,
                            0.95f,
                            1.0f );

                        drewContact = true;
                    }

                    if ( drewContact )
                    {
                        return;
                    }
                }
            }
        }
        else if ( camera.focusKind == RunReplayCameraFocusKind::PredictionContact )
        {
            ReplayFrameIndex focusFrame = 0;
            int focusedModelIndex = camera.focusModelRow.value;
            int counterpartModelIndex = camera.focusCounterpartModelRow.value;

            if ( causeTree.selectedRow >= 0 && causeTree.selectedRow < static_cast<int>( causeTree.rows.size() ) )
            {
                const RunReplayCauseTreeRow& row = causeTree.rows[static_cast<std::size_t>( causeTree.selectedRow )];
                if ( row.kind == RunReplayCauseTreeRowKind::PredictionContact &&
                     row.id.value == camera.focusedId.value )
                {
                    focusFrame = row.firstFrame;
                    focusedModelIndex = row.modelRow.value;
                    counterpartModelIndex = row.counterpartModelRow.value;
                }
            }
            else if ( camera.focusContactIndex >= 0 &&
                      camera.focusContactIndex < static_cast<int>( prediction.futureNodes.size() ) )
            {
                const RunReplayPathTraceNode&
                    node = prediction.futureNodes[static_cast<std::size_t>( camera.focusContactIndex )];

                if ( node.id.value == camera.focusedId.value && node.contactDerived )
                {
                    focusFrame = node.firstFrame;
                    focusedModelIndex = node.modelRow.value;
                    counterpartModelIndex = node.parentModelRow.value;
                }
            }

            bool drewPredictionManifold = false;
            for ( const RunReplayPredictionFrame& frame : prediction.frames )
            {
                if ( frame.frameIndex != focusFrame )
                {
                    continue;
                }

                // Why: the selected future-tree contact names a body pair, but
                // the complete manifold remains in the immutable frame debug values.
                for ( const PhysicsDebugContact& contact : frame.debugContacts )
                {
                    const int contactModelA = ReplayRagdollTorsoModelIndexForPart( entities, contact.bodyA );
                    const int contactModelB = contact.bodyB >= 0
                                                  ? ReplayRagdollTorsoModelIndexForPart( entities, contact.bodyB )
                                                  : contact.bodyB;

                    const bool selectedPairAB = contactModelA == focusedModelIndex &&
                                                ( counterpartModelIndex < 0 || contactModelB == counterpartModelIndex );

                    const bool selectedPairBA = contactModelB == focusedModelIndex &&
                                                ( counterpartModelIndex < 0 || contactModelA == counterpartModelIndex );

                    if ( !selectedPairAB && !selectedPairBA )
                    {
                        continue;
                    }

                    Vector3 normal = contact.normal;
                    if ( selectedPairBA && contactModelB >= 0 )
                    {
                        normal = normal * -1.0f;
                    }

                    tracer.AddReplayContactMarker( contact.point,
                                                   ReplayNormalizeOr( normal, Vector3( 0.0f, 1.0f, 0.0f ) ),
                                                   0.1f,
                                                   0.95f,
                                                   1.0f );

                    drewPredictionManifold = true;
                }

                break;
            }

            if ( drewPredictionManifold )
            {
                return;
            }
        }

        tracer.AddReplayContactMarker( camera.targetPoint, camera.targetNormal, 0.1f, 0.95f, 1.0f );
        return;
    }

    if ( camera.focusKind == RunReplayCameraFocusKind::SolverRow )
    {
        tracer.AddReplayContactMarker( camera.targetPoint, camera.targetNormal, 0.2f, 0.85f, 1.0f );
        tracer.AddReplayImpulseVector( camera.targetPoint, camera.impulseVector, 1.0f, 0.32f, 0.12f );
    }
}
} // namespace SkullbonezCore::Runtime
