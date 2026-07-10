/*
File: SkullbonezSource/Maths/Quaternion.h
Purpose:
  Declares quaternion orientation math for rigid bodies and cameras.

Mental model:
  Quaternion.h declares quaternion orientation math for rigid bodies and
  cameras. As a public header, keep edits anchored on units, basis
  conventions, and numerical assumptions and on the glossary/invariants below.

Glossary:
  Quaternion: Four-component rotation representation that avoids gimbal lock
  during incremental orientation updates.
  Identity quaternion: No-rotation value (0,0,0,1); multiplying by it leaves an
  orientation unchanged.
  Engine quaternion order: This operator convention composes rotations in the
  order expected by Quaternion.cpp and the engine orientation matrix path.
  Euler decomposition: Splitting rotation into ordered X/Y/Z angles; this file
  avoids depending on that order for incremental angular displacement.

Invariants:
  - IDENTITY_QUATERNION is the no-rotation sentinel and must stay (0,0,0,1).
  - Operator multiplication follows the engine convention documented in
    Quaternion.cpp; do not silently swap it to library/Hamilton order.

Related:
  - SkullbonezSource/Maths/Quaternion.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "MathsCommon.h"
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
    Quaternion();                                               // Initializes to identity orientation.
    Quaternion( float fX,
                float fY,
                float fZ,
                float fW );                                     // Explicit component construction for deserialization/math helpers.
    ~Quaternion() = default;
    void Identity();                                            // Resets orientation to the no-rotation value.
    void Normalise();                                           // Removes floating-point drift before conversion to matrices or solver rows.
    void RotateAboutXYZ( const Vector::Vector3& vRadians );     // Treats xyz radians as one angular-displacement vector.
    void RotateAboutAxis(
        const Vector::Vector3& axis,
        float angle );                                          // Rotate by angle radians about an arbitrary world-space axis (no Euler decomposition)
    Transformation::RotationMatrix
    GetOrientationMatrix();                                     // Converts orientation to the matrix form expected by transforms/collision.
    void RotateAboutXYZ( float xRadians,
                         float yRadians,
                         float zRadians );                      // Rotate by angular-displacement components without Euler decomposition
    Quaternion operator*( const Quaternion& q ) const;          // Combines this rotation with q in engine multiplication order.
    Quaternion& operator*=( const Quaternion& q );              // In-place rotation composition; caller normalizes if drift matters.
    void GetComponents( float& x,
                        float& y,
                        float& z,
                        float& w ) const;                       // Exposes raw components for deterministic serialization.

  private:
    float m_x, m_y, m_z, m_w;                                   // Stored as vector part xyz plus scalar w.

    Quaternion GetQtnRotatedAboutX( float fRadians );           // Builds an X-axis delta rotation in radians.
    Quaternion GetQtnRotatedAboutY( float fRadians );           // Builds a Y-axis delta rotation in radians.
    Quaternion GetQtnRotatedAboutZ( float fRadians );           // Builds a Z-axis delta rotation in radians.
};

const Quaternion IDENTITY_QUATERNION( 0.0f, 0.0f, 0.0f, 1.0f ); // Shared no-rotation value.
} // namespace Orientation
} // namespace Math
} // namespace SkullbonezCore
