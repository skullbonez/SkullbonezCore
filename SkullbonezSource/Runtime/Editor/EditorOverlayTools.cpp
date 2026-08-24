/*
File: SkullbonezSource/Runtime/Editor/EditorOverlayTools.cpp
Purpose:
  Owns editor preview and transition state independently of generic tool rendering.

Summary:
  EditorToolsOwner refreshes placement/gizmo state from detached input and clears
  it at interaction or scene boundaries. App separately projects that state for
  the Tools-owned overlay renderer.

Invariants:
  - Preview refresh never submits render work.
  - Scene lifecycle clears editor state once per generation.
*/
#include "EditorOverlayTools.h"
#include "EditorPlacementAssets.h"
#include "EditorTools.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Scene/SceneWorld.h"
#include "../Tools/EditorTracer.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"

#include <algorithm>

namespace SkullbonezCore::Runtime
{
void EditorToolsOwner::AppendPlacementGhost( EditorTracer& tracer, const Assets::AssetSystem& assets ) const
{
    if ( !m_editor.editorModeEnabled || !m_editor.placementModeEnabled || !m_editor.placementPreviewVisible )
    {
        return;
    }

    using namespace Math::CollisionDetection;
    using Math::Orientation::Quaternion;
    using Math::Transformation::RotationMatrix;
    using Math::Vector::Vector3;
    constexpr float ghostR = 0.25f;
    constexpr float ghostG = 1.0f;
    constexpr float ghostB = 0.85f;
    const int type = std::clamp( m_editor.objectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const Vector3 scale = EditorClampPlacementScale( type, m_editor.placementScale );
    Quaternion orientation = m_editor.placementOrientation;
    const RotationMatrix rotation = orientation.GetOrientationMatrix();
    const Vector3 terrainPoint = m_editor.placementTerrainPoint;
    const Vector3 center = m_editor.placementCenter;

    auto appendHull = [&]( const ConvexHullShape& hull, const Vector3& hullCenter, const RotationMatrix& hullRotation )
    {
        for ( uint16_t edgeIndex = 0; edgeIndex < hull.GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull.GetEdge( edgeIndex );
            tracer.AddLine( hullCenter + hullRotation * hull.GetVertex( edge.vertexA ),
                            hullCenter + hullRotation * hull.GetVertex( edge.vertexB ), ghostR, ghostG, ghostB );
        }
    };

    if ( const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );

        for ( int partIndex = 0; partIndex < tree->partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = tree->parts[partIndex];

            if ( const ConvexHullShape* hull = CachedEditorHullForAsset( m_resultDiagnostics, part.hullAsset ) )
            {
                appendHull( *hull,
                            base + rotation * ( Vector3( part.offsetX, part.offsetY, part.offsetZ ) +
                                                HullAuthoredLocalOffset( *hull ) ),
                            rotation );
            }
        }

        return;
    }

    if ( EditorBuildingDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        ForEachEditorBuildingPart( type, assets,
                                   [&]( const EditorPlacementJson& part )
                                   {
                                       const Vector3 bodyCenter = base +
                                                                  rotation * EditorJsonVec3Or( part, "offset",
                                                                                               Vector3( 0.0f, 0.0f, 0.0f ) );
                                       Quaternion partOrientation = EditorBuildingPartOrientation( orientation, part );
                                       const RotationMatrix partRotation = partOrientation.GetOrientationMatrix();
                                       const std::string primitiveType = EditorAssetPrimitiveType( part );

                                       if ( primitiveType == "convexHull" )
                                       {
                                           const std::string hullPath = EditorJsonStringOr( part, "hull", "" );

                                           if ( const ConvexHullShape*
                                                    hull = hullPath.empty()
                                                               ? nullptr
                                                               : CachedEditorBuildingHull( m_resultDiagnostics, hullPath ) )
                                           {
                                               appendHull( *hull,
                                                           bodyCenter + partRotation * ( hull->GetAuthoredCenterOfMass() +
                                                                                         hull->GetPosition() ),
                                                           partRotation );
                                           }
                                       }
                                       else if ( primitiveType == "box" )
                                       {
                                           Vector3 halfExtents;

                                           if ( TryReadEditorBoxHalfExtents( part, halfExtents ) )
                                           {
                                               tracer.AddBoxOutline( bodyCenter,
                                                                     partRotation * Vector3( halfExtents.x, 0.0f, 0.0f ),
                                                                     partRotation * Vector3( 0.0f, halfExtents.y, 0.0f ),
                                                                     partRotation * Vector3( 0.0f, 0.0f, halfExtents.z ),
                                                                     ghostR, ghostG, ghostB );
                                           }
                                       }
                                       else if ( primitiveType == "sphere" )
                                       {
                                           float radius = 0.0f;

                                           if ( TryReadEditorSphereRadius( part, radius ) )
                                           {
                                               tracer.AddSphereOutline( bodyCenter, radius, ghostR, ghostG, ghostB );
                                           }
                                       }
                                   } );
        return;
    }

    if ( const EditorHouseDefinition* house = EditorHouseDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );

        for ( int partIndex = 0; partIndex < house->partCount; ++partIndex )
        {
            const EditorHousePartDefinition& part = house->parts[partIndex];
            tracer.AddBoxOutline( base + rotation * Vector3( part.offsetX, part.offsetY, part.offsetZ ),
                                  rotation * Vector3( part.halfX, 0.0f, 0.0f ), rotation * Vector3( 0.0f, part.halfY, 0.0f ),
                                  rotation * Vector3( 0.0f, 0.0f, part.halfZ ), ghostR, ghostG, ghostB );
        }

        return;
    }

