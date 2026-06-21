/*
File: SkullbonezSource/BoundingBox.h
Purpose:
  Defines oriented-box collision geometry and its broadphase/render helper math.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  AABB (Axis-Aligned Bounding Box): Box aligned to world axes, often used for
  cheap broadphase overlap tests.
  OBB (Oriented Bounding Box): Box with rotation, used for exact object-space
  collision tests.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/BoundingBox.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "Common.h"
#include "Vector3.h"
#include "GeometricStructures.h"
#include "Matrix4.h"

namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
class BoundingSphere; // Forward declaration
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
      - Orientation:   provided externally by the RigidBody's quaternion — this class
                       stores only the shape definition, not the current rotation

    World-space vertex positions:
      For each corner: world_vertex = body_position + R * (±he.x, ±he.y, ±he.z)
      where R is the 3×3 rotation matrix from the RigidBody's orientation quaternion.

    Moment of Inertia (solid box, half-extents a, b, c, mass m):
      I_xx = m/3 * (b² + c²)
      I_yy = m/3 * (a² + c²)
      I_zz = m/3 * (a² + b²)
    (These are computed by the owning GameModel/Physics layer, not this class.)

    Bounding radius (used for broadphase): distance from centre to corner = sqrt(a²+b²+c²)

    Interface matches BoundingSphere so it can participate in the
    std::variant<BoundingSphere, BoundingBox> CollisionShape type.
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class BoundingBox
{

  private:
    Vector::Vector3 m_position;    // Local-space offset (usually zero)
    Vector::Vector3 m_halfExtents; // Half-size along each local axis (x, y, z)

  public:
    BoundingBox();
    BoundingBox( const Vector::Vector3& halfExtents, const Vector::Vector3& position );

    // --- Shape interface (matches BoundingSphere for std::visit dispatch) ---
    Transformation::Matrix4 GetModelMatrix( const Vector::Vector3& worldPos,
                                            const Transformation::Matrix4& rotation ) const;
    float GetVolume() const;
    float GetSubmergedVolumePercent( float fluidSurfaceHeight ) const;
    float GetDragCoefficient() const;
    float GetProjectedSurfaceArea() const;
    float GetBoundingRadius() const;
    const Vector::Vector3& GetPosition() const;

    // --- Box-specific accessors ---
    const Vector::Vector3& GetHalfExtents() const;

    // --- Collision tests ---
    // Sphere-box: sphere sweeps against this box (returns collision time [0,1] or NO_COLLISION)
    float
    TestCollision( const BoundingSphere& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;

    // Box-sphere: this box sweeps against a sphere
    float
    TestCollision( const BoundingBox& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
    float
    TestCollision( const ConvexHullShape& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
