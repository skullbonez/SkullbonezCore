/*
File: SkullbonezSource/Physics/BoundingSphere.h
Purpose:
  Defines sphere collision geometry, swept tests, volume facts, and render transforms.

Summary:
  BoundingSphere stores an authored radius and local center offset. Physics
  queries ignore orientation, while shared shape visitors expose swept tests,
  volume/area facts, broadphase radius, and render transforms.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - m_position is the sphere center relative to the owning body origin; only
    the resulting world center affects collision queries.
  - Radius determines volume, projected area, isotropic solid-sphere inertia,
    and bounding radius; the drag coefficient is authored separately.

Related:
  - SkullbonezSource/Physics/BoundingSphere.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Maths/Vector3.h"
#include "../Maths/GeometricStructures.h"
#include "../Maths/Matrix4.h"


// Forward declarations:
namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
class ConvexHullShape;
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore


namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{

class BoundingSphere
{

  private:
    Vector::Vector3 m_position;                                                                                               // Local-space collision offset from owning body origin, in meters.
    float m_radius;                                                                                                           // Collision radius in meters; also the conservative broadphase radius.
    float m_dragCoefficient;                                                                                                  // Runtime-configured drag coefficient cached with the authored sphere.

    float CollisionDetect( const BoundingSphere& target, const Geometry::Ray& targetRay,
                           const Geometry::Ray& focusRay )
        const;                                                                                                                // Swept sphere-sphere helper; returns earliest quadratic root or NO_COLLISION.

  public:
    BoundingSphere();                                                                                                         // Creates an empty sphere at the local origin for staged shape setup.
    BoundingSphere( float radius, const Vector::Vector3& localPosition, float dragCoefficient = 0.4f );                       // radius is meters; localPosition is the owning body's local-space offset.
    void SetDragCoefficient( float dragCoefficient );
    Transformation::Matrix4 GetModelMatrix( const Vector::Vector3& worldPos, const Transformation::Matrix4& rotation ) const; // T(worldPos) * R * T(localOffset) * S(radius) — used for visual sphere mesh
    float GetVolume() const;                                                                                                  // V = (4/3) * π * r³

    // Fraction [0,1] of sphere volume below fluidSurfaceHeight  (spherical cap integral)
    float GetDragCoefficient() const;                                                                                         // C_d ≈ 0.47  (smooth sphere)
    float GetProjectedSurfaceArea() const;                                                                                    // A = π * r²  (circular cross-section)
    float GetRadius() const;                                                                                                  // Collision radius in meters.
    float GetBoundingRadius() const;                                                                                          // Conservative broadphase radius; identical to radius for spheres.
    const Vector::Vector3& GetPosition() const;                                                                               // Local-space centre offset used by model transforms and collision queries.
    float TestCollision( const BoundingSphere& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const; // Public swept sphere-sphere test (delegates to CollisionDetect)
    float TestCollision( const ConvexHullShape& target, const Geometry::Ray& targetRay,
                         const Geometry::Ray& focusRay ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
