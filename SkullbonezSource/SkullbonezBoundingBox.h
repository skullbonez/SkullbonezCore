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
class BoundingSphere; // Forward declaration
/* -- BoundingBox ----------------------------------------------------------------------------------------------------------------------------------------------------

    Oriented Bounding Box (OBB) collision shape.
    Stores half-extents in local space. The orientation comes from the owning
    RigidBody's quaternion — this class stores only the shape definition.

    Interface matches BoundingSphere so it can participate in the
    std::variant<BoundingSphere, BoundingBox> CollisionShape type.
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class BoundingBox
{

  private:
    Vector3 m_position;    // Local-space offset (usually zero)
    Vector3 m_halfExtents; // Half-size along each local axis (x, y, z)

  public:
    BoundingBox();
    BoundingBox( const Vector3& halfExtents, const Vector3& position );

    // --- Shape interface (matches BoundingSphere for std::visit dispatch) ---
    Transformation::Matrix4 GetModelMatrix( const Vector3& worldPos, const Transformation::Matrix4& rotation ) const;
    float GetVolume() const;
    float GetSubmergedVolumePercent( float fluidSurfaceHeight ) const;
    float GetDragCoefficient() const;
    float GetProjectedSurfaceArea() const;
    float GetBoundingRadius() const;
    const Vector3& GetPosition() const;

    // --- Box-specific accessors ---
    const Vector3& GetHalfExtents() const;

    // --- Collision tests ---
    // Sphere-box: sphere sweeps against this box (returns collision time [0,1] or NO_COLLISION)
    float TestCollision( const BoundingSphere& target, const Ray& targetRay, const Ray& focusRay ) const;

    // Box-sphere: this box sweeps against a sphere
    float TestCollision( const BoundingBox& target, const Ray& targetRay, const Ray& focusRay ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
