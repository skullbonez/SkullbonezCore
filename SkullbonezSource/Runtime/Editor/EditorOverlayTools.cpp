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
EditorInteractionPreviewResult UpdateEditorInteractionPreview( RunEditorPlacementState& editor, SceneWorld& world,
                                                               RuntimeInteractionController& interaction,
                                                               const Assets::AssetSystem& assets,
                                                               const EditorInteractionPreviewInput& input )
{
    EditorInteractionPreviewResult result;

    // Concept: Preview refresh is the editor's input-facing phase. It may alter
    // hot axes and placement ghost state, while later overlay tracing only
    // renders the state produced here.
    editor.placementPreviewVisible = false;
    editor.hotGizmoAxis = -1;
    editor.hotRotationAxis = -1;
    Geometry::Terrain* terrain = world.Terrain().Get();
    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const ColliderStore& colliderStore = world.Colliders();

    if ( input.uiBlocksCameraMouse || editor.viewportLookActive )
    {
        return result;
    }

    if ( !editor.editorModeEnabled && !input.inspectGizmoActive )
    {
        return result;
    }

    if ( editor.editorModeEnabled && editor.placementModeEnabled )
    {
        const bool placementScaleActive = interaction.Gesture().kind ==
                                          RuntimeInteractionGestureKind::EditorPlacementScaleDrag;

        EditorTerrainPlacement terrainPlacement;
        const EditorTerrainPlacement* terrainPlacementForPreview = nullptr;

        if ( !placementScaleActive && input.hasMouseRay &&
             TryGetEditorTerrainPlacement( terrain, input.mouseRayOrigin, input.mouseRayDirection, terrainPlacement ) )
        {
            terrainPlacementForPreview = &terrainPlacement;
        }

        editor.placementPreviewVisible = TryUpdateEditorPlacementPreview( editor, terrain, assets, placementScaleActive,
                                                                          editor.objectType, terrainPlacementForPreview );
    }

    const int selectedModelIndex = ResolveSelectedEditorModelIndex( editor, bodyStore );
    const bool hasSelection = selectedModelIndex >= 0;
    bool selectionHandlesValid = false;

    if ( hasSelection && editor.selectedBody.IsValid() && editor.selectedCollider.IsValid() )
    {
        const PhysicsBodyRecord* body = bodyStore.RecordForHandle( editor.selectedBody );
        const ColliderRecord* collider = colliderStore.RecordForHandle( editor.selectedCollider );
        selectionHandlesValid = body && collider &&
                                bodyStore.ModelIndexForHandle( editor.selectedBody ) == selectedModelIndex &&
                                colliderStore.ModelIndexForHandle( editor.selectedCollider ) == selectedModelIndex &&
                                collider->body == editor.selectedBody;
    }

    if ( editor.selectedModelRow.value >= world.SceneEntityCount() ||
         ( editor.selectedBody.IsValid() && !selectionHandlesValid ) )
    {

        // Invariant: Selection stores handles plus a model-row hint. If
        // topology invalidates either side, clear through the interaction
        // command path instead of letting later gizmo code read a stale row.
        result.clearInvalidSelection = true;
        result.inspectSelectionScope = input.inspectGizmoActive;
        return result;
    }

    if ( selectedModelIndex >= 0 && interaction.Gesture().kind != RuntimeInteractionGestureKind::GizmoDrag &&
         !editor.placementModeEnabled && input.hasMouseRay )
    {
        UpdateEditorGizmoHotAxes( editor, world, input.mouseRayOrigin, input.mouseRayDirection, input.scaleMode );
    }

    return result;
}