    if ( type == UI::EditorTab::OBJECT_BOX )
    {
        tracer.AddBoxOutline( center, rotation * Vector3( scale.x, 0.0f, 0.0f ), rotation * Vector3( 0.0f, scale.y, 0.0f ),
                              rotation * Vector3( 0.0f, 0.0f, scale.z ), ghostR, ghostG, ghostB );
    }
    else if ( type == UI::EditorTab::OBJECT_BALL || type == UI::EditorTab::OBJECT_SPHERE )
    {
        tracer.AddSphereOutline( center, scale.x, ghostR, ghostG, ghostB );
    }
    else if ( type == UI::EditorTab::OBJECT_RAGDOLL || type == UI::EditorTab::OBJECT_RAGDOLL_SLEEP )
    {
        tracer.AddRagdollOutline( terrainPoint, scale.x, orientation, ghostR, ghostG, ghostB );
    }
    else
    {
        ConvexHullShape hull;

        if ( TryBuildScaledEditorHullForType( m_resultDiagnostics, type, scale, hull ) )
        {
            appendHull( hull, center + rotation * hull.GetPosition(), rotation );
        }
    }
}

bool EditorToolsOwner::HasActiveEditorInteractionState( const RuntimeInteractionController& interaction ) const
{
    const RuntimeInteractionGestureKind gesture = interaction.Gesture().kind;
    return m_editor.editorModeEnabled || m_editor.placementModeEnabled || m_editor.viewportLookActive ||
           m_editor.placementPreviewVisible || gesture == RuntimeInteractionGestureKind::EditorPlacementScaleDrag ||
           gesture == RuntimeInteractionGestureKind::GizmoDrag || m_editor.hotGizmoAxis >= 0 ||
           m_editor.hotRotationAxis >= 0;
}

void EditorToolsOwner::ClearEditorInteractionForTransition( bool clearSelection, SceneWorld& world,
                                                            RuntimeInteractionController& interaction )
{
    ClearEditorManipulationState( m_editor, interaction );
    m_editor.viewportLookActive = false;
    m_editor.placementModeEnabled = false;
    m_editor.hotGizmoAxis = -1;
    m_editor.hotRotationAxis = -1;

    if ( clearSelection )
    {
        RuntimeInteractionCommand command;
        command.type = RuntimeInteractionCommandType::SetEditorSelection;
        command.claimSelectionOwner = false;
        ApplySelectionCommand( command, world );
    }
}

void EditorToolsOwner::ObserveSceneLifecycle( const SceneLifecyclePacket& packet, SceneWorld& world,
                                              RuntimeInteractionController& interaction )
{
    if ( !m_sceneLifecycleObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        return;
    }

    ClearEditorInteractionForTransition( false, world, interaction );
    ClearEditorHistory();
}

EditorInteractionPreviewResult UpdateEditorInteractionPreview( Core::SbDiagnosticStore& diagnostics,
                                                               RunEditorPlacementState& editor, SceneWorld& world,
                                                               RuntimeInteractionController& interaction,
                                                               const Assets::AssetSystem& assets,
                                                               const EditorInteractionPreviewInput& input )
{
    EditorInteractionPreviewResult result;
    editor.placementPreviewVisible = false;
    editor.hotGizmoAxis = -1;
    editor.hotRotationAxis = -1;
    Geometry::Terrain* terrain = world.Terrain().Get();
    const Physics::PhysicsBodyStore& bodyStore = world.BodyStore();
    const Physics::ColliderStore& colliderStore = world.Colliders();

    if ( input.uiBlocksCameraMouse || editor.viewportLookActive ||
         ( !editor.editorModeEnabled && !input.inspectGizmoActive ) )
    {
        return result;
    }

    if ( editor.editorModeEnabled && editor.placementModeEnabled )
    {
        const bool scaleActive = interaction.Gesture().kind == RuntimeInteractionGestureKind::EditorPlacementScaleDrag;
        EditorTerrainPlacement terrainPlacement;
        const EditorTerrainPlacement* placement = nullptr;

        if ( !scaleActive && input.hasMouseRay &&
             TryGetEditorTerrainPlacement( terrain, input.mouseRayOrigin, input.mouseRayDirection, terrainPlacement ) )
        {
            placement = &terrainPlacement;
        }

        editor.placementPreviewVisible = TryUpdateEditorPlacementPreview( diagnostics, editor, terrain, assets, scaleActive,
                                                                          editor.objectType, placement );
    }

    const int selectedModelIndex = ResolveSelectedEditorModelIndex( editor, bodyStore );
    bool selectionHandlesValid = false;

    if ( selectedModelIndex >= 0 && editor.selectedBody.IsValid() && editor.selectedCollider.IsValid() )
    {
        const Physics::PhysicsBodyRecord* body = bodyStore.RecordForHandle( editor.selectedBody );
        const Physics::ColliderRecord* collider = colliderStore.RecordForHandle( editor.selectedCollider );
        selectionHandlesValid = body && collider &&
                                bodyStore.ModelIndexForHandle( editor.selectedBody ) == selectedModelIndex &&
                                colliderStore.ModelIndexForHandle( editor.selectedCollider ) == selectedModelIndex &&
                                collider->body == editor.selectedBody;
    }

    if ( editor.selectedModelRow.value >= world.SceneEntityCount() ||
         ( editor.selectedBody.IsValid() && !selectionHandlesValid ) )
    {
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
} // namespace SkullbonezCore::Runtime
