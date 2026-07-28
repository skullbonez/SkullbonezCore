/*
File: SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.cpp
Purpose:
  Implements exact CPU ray tests for runtime model picking.

Summary:
  The editor picker used to answer "did the ray pass through this model's broad
  sphere?" That was forgiving, but it let large foliage envelopes hide narrow
  trunks. This file answers the narrower question: "where does the ray first
  enter the model's authored collision shape?"

Glossary:
  Slab test: Ray-vs-box interval clipping against the min/max plane pair for
    each local axis.
  Face plane: Convex hull boundary represented as normal dot point <= offset.
  Ray interval: The [enter, exit] range along the ray that remains inside a
    candidate shape after clipping.

Invariants:
  - Returned RayT values are non-negative; rays starting inside a shape return
    0.0f.
  - Convex hull faces use outward normals and the engine's baked
    normalLocal/planeOffsetLocal contract.

Related:
  - SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.h
  - SkullbonezSource/Runtime/Interaction/RuntimePickService.cpp
*/
#include "RuntimePickGeometry.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
constexpr float PICK_AXIS_EPSILON = 1.0e-6f;
constexpr float PICK_CLIP_EPSILON = 1.0e-5f;


Math::Transformation::RotationMatrix BuildPickRotation( const Math::Orientation::Quaternion& orientation )
{
    Math::Orientation::Quaternion copy = orientation;
    return copy.GetOrientationMatrix();
}


bool IntersectRaySphereExact( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                              const Math::Vector::Vector3& center, float radius, float& outT )
{

    if ( radius <= 0.0f )
    {
        return false;
    }

    const Math::Vector::Vector3 m = rayOrigin - center;
    const float b = Dot( m, rayDirection );
    const float c = ( Dot( m, m ) ) - radius * radius;

    if ( c > 0.0f && b > 0.0f )
    {
        return false;
    }

    const float discriminant = b * b - c;

    if ( discriminant < 0.0f )
    {
        return false;
    }

    outT = -b - sqrtf( discriminant );

    if ( outT < 0.0f )
    {
        outT = 0.0f;
    }

    return true;
}


bool ClipBoxAxis( float origin, float direction, float minValue, float maxValue, float& inOutEnter, float& inOutExit )
{

    if ( fabsf( direction ) <= PICK_AXIS_EPSILON )
    {
        return origin >= minValue - PICK_CLIP_EPSILON && origin <= maxValue + PICK_CLIP_EPSILON;
    }

    const float invDirection = 1.0f / direction;
    float axisEnter = ( minValue - origin ) * invDirection;
    float axisExit = ( maxValue - origin ) * invDirection;

    if ( axisEnter > axisExit )
    {
        std::swap( axisEnter, axisExit );
    }

    inOutEnter = (std::max)( inOutEnter, axisEnter );
    inOutExit = (std::min)( inOutExit, axisExit );
    return inOutEnter <= inOutExit + PICK_CLIP_EPSILON;
}


bool IntersectRayBoxLocal( const Math::Vector::Vector3& localOrigin, const Math::Vector::Vector3& localDirection,
                           const Math::Vector::Vector3& halfExtents, float& outT )
{
    float enter = 0.0f;
    float exit = FLT_MAX;

    if ( !ClipBoxAxis( localOrigin.x, localDirection.x, -halfExtents.x, halfExtents.x, enter, exit ) ||
         !ClipBoxAxis( localOrigin.y, localDirection.y, -halfExtents.y, halfExtents.y, enter, exit ) ||
         !ClipBoxAxis( localOrigin.z, localDirection.z, -halfExtents.z, halfExtents.z, enter, exit ) )
    {
        return false;
    }

    if ( exit < -PICK_CLIP_EPSILON )
    {
        return false;
    }

    outT = enter < 0.0f ? 0.0f : enter;
    return true;
}


bool IntersectRayConvexHullLocal( const Math::Vector::Vector3& localOrigin, const Math::Vector::Vector3& localDirection,
                                  const Math::CollisionDetection::ConvexHullShape& hull, float& outT )
{
    float enter = 0.0f;
    float exit = FLT_MAX;

    for ( uint16_t faceIndex = 0; faceIndex < hull.GetFaceCount(); ++faceIndex )
    {
        const Math::CollisionDetection::ConvexHullFace& face = hull.GetFace( faceIndex );
        const float numerator = face.planeOffsetLocal - ( Dot( face.normalLocal, localOrigin ) );
        const float denominator = Dot( face.normalLocal, localDirection );

        if ( fabsf( denominator ) <= PICK_AXIS_EPSILON )
        {

            if ( numerator < -PICK_CLIP_EPSILON )
            {
                return false;
            }

            continue;
        }

        const float t = numerator / denominator;

        if ( denominator < 0.0f )
        {
            enter = (std::max)( enter, t );
        }
        else
        {
            exit = (std::min)( exit, t );
        }

        if ( enter > exit + PICK_CLIP_EPSILON )
        {
            return false;
        }
    }

    if ( exit < -PICK_CLIP_EPSILON )
    {
        return false;
    }

    outT = enter < 0.0f ? 0.0f : enter;
    return true;
}


struct PickShapeVisitor
{
    const RuntimePickShapeTransform& transform;
    const Math::Vector::Vector3& rayOrigin;
    const Math::Vector::Vector3& rayDirection;
    const Math::Transformation::RotationMatrix rotation;
    float& outT;

    bool operator()( const Math::CollisionDetection::BoundingSphere& sphere ) const
    {
        const Math::Vector::Vector3 worldCenter = transform.position + rotation * sphere.GetPosition();
        return IntersectRaySphereExact( rayOrigin, rayDirection, worldCenter, sphere.GetRadius(), outT );
    }

    bool operator()( const Math::CollisionDetection::BoundingBox& box ) const
    {

        // Invariant: BoundingBox::GetModelMatrix translates by worldPos +
        // box.GetPosition(), then rotates the unit cube. Match that authored
        // offset convention so editor picking agrees with the rendered box.
        const Math::Vector::Vector3 worldCenter = transform.position + box.GetPosition();
        const Math::Vector::Vector3 localOrigin = rotation.TransposeMultiply( rayOrigin - worldCenter );
        const Math::Vector::Vector3 localDirection = rotation.TransposeMultiply( rayDirection );
        return IntersectRayBoxLocal( localOrigin, localDirection, box.GetHalfExtents(), outT );
    }

    bool operator()( const Math::CollisionDetection::ConvexHullShape& hull ) const
    {
        const Math::Vector::Vector3 localOrigin = rotation.TransposeMultiply( rayOrigin - transform.position ) -
                                                  hull.GetPosition();

        const Math::Vector::Vector3 localDirection = rotation.TransposeMultiply( rayDirection );
        return IntersectRayConvexHullLocal( localOrigin, localDirection, hull, outT );
    }
};
} // namespace


bool TryIntersectRuntimePickShape( const Math::CollisionDetection::CollisionShapeReference& shape,
                                   const RuntimePickShapeTransform& transform, const Math::Vector::Vector3& rayOrigin,
                                   const Math::Vector::Vector3& rayDirection, float& outT )
{
    outT = 0.0f;
    const Math::Transformation::RotationMatrix rotation = BuildPickRotation( transform.orientation );
    return Math::CollisionDetection::VisitCollisionShape( shape, PickShapeVisitor { transform, rayOrigin, rayDirection,
                                                                                    rotation, outT } );
}
} // namespace Runtime
} // namespace SkullbonezCore
