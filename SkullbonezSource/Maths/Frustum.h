/*
File: SkullbonezSource/Maths/Frustum.h
Purpose:
  Declares the allocation-free camera frustum used by render visibility tests.

Mental model:
  A view-projection matrix maps world points into clip space. Its six clip
  inequalities become normalized world-space planes; a bounding sphere is
  visible unless it lies wholly behind any plane.

Glossary:
  Frustum plane: Inward-facing plane whose non-negative half-space is visible.
  Conservative epsilon: Extra radius retained around a sphere so numerical
    noise can reduce culling efficiency but cannot remove visible geometry.
  Half-space: The visible side of one plane, used by the planar-reflection
    water clip independently of the six frustum planes.

Invariants:
  - Plane normals are unit length, so signed distance and sphere radius use the
    same world-space units.
  - The DX12 near plane uses z >= 0; the other clip bounds use -w <= x/y <= w
    and z <= w.

Related:
  - SkullbonezSource/Maths/Frustum.cpp
  - SkullbonezSource/Rendering/GameModelRenderer.cpp
*/
#pragma once

#include "Matrix4.h"
#include "Vector3.h"

namespace SkullbonezCore::Math::Visibility
{
struct FrustumPlane
{
    Vector::Vector3 normal;
    float distance = 0.0f;
};

class Frustum
{
  public:
    static constexpr int PLANE_COUNT = 6;

    static Frustum FromViewProjection( const Transformation::Matrix4& view, const Transformation::Matrix4& projection );

    bool IntersectsSphere( const Vector::Vector3& center, float radius, float conservativeEpsilon = 0.05f ) const;

    // Accepts normalized or unnormalized plane coefficients and returns true
    // unless the expanded sphere lies wholly outside the visible half-space.
    static bool IntersectsHalfSpace( const Vector::Vector3& center,
                                     float radius,
                                     const float plane[4],
                                     float conservativeEpsilon = 0.05f );

    const FrustumPlane& Plane( int index ) const;

  private:
    FrustumPlane m_planes[PLANE_COUNT] = {};
};
} // namespace SkullbonezCore::Math::Visibility
