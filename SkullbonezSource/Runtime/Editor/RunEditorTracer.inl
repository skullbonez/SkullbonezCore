/*
File: SkullbonezSource/Runtime/Editor/RunEditorTracer.inl
Purpose:
  Implements runtime editor overlay tracer primitives and draw submission.

Mental model:
  The tracer turns editor/replay tool state into transient colored lines. It observes
  state prepared elsewhere and should not mutate selection, physics, or replay ownership.

Glossary:
  Tracer: Per-frame line builder for placement rays, gizmos, replay paths, and selection outlines.
  Gizmo: World-space translate, rotate, or scale affordance drawn over selected models.
  Selection outline: Shape-accurate wire outline drawn from explicit pose and
    collision-shape values supplied by the owning tool.
  Replay target marker: Replay overlay outline/ring drawn from explicit
    body-store pose and collider-store shape/radius values.
  Replay future marker: Shape-accurate downstream collision outline drawn at
    the latest visible predicted/retained pose, never from a broadphase radius substitute.
  Placement ghost: Preview outline drawn before an editor placement commit; it
    must match the primitive bodies that placement will actually spawn.

Invariants:
  - Trace generation must stay transient; line buffers are cleared every frame by the caller.
  - Replay causal entry/rest markers use priority line storage so expensive
    prediction paths can degrade without erasing already-revealed boxes.
  - This file must only be included from RunEditorTools.cpp after tracer helper math is declared.

Related:
  - SkullbonezSource/Runtime/Editor/EditorOverlayTools.h
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/

namespace
{
constexpr std::size_t RUN_EDITOR_TRACER_LINE_FLOAT_CAPACITY = 262144;
constexpr std::size_t RUN_EDITOR_TRACER_PRIORITY_LINE_FLOAT_CAPACITY = 524288;
constexpr std::size_t RUN_EDITOR_TRACER_FLOATS_PER_LINE = 12;
constexpr float RUN_EDITOR_TRACER_REPLAY_LINE_OPACITY = 0.5f;
} // namespace


RunEditorTracer::RunEditorTracer()
{
    // Runtime allocation policy: overlay line storage is paid once during tool
    // construction. EmitLine refuses overflow so replay prediction, gizmos, and
    // target markers cannot grow this vector while render builds the frame.
    m_lineData.reserve( RUN_EDITOR_TRACER_LINE_FLOAT_CAPACITY );
    m_priorityLineData.reserve( RUN_EDITOR_TRACER_PRIORITY_LINE_FLOAT_CAPACITY );
    m_renderLineData.reserve( RUN_EDITOR_TRACER_LINE_FLOAT_CAPACITY + RUN_EDITOR_TRACER_PRIORITY_LINE_FLOAT_CAPACITY );
}


void RunEditorTracer::Clear()
{
    m_lineData.clear();
    m_priorityLineData.clear();
    m_renderLineData.clear();
}


void RunEditorTracer::EmitLineTo( std::vector<float>& lineData,
                                  const Vector3& a,
                                  const Vector3& b,
                                  float r,
                                  float g,
                                  float bl )
{
    if ( lineData.size() + RUN_EDITOR_TRACER_FLOATS_PER_LINE > lineData.capacity() )
    {
        return;
    }
    lineData.insert( lineData.end(), { a.x, a.y, a.z, r, g, bl, b.x, b.y, b.z, r, g, bl } );
}


void RunEditorTracer::EmitLine( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    EmitLineTo( m_lineData, a, b, r, g, bl );
}


void RunEditorTracer::EmitArrow( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    EmitLine( a, b, r, g, bl );

    Vector3 dir = b - a;
    const float len = VectorMag( dir );
    if ( len <= TOLERANCE )
    {
        return;
    }
    dir /= len;

    Vector3 side = fabsf( dir.y ) < 0.8f ? CrossProduct( dir, Vector3( 0.0f, 1.0f, 0.0f ) )
                                         : CrossProduct( dir, Vector3( 1.0f, 0.0f, 0.0f ) );
    const float sideLen = VectorMag( side );
    if ( sideLen <= TOLERANCE )
    {
        return;
    }
    side /= sideLen;

    const float head = (std::min)( len * 0.25f, 2.0f );
    const Vector3 base = b - dir * head;
    EmitLine( b, base + side * ( head * 0.45f ), r, g, bl );
    EmitLine( b, base - side * ( head * 0.45f ), r, g, bl );
}


void RunEditorTracer::EmitRing( const Vector3& center, int axis, float radius, float r, float g, float bl )
{
    constexpr int segments = 64;
    const Vector3 basisA = EditorRotationRingBasisA( axis );
    const Vector3 basisB = EditorRotationRingBasisB( axis );
    Vector3 previous = center + basisA * radius;
    for ( int i = 1; i <= segments; ++i )
    {
        const float theta = static_cast<float>( i ) * ( 2.0f * _PI / static_cast<float>( segments ) );
        const Vector3 next = center + basisA * ( cosf( theta ) * radius ) + basisB * ( sinf( theta ) * radius );
        EmitLine( previous, next, r, g, bl );
        previous = next;
    }
}


void RunEditorTracer::EmitSphereTo( std::vector<float>& lineData,
                                    const Vector3& center,
                                    float radius,
                                    float r,
                                    float g,
                                    float bl )
{
    constexpr int segments = 32;
    for ( int plane = 0; plane < 3; ++plane )
    {
        Vector3 previous;
        for ( int i = 0; i <= segments; ++i )
        {
            const float theta = static_cast<float>( i ) * ( 2.0f * _PI / static_cast<float>( segments ) );
            const float c = cosf( theta ) * radius;
            const float s = sinf( theta ) * radius;
            Vector3 next = center;
            if ( plane == 0 )
            {
                next.x += c;
                next.z += s;
            }
            else if ( plane == 1 )
            {
                next.x += c;
                next.y += s;
            }
            else
            {
                next.y += c;
                next.z += s;
            }

            if ( i > 0 )
            {
                EmitLineTo( lineData, previous, next, r, g, bl );
            }
            previous = next;
        }
    }
}


void RunEditorTracer::EmitSphere( const Vector3& center, float radius, float r, float g, float bl )
{
    EmitSphereTo( m_lineData, center, radius, r, g, bl );
}


void RunEditorTracer::EmitBoxTo( std::vector<float>& lineData,
                                 const Vector3& center,
                                 const Vector3& xAxis,
                                 const Vector3& yAxis,
                                 const Vector3& zAxis,
                                 float r,
                                 float g,
                                 float bl )
{
    const Vector3 corners[8] = {
        center - xAxis - yAxis - zAxis,
        center + xAxis - yAxis - zAxis,
        center + xAxis + yAxis - zAxis,
        center - xAxis + yAxis - zAxis,
        center - xAxis - yAxis + zAxis,
        center + xAxis - yAxis + zAxis,
        center + xAxis + yAxis + zAxis,
        center - xAxis + yAxis + zAxis,
    };

    static constexpr int kEdges[12][2] = {
        { 0, 1 },
        { 1, 2 },
        { 2, 3 },
        { 3, 0 },
        { 4, 5 },
        { 5, 6 },
        { 6, 7 },
        { 7, 4 },
        { 0, 4 },
        { 1, 5 },
        { 2, 6 },
        { 3, 7 },
    };
    for ( const auto& edge : kEdges )
    {
        EmitLineTo( lineData, corners[edge[0]], corners[edge[1]], r, g, bl );
    }
}


void RunEditorTracer::EmitBox( const Vector3& center,
                               const Vector3& xAxis,
                               const Vector3& yAxis,
                               const Vector3& zAxis,
                               float r,
                               float g,
                               float bl )
{
    EmitBoxTo( m_lineData, center, xAxis, yAxis, zAxis, r, g, bl );
}


void RunEditorTracer::EmitShapeOutlineTo( std::vector<float>& lineData,
                                          const Vector3& position,
                                          const Quaternion& orientation,
                                          const CollisionShape& shape,
                                          float r,
                                          float g,
                                          float b )
{
    Quaternion outlineOrientation = orientation;
    const RotationMatrix rot = outlineOrientation.GetOrientationMatrix();

    if ( const BoundingSphere* sphere = std::get_if<BoundingSphere>( &shape ) )
    {
        EmitSphereTo( lineData, position + rot * sphere->GetPosition(), sphere->GetBoundingRadius(), r, g, b );
        return;
    }
    if ( const BoundingBox* box = std::get_if<BoundingBox>( &shape ) )
    {
        const Vector3& he = box->GetHalfExtents();
        const Vector3 center = position + rot * box->GetPosition();
        EmitBoxTo( lineData,
                   center,
                   rot * Vector3( he.x, 0.0f, 0.0f ),
                   rot * Vector3( 0.0f, he.y, 0.0f ),
                   rot * Vector3( 0.0f, 0.0f, he.z ),
                   r,
                   g,
                   b );
        return;
    }
    if ( const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &shape ) )
    {
        const Vector3 hullCenter = position + rot * hull->GetPosition();
        for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
            EmitLineTo( lineData,
                        hullCenter + rot * hull->GetVertex( edge.vertexA ),
                        hullCenter + rot * hull->GetVertex( edge.vertexB ),
                        r,
                        g,
                        b );
        }
    }
}


void RunEditorTracer::EmitShapeOutline( const Vector3& position,
                                        const Quaternion& orientation,
                                        const CollisionShape& shape,
                                        float r,
                                        float g,
                                        float b )
{
    EmitShapeOutlineTo( m_lineData, position, orientation, shape, r, g, b );
}


void RunEditorTracer::AddPlacementRay( const Vector3& rayOrigin, const Vector3& hitPoint )
{
    EmitLine( rayOrigin, hitPoint, 0.25f, 0.80f, 1.0f );
}


void RunEditorTracer::AddPlacementGhost( int objectType,
                                         const Vector3& center,
                                         const Vector3& terrainPoint,
                                         const Vector3& placementScale,
                                         const Quaternion& orientation,
                                         const Assets::AssetSystem& assets )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const Vector3 scale = EditorClampPlacementScale( type, placementScale );
    Quaternion orientationCopy = orientation;
    const RotationMatrix rotation = orientationCopy.GetOrientationMatrix();
    constexpr float ghostR = 0.25f;
    constexpr float ghostG = 1.0f;
    constexpr float ghostB = 0.85f;

    if ( const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        for ( int partIndex = 0; partIndex < tree->partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = tree->parts[partIndex];
            const ConvexHullShape* hull = CachedEditorHullForAsset( part.hullAsset );
            if ( !hull )
            {
                continue;
            }
            const Vector3 hullCenter = base + rotation * ( Vector3( part.offsetX, part.offsetY, part.offsetZ ) +
                                                           HullAuthoredLocalOffset( *hull ) );
            for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
            {
                const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
                EmitLine( hullCenter + rotation * hull->GetVertex( edge.vertexA ),
                          hullCenter + rotation * hull->GetVertex( edge.vertexB ),
                          ghostR,
                          ghostG,
                          ghostB );
            }
        }
        return;
    }
    if ( EditorBuildingDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        ForEachEditorBuildingPart(
            type,
            assets,
            [&]( const Json& part )
            {
                const Vector3 offset = EditorJsonVec3Or( part, "offset", Vector3( 0.0f, 0.0f, 0.0f ) );
                const Quaternion partOrientation = EditorBuildingPartOrientation( orientation, part );
                Quaternion partCopy = partOrientation;
                const RotationMatrix partRotation = partCopy.GetOrientationMatrix();
                const Vector3 bodyCenter = base + rotation * offset;
                const std::string primitiveType = EditorAssetPrimitiveType( part );
                if ( primitiveType == "convexHull" )
                {
                    const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
                    const ConvexHullShape* hull = hullPath.empty() ? nullptr : CachedEditorBuildingHull( hullPath );
                    if ( !hull )
                    {
                        return;
                    }
                    const Vector3 hullCenter =
                        bodyCenter + partRotation * ( hull->GetAuthoredCenterOfMass() + hull->GetPosition() );
                    for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
                    {
                        const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
                        EmitLine( hullCenter + partRotation * hull->GetVertex( edge.vertexA ),
                                  hullCenter + partRotation * hull->GetVertex( edge.vertexB ),
                                  ghostR,
                                  ghostG,
                                  ghostB );
                    }
                    return;
                }
                if ( primitiveType == "box" )
                {
                    Vector3 halfExtents;
                    if ( !TryReadEditorBoxHalfExtents( part, halfExtents ) )
                    {
                        return;
                    }
                    EmitBox( bodyCenter,
                             partRotation * Vector3( halfExtents.x, 0.0f, 0.0f ),
                             partRotation * Vector3( 0.0f, halfExtents.y, 0.0f ),
                             partRotation * Vector3( 0.0f, 0.0f, halfExtents.z ),
                             ghostR,
                             ghostG,
                             ghostB );
                    return;
                }
                if ( primitiveType == "sphere" )
                {
                    float radius = 0.0f;
                    if ( TryReadEditorSphereRadius( part, radius ) )
                    {
                        EmitSphere( bodyCenter, radius, ghostR, ghostG, ghostB );
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
            const Vector3 partCenter = base + rotation * Vector3( part.offsetX, part.offsetY, part.offsetZ );
            EmitBox( partCenter,
                     rotation * Vector3( part.halfX, 0.0f, 0.0f ),
                     rotation * Vector3( 0.0f, part.halfY, 0.0f ),
                     rotation * Vector3( 0.0f, 0.0f, part.halfZ ),
                     ghostR,
                     ghostG,
                     ghostB );
        }
        return;
    }

    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_BOX:
        EmitBox( center,
                 rotation * Vector3( scale.x, 0.0f, 0.0f ),
                 rotation * Vector3( 0.0f, scale.y, 0.0f ),
                 rotation * Vector3( 0.0f, 0.0f, scale.z ),
                 ghostR,
                 ghostG,
                 ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_BALL:
        EmitSphere( center, scale.x, ghostR, ghostG, ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_SPHERE:
        EmitSphere( center, scale.x, ghostR, ghostG, ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_RAGDOLL:
    case SkullbonezCore::UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        Ragdoll::AddPreviewLines( m_lineData, terrainPoint, scale.x, orientation, ghostR, ghostG, ghostB );
        break;
    default:
    {
        ConvexHullShape hull;
        if ( !TryBuildScaledEditorHullForType( type, scale, hull ) )
        {
            return;
        }
        const Vector3 hullCenter = center + rotation * hull.GetPosition();
        for ( uint16_t edgeIndex = 0; edgeIndex < hull.GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull.GetEdge( edgeIndex );
            EmitLine( hullCenter + rotation * hull.GetVertex( edge.vertexA ),
                      hullCenter + rotation * hull.GetVertex( edge.vertexB ),
                      ghostR,
                      ghostG,
                      ghostB );
        }
        break;
    }
    }
}


void RunEditorTracer::AddRayCastTestLine( const Vector3& start, const Vector3& end, float alpha, bool hit )
{
    alpha = std::clamp( alpha, 0.0f, 1.0f );
    if ( alpha <= 0.0f )
    {
        return;
    }

    const float r = hit ? 1.0f : 0.35f;
    const float g = hit ? 0.34f : 0.72f;
    const float b = hit ? 0.12f : 1.0f;
    EmitLine( start, end, r * alpha, g * alpha, b * alpha );
}

void RunEditorTracer::AddReplayPathSegment( const Vector3& start, const Vector3& end, float r, float g, float b )
{
    EmitLine( start,
              end,
              r * RUN_EDITOR_TRACER_REPLAY_LINE_OPACITY,
              g * RUN_EDITOR_TRACER_REPLAY_LINE_OPACITY,
              b * RUN_EDITOR_TRACER_REPLAY_LINE_OPACITY );
}


void RunEditorTracer::AddReplayCausalTrailSegment( const Vector3& start, const Vector3& end, float r, float g, float b )
{
    // Why: retained causal trails are the evidence attached to yellow/grey/ghost
    // boxes. They live with the priority markers so overflow in ordinary path
    // rendering cannot leave a marker without its sampled route.
    EmitLineTo( m_priorityLineData,
                start,
                end,
                r * RUN_EDITOR_TRACER_REPLAY_LINE_OPACITY,
                g * RUN_EDITOR_TRACER_REPLAY_LINE_OPACITY,
                b * RUN_EDITOR_TRACER_REPLAY_LINE_OPACITY );
}


void RunEditorTracer::AddReplayContactMarker( const Vector3& point, const Vector3& normal, float r, float g, float b )
{
    constexpr float crossSize = 0.55f;
    EmitLine( point - Vector3( crossSize, 0.0f, 0.0f ), point + Vector3( crossSize, 0.0f, 0.0f ), r, g, b );
    EmitLine( point - Vector3( 0.0f, crossSize, 0.0f ), point + Vector3( 0.0f, crossSize, 0.0f ), r, g, b );
    EmitLine( point - Vector3( 0.0f, 0.0f, crossSize ), point + Vector3( 0.0f, 0.0f, crossSize ), r, g, b );
    if ( VectorMagSquared( normal ) > TOLERANCE * TOLERANCE )
    {
        EmitArrow( point, point + normal * 1.8f, r, g, b );
    }
}


void RunEditorTracer::AddReplayImpulseVector( const Vector3& point, const Vector3& impulse, float r, float g, float b )
{
    const float magSq = VectorMagSquared( impulse );
    if ( magSq <= TOLERANCE * TOLERANCE )
    {
        return;
    }

    Vector3 direction = impulse;
    const float magnitude = sqrtf( magSq );
    direction /= magnitude;
    const float length = std::clamp( sqrtf( magnitude ) * 3.0f, 1.8f, 12.0f );
    EmitArrow( point, point + direction * length, r, g, b );
}


void RunEditorTracer::AddReplayFutureTargetMarker( const Vector3& position,
                                                   const Quaternion& orientation,
                                                   const CollisionShape& shape,
                                                   int depth )
{
    const float depthFade = std::clamp( static_cast<float>( depth - 1 ) * 0.10f, 0.0f, 0.34f );
    const float r = std::clamp( 0.98f - depthFade * 0.55f, 0.52f, 1.0f );
    const float g = std::clamp( 0.72f - depthFade * 0.22f, 0.42f, 0.82f );
    const float b = std::clamp( 0.22f - depthFade * 0.12f, 0.10f, 0.32f );
    EmitShapeOutline( position, orientation, shape, r, g, b );
}


void RunEditorTracer::AddReplayCausalEntryMarker( const Vector3& position,
                                                  const Quaternion& orientation,
                                                  const CollisionShape& shape )
{
    // Why: entry and rest form a fixed two-color vocabulary. Yellow always
    // means "joined the causal tree here", so no depth fade is applied. Use the
    // priority buffer so path-line overflow cannot erase already-revealed boxes.
    EmitShapeOutlineTo( m_priorityLineData, position, orientation, shape, 1.0f, 0.85f, 0.25f );
}


void RunEditorTracer::AddReplayCausalRestMarker( const Vector3& position,
                                                 const Quaternion& orientation,
                                                 const CollisionShape& shape )
{
    EmitShapeOutlineTo( m_priorityLineData, position, orientation, shape, 0.58f, 0.58f, 0.62f );
}


void RunEditorTracer::AddReplayCausalHorizonMarker( const Vector3& position,
                                                    const Quaternion& orientation,
                                                    const CollisionShape& shape )
{
    // Concept: horizon ghosts are not landings. They mark "this is where the
    // prediction buffer ends" for a body still mid-flight, so the color stays
    // distinct from grey resting boxes.
    EmitShapeOutlineTo( m_priorityLineData, position, orientation, shape, 0.45f, 0.92f, 1.0f );
}


void RunEditorTracer::AddReplayTargetMarker( const Vector3& position,
                                             const Quaternion& orientation,
                                             const CollisionShape& shape,
                                             float radius )
{
    AddSelectionOutline( position, orientation, shape );
    EmitRing( position, 1, (std::max)( 1.0f, radius ), 1.0f, 1.0f, 1.0f );
}


void RunEditorTracer::AddAttachedCameraTargetMarker( const Vector3& position,
                                                     const Quaternion& orientation,
                                                     const CollisionShape& shape,
                                                     float radius,
                                                     bool activeFollow )
{
    AddSelectionOutline( position, orientation, shape );
    radius = (std::max)( 1.0f, radius );
    const float r = activeFollow ? 0.16f : 1.0f;
    const float g = activeFollow ? 1.0f : 0.72f;
    const float b = activeFollow ? 0.92f : 0.24f;
    EmitRing( position, 1, radius, r, g, b );
    EmitRing( position, 0, radius * 0.68f, r, g, b );
}


void RunEditorTracer::AddSelectionOutline( const Vector3& position,
                                           const Quaternion& orientation,
                                           const CollisionShape& shape )
{
    constexpr float outlineR = 1.0f;
    constexpr float outlineG = 1.0f;
    constexpr float outlineB = 0.55f;
    EmitShapeOutline( position, orientation, shape, outlineR, outlineG, outlineB );
}


void RunEditorTracer::AddGizmo( const Vector3& origin,
                                float radius,
                                int hotTranslateAxis,
                                int hotRotationAxis,
                                int activeAxis,
                                bool activeRotation,
                                bool scaleMode,
                                bool activeScale )
{
    // Concept: Translate and scale share axis lines, while rotate owns rings.
    // Keeping both in one tracer method makes hover/active color priority
    // identical for editor placement and replay velocity overlays.
    const float length = EditorGizmoAxisLength( radius );
    for ( int axis = 0; axis < 3; ++axis )
    {
        float r = axis == 0 ? 1.0f : 0.08f;
        float g = axis == 1 ? 0.95f : 0.10f;
        float b = axis == 2 ? 1.0f : 0.08f;
        if ( ( activeScale || ( !scaleMode && !activeRotation ) ) && activeAxis == axis )
        {
            r = 1.0f;
            g = 1.0f;
            b = 0.15f;
        }
        else if ( hotTranslateAxis == axis )
        {
            r = (std::min)( 1.0f, r + 0.45f );
            g = (std::min)( 1.0f, g + 0.45f );
            b = (std::min)( 1.0f, b + 0.45f );
        }

        const Vector3 axisVector = EditorAxisVector( axis );
        const Vector3 endpoint = origin + axisVector * length;
        if ( scaleMode || activeScale )
        {
            const float handle = (std::max)( 0.75f, length * 0.045f );
            EmitLine( origin, endpoint, r, g, b );
            EmitBox( endpoint,
                     Vector3( handle, 0.0f, 0.0f ),
                     Vector3( 0.0f, handle, 0.0f ),
                     Vector3( 0.0f, 0.0f, handle ),
                     r,
                     g,
                     b );
        }
        else
        {
            EmitArrow( origin, endpoint, r, g, b );
        }
    }

    if ( scaleMode || activeScale )
    {
        return;
    }

    const float ringRadius = EditorGizmoRotationRadius( radius );
    for ( int axis = 0; axis < 3; ++axis )
    {
        float r = axis == 0 ? 1.0f : 0.08f;
        float g = axis == 1 ? 0.95f : 0.10f;
        float b = axis == 2 ? 1.0f : 0.08f;
        if ( activeRotation && activeAxis == axis )
        {
            r = 1.0f;
            g = 1.0f;
            b = 0.15f;
        }
        else if ( hotRotationAxis == axis )
        {
            r = (std::min)( 1.0f, r + 0.45f );
            g = (std::min)( 1.0f, g + 0.45f );
            b = (std::min)( 1.0f, b + 0.45f );
        }
        EmitRing( origin, axis, ringRadius, r, g, b );
    }
}


void RunEditorTracer::AddReplayVelocityGizmo( const Vector3& origin,
                                              const Quaternion& orientation,
                                              const CollisionShape& shape,
                                              float radius,
                                              const Vector3& linearVelocity,
                                              const Vector3& angularVelocity,
                                              int hotLinearAxis,
                                              int hotAngularAxis,
                                              int activeAxis,
                                              bool activeAngular )
{
    AddSelectionOutline( origin, orientation, shape );

    const float baseLength = ReplayVelocityLinearBaseLength( radius );

    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float component = ReplayVelocityAxisComponent( linearVelocity, axis );
        const float heat = std::clamp( fabsf( component ) / REPLAY_VELOCITY_EDIT_LINEAR_MAX, 0.0f, 1.0f );
        const bool hot = hotLinearAxis == axis;
        const bool active = !activeAngular && activeAxis == axis;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        ReplayVelocityAxisColor( axis, heat, hot, active, r, g, b );

        const float axisT = ReplayVelocityLinearVisualAxisT( radius, component );
        const Vector3 endpoint = origin + axisVector * axisT;
        EmitLine( origin - axisVector * ( baseLength * 0.24f ),
                  origin + axisVector * ( baseLength * 0.24f ),
                  r * 0.34f,
                  g * 0.34f,
                  b * 0.34f );
        EmitArrow( origin, endpoint, r, g, b );
    }

    for ( int axis = 0; axis < 3; ++axis )
    {
        const float component = ReplayVelocityAxisComponent( angularVelocity, axis );
        const float heat = std::clamp( fabsf( component ) / REPLAY_VELOCITY_EDIT_ANGULAR_MAX, 0.0f, 1.0f );
        const bool hot = hotAngularAxis == axis;
        const bool active = activeAngular && activeAxis == axis;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        ReplayVelocityAxisColor( axis, heat, hot, active, r, g, b );
        EmitRing( origin, axis, ReplayVelocityAngularVisualRadius( radius, component ), r, g, b );
    }
}


void RunEditorTracer::Render( const Matrix4& viewProjection, Rendering::IRenderCommandContext& renderCommands )
{
    if ( m_lineData.empty() && m_priorityLineData.empty() )
    {
        return;
    }
    // Invariant: m_lineData stores colored vertices as xyz/rgb floats; every
    // pair of vertices is one line segment consumed by DrawLinesColored.
    const float* lineData = m_lineData.data();
    std::size_t floatCount = m_lineData.size();
    if ( !m_priorityLineData.empty() )
    {
        // Build one pre-reserved stream so ordinary paths and priority causal
        // markers keep independent caps while the caller-owned render context
        // performs the single debug-line draw.
        m_renderLineData.clear();
        m_renderLineData.insert( m_renderLineData.end(), m_lineData.begin(), m_lineData.end() );
        m_renderLineData.insert( m_renderLineData.end(), m_priorityLineData.begin(), m_priorityLineData.end() );
        lineData = m_renderLineData.data();
        floatCount = m_renderLineData.size();
    }
    renderCommands.DrawLinesColored( lineData, static_cast<int>( floatCount / 6 ), viewProjection.Data() );
}
