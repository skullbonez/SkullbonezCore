#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezMatrix4.h"


// --- Usings ---
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;


// --- Forward declarations ---
namespace SkullbonezCore { namespace Math { namespace CollisionDetection { class BoundingBox; } } }


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
    Vector3 m_position; // Local-space offset of sphere centre relative to model
    float m_radius;     // Radius of sphere

    float CollisionDetect( const BoundingSphere& target, const Ray& targetRay, const Ray& focusRay ) const; // Swept sphere-sphere test; returns earliest collision time t ∈ [0,1] or NO_COLLISION

  public:
    BoundingSphere();                                                                                                 // Default constructor (zero radius, origin)
    BoundingSphere( float fRadius, const Vector3& vPosition );                                                        // fRadius = sphere radius (m), vPosition = local-space centre offset
    Transformation::Matrix4 GetModelMatrix( const Vector3& worldPos, const Transformation::Matrix4& rotation ) const; // T(worldPos) * R * T(localOffset) * S(radius) — used for visual sphere mesh
    float GetVolume() const;                                                                                          // V = (4/3) * π * r³
    float GetSubmergedVolumePercent( float fluidSurfaceHeight ) const;                                                // Fraction [0,1] of sphere volume below fluidSurfaceHeight  (spherical cap integral)
    float GetDragCoefficient() const;                                                                                 // C_d ≈ 0.47  (smooth sphere)
    float GetProjectedSurfaceArea() const;                                                                            // A = π * r²  (circular cross-section)
    float GetRadius() const;                                                                                          // Returns radius r
    float GetBoundingRadius() const;                                                                                  // Returns r  (bounding radius == radius for spheres)
    const Vector3& GetPosition() const;                                                                               // Returns local-space centre offset (m_position)
    float TestCollision( const BoundingSphere& target, const Ray& targetRay, const Ray& focusRay ) const;             // Public swept sphere-sphere test (delegates to CollisionDetect)
    float TestCollision( const BoundingBox& target, const Ray& targetRay, const Ray& focusRay ) const;               // Sphere vs box: approximated via bounding-radius sphere test
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
