/*
File: SkullbonezSource/Physics/BoundingBox.h
Purpose:
  Defines oriented-box collision geometry and its broadphase/render helper math.

Summary:
  BoundingBox.h defines oriented-box collision geometry and its
  broadphase/render helper math.

Glossary:
  Half-extents: Positive distance from the box center to one face along each
  local axis; full box dimensions are twice these values.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/BoundingBox.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Maths/Vector3.h"
#include "../Maths/GeometricStructures.h"
#include "../Maths/Matrix4.h"

namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
class BoundingSphere;              // Forward declaration
class ConvexHullShape;

/* -- BoundingBox
----------------------------------------------------------------------------------------------------------------------------------------------------

    Oriented Bounding Box (OBB) collision shape.

    An OBB is a box that can be oriented at any angle, unlike an AABB (axis-aligned bounding box) which is always
aligned to world axes. It provides a tighter fit for rotated objects.

    Representation:
      - m_halfExtents: half the box dimensions along its LOCAL axes
                       (x = width/2,  y = height/2,  z = depth/2)
      - m_position:    local-space centre offset from the owning body's origin (usually zero)
      - Orientation:   provided externally by the body row's quaternion — this class
                       stores only the shape definition, not the current rotation

    World-space vertex positions:
      For each corner: world_vertex = body_position + R * (±he.x, ±he.y, ±he.z)
      where R is the 3×3 rotation matrix from the body row's orientation quaternion.

    Moment of Inertia (solid box, half-extents a, b, c, mass m):
      I_xx = m/3 * (b² + c²)
      I_yy = m/3 * (a² + c²)
      I_zz = m/3 * (a² + b²)
    (These are computed by the owning body-authoring layer, not this class.)

    Bounding radius (used for broadphase): distance from centre to corner = sqrt(a²+b²+c²)

    Interface matches BoundingSphere so it can participate in the
    std::variant<BoundingSphere, BoundingBox> CollisionShape type.
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class BoundingBox
{

  private:
    Vector::Vector3 m_position;    // Local-space shape offset from the owning rigid-body origin.
    Vector::Vector3 m_halfExtents; // Half-size along each local axis; feeds volume, inertia, and support points.

  public:
    BoundingBox();
    BoundingBox( const Vector::Vector3& halfExtents, const Vector::Vector3& position );

    // --- Shape interface (matches BoundingSphere for std::visit dispatch) ---
    Transformation::Matrix4 GetModelMatrix( const Vector::Vector3& worldPos, const Transformation::Matrix4& rotation ) const;
    float GetVolume() const;
    float GetDragCoefficient() const;
    float GetProjectedSurfaceArea() const;
    float GetBoundingRadius() const;
    const Vector::Vector3& GetPosition() const;

    // --- Box-specific accessors ---
    const Vector::Vector3& GetHalfExtents() const;

    // --- Collision tests ---
    // Sphere-box sweep: broadphase-style time query; manifold generation owns exact resting contacts.
    float TestCollision( const BoundingSphere& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;

    // Box/object sweeps keep the same visitor surface as BoundingSphere for CollisionShape dispatch.
    float TestCollision( const BoundingBox& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
    float TestCollision( const ConvexHullShape& target, const Geometry::Ray& targetRay,
                         const Geometry::Ray& focusRay ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
