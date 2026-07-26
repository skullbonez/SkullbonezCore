/*
File: SkullbonezSource/Runtime/Editor/EditorHistory.cpp
Purpose:
  Captures and applies editor transform, primitive placement, and deletion history.

Summary:
  Gesture and placement code publishes fixed command facts here. This owner
  resolves stable scene ids at apply time, calls scene/physics command APIs, and
  advances history only after the complete inverse or forward command succeeds.

Glossary:
  Recreate recipe: Fixed standalone entity/body/collider facts used after delete.
  Command side: Before values for undo or after values for redo.
  Stable resolution: Finding the current dense row from PhysicsSceneObjectId.

Invariants:
  - Only standalone sphere/box recipes enter place/delete history.
  - Transform apply preflights every stable id before mutating any row.
  - Undo/redo never stores or resolves stale body/collider handles from entries.

Related:
  - SkullbonezSource/Runtime/Editor/EditorCommandHistory.h
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - SkullbonezSource/Runtime/Scene/SceneWorld.cpp
*/
#include "../Tools/RuntimeTools.h"

#include "EditorTools.h"
#include "../Scene/SceneControllerState.h"
#include "../Scene/SceneRuntime.h"
#include "../Scene/SceneWorld.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
const ColliderRecord* ColliderForModelIndex( const ColliderStore& colliders, int modelIndex )
{
    const PhysicsColliderHandle handle = colliders.HandleForModelIndex( modelIndex );
    return handle.IsValid() ? colliders.RecordForHandle( handle ) : nullptr;
}


const ColliderAuthoringRecord* ColliderAuthoringForModelIndex( const ColliderStore& colliders, int modelIndex )
{
    return colliders.AuthoringRecordForModelIndex( modelIndex );
}


bool PosesDiffer( const EditorTransformSnapshot& before, const EditorTransformSnapshot& after )
{
    if ( VectorMagSquared( after.position - before.position ) > 1.0e-8f )
    {
        return true;
    }

    float beforeX = 0.0f;
    float beforeY = 0.0f;
    float beforeZ = 0.0f;
    float beforeW = 1.0f;
    float afterX = 0.0f;
    float afterY = 0.0f;
    float afterZ = 0.0f;
    float afterW = 1.0f;
    before.orientation.GetComponents( beforeX, beforeY, beforeZ, beforeW );
    after.orientation.GetComponents( afterX, afterY, afterZ, afterW );
    if ( fabsf( beforeX - afterX ) > 1.0e-6f || fabsf( beforeY - afterY ) > 1.0e-6f ||
         fabsf( beforeZ - afterZ ) > 1.0e-6f || fabsf( beforeW - afterW ) > 1.0e-6f )
    {
        return true;
    }

    if ( before.hasShape != after.hasShape )
    {
        return true;
    }

    return before.hasShape && ( before.shape.kind != after.shape.kind ||
                                VectorMagSquared( after.shape.dimensions - before.shape.dimensions ) > 1.0e-8f );
}


