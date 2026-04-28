#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezMatrix4.h"


// --- Usings ---
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;


namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
/* -- BoundingSphere -------------------------------------------------------------------------------------------------------------------------------------------------

    Represents a sphere for collision tests. Plain value type - no inheritance,
    no virtual methods. Dispatched via std::variant<BoundingSphere, ...> in
    CollisionShape.
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class BoundingSphere
{

  private:
    Vector3 m_position; // Local-space offset of sphere centre relative to model
    float m_radius;     // Radius of sphere

    float CollisionDetect( const BoundingSphere& target, const Ray& targetRay, const Ray& focusRay ) const; // Collision detect against bounding sphere

  public:
    BoundingSphere();                                                                                                 // Default constructor
    BoundingSphere( float fRadius, const Vector3& vPosition );                                                        // Overloaded constructor
    Transformation::Matrix4 GetModelMatrix( const Vector3& worldPos, const Transformation::Matrix4& rotation ) const; // Compute model matrix: T(worldPos) * R * T(localOffset) * S(radius)
    float GetVolume() const;                                                                                          // Returns the volume of the sphere
    float GetSubmergedVolumePercent( float fluidSurfaceHeight ) const;                                                // Calculates the total volume of the sphere below the fluid surface height
    float GetDragCoefficient() const;                                                                                 // Returns the drag coefficient of a sphere
    float GetProjectedSurfaceArea() const;                                                                            // Returns the surface area of a 2d-projected sphere
    float GetRadius() const;                                                                                          // Returns the radius of the sphere
    float GetBoundingRadius() const;                                                                                  // Returns the bounding radius (same as GetRadius for spheres)
    const Vector3& GetPosition() const;                                                                               // Returns the local-space position offset
    float TestCollision( const BoundingSphere& target, const Ray& targetRay, const Ray& focusRay ) const;             // Sweep test against another bounding sphere
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
