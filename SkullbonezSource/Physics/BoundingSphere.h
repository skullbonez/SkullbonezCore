/*
File: SkullbonezSource/Physics/BoundingSphere.h
Purpose:
  Defines sphere collision geometry, swept tests, volume facts, and render transforms.

Summary:
  Defines sphere collision geometry, swept
  tests, volume facts, and render transforms.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/BoundingSphere.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Maths/Vector3.h"
#include "../Maths/GeometricStructures.h"
#include "../Maths/Matrix4.h"


// --- Forward declarations ---
namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
class BoundingBox;
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

/* -- BoundingSphere
-------------------------------------------------------------------------------------------------------------------------------------------------

    Sphere-shaped collision primitive.  Plain value type — no inheritance, no virtual
    methods.  Lives in a std::variant<BoundingSphere, BoundingBox> (CollisionShape),
    dispatched via std::visit.

    Shape properties:
      Volume:             V = (4/3) * π * r³
      Moment of inertia:  I = (2/5) * m * r²   (solid sphere, rotational symmetry — same for all axes)
      Bounding radius:    equal to r (no extra envelope needed)
      Drag coefficient:   C_d ≈ 0.47  (smooth sphere in turbulent flow, Re > 10⁵)
      Projected area:     A = π * r²  (circular cross-section)

    Orientation:  spheres have no preferred axis, so only the world-space centre
    position matters for all physics queries.  The render path may still pass an
    orientation matrix to GetModelMatrix() to keep visual meshes aligned with the
    body row.

    Local-space offset (m_position):
      Centre of the sphere relative to the owning body's origin.  Usually (0,0,0),
      but may be non-zero for asymmetric objects with an offset collision volume.
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class BoundingSphere
{

  private:
    Vector::Vector3 m_position;                           // Local-space collision offset from owning body origin, in meters.
    float m_radius;                                       // Collision radius in meters; also the conservative broadphase radius.
    float m_dragCoefficient;                              // Runtime-configured drag coefficient cached with the authored sphere.

    float CollisionDetect( const BoundingSphere& target, const Geometry::Ray& targetRay,
                           const Geometry::Ray& focusRay )
        const;                                            // Swept sphere-sphere helper; returns earliest quadratic root or NO_COLLISION.

  public:
    BoundingSphere();                                     // Creates an empty sphere at the local origin for staged shape setup.
    BoundingSphere( float fRadius, const Vector::Vector3& vPosition,
                    float fDragCoefficient = 0.4f );      // fRadius is meters; vPosition is the owning body's local-space offset.
    void SetDragCoefficient( float fDragCoefficient );
    Transformation::Matrix4 GetModelMatrix( const Vector::Vector3& worldPos,
                                            const Transformation::Matrix4& rotation )
        const;                                            // T(worldPos) * R * T(localOffset) * S(radius) — used for visual sphere mesh
    float GetVolume() const;                              // V = (4/3) * π * r³

    // Fraction [0,1] of sphere volume below fluidSurfaceHeight  (spherical cap integral)
    float GetDragCoefficient() const;                     // C_d ≈ 0.47  (smooth sphere)
    float GetProjectedSurfaceArea() const;                // A = π * r²  (circular cross-section)
    float GetRadius() const;                              // Collision radius in meters.
    float GetBoundingRadius() const;                      // Conservative broadphase radius; identical to radius for spheres.
    const Vector::Vector3& GetPosition() const;           // Local-space centre offset used by model transforms and collision queries.
    float
    TestCollision( const BoundingSphere& target, const Geometry::Ray& targetRay,
                   const Geometry::Ray& focusRay ) const; // Public swept sphere-sphere test (delegates to CollisionDetect)
    float
    TestCollision( const BoundingBox& target, const Geometry::Ray& targetRay,
                   const Geometry::Ray& focusRay ) const; // Sphere vs box: approximated via bounding-radius sphere test
    float TestCollision( const ConvexHullShape& target, const Geometry::Ray& targetRay,
                         const Geometry::Ray& focusRay ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
