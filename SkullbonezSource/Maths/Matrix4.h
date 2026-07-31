/*
File: SkullbonezSource/Maths/Matrix4.h
Purpose:
  Declares the engine matrix type and common transform operations.

Summary:
  Matrix4.h declares the engine matrix type and common transform operations.

Glossary:
  Clip space: Coordinate range produced by projection matrices before viewport
  mapping. DX12 uses a [0,1] depth range.
  DX12 (DirectX 12): The production renderer API; its projection path expects
  normalized depth in the [0,1] range.

Invariants:
  - Matrix memory is column-major: Data() returns the shader-facing layout.
  - Legacy projection helpers keep old depth conventions, while ZeroToOne
    helpers are the DX12 render path contract.

Related:
  - SkullbonezSource/Maths/Matrix4.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "MathsCommon.h"
#include "Vector3.h"

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

/* -- Matrix4
-----------------------------------------------------------------------------------------------------------------------------------------------

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

    Matrix4();                                             // Identity matrix by default.
    Matrix4( const float* values );                        // Copies a 16-float column-major array.

    static Matrix4 PerspectiveZeroToOne( float fovDegrees, float aspect, float nearPlane,
                                         float farPlane ); // Perspective projection matrix (DX12 depth [0,1])
    static Matrix4 Ortho( float left, float right, float bottom, float top, float nearPlane,
                          float farPlane );                // Orthographic projection with legacy depth convention.
    static Matrix4 OrthoZeroToOne( float left, float right, float bottom, float top, float nearPlane,
                                   float farPlane );       // Orthographic projection matrix (DX12 depth [0,1])
    static Matrix4 LookAt( const Vector::Vector3& eye, const Vector::Vector3& center,
                           const Vector::Vector3& up );    // Camera view matrix from eye/target/up vectors.
    static Matrix4 Translate( const Vector::Vector3& v );  // Translation by vector components.
    static Matrix4 Translate( float x, float y, float z ); // Translation by explicit components.

    // Non-uniform scale by vector components.
    static Matrix4 Scale( float x, float y, float z );     // Non-uniform scale by explicit components.
    static Matrix4 Scale( float uniform );                 // Uniform scale for all axes.

    static Matrix4
    FromQuaternion( const Orientation::Quaternion& q );    // Quaternion orientation converted for transform composition.

    Matrix4 operator*( const Matrix4& rhs ) const;         // Transform composition in engine matrix order.
    Matrix4& operator*=( const Matrix4& rhs );             // In-place transform composition in engine matrix order.
    Matrix4 Inverse() const;
    const float* Data() const;                             // Column-major memory pointer for shader constant uploads.
};
} // namespace Transformation
} // namespace Math
} // namespace SkullbonezCore
