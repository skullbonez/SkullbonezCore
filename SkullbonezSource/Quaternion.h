/*
File: SkullbonezSource/Quaternion.h
Purpose:
  Declares quaternion orientation math for rigid bodies and cameras.

Mental model:
  Math code is shared infrastructure. Coordinate conventions, units,
  handedness, and simplifications matter because subtle assumptions spread
  through rendering and physics.

Glossary:
  Engine module: A source file with one focused responsibility inside the
  SkullbonezCore runtime.

Related:
  - SkullbonezSource/Quaternion.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "Common.h"
#include "Vector3.h"
#include "RotationMatrix.h"

namespace SkullbonezCore
{
namespace Math
{
namespace Orientation
{
/* -- Quaternion
---------------------------------------------------------------------------------------------------------------------------------------------

    Represents a quaternion to express orientation in 3d space.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Quaternion
{

  public:
    Quaternion();                                                       // Default constructor
    Quaternion( float fX, float fY, float fZ, float fW );               // Overloaded constructor
    ~Quaternion() = default;
    void Identity();                                                    // Sets the quaternion back to the identity value
    void Normalise();                                                   // Normalises the quaternion (do this to combat floating point error creep)
    void RotateAboutXYZ( const Vector::Vector3& vRadians );             // Overload taking an angular-displacement vector in radians
    void RotateAboutAxis(
        const Vector::Vector3& axis,
        float angle );                                                  // Rotate by angle radians about an arbitrary world-space axis (no Euler decomposition)
    Transformation::RotationMatrix GetOrientationMatrix();              // Returns the orientation expressed in matrix form
    void RotateAboutXYZ( float xRadians,
                         float yRadians,
                         float zRadians );                              // Rotate by angular-displacement components without Euler decomposition
    Quaternion operator*( const Quaternion& q ) const;                  // Quaternion dot product, overload * operator for this
    Quaternion& operator*=( const Quaternion& q );                      // *= Overload
    void GetComponents( float& x, float& y, float& z, float& w ) const; // Expose components for serialization

  private:
    float m_x, m_y, m_z, m_w;                                           // Quaternion components

    Quaternion GetQtnRotatedAboutX( float fRadians );                   // Returns a new quaternion that has been rotated about the X axis
                                                      // the quantity specified by fRadians
    Quaternion GetQtnRotatedAboutY( float fRadians );                   // Returns a new quaternion that has been rotated about the Y axis
                                                      // the quantity specified by fRadians
    Quaternion GetQtnRotatedAboutZ( float fRadians );                   // Returns a new quaternion that has been rotated about the Z axis
                                                      // the quantity specified by fRadians
};

const Quaternion IDENTITY_QUATERNION( 0.0f, 0.0f, 0.0f, 1.0f );         // Identity quaternion static variable
} // namespace Orientation
} // namespace Math
} // namespace SkullbonezCore
