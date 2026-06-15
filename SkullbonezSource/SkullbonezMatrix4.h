/*
File: SkullbonezSource/SkullbonezMatrix4.h
Purpose:
  Declares the engine matrix type and common transform operations.

Mental model:
  Math code is shared infrastructure. Coordinate conventions, units,
  handedness, and simplifications matter because subtle assumptions spread
  through rendering and physics.

Glossary:
  Clip space: Coordinate range produced by projection matrices before viewport
  mapping. DX12 uses a [0,1] depth range.

Related:
  - SkullbonezSource/SkullbonezMatrix4.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"

namespace SkullbonezCore
{
namespace Math
{
namespace Orientation
{
class Quaternion;
}
} // namespace Math
} // namespace SkullbonezCore

namespace SkullbonezCore
{
namespace Math
{
namespace Transformation
{
/* -- Matrix4 -----------------------------------------------------------------------------------------------------------------------------------------------

    A 4x4 column-major matrix for 3D transformations (projection, view, model).
    Stored in column-major order. Engine math and shaders agree on this memory
    layout:
    m[0] m[4] m[8]  m[12]
    m[1] m[5] m[9]  m[13]
    m[2] m[6] m[10] m[14]
    m[3] m[7] m[11] m[15]
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Matrix4
{

  public:
    float m[16];

    Matrix4();                      // Default constructor (identity)
    Matrix4( const float* values ); // Construct from 16-element column-major array

    static Matrix4 Perspective( float fovDegrees, float aspect, float nearPlane, float farPlane );                      // Perspective projection matrix with legacy [-1,1] depth
    static Matrix4 PerspectiveZeroToOne( float fovDegrees, float aspect, float nearPlane, float farPlane );             // Perspective projection matrix (DX depth [0,1])
    static Matrix4 Ortho( float left, float right, float bottom, float top, float nearPlane, float farPlane );          // Orthographic projection matrix
    static Matrix4 OrthoZeroToOne( float left, float right, float bottom, float top, float nearPlane, float farPlane ); // Orthographic projection matrix (DX depth [0,1])
    static Matrix4 LookAt( const Vector::Vector3& eye, const Vector::Vector3& center, const Vector::Vector3& up );      // View matrix
    static Matrix4 Translate( const Vector::Vector3& v );                                                               // Translation matrix
    static Matrix4 Translate( float x, float y, float z );                                                              // Translation matrix (component form)
    static Matrix4 Scale( const Vector::Vector3& v );                                                                   // Scale matrix
    static Matrix4 Scale( float x, float y, float z );                                                                  // Scale matrix (component form)
    static Matrix4 Scale( float uniform );                                                                              // Uniform scale matrix
    static Matrix4 RotateAxis( float angleDeg, float axisX, float axisY, float axisZ );                                 // Axis-angle rotation matrix
    static Matrix4 FromQuaternion( const Orientation::Quaternion& q );                                                  // Rotation matrix from quaternion
    static Matrix4 ShadowFromNormal( float tx, float ty, float tz, const Vector::Vector3& N, float scale );             // Fused T(tx,ty,tz)*RotFromUpToN*Scale(s); zero acosf/cosf/sinf, zero Matrix4 products

    Matrix4 operator*( const Matrix4& rhs ) const; // Matrix multiplication
    Matrix4& operator*=( const Matrix4& rhs );     // In-place matrix multiplication
    Matrix4 Inverse() const;                       // Compute the inverse of this matrix
    const float* Data() const;                     // Pointer to column-major data for shader uploads
};
} // namespace Transformation
} // namespace Math
} // namespace SkullbonezCore
