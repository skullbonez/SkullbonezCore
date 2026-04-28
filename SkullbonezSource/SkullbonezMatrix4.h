/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_MATRIX4_H
#define SKULLBONEZ_MATRIX4_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"

/* -- FORWARD DECLARATIONS ----------------------------------------------------------------------------------------------------------------------------------------------*/
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

/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;

namespace SkullbonezCore
{
namespace Math
{
namespace Transformation
{
/* -- Matrix4 -----------------------------------------------------------------------------------------------------------------------------------------------

    A 4x4 column-major matrix for 3D transformations (projection, view, model).
    Stored in column-major order to match OpenGL's convention:
    m[0] m[4] m[8]  m[12]
    m[1] m[5] m[9]  m[13]
    m[2] m[6] m[10] m[14]
    m[3] m[7] m[11] m[15]
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Matrix4
{

  public:
    float m[16];

    Matrix4();                // Default constructor (identity)
    Matrix4( const float* values ); // Construct from 16-element column-major array

    static Matrix4 Perspective( float fovDegrees, float aspect, float nearPlane, float farPlane );             // Perspective projection matrix
    static Matrix4 Ortho( float left, float right, float bottom, float top, float nearPlane, float farPlane ); // Orthographic projection matrix
    static Matrix4 LookAt( const Vector3& eye, const Vector3& center, const Vector3& up );                     // View matrix
    static Matrix4 Translate( const Vector3& v );                                                              // Translation matrix
    static Matrix4 Translate( float x, float y, float z );                                                     // Translation matrix (component form)
    static Matrix4 Scale( const Vector3& v );                                                                  // Scale matrix
    static Matrix4 Scale( float x, float y, float z );                                                         // Scale matrix (component form)
    static Matrix4 Scale( float uniform );                                                                     // Uniform scale matrix
    static Matrix4 RotateAxis( float angleDeg, float axisX, float axisY, float axisZ );                        // Axis-angle rotation matrix
    static Matrix4 FromQuaternion( const Orientation::Quaternion& q );                                         // Rotation matrix from quaternion

    Matrix4 operator*( const Matrix4& rhs ) const; // Matrix multiplication
    Matrix4& operator*=( const Matrix4& rhs );     // In-place matrix multiplication
    const float* Data() const;               // Pointer to column-major data for glUniformMatrix4fv
};
} // namespace Transformation
} // namespace Math
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
