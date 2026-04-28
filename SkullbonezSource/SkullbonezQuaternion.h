/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_QUATERNION_H
#define SKULLBONEZ_QUATERNION_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include "SkullbonezRotationMatrix.h"

/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;

namespace SkullbonezCore
{
namespace Math
{
namespace Orientation
{
/* -- Quaternion ---------------------------------------------------------------------------------------------------------------------------------------------

    Represents a quaternion to express orientation in 3d space.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Quaternion
{

  public:
    Quaternion();                                   // Default constructor
    Quaternion( float fX, float fY, float fZ, float fW ); // Overloaded constructor
    ~Quaternion() = default;
    void Identity();                                                 // Sets the quaternion back to the identity value
    void Normalise();                                                // Normalises the quaternion (do this to combat floating point error creep)
    void RotateAboutXYZ( const Vector3& vRadians );                        // Overload taking a Vector3 containing each rotation component in radians
    RotationMatrix GetOrientationMatrix();                           // Returns the orientation expressed in matrix form
    void RotateAboutXYZ( float xRadians, float yRadians, float zRadians ); // Rotate the quaternion about the x-axis, y-axis then z-axis
    Quaternion operator*( const Quaternion& q ) const;                     // Quaternion dot product, overload * operator for this
    Quaternion& operator*=( const Quaternion& q );                         // *= Overload

  private:
    float m_x, m_y, m_z, m_w; // Quaternion components

    Quaternion GetQtnRotatedAboutX( float fRadians ); // Returns a new quaternion that has been rotated about the X axis the quantity specified by fRadians
    Quaternion GetQtnRotatedAboutY( float fRadians ); // Returns a new quaternion that has been rotated about the Y axis the quantity specified by fRadians
    Quaternion GetQtnRotatedAboutZ( float fRadians ); // Returns a new quaternion that has been rotated about the Z axis the quantity specified by fRadians
};

const Quaternion IDENTITY_QUATERNION( 0.0f, 0.0f, 0.0f, 1.0f ); // Identity quaternion static variable
} // namespace Orientation
} // namespace Math
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
