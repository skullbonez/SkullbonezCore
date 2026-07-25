/*
File: SkullbonezSource/Runtime/Editor/EditorOverlayTools.cpp
Purpose:
  Builds editor hover, placement-preview, and gizmo overlay traces.

Summary:
  Overlay trace work is presentation of editor state. It computes hover axes and
  preview markers without owning the model collection or committing placement.

Glossary:
  Preview: Non-authoritative placement or selection feedback before a click.
  Overlay trace: Frame-local line/shape instructions consumed by EditorTracer.
  Body store: Physics-owned live pose rows used for selection and tool markers.
  Collider store: Physics-owned shape rows used for shape-accurate outlines.

Invariants:
  - Overlay building must not mutate scene objects.
  - Hover state is recomputed every frame from the current mouse ray.

Related:
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - SkullbonezSource/Runtime/Editor/EditorTracer.cpp
*/
#include "EditorOverlayTools.h"
#include "EditorTools.h"
#include "../Tools/RuntimeTools.h"
#include "../Scene/SceneWorld.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"

#include <algorithm>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyOrientation;
using SkullbonezCore::Physics::PhysicsBodyPosition;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderHandle;

namespace SkullbonezCore
{
namespace Runtime
{
namespace RunInternal
{
EditorInteractionPreviewResult UpdateEditorInteractionPreview( EditorInteractionPreviewContext context,
                                                               const EditorInteractionPreviewInput& input )
{
    EditorInteractionPreviewResult result;
    // Concept: Preview refresh is the editor's input-facing phase. It may alter
    // hot axes and placement ghost state, while later overlay tracing only
    // renders the state produced here.
    context.editor.placementPreviewVisible = false;
    context.editor.hotGizmoAxis = -1;
    context.editor.hotRotationAxis = -1;
    Geometry::Terrain* terrain = context.world.Terrain().Get();
    const PhysicsBodyStore& bodyStore = context.world.BodyStore();
    const ColliderStore& colliderStore = context.world.Colliders();

    if ( input.uiBlocksCameraMouse || context.editor.viewportLookActive )
    {
        return result;
    }

    if ( !context.editor.editorModeEnabled && !input.inspectGizmoActive )
    {
        return result;
    }

    if ( context.editor.editorModeEnabled && context.editor.placementModeEnabled )
    {
        const bool placementScaleActive = context.interaction.Gesture().kind ==
                                          RuntimeInteractionGestureKind::EditorPlacementScaleDrag;

        EditorTerrainPlacement terrainPlacement;
        const EditorTerrainPlacement* terrainPlacementForPreview = nullptr;
        if ( !placementScaleActive && input.hasMouseRay &&
             TryGetEditorTerrainPlacement( terrain, input.mouseRayOrigin, input.mouseRayDirection, terrainPlacement ) )
        {
            terrainPlacementForPreview = &terrainPlacement;
        }

        context.editor.placementPreviewVisible = TryUpdateEditorPlacementPreview(
            { context.editor, terrain, context.assets, placementScaleActive },
            context.editor.objectType,
            terrainPlacementForPreview );
    }

    const int selectedModelIndex = ResolveSelectedEditorModelIndex( context.editor, bodyStore );
    const bool hasSelection = selectedModelIndex >= 0;
    bool selectionHandlesValid = false;
    if ( hasSelection && context.editor.selectedBody.IsValid() && context.editor.selectedCollider.IsValid() )
    {
        const PhysicsBodyRecord* body = bodyStore.RecordForHandle( context.editor.selectedBody );
        const ColliderRecord* collider = colliderStore.RecordForHandle( context.editor.selectedCollider );
        selectionHandlesValid = body && collider &&
                                bodyStore.ModelIndexForHandle( context.editor.selectedBody ) == selectedModelIndex &&
                                colliderStore.ModelIndexForHandle( context.editor.selectedCollider ) ==
                                    selectedModelIndex &&
                                collider->body == context.editor.selectedBody;
    }

    if ( context.editor.selectedModelRow.value >= context.world.SceneEntityCount() ||
         ( context.editor.selectedBody.IsValid() && !selectionHandlesValid ) )
    {
        // Invariant: Selection stores handles plus a model-row hint. If
        // topology invalidates either side, clear through the interaction
        // command path instead of letting later gizmo code read a stale row.
        result.clearInvalidSelection = true;
        result.inspectSelectionScope = input.inspectGizmoActive;
        return result;
    }

    if ( selectedModelIndex >= 0 && context.interaction.Gesture().kind != RuntimeInteractionGestureKind::GizmoDrag &&
         !context.editor.placementModeEnabled && input.hasMouseRay )
    {
        UpdateEditorGizmoHotAxes( { context.editor, context.world, context.interaction },
                                  input.mouseRayOrigin,
                                  input.mouseRayDirection,
                                  input.scaleMode );
    }

    return result;
}


void BuildEditorToolOverlayTrace( EditorToolOverlayTraceContext context, const EditorToolOverlayTraceInput& input )
{
    const PhysicsBodyStore& bodyStore = context.world.BodyStore();
    const ColliderStore& colliderStore = context.world.Colliders();
    // Concept: Overlay trace building is a pure visual pass over editor state.
    // It appends lines, ghosts, markers, and gizmos without claiming input or
    // mutating physics.
    const float rayLinger = (std::max)( 0.0f, input.rayLingerSeconds );
    if ( rayLinger > 0.0f )
    {
        for ( const RunRayCastTestLine& line : context.rayCastTest.lines )
        {
            if ( line.active && line.ageSeconds < rayLinger )
            {
                context.tracer.AddRayCastTestLine( line.start, line.end, 1.0f - line.ageSeconds / rayLinger, line.hit );
            }
        }
    }

    if ( context.editor.editorModeEnabled && context.editor.placementModeEnabled &&
         context.editor.placementPreviewVisible )
    {
        context.tracer.AddPlacementRay( context.editor.placementRayOrigin, context.editor.placementRayHit );
        context.tracer.AddPlacementGhost( context.editor.objectType,
                                          context.editor.placementCenter,
                                          context.editor.placementTerrainPoint,
                                          context.editor.placementScale,
                                          context.editor.placementOrientation,
                                          context.assets );
    }

    if ( ( context.editor.editorModeEnabled || input.inspectGizmoActive ) && !context.editor.placementModeEnabled &&
         context.editor.selectedBody.IsValid() )
    {
        const int selectedModelIndex = PeekSelectedEditorModelIndex( context.editor, bodyStore );
        Vector3 gizmoOrigin;
        float radius = 1.0f;
        const bool gizmoDragActive = input.gesture.kind == RuntimeInteractionGestureKind::GizmoDrag;
        const bool gizmoScale = gizmoDragActive && input.gesture.gizmoKind == RuntimeGizmoDragKind::Scale;
        const bool gizmoRotation = gizmoDragActive && input.gesture.gizmoKind == RuntimeGizmoDragKind::Rotate;
        const bool scaleMode = gizmoScale || input.scaleMode;
        if ( selectedModelIndex >= 0 && selectedModelIndex < context.world.SceneEntityCount() &&
             TryTraceEditorSelectionOverlayFromStores( context.world,
                                                       context.editor.selectedBody,
                                                       context.editor.selectedCollider,
                                                       selectedModelIndex,
                                                       context.tracer,
                                                       gizmoOrigin,
                                                       radius ) )
        {
            context.tracer.AddGizmo( gizmoOrigin,
                                     radius,
                                     context.editor.hotGizmoAxis,
                                     context.editor.hotRotationAxis,
                                     gizmoDragActive ? input.gesture.axis : -1,
                                     gizmoRotation,
                                     scaleMode,
                                     gizmoScale );
        }
    }

    if ( input.gesture.kind == RuntimeInteractionGestureKind::MousePickupDrag && context.mousePickup.body.IsValid() )
    {
        const PhysicsBodyRecord* body = bodyStore.RecordForHandle( context.mousePickup.body );
        const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( context.mousePickup.body );
        const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
        const int modelIndex = bodyStore.ModelIndexForHandle( context.mousePickup.body );
        if ( !body || !collider || modelIndex < 0 || modelIndex >= context.world.SceneEntityCount() ||
             collider->body != context.mousePickup.body )
        {
            // Stale drag state can happen after editor deletion or scene reload.
            // Leave cancellation to the input/physics owner and just omit the
            // presentation trace for this frame.
        }
        else
        {
            // Why: Mouse pickup stores a body handle when the drag begins.
            // Overlay drawing should follow that live store row instead of
            // requiring post-step authoring/presentation data to be current.
            const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
            const auto hotFields = bodyStore.HotFields();
            const Vector3 bodyPosition = PhysicsBodyPosition( hotFields, bodyIndex );
            const Vector3 grabPoint = bodyPosition + context.mousePickup.grabOffset;
            context.tracer.AddSelectionOutline( bodyPosition,
                                                PhysicsBodyOrientation( hotFields, bodyIndex ),
                                                collider->shape );

            context.tracer.AddReplayPathSegment( grabPoint, context.mousePickup.targetPoint, 0.1f, 0.95f, 1.0f );
            context.tracer.AddReplayContactMarker( context.mousePickup.targetPoint,
                                                   context.mousePickup.planeNormal,
                                                   0.1f,
                                                   0.95f,
                                                   1.0f );

            context.tracer.AddReplayImpulseVector( grabPoint, context.mousePickup.lastImpulse, 0.1f, 0.95f, 1.0f );
        }
    }

    if ( input.attachedCameraTargetIndex >= 0 && input.attachedCameraTargetIndex < context.world.SceneEntityCount() )
    {
        const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( input.attachedCameraTargetIndex );
        const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
        const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( bodyHandle );
        const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
        if ( body && collider && bodyStore.ModelIndexForHandle( bodyHandle ) == input.attachedCameraTargetIndex &&
             collider->body == bodyHandle )
        {
            // Why: attached-camera follow is already store-backed; its overlay
            // marker should read the same live body/collider rows instead of
            // keeping legacy model-side pose/shape caches hot for presentation.
            const float markerRadius = EditorColliderRadius( *collider ) * 1.24f;
            const std::size_t bodyIndex = static_cast<std::size_t>( input.attachedCameraTargetIndex );
            const auto hotFields = bodyStore.HotFields();
            context.tracer.AddAttachedCameraTargetMarker( PhysicsBodyPosition( hotFields, bodyIndex ),
                                                          PhysicsBodyOrientation( hotFields, bodyIndex ),
                                                          collider->shape,
                                                          markerRadius,
                                                          input.attachedCameraActiveFollow );
        }
    }
}
} // namespace RunInternal
} // namespace Runtime
} // namespace SkullbonezCore