bool CapturePrimitiveRecipe( const SceneWorld& world, int modelIndex, EditorPrimitiveRecreateRecipe& outRecipe )
{
    if ( modelIndex < 0 || modelIndex >= world.SceneEntityCount() )
    {
        return false;
    }

    const SceneEntityRecord& entity = world.Entities().At( modelIndex );
    if ( entity.behaviorGroup.kind != SceneBehaviorGroupKind::None || entity.asset.isAssetBacked )
    {
        return false;
    }

    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex );
    const ColliderRecord* collider = ColliderForModelIndex( world.Colliders(), modelIndex );
    const ColliderAuthoringRecord* colliderAuthoring = ColliderAuthoringForModelIndex( world.Colliders(), modelIndex );
    if ( !body || !collider || !colliderAuthoring || body->sceneObjectId.value != entity.sceneObjectId.value ||
         !TryCaptureEditorPrimitiveShape( collider->shape, outRecipe.shape ) )
    {
        return false;
    }

    outRecipe.entity = SceneEntityCreateDesc {};
    outRecipe.entity.sceneObjectId = entity.sceneObjectId;
    outRecipe.entity.renderMaterial = entity.renderMaterial;
    outRecipe.entity.asset = entity.asset;
    outRecipe.entity.behaviorGroup = entity.behaviorGroup;
    strcpy_s( outRecipe.entity.displayName, entity.displayName );
    outRecipe.entity.editorVisible = entity.editorVisible;
    outRecipe.entity.editorLocked = entity.editorLocked;
    // Invariant: recreation facts exclude the live handle and transient
    // pending-impulse/sleep state; stable identity comes from the entity recipe.
    const PhysicsBodyHotState hotState = LoadPhysicsBodyHotState( bodyStore.HotFields(),
                                                                  static_cast<std::size_t>( modelIndex ) );

    outRecipe.body.position = hotState.position;
    outRecipe.body.orientation = hotState.orientation;
    outRecipe.body.linearVelocity = hotState.linearVelocity;
    outRecipe.body.angularVelocity = hotState.angularVelocity;
    outRecipe.body.rotationalInertia = body->rotationalInertia;
    outRecipe.body.mass = body->mass;
    outRecipe.body.boundingRadius = hotState.boundingRadius;
    outRecipe.body.volume = body->volume;
    outRecipe.body.projectedSurfaceArea = body->projectedSurfaceArea;
    outRecipe.body.dragCoefficient = body->dragCoefficient;
    outRecipe.body.contactReleaseImpulseThreshold = body->contactReleaseImpulseThreshold;
    outRecipe.body.angularVelocityLimit = body->angularVelocityLimit;
    outRecipe.body.contactEpsilon = body->contactEpsilon;
    outRecipe.body.isFixed = hotState.fixed;
    outRecipe.body.isSleeping = !hotState.awake;
    outRecipe.body.releasesFromFixedOnContact = body->releasesFromFixedOnContact;
    outRecipe.body.usesWorldInertia = body->usesWorldInertia;
    outRecipe.restitution = collider->restitution;
    outRecipe.friction = collider->friction;
    outRecipe.contactMaterialId = collider->contactMaterialId;
    strcpy_s( outRecipe.contactMaterialName, colliderAuthoring->contactMaterialName );
    return true;
}


bool RecreatePrimitive( SceneWorld& world,
                        SceneSessionState& scene,
                        const EditorPrimitiveRecreateRecipe& recipe,
                        PhysicsBodyHandle& outBody,
                        PhysicsColliderHandle& outCollider )
{
    CollisionShape shape;
    if ( !TryBuildEditorPrimitiveShape( recipe.shape, shape ) )
    {
        return false;
    }

    PhysicsBodyCreateDesc bodyDesc;
    bodyDesc.sceneObjectId = recipe.entity.sceneObjectId;
    bodyDesc.shape = shape;
    bodyDesc.position = recipe.body.position;
    bodyDesc.orientation = recipe.body.orientation;
    bodyDesc.linearVelocity = recipe.body.linearVelocity;
    bodyDesc.angularVelocity = recipe.body.angularVelocity;
    bodyDesc.rotationalInertia = recipe.body.rotationalInertia;
    bodyDesc.mass = recipe.body.mass;
    bodyDesc.restitution = recipe.restitution;
    bodyDesc.friction = recipe.friction;
    bodyDesc.boundingRadius = recipe.body.boundingRadius;
    bodyDesc.volume = recipe.body.volume;
    bodyDesc.projectedSurfaceArea = recipe.body.projectedSurfaceArea;
    bodyDesc.dragCoefficient = recipe.body.dragCoefficient;
    bodyDesc.angularVelocityLimit = recipe.body.angularVelocityLimit;
    bodyDesc.contactEpsilon = recipe.body.contactEpsilon;
    bodyDesc.motionKind = recipe.body.isFixed ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic;
    bodyDesc.startsAsleep = recipe.body.isSleeping;
    bodyDesc.releasesFromFixedOnContact = recipe.body.releasesFromFixedOnContact;
    bodyDesc.usesWorldInertia = recipe.body.usesWorldInertia;
    bodyDesc.contactReleaseImpulseThreshold = recipe.body.contactReleaseImpulseThreshold;
    PhysicsColliderCreateDesc colliderDesc = MakeColliderCreateDesc( shape,
                                                                     recipe.restitution,
                                                                     recipe.contactMaterialId,
                                                                     recipe.contactMaterialName );

    colliderDesc.friction = recipe.friction;
    const SceneEntityCreateResult result = world.TryCreateSceneEntity( recipe.entity,
                                                                       std::move( bodyDesc ),
                                                                       std::move( colliderDesc ) );

    if ( !result.status.ok )
    {
        return false;
    }

    outBody = result.body;
    outCollider = world.Colliders().HandleForBodyHandle( outBody );
    scene.modelCount = world.SceneEntityCount();
    if ( outCollider.IsValid() )
    {
        return true;
    }

    // Hazard: never report a failed inverse after leaving its partially
    // recreated entity live; that would desynchronize the cursor and scene.
    if ( !world.DestroySceneEntity( outBody ) )
    {
        // Lane F: a successful create must remain synchronously removable
        // before any later command can observe it.
        SB_FATAL( "EditorCommandHistory", "Failed to roll back an incomplete primitive recreation." );
    }

    scene.modelCount = world.SceneEntityCount();
    outBody = {};
    return false;
}


