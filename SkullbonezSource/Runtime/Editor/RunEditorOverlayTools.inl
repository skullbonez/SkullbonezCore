/*
File: SkullbonezSource/Runtime/Editor/RunEditorOverlayTools.inl
Purpose:
  Builds editor hover, placement-preview, and gizmo overlay traces.

Mental model:
  Overlay trace work is presentation of editor state. It computes hover axes and
  preview markers without owning the model collection or committing placement.

Glossary:
  Preview: Non-authoritative placement or selection feedback before a click.
  Overlay trace: Frame-local line/shape instructions consumed by RunEditorTracer.
  Body store: Physics-owned live pose rows used for selection and tool markers.
  Collider store: Physics-owned shape rows used for shape-accurate outlines.

Invariants:
  - Overlay building must not mutate scene objects.
  - Hover state is recomputed every frame from the current mouse ray.

Related:
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - SkullbonezSource/Runtime/Editor/RunEditorTracer.inl
*/
namespace SkullbonezCore
{
namespace Basics
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
        EditorTerrainPlacement terrainPlacement;
        const EditorTerrainPlacement* terrainPlacementForPreview = nullptr;
        if ( !context.editor.placementScaleActive && input.hasMouseRay &&
             TryGetEditorTerrainPlacement( context.terrain,
                                           input.mouseRayOrigin,
                                           input.mouseRayDirection,
                                           terrainPlacement ) )
        {
            terrainPlacementForPreview = &terrainPlacement;
        }
        context.editor.placementPreviewVisible =
            TryUpdateEditorPlacementPreview( { context.editor, context.terrain, context.assets },
                                             context.editor.objectType,
                                             terrainPlacementForPreview );
    }

    const bool hasSelection = context.editor.selectedModelIndex >= 0;
    bool selectionHandlesValid = false;
    if ( hasSelection && context.bodyStore && context.colliderStore && context.editor.selectedBody.IsValid() &&
         context.editor.selectedCollider.IsValid() )
    {
        const PhysicsBodyRecord* body = context.bodyStore->RecordForHandle( context.editor.selectedBody );
        const ColliderRecord* collider = context.colliderStore->RecordForHandle( context.editor.selectedCollider );
        selectionHandlesValid = body && collider &&
                                context.bodyStore->ModelIndexForHandle( context.editor.selectedBody ) ==
                                    context.editor.selectedModelIndex &&
                                context.colliderStore->ModelIndexForHandle( context.editor.selectedCollider ) ==
                                    context.editor.selectedModelIndex &&
                                collider->body == context.editor.selectedBody;
    }

    if ( context.editor.selectedModelIndex >= context.models.GetModelCount() ||
         ( hasSelection && !selectionHandlesValid ) )
    {
        // Invariant: Selection stores handles plus a model-index hint. If
        // topology invalidates either side, clear through the interaction
        // command path instead of letting later gizmo code read a stale row.
        result.clearInvalidSelection = true;
        result.inspectSelectionScope = input.inspectGizmoActive;
        return result;
    }

    if ( context.editor.selectedModelIndex >= 0 && !context.editor.gizmoDragActive &&
         !context.editor.placementModeEnabled && input.hasMouseRay )
    {
        UpdateEditorGizmoHotAxes( { context.editor, context.models, context.interaction },
                                  input.mouseRayOrigin,
                                  input.mouseRayDirection,
                                  input.scaleMode );
    }

    return result;
}


void BuildEditorToolOverlayTrace( EditorToolOverlayTraceContext context, const EditorToolOverlayTraceInput& input )
{
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
         context.editor.selectedModelIndex >= 0 && context.editor.selectedModelIndex < context.models.GetModelCount() )
    {
        Vector3 gizmoOrigin;
        float radius = 1.0f;
        const bool scaleMode = context.editor.gizmoDragIsScale || input.scaleMode;
        if ( TryTraceEditorSelectionOverlayFromStores( context.models,
                                                       context.bodyStore,
                                                       context.colliderStore,
                                                       context.editor.selectedBody,
                                                       context.editor.selectedCollider,
                                                       context.editor.selectedModelIndex,
                                                       context.tracer,
                                                       gizmoOrigin,
                                                       radius ) )
        {
            context.tracer.AddGizmo( gizmoOrigin,
                                     radius,
                                     context.editor.hotGizmoAxis,
                                     context.editor.hotRotationAxis,
                                     context.editor.activeGizmoAxis,
                                     context.editor.gizmoDragIsRotation,
                                     scaleMode,
                                     context.editor.gizmoDragIsScale );
        }
    }

    if ( context.mousePickup.active && context.mousePickup.modelIndex >= 0 &&
         context.mousePickup.modelIndex < context.models.GetModelCount() )
    {
        const PhysicsBodyRecord* body = context.bodyStore.RecordForHandle( context.mousePickup.body );
        const PhysicsColliderHandle colliderHandle =
            context.colliderStore.HandleForBodyHandle( context.mousePickup.body );
        const ColliderRecord* collider = context.colliderStore.RecordForHandle( colliderHandle );
        if ( !body || !collider ||
             context.bodyStore.ModelIndexForHandle( context.mousePickup.body ) != context.mousePickup.modelIndex ||
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
            const Vector3 grabPoint = body->position + context.mousePickup.grabOffset;
            context.tracer.AddSelectionOutline( body->position, body->orientation, collider->shape );
            context.tracer.AddReplayPathSegment( grabPoint, context.mousePickup.targetPoint, 0.1f, 0.95f, 1.0f );
            context.tracer.AddReplayContactMarker( context.mousePickup.targetPoint,
                                                   context.mousePickup.planeNormal,
                                                   0.1f,
                                                   0.95f,
                                                   1.0f );
            context.tracer.AddReplayImpulseVector( grabPoint, context.mousePickup.lastImpulse, 0.1f, 0.95f, 1.0f );
        }
    }

    if ( input.attachedCameraTargetIndex >= 0 && input.attachedCameraTargetIndex < context.models.GetModelCount() )
    {
        const PhysicsBodyHandle bodyHandle = context.bodyStore.HandleForModelIndex( input.attachedCameraTargetIndex );
        const PhysicsBodyRecord* body = context.bodyStore.RecordForHandle( bodyHandle );
        const PhysicsColliderHandle colliderHandle =
            context.colliderStore.HandleForBodyHandle( bodyHandle );
        const ColliderRecord* collider = context.colliderStore.RecordForHandle( colliderHandle );
        if ( body && collider &&
             context.bodyStore.ModelIndexForHandle( bodyHandle ) == input.attachedCameraTargetIndex &&
             collider->body == bodyHandle )
        {
            // Why: attached-camera follow is already store-backed; its overlay
            // marker should read the same live body/collider rows instead of
            // keeping legacy model-side pose/shape caches hot for presentation.
            const float markerRadius = EditorColliderRadius( *collider ) * 1.24f;
            context.tracer.AddAttachedCameraTargetMarker( body->position,
                                                          body->orientation,
                                                          collider->shape,
                                                          markerRadius,
                                                          input.attachedCameraActiveFollow );
        }
    }
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
