/*
File: SkullbonezSource/SkullbonezBoundingSphere.h
Purpose:
  Defines sphere collision geometry, swept tests, volume facts, and render transforms.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/SkullbonezBoundingSphere.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezMatrix4.h"


// --- Forward declarations ---
namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
class BoundingBox;
class ConvexHullShape;
}
} // namespace Math
} // namespace SkullbonezCore


namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
/* -- BoundingSphere -------------------------------------------------------------------------------------------------------------------------------------------------

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
    position matters for all physics queries.  The rotation matrix from the owning
    RigidBody is passed to GetModelMatrix() only to orient the visual mesh.

    Local-space offset (m_position):
      Centre of the sphere relative to the owning body's origin.  Usually (0,0,0),
      but may be non-zero for asymmetric objects with an offset collision volume.
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class BoundingSphere
{

  private:
    Vector::Vector3 m_position; // Local-space offset of sphere centre relative to model
    float m_radius;             // Radius of sphere

    float CollisionDetect( const BoundingSphere& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const; // Swept sphere-sphere test; returns earliest collision time t ∈ [0,1] or NO_COLLISION

  public:
    BoundingSphere();                                                                                                         // Default constructor (zero radius, origin)
    BoundingSphere( float fRadius, const Vector::Vector3& vPosition );                                                        // fRadius = sphere radius (m), vPosition = local-space centre offset
    Transformation::Matrix4 GetModelMatrix( const Vector::Vector3& worldPos, const Transformation::Matrix4& rotation ) const; // T(worldPos) * R * T(localOffset) * S(radius) — used for visual sphere mesh
    float GetVolume() const;                                                                                                  // V = (4/3) * π * r³
    float GetSubmergedVolumePercent( float fluidSurfaceHeight ) const;                                                        // Fraction [0,1] of sphere volume below fluidSurfaceHeight  (spherical cap integral)
    float GetDragCoefficient() const;                                                                                         // C_d ≈ 0.47  (smooth sphere)
    float GetProjectedSurfaceArea() const;                                                                                    // A = π * r²  (circular cross-section)
    float GetRadius() const;                                                                                                  // Returns radius r
    float GetBoundingRadius() const;                                                                                          // Returns r  (bounding radius == radius for spheres)
    const Vector::Vector3& GetPosition() const;                                                                               // Returns local-space centre offset (m_position)
    float TestCollision( const BoundingSphere& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const; // Public swept sphere-sphere test (delegates to CollisionDetect)
    float TestCollision( const BoundingBox& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;    // Sphere vs box: approximated via bounding-radius sphere test
    float TestCollision( const ConvexHullShape& target, const Geometry::Ray& targetRay, const Geometry::Ray& focusRay ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