bool DestroyBySceneId( SceneWorld& world, SceneSessionState& scene, PhysicsSceneObjectId sceneObjectId )
{
    const int modelIndex = world.Entities().FindBySceneObjectId( sceneObjectId );
    if ( modelIndex < 0 )
    {
        return false;
    }

    const PhysicsBodyHandle body = world.BodyStore().HandleForModelIndex( modelIndex );
    if ( !body.IsValid() || !world.DestroySceneEntity( body ) )
    {
        return false;
    }

    scene.modelCount = world.SceneEntityCount();
    return true;
}


bool ApplyTransformEntry( SceneWorld& world, const EditorCommandEntry& entry, bool useAfter )
{
    // Invariant: resolve every stable id and shape before the first mutation so
    // an invalid command cannot apply only a prefix of a group gesture.
    std::array<int, EDITOR_COMMAND_TRANSFORM_CAPACITY> modelIndices = {};
    std::array<CollisionShape, EDITOR_COMMAND_TRANSFORM_CAPACITY> shapes = {};

    for ( std::size_t index = 0; index < entry.transformCount; ++index )
    {
        const EditorTransformHistoryItem& item = entry.transforms[index];
        const EditorTransformSnapshot& snapshot = useAfter ? item.after : item.before;
        modelIndices[index] = world.Entities().FindBySceneObjectId( item.sceneObjectId );
        if ( modelIndices[index] < 0 ||
             ( snapshot.hasShape && ( !TryBuildEditorPrimitiveShape( snapshot.shape, shapes[index] ) ||
                                      !ColliderForModelIndex( world.Colliders(), modelIndices[index] ) ) ) )
        {
            return false;
        }
    }

    for ( std::size_t index = 0; index < entry.transformCount; ++index )
    {
        const EditorTransformSnapshot& snapshot = useAfter ? entry.transforms[index].after
                                                           : entry.transforms[index].before;

        PhysicsBodyUpdateDesc update;
        update.updateMask = PHYSICS_BODY_UPDATE_POSE;
        update.position = snapshot.position;
        update.orientation = snapshot.orientation;
        if ( snapshot.hasShape )
        {
            const ColliderRecord* collider = ColliderForModelIndex( world.Colliders(), modelIndices[index] );
            const ColliderAuthoringRecord* colliderAuthoring = ColliderAuthoringForModelIndex( world.Colliders(),
                                                                                               modelIndices[index] );

            if ( !collider || !colliderAuthoring )
            {
                return false;
            }

            PhysicsColliderCreateDesc colliderDesc = MakeColliderCreateDesc( shapes[index],
                                                                             collider->restitution,
                                                                             collider->contactMaterialId,
                                                                             colliderAuthoring->contactMaterialName );

            colliderDesc.friction = collider->friction;
            if ( !RunInternal::ResetEditorModelMotionAndWake( world,
                                                              modelIndices[index],
                                                              update,
                                                              std::move( colliderDesc ) ) )
            {
                // Lane F: preflight resolved this owned body/collider. Failure
                // here would otherwise leave a group inverse partially applied.
                SB_FATAL( "EditorCommandHistory", "Preflighted scale inverse failed during commit." );
            }
        }
        else
        {
            if ( !RunInternal::ResetEditorModelMotionAndWake( world, modelIndices[index], update ) )
            {
                // Lane F: stable-id preflight makes an update rejection an
                // ownership invariant failure, not a recoverable cursor miss.
                SB_FATAL( "EditorCommandHistory", "Preflighted transform inverse failed during commit." );
            }
        }
    }

    return true;
}


bool ApplyHistoryEntry( SceneWorld& world,
                        SceneSessionState& scene,
                        const EditorCommandEntry& entry,
                        bool redo,
                        PhysicsBodyHandle& outBody,
                        PhysicsColliderHandle& outCollider )
{
    if ( entry.kind == EditorCommandKind::Transform )
    {
        return ApplyTransformEntry( world, entry, redo );
    }

    if ( entry.kind == EditorCommandKind::Place )
    {
        return redo ? RecreatePrimitive( world, scene, entry.primitive, outBody, outCollider )
                    : DestroyBySceneId( world, scene, entry.primitive.entity.sceneObjectId );
    }

    if ( entry.kind == EditorCommandKind::Delete )
    {
        return redo ? DestroyBySceneId( world, scene, entry.primitive.entity.sceneObjectId )
                    : RecreatePrimitive( world, scene, entry.primitive, outBody, outCollider );
    }

    return false;
}
} // namespace


