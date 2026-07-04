/*
File: SkullbonezSource/Runtime/Editor/RunEditorGizmoTools.inl
Purpose:
  Contains editor transform gizmo hit testing and drag mutation helpers.

Mental model:
  The editor gizmo owns selection-frame math and per-axis drag application.
  Run supplies the current interaction context, but this slice owns the local
  rules for translation, rotation, scale, and grouped ragdoll transforms.

Glossary:
  Gizmo: World-space axes and rotation rings used to transform selected bodies.
  Drag group: Ragdoll parts that move together when a grouped body is selected.
  Hot axis: Axis currently under the mouse ray and eligible for capture.

Invariants:
  - Gizmo group indices are frame-local model indices and must be revalidated
    against the current model count before use.
  - Movement resets model motion so editor transforms do not leave stale solver
    impulses behind.

Related:
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - SkullbonezSource/Runtime/RunInternal.h
*/
namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
int HitEditorGizmoAxis( EditorGizmoContext context, const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( context.editor.selectedModelIndex < 0 || context.editor.selectedModelIndex >= context.models.GetModelCount() )
    {
        return -1;
    }

    const std::vector<GameModel>& models = context.models.Models();
    Vector3 origin;
    float radius = 1.0f;
    if ( !TryGetEditorSelectionFrame( models, context.editor.selectedModelIndex, origin, radius ) )
    {
        return -1;
    }
    const float length = EditorGizmoAxisLength( radius );
    const float threshold = (std::max)( 1.25f, length * 0.06f );
    const float thresholdSq = threshold * threshold;

    int bestAxis = -1;
    float bestDistanceSq = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float distanceSq =
            DistanceRayToSegmentSquared( rayOrigin, rayDirection, origin, origin + axisVector * length );
        if ( distanceSq <= thresholdSq && distanceSq < bestDistanceSq )
        {
            bestDistanceSq = distanceSq;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


int HitEditorRotationGizmoAxis( EditorGizmoContext context, const Vector3& rayOrigin, const Vector3& rayDirection )
{
    if ( context.editor.selectedModelIndex < 0 || context.editor.selectedModelIndex >= context.models.GetModelCount() )
    {
        return -1;
    }

    const std::vector<GameModel>& models = context.models.Models();
    Vector3 origin;
    float radius = 1.0f;
    if ( !TryGetEditorSelectionFrame( models, context.editor.selectedModelIndex, origin, radius ) )
    {
        return -1;
    }
    const float ringRadius = EditorGizmoRotationRadius( radius );
    const float threshold = (std::max)( 1.10f, ringRadius * 0.08f );

    int bestAxis = -1;
    float bestDiff = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 normal = EditorAxisVector( axis );
        const float denom = normal * rayDirection;
        if ( fabsf( denom ) <= 1e-4f )
        {
            continue;
        }

        const float rayT = ( normal * ( origin - rayOrigin ) ) / denom;
        if ( rayT < 0.0f )
        {
            continue;
        }

        const Vector3 hitPoint = rayOrigin + rayDirection * rayT;
        const Vector3 radial = hitPoint - origin;
        const float radialDistance = VectorMag( radial - normal * ( radial * normal ) );
        const float diff = fabsf( radialDistance - ringRadius );
        if ( diff <= threshold && diff < bestDiff )
        {
            bestDiff = diff;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


bool TryEditorAxisRayParameter( EditorGizmoContext context,
                                int axis,
                                const Vector3& rayOrigin,
                                const Vector3& rayDirection,
                                float& outAxisT )
{
    if ( axis < 0 || axis > 2 || context.editor.selectedModelIndex < 0 ||
         context.editor.selectedModelIndex >= context.models.GetModelCount() )
    {
        return false;
    }

    Vector3 axisOrigin;
    float radius = 1.0f;
    if ( !TryGetEditorSelectionFrame( context.models.Models(), context.editor.selectedModelIndex, axisOrigin, radius ) )
    {
        return false;
    }
    const Vector3 axisVector = EditorAxisVector( axis );
    const Vector3 w = axisOrigin - rayOrigin;
    const float b = axisVector * rayDirection;
    const float d = axisVector * w;
    const float e = rayDirection * w;
    const float denom = 1.0f - b * b;
    if ( fabsf( denom ) <= 1e-5f )
    {
        return false;
    }

    outAxisT = ( b * e - d ) / denom;
    return true;
}


// Concept: Translation drags use a plane that contains the active world axis.
// The plane is frozen at gesture start, so later mouse rays project into one
// stable space instead of chasing the already-moved selection frame.
Vector3 EditorAxisDragPlaneNormal( int axis, const Vector3& rayDirection )
{
    const Vector3 axisVector = EditorAxisVector( axis );
    Vector3 normal = rayDirection - axisVector * ( rayDirection * axisVector );
    float normalLenSq = VectorMagSquared( normal );
    if ( normalLenSq <= TOLERANCE * TOLERANCE )
    {
        const Vector3 fallback =
            fabsf( axisVector.y ) < 0.9f ? Vector3( 0.0f, 1.0f, 0.0f ) : Vector3( 1.0f, 0.0f, 0.0f );
        normal = fallback - axisVector * ( fallback * axisVector );
        normalLenSq = VectorMagSquared( normal );
    }
    if ( normalLenSq <= TOLERANCE * TOLERANCE )
    {
        return SkullbonezCore::Math::Vector::ZERO_VECTOR;
    }
    return normal * ( 1.0f / sqrtf( normalLenSq ) );
}


bool TryEditorAxisPlaneRayParameter( int axis,
                                     const Vector3& planeOrigin,
                                     const Vector3& planeNormal,
                                     const Vector3& rayOrigin,
                                     const Vector3& rayDirection,
                                     float& outAxisT )
{
    if ( axis < 0 || axis > 2 )
    {
        return false;
    }

    const float normalLenSq = VectorMagSquared( planeNormal );
    if ( normalLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    const float denom = rayDirection * planeNormal;
    if ( fabsf( denom ) <= 1e-5f )
    {
        return false;
    }

    const float rayT = ( ( planeOrigin - rayOrigin ) * planeNormal ) / denom;
    if ( rayT < 0.0f )
    {
        return false;
    }

    const Vector3 hitPoint = rayOrigin + rayDirection * rayT;
    outAxisT = ( hitPoint - planeOrigin ) * EditorAxisVector( axis );
    return std::isfinite( outAxisT );
}


bool TryEditorRotationRayAngle( EditorGizmoContext context,
                                int axis,
                                const Vector3& rayOrigin,
                                const Vector3& rayDirection,
                                float& outAngle )
{
    if ( axis < 0 || axis > 2 || context.editor.selectedModelIndex < 0 ||
         context.editor.selectedModelIndex >= context.models.GetModelCount() )
    {
        return false;
    }

    Vector3 origin;
    float radius = 1.0f;
    if ( !TryGetEditorSelectionFrame( context.models.Models(), context.editor.selectedModelIndex, origin, radius ) )
    {
        return false;
    }
    const Vector3 normal = EditorAxisVector( axis );
    const float denom = normal * rayDirection;
    if ( fabsf( denom ) <= 1e-4f )
    {
        return false;
    }

    const float rayT = ( normal * ( origin - rayOrigin ) ) / denom;
    if ( rayT < 0.0f )
    {
        return false;
    }

    Vector3 radial = rayOrigin + rayDirection * rayT - origin;
    radial -= normal * ( radial * normal );
    const float radialLenSq = radial * radial;
    if ( radialLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }
    radial = radial * ( 1.0f / sqrtf( radialLenSq ) );

    const Vector3 basisA = EditorRotationRingBasisA( axis );
    const Vector3 basisB = EditorRotationRingBasisB( axis );
    outAngle = atan2f( radial * basisB, radial * basisA );
    return true;
}


void MoveSelectedEditorObjectAlongAxis( EditorGizmoContext context,
                                        const Vector3& rayOrigin,
                                        const Vector3& rayDirection )
{
    if ( !context.editor.gizmoDragActive || context.editor.activeGizmoAxis < 0 )
    {
        return;
    }

    float axisT = 0.0f;
    if ( !TryEditorAxisPlaneRayParameter( context.editor.activeGizmoAxis,
                                          context.editor.gizmoDragStartPosition,
                                          context.editor.gizmoDragPlaneNormal,
                                          rayOrigin,
                                          rayDirection,
                                          axisT ) )
    {
        return;
    }

    const int index = context.editor.selectedModelIndex;
    if ( index < 0 || index >= context.models.GetModelCount() )
    {
        CancelEditorGizmoDragState( context );
        return;
    }

    GameModel& model = context.models.GetModelAtIndex( index );
    const Vector3 axisVector = EditorAxisVector( context.editor.activeGizmoAxis );
    const Vector3 delta = axisVector * ( axisT - context.editor.gizmoDragStartAxisT );
    const int groupCount = ValidCapturedEditorGizmoGroupCount( context.editor, context.models.GetModelCount() );
    if ( groupCount > 0 )
    {
        // Invariant: Group drags reuse the gesture-start transform snapshot for
        // every member, so multi-part ragdolls move rigidly even if physics
        // wakes during the drag.
        for ( int groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const int modelIndex = context.editor.gizmoDragGroupIndices[static_cast<std::size_t>( groupIndex )];
            GameModel& groupModel = context.models.GetModelAtIndex( modelIndex );
            groupModel.SetPosition(
                context.editor.gizmoDragGroupStartPositions[static_cast<std::size_t>( groupIndex )] + delta );
            ResetEditorModelMotionAndWake( context.models, context.models.GetPhysicsEngine(), modelIndex, groupModel );
        }
    }
    else
    {
        const Vector3 newPosition = context.editor.gizmoDragStartPosition + delta;
        model.SetPosition( newPosition );
        ResetEditorModelMotionAndWake( context.models, context.models.GetPhysicsEngine(), index, model );
    }
}


void ScaleSelectedEditorObjectAlongAxis( EditorGizmoContext context,
                                         const Vector3& rayOrigin,
                                         const Vector3& rayDirection )
{
    if ( !context.editor.gizmoDragActive || !context.editor.gizmoDragIsScale || context.editor.activeGizmoAxis < 0 )
    {
        return;
    }

    float axisT = 0.0f;
    if ( !TryEditorAxisRayParameter( context, context.editor.activeGizmoAxis, rayOrigin, rayDirection, axisT ) )
    {
        return;
    }

    const int index = context.editor.selectedModelIndex;
    if ( index < 0 || index >= context.models.GetModelCount() )
    {
        CancelEditorGizmoDragState( context );
        return;
    }

    const float startExtent =
        EditorShapeAxisExtent( context.editor.gizmoDragStartShape, context.editor.activeGizmoAxis );
    const float targetExtent = (std::max)( 0.25f, startExtent + axisT - context.editor.gizmoDragStartAxisT );
    const float factor = targetExtent / startExtent;

    GameModel& model = context.models.GetModelAtIndex( index );
    if ( model.ScaleCollisionShapeAxisFromBase( context.editor.gizmoDragStartShape,
                                                context.editor.activeGizmoAxis,
                                                factor ) )
    {
        ResetEditorModelMotionAndWake( context.models, context.models.GetPhysicsEngine(), index, model, true );
    }
}


void RotateSelectedEditorObjectAroundAxis( EditorGizmoContext context,
                                           const Vector3& rayOrigin,
                                           const Vector3& rayDirection )
{
    if ( !context.editor.gizmoDragActive || !context.editor.gizmoDragIsRotation || context.editor.activeGizmoAxis < 0 )
    {
        return;
    }

    float currentAngle = 0.0f;
    if ( !TryEditorRotationRayAngle( context, context.editor.activeGizmoAxis, rayOrigin, rayDirection, currentAngle ) )
    {
        return;
    }

    const int index = context.editor.selectedModelIndex;
    if ( index < 0 || index >= context.models.GetModelCount() )
    {
        CancelEditorGizmoDragState( context );
        return;
    }

    const Vector3 axisVector = EditorAxisVector( context.editor.activeGizmoAxis );
    const float angleDelta = WrapEditorAngleDelta( currentAngle - context.editor.gizmoDragStartRotationAngle );
    const int groupCount = ValidCapturedEditorGizmoGroupCount( context.editor, context.models.GetModelCount() );
    if ( groupCount > 0 )
    {
        // Invariant: Rotation groups pivot around the captured selection
        // center, not each part's own center, preserving the authored assembly.
        for ( int groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const int modelIndex = context.editor.gizmoDragGroupIndices[static_cast<std::size_t>( groupIndex )];
            GameModel& groupModel = context.models.GetModelAtIndex( modelIndex );
            const Vector3 startOffset =
                context.editor.gizmoDragGroupStartPositions[static_cast<std::size_t>( groupIndex )] -
                context.editor.gizmoDragStartPosition;
            Quaternion orientation =
                context.editor.gizmoDragGroupStartOrientations[static_cast<std::size_t>( groupIndex )];
            orientation.RotateAboutAxis( axisVector, angleDelta );
            groupModel.SetPosition( context.editor.gizmoDragStartPosition +
                                    RotatePointAboutArbitrary( angleDelta, axisVector, startOffset ) );
            groupModel.SetOrientation( orientation );
            ResetEditorModelMotionAndWake( context.models, context.models.GetPhysicsEngine(), modelIndex, groupModel );
        }
    }
    else
    {
        Quaternion orientation = context.editor.gizmoDragStartOrientation;
        orientation.RotateAboutAxis( axisVector, angleDelta );
        GameModel& model = context.models.GetModelAtIndex( index );
        model.SetOrientation( orientation );
        ResetEditorModelMotionAndWake( context.models, context.models.GetPhysicsEngine(), index, model );
    }
}


void UpdateEditorGizmoHotAxes( EditorGizmoContext context,
                               const Vector3& rayOrigin,
                               const Vector3& rayDirection,
                               bool scaleMode )
{
    if ( scaleMode )
    {
        context.editor.hotGizmoAxis = HitEditorGizmoAxis( context, rayOrigin, rayDirection );
        return;
    }

    context.editor.hotRotationAxis = HitEditorRotationGizmoAxis( context, rayOrigin, rayDirection );
    context.editor.hotGizmoAxis =
        context.editor.hotRotationAxis < 0 ? HitEditorGizmoAxis( context, rayOrigin, rayDirection ) : -1;
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
