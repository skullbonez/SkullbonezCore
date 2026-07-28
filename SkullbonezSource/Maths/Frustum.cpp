/*
File: SkullbonezSource/Maths/Frustum.cpp
Purpose:
  Extracts normalized DX12 clip planes and tests conservative world spheres.

Summary:
  Matrix rows encode clip-space inequalities even though Matrix4 stores values
  column-major. Adding or subtracting the w row yields side/far planes; DX12's
  zero-to-one depth convention makes the z row itself the near plane.

Glossary:
  Signed distance: Plane equation dot(normal, point) + distance.
  Degenerate plane: A plane with a near-zero normal that cannot safely cull.

Invariants:
  - Degenerate extraction becomes a permissive plane instead of rejecting
    geometry.
  - Epsilon expands the tested sphere; it never tightens the visible volume.

Related:
  - SkullbonezSource/Maths/Frustum.h
  - SkullbonezTests/TestFrustum.cpp
*/
#include "Frustum.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace SkullbonezCore::Math::Visibility
{
namespace
{
FrustumPlane NormalizedPlane( float x, float y, float z, float distance )
{
    const float lengthSquared = x * x + y * y + z * z;

    if ( lengthSquared <= 1.0e-12f )
    {

        // Conservative fallback: a zero normal with positive distance cannot
        // reject a sphere, which is safer than culling from invalid camera math.
        return { Vector::Vector3( 0.0f, 0.0f, 0.0f ), 1.0f };
    }

    const float inverseLength = 1.0f / std::sqrt( lengthSquared );
    return { Vector::Vector3( x * inverseLength, y * inverseLength, z * inverseLength ), distance * inverseLength };
}
} // namespace

Frustum Frustum::FromViewProjection( const Transformation::Matrix4& view, const Transformation::Matrix4& projection )
{
    const Transformation::Matrix4 clip = projection * view;
    Frustum result;

    // Matrix4 is column-major. These expressions address rows explicitly:
    // row0=(m0,m4,m8,m12), row3=(m3,m7,m11,m15), and so on.
    result.m_planes[0] = NormalizedPlane( clip.m[3] + clip.m[0], clip.m[7] + clip.m[4], clip.m[11] + clip.m[8],
                                          clip.m[15] + clip.m[12] ); // left

    result.m_planes[1] = NormalizedPlane( clip.m[3] - clip.m[0], clip.m[7] - clip.m[4], clip.m[11] - clip.m[8],
                                          clip.m[15] - clip.m[12] ); // right

    result.m_planes[2] = NormalizedPlane( clip.m[3] + clip.m[1], clip.m[7] + clip.m[5], clip.m[11] + clip.m[9],
                                          clip.m[15] + clip.m[13] ); // bottom

    result.m_planes[3] = NormalizedPlane( clip.m[3] - clip.m[1], clip.m[7] - clip.m[5], clip.m[11] - clip.m[9],
                                          clip.m[15] - clip.m[13] ); // top

    result.m_planes[4] = NormalizedPlane( clip.m[2], clip.m[6], clip.m[10], clip.m[14] ); // near, DX12 z >= 0

    result.m_planes[5] = NormalizedPlane( clip.m[3] - clip.m[2], clip.m[7] - clip.m[6], clip.m[11] - clip.m[10],
                                          clip.m[15] - clip.m[14] ); // far

    return result;
}

bool Frustum::IntersectsSphere( const Vector::Vector3& center, float radius, float conservativeEpsilon ) const
{
    const float expandedRadius = (std::max)( 0.0f, radius ) + (std::max)( 0.0f, conservativeEpsilon );

    for ( const FrustumPlane& plane : m_planes )
    {
        const float signedDistance = Dot( plane.normal, center ) + plane.distance;

        if ( signedDistance < -expandedRadius )
        {
            return false;
        }
    }

    return true;
}

bool Frustum::IntersectsHalfSpace( const Vector::Vector3& center, float radius, const float plane[4],
                                   float conservativeEpsilon )
{
    const float normalLengthSquared = plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2];

    if ( normalLengthSquared <= 1.0e-12f )
    {
        return true;
    }

    const float expandedRadius = ( (std::max)( 0.0f, radius ) + (std::max)( 0.0f, conservativeEpsilon ) ) *
                                 std::sqrt( normalLengthSquared );

    const float signedDistance = plane[0] * center.x + plane[1] * center.y + plane[2] * center.z + plane[3];
    return signedDistance >= -expandedRadius;
}

const FrustumPlane& Frustum::Plane( int index ) const
{
    assert( index >= 0 && index < PLANE_COUNT );
    return m_planes[index];
}
} // namespace SkullbonezCore::Math::Visibility
