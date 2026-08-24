/*
File: SkullbonezSource/Physics/BoundingBox.h
Purpose:
  Defines oriented-box collision geometry and its broadphase/render helper math.

Summary:
  BoundingBox stores local half-extents and a center offset while the owning
  body row supplies world orientation. Queries derive oriented vertices,
  broadphase radius, volume, and render transforms from that split.

Glossary:
  Half-extents: Positive distance from the box center to one face along each
  local axis; full box dimensions are twice these values.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - m_halfExtents are positive local-axis half-dimensions; m_position is a
    local-space center offset, not a world position.
  - BoundingBox stores no current orientation. The owning body row supplies the
    quaternion, and the authoring layer owns mass-dependent inertia.

Related:
  - SkullbonezSource/Physics/BoundingBox.cpp
  - Agentic/Reference/physics-overview.md
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
class ConvexHullShape;

class BoundingBox
{

  private:
    Vector::Vector3 m_position;    // Local-space shape offset from the owning rigid-body origin.
    Vector::Vector3 m_halfExtents; // Half-size along each local axis; feeds volume, inertia, and support points.

  public:
    BoundingBox();
    BoundingBox( const Vector::Vector3& halfExtents, const Vector::Vector3& position );

    // Shape interface (matches BoundingSphere for std::visit dispatch):
    Transformation::Matrix4 GetModelMatrix( const Vector::Vector3& worldPos, const Transformation::Matrix4& rotation ) const;
    float GetVolume() const;
    float GetDragCoefficient() const;
    float GetProjectedSurfaceArea() const;
    float GetBoundingRadius() const;
    const Vector::Vector3& GetPosition() const;

    // Box-specific accessors:
    const Vector::Vector3& GetHalfExtents() const;

    // Collision tests:
    // Box/object sweeps are conservative front-ends; manifold generation owns exact resting contacts.
    float TestCollision( const BoundingBox& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
    float TestCollision( const ConvexHullShape& target, const Geometry::Ray& targetRay,
                         const Geometry::Ray& focusRay ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