void RuntimeTools::RecordEditorTransformHistory( SceneWorld& world,
                                                 RuntimeGizmoDragKind gizmoKind,
                                                 int selectedModelIndex )
{
    if ( !m_editor.editorModeEnabled || selectedModelIndex < 0 )
    {
        return;
    }

    EditorCommandEntry entry;
    entry.kind = EditorCommandKind::Transform;
    const PhysicsBodyStore& bodies = world.BodyStore();
    const ColliderStore& colliders = world.Colliders();

    if ( gizmoKind == RuntimeGizmoDragKind::Scale )
    {
        const PhysicsBodyRecord* body = bodies.RecordForModelIndex( selectedModelIndex );
        const ColliderRecord* collider = ColliderForModelIndex( colliders, selectedModelIndex );
        if ( !body || !collider )
        {
            m_editor.history.InvalidateForNonUndoableEdit();
            return;
        }

        EditorTransformHistoryItem& item = entry.transforms[0];
        item.sceneObjectId = body->sceneObjectId;
        item.before.position = m_editor.gizmoDragStartPosition;
        item.before.orientation = m_editor.gizmoDragStartOrientation;
        const std::size_t bodyIndex = static_cast<std::size_t>( selectedModelIndex );
        const auto hotFields = bodies.HotFields();
        item.after.position = PhysicsBodyPosition( hotFields, bodyIndex );
        item.after.orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
        item.before.hasShape = TryCaptureEditorPrimitiveShape( m_editor.gizmoDragStartShape, item.before.shape );
        item.after.hasShape = TryCaptureEditorPrimitiveShape( collider->shape, item.after.shape );
        if ( !item.before.hasShape || !item.after.hasShape || !PosesDiffer( item.before, item.after ) )
        {
            if ( !item.before.hasShape || !item.after.hasShape )
            {
                // Hazard: a committed convex-hull scale has no bounded inverse.
                // Clear history so stale redo cannot cross that mutation.
                m_editor.history.InvalidateForNonUndoableEdit();
            }

            return;
        }

        entry.transformCount = 1;
    }
    else
    {
        const int groupCount = RunInternal::ValidCapturedEditorGizmoGroupCount( m_editor, world.SceneEntityCount() );
        const int count = groupCount > 0 ? groupCount : 1;
        for ( int groupIndex = 0; groupIndex < count; ++groupIndex )
        {
            const int modelIndex = groupCount > 0
                                       ? m_editor.gizmoDragGroupIndices[static_cast<std::size_t>( groupIndex )]
                                       : selectedModelIndex;

            const PhysicsBodyRecord* body = bodies.RecordForModelIndex( modelIndex );
            if ( !body )
            {
                m_editor.history.InvalidateForNonUndoableEdit();
                return;
            }

            EditorTransformHistoryItem& item = entry.transforms[entry.transformCount];
            item.sceneObjectId = body->sceneObjectId;
            item.before.position = groupCount > 0
                                       ? m_editor.gizmoDragGroupStartPositions[static_cast<std::size_t>( groupIndex )]
                                       : m_editor.gizmoDragStartPosition;

            item.before.orientation = groupCount > 0
                                          ? m_editor
                                                .gizmoDragGroupStartOrientations[static_cast<std::size_t>( groupIndex )]
                                          : m_editor.gizmoDragStartOrientation;

            const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
            const auto hotFields = bodies.HotFields();
            item.after.position = PhysicsBodyPosition( hotFields, bodyIndex );
            item.after.orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
            if ( PosesDiffer( item.before, item.after ) )
            {
                ++entry.transformCount;
            }
        }

        if ( entry.transformCount == 0 )
        {
            return;
        }
    }

    m_editor.history.Push( entry );
}


void RuntimeTools::RecordEditorPlacementHistory( SceneWorld& world, int modelCountBefore, int modelCountAfter )
{
    if ( !m_editor.editorModeEnabled || modelCountAfter <= modelCountBefore )
    {
        return;
    }

    EditorCommandEntry entry;
    entry.kind = EditorCommandKind::Place;
    if ( modelCountAfter == modelCountBefore + 1 && CapturePrimitiveRecipe( world, modelCountBefore, entry.primitive ) )
    {
        m_editor.history.Push( entry );
        return;
    }

    // Hazard: multi-entity and nonprimitive placement recipes are deliberately
    // deferred. Clear history so a prior redo suffix cannot cross the edit.
    m_editor.history.InvalidateForNonUndoableEdit();
}


