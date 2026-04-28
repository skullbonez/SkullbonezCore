/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_BOUNDING_SPHERE_H
#define SKULLBONEZ_BOUNDING_SPHERE_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezMatrix4.h"

/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
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
    BoundingSphere( void );                                                                                                                                                                                                // Default constructor
    BoundingSphere( float fRadius, const Vector3& vPosition );                                                                                                                                                             // Overloaded constructor
    Transformation::Matrix4 GetRenderMatrix( const Vector3& worldSpaceCoords, const Transformation::Matrix4& rotation ) const;                                                                                              // Get model matrix for rendering
    void DEBUG_RenderCollisionVolume( const Vector3& worldSpaceCoords, const Transformation::Matrix4& rotation, const Transformation::Matrix4& view, const Transformation::Matrix4& proj, const float lightPos[4] ) const; // Debug routine to render a representation of the collision volume
    float GetVolume( void ) const;                                                                                                                                                                                         // Returns the volume of the sphere
    float GetSubmergedVolumePercent( float fluidSurfaceHeight ) const;                                                                                                                                                     // Calculates the total volume of the sphere below the fluid surface height
    float GetDragCoefficient( void ) const;                                                                                                                                                                                // Returns the drag coefficient of a sphere
    float GetProjectedSurfaceArea( void ) const;                                                                                                                                                                           // Returns the surface area of a 2d-projected sphere
    float GetRadius( void ) const;                                                                                                                                                                                         // Returns the radius of the sphere
    float GetBoundingRadius( void ) const;                                                                                                                                                                                 // Returns the bounding radius (same as GetRadius for spheres)
    const Vector3& GetPosition( void ) const;                                                                                                                                                                              // Returns the local-space position offset
    float TestCollision( const BoundingSphere& target, const Ray& targetRay, const Ray& focusRay ) const;                                                                                                                  // Sweep test against another bounding sphere
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