void BuildEditorToolOverlayTrace( const RunEditorPlacementState& editor, const RunRayCastTestState& rayCastTest,
                                  const RunMousePickupState& mousePickup, const SceneWorld& world,
                                  const Assets::AssetSystem& assets, EditorTracer& tracer,
                                  const EditorToolOverlayTraceInput& input )
{
    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const ColliderStore& colliderStore = world.Colliders();

    // Concept: Overlay trace building is a pure visual pass over editor state.
    // It appends lines, ghosts, markers, and gizmos without claiming input or
    // mutating physics.
    const float rayLinger = (std::max)( 0.0f, input.rayLingerSeconds );

    if ( rayLinger > 0.0f )
    {

        for ( const RunRayCastTestLine& line : rayCastTest.lines )
        {

            if ( line.active && line.ageSeconds < rayLinger )
            {
                tracer.AddRayCastTestLine( line.start, line.end, 1.0f - line.ageSeconds / rayLinger, line.hit );
            }
        }
    }

    if ( editor.editorModeEnabled && editor.placementModeEnabled && editor.placementPreviewVisible )
    {
        tracer.AddPlacementRay( editor.placementRayOrigin, editor.placementRayHit );
        tracer.AddPlacementGhost( editor.objectType, editor.placementCenter, editor.placementTerrainPoint,
                                  editor.placementScale, editor.placementOrientation, assets );
    }

    if ( ( editor.editorModeEnabled || input.inspectGizmoActive ) && !editor.placementModeEnabled &&
         editor.selectedBody.IsValid() )
    {
        const int selectedModelIndex = PeekSelectedEditorModelIndex( editor, bodyStore );
        Vector3 gizmoOrigin;
        float radius = 1.0f;
        const bool gizmoDragActive = input.gesture.kind == RuntimeInteractionGestureKind::GizmoDrag;
        const bool gizmoScale = gizmoDragActive && input.gesture.gizmoKind == RuntimeGizmoDragKind::Scale;
        const bool gizmoRotation = gizmoDragActive && input.gesture.gizmoKind == RuntimeGizmoDragKind::Rotate;
        const bool scaleMode = gizmoScale || input.scaleMode;

        if ( selectedModelIndex >= 0 && selectedModelIndex < world.SceneEntityCount() &&
             TryTraceEditorSelectionOverlayFromStores( world, editor.selectedBody, editor.selectedCollider,
                                                       selectedModelIndex, tracer, gizmoOrigin, radius ) )
        {
            tracer.AddGizmo( gizmoOrigin, radius, editor.hotGizmoAxis, editor.hotRotationAxis,
                             gizmoDragActive ? input.gesture.axis : -1, gizmoRotation, scaleMode, gizmoScale );
        }
    }

    if ( input.gesture.kind == RuntimeInteractionGestureKind::MousePickupDrag && mousePickup.body.IsValid() )
    {
        const PhysicsBodyRecord* body = bodyStore.RecordForHandle( mousePickup.body );
        const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( mousePickup.body );
        const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
        const int modelIndex = bodyStore.ModelIndexForHandle( mousePickup.body );

        if ( !body || !collider || modelIndex < 0 || modelIndex >= world.SceneEntityCount() ||
             collider->body != mousePickup.body )
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
            const Vector3 grabPoint = bodyPosition + mousePickup.grabOffset;
            tracer.AddSelectionOutline( bodyPosition, PhysicsBodyOrientation( hotFields, bodyIndex ), collider->shape );

            tracer.AddReplayPathSegment( grabPoint, mousePickup.targetPoint, 0.1f, 0.95f, 1.0f );
            tracer.AddReplayContactMarker( mousePickup.targetPoint, mousePickup.planeNormal, 0.1f, 0.95f, 1.0f );

            tracer.AddReplayImpulseVector( grabPoint, mousePickup.lastImpulse, 0.1f, 0.95f, 1.0f );
        }
    }

    if ( input.attachedCameraTargetIndex >= 0 && input.attachedCameraTargetIndex < world.SceneEntityCount() )
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
            tracer.AddAttachedCameraTargetMarker( PhysicsBodyPosition( hotFields, bodyIndex ),
                                                  PhysicsBodyOrientation( hotFields, bodyIndex ), collider->shape,
                                                  markerRadius, input.attachedCameraActiveFollow );
        }
    }
}
} // namespace Runtime
} // namespace SkullbonezCore