bool RuntimeTools::UndoEditorCommand( SceneWorld& world, SceneSessionState& scene )
{
    const EditorCommandEntry* entry = m_editor.history.PendingUndo();
    PhysicsBodyHandle body;
    PhysicsColliderHandle collider;
    if ( !m_editor.editorModeEnabled || !entry || !ApplyHistoryEntry( world, scene, *entry, false, body, collider ) )
    {
        return false;
    }

    if ( body.IsValid() )
    {
        m_editor.selectedBody = body;
        m_editor.selectedCollider = collider;
        m_editor.selectedModelRow.value = world.BodyStore().ModelIndexForHandle( body );
    }
    else if ( m_editor.selectedBody.IsValid() && !world.BodyStore().Contains( m_editor.selectedBody ) )
    {
        m_editor.selectedBody = {};

        m_editor.selectedCollider = {};

        m_editor.selectedModelRow.value = -1;
    }

    return m_editor.history.CommitUndo();
}


bool RuntimeTools::RedoEditorCommand( SceneWorld& world, SceneSessionState& scene )
{
    const EditorCommandEntry* entry = m_editor.history.PendingRedo();
    PhysicsBodyHandle body;
    PhysicsColliderHandle collider;
    if ( !m_editor.editorModeEnabled || !entry || !ApplyHistoryEntry( world, scene, *entry, true, body, collider ) )
    {
        return false;
    }

    if ( body.IsValid() )
    {
        m_editor.selectedBody = body;
        m_editor.selectedCollider = collider;
        m_editor.selectedModelRow.value = world.BodyStore().ModelIndexForHandle( body );
    }
    else if ( m_editor.selectedBody.IsValid() && !world.BodyStore().Contains( m_editor.selectedBody ) )
    {
        m_editor.selectedBody = {};

        m_editor.selectedCollider = {};

        m_editor.selectedModelRow.value = -1;
    }

    return m_editor.history.CommitRedo();
}


bool RuntimeTools::DuplicateEditorSelection( SceneWorld& world, SceneSessionState& scene )
{
    const int modelIndex = RunInternal::ResolveSelectedEditorModelIndex( m_editor, world.BodyStore() );
    EditorCommandEntry entry;
    entry.kind = EditorCommandKind::Place;
    if ( !m_editor.editorModeEnabled || modelIndex < 0 || world.Entities().At( modelIndex ).editorLocked ||
         !CapturePrimitiveRecipe( world, modelIndex, entry.primitive ) )
    {
        return false;
    }

    entry.primitive.entity.sceneObjectId = scene.AllocateSceneObjectId();
    entry.primitive.entity.editorVisible = true;
    entry.primitive.entity.editorLocked = false;
    entry.primitive.body.position.x += 2.0f;
    entry.primitive.body.position.z += 2.0f;
    const char* sourceName = entry.primitive.entity.displayName[0] != '\0' ? entry.primitive.entity.displayName
                                                                           : "Object";

    char duplicateName[64] = {};

    snprintf( duplicateName, sizeof( duplicateName ), "%.52s Copy", sourceName );
    strcpy_s( entry.primitive.entity.displayName, duplicateName );

    PhysicsBodyHandle body;
    PhysicsColliderHandle collider;
    if ( !RecreatePrimitive( world, scene, entry.primitive, body, collider ) )
    {
        return false;
    }

    m_editor.history.Push( entry );
    m_editor.selectedBody = body;
    m_editor.selectedCollider = collider;
    m_editor.selectedModelRow.value = world.BodyStore().ModelIndexForHandle( body );
    return true;
}


bool RuntimeTools::DeleteEditorSelection( SceneWorld& world, SceneSessionState& scene )
{
    const int modelIndex = RunInternal::ResolveSelectedEditorModelIndex( m_editor, world.BodyStore() );
    EditorCommandEntry entry;
    entry.kind = EditorCommandKind::Delete;
    if ( !m_editor.editorModeEnabled || modelIndex < 0 || world.Entities().At( modelIndex ).editorLocked ||
         !CapturePrimitiveRecipe( world, modelIndex, entry.primitive ) ||
         !world.DestroySceneEntity( m_editor.selectedBody ) )
    {
        return false;
    }

    scene.modelCount = world.SceneEntityCount();
    m_editor.history.Push( entry );
    m_editor.selectedBody = {};

    m_editor.selectedCollider = {};

    m_editor.selectedModelRow.value = -1;
    return true;
}


void RuntimeTools::ClearEditorHistory()
{
    m_editor.history.Clear();
}
} // namespace Runtime
} // namespace SkullbonezCore
