/*
File: SkullbonezSource/Maths/RotationMatrix.h
Purpose:
  Declares rotation matrix helpers used by transforms and collision code.

Summary:
  RotationMatrix owns orthogonal-basis storage plus world/local vector and support-extent operations over that basis.

Invariants:
  - RotationMatrix stores an orthogonal basis; callers use TransposeMultiply as
    the inverse only under that assumption.
  - SupportExtentY expects local half-extents and returns the world-space
    downward extent for terrain/support tests.

Related:
  - SkullbonezSource/Maths/RotationMatrix.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "MathsCommon.h"
#include "Vector3.h"
#if ( defined( SKULLBONEZ_INTRINSICS ) && SKULLBONEZ_INTRINSICS ) ||                                                        \
    ( !defined( SKULLBONEZ_INTRINSICS ) && !defined( _DEBUG ) )
#include <immintrin.h> // SSE4.1 intrinsics for LoadSSE
#endif

namespace SkullbonezCore
{
namespace Math
{
namespace Transformation
{

class RotationMatrix
{

  public:
    RotationMatrix();                                                    // Initializes to identity rotation.
    RotationMatrix( float f11, float f12, float f13, float f21, float f22, float f23, float f31, float f32,
                    float f33 );                                         // Explicit row-major component construction.
    ~RotationMatrix() = default;

    // Resets to no-rotation matrix.
    Vector::Vector3 operator*( const Vector::Vector3& v ) const;         // Applies this rotation to v.
    Vector::Vector3 operator*=( const Vector::Vector3& v ) const;        // Legacy spelling for applying this rotation to v.
    Vector::Vector3 TransposeMultiply( const Vector::Vector3& v ) const; // R^T * v (inverse rotation for orthogonal matrices)

    // dot(abs(row_Y), v) is the maximum downward extent of an OBB with half-extents v.
    // Used for closed-form terrain bottom offset: avoids iterating all 8 vertices.
    float SupportExtentY( const Vector::Vector3& halfExtents ) const
    {
        return fabsf( m21 ) * halfExtents.x + fabsf( m22 ) * halfExtents.y + fabsf( m23 ) * halfExtents.z;
    }

    // Load matrix rows and columns into SSE registers for fast matrix-vector multiply.
    // row0 = {m11,m12,m13,0}, row1 = {m21,m22,m23,0}, row2 = {m31,m32,m33,0}
    // col0 = {m11,m21,m31,0}, col1 = {m12,m22,m32,0}, col2 = {m13,m23,m33,0}
#if ( defined( SKULLBONEZ_INTRINSICS ) && SKULLBONEZ_INTRINSICS ) ||                                                        \
    ( !defined( SKULLBONEZ_INTRINSICS ) && !defined( _DEBUG ) )
    void LoadSSE( __m128& row0, __m128& row1, __m128& row2, __m128& col0, __m128& col1, __m128& col2 ) const
    {
        row0 = _mm_setr_ps( m11, m12, m13, 0.0f );
        row1 = _mm_setr_ps( m21, m22, m23, 0.0f );
        row2 = _mm_setr_ps( m31, m32, m33, 0.0f );
        col0 = _mm_setr_ps( m11, m21, m31, 0.0f );
        col1 = _mm_setr_ps( m12, m22, m32, 0.0f );
        col2 = _mm_setr_ps( m13, m23, m33, 0.0f );
    }
#endif

  private:
    float m11, m12, m13, m21, m22, m23, m31, m32, m33;                   // Row-major 3x3 basis vectors.
};

// One program-wide no-rotation matrix shared by every including translation unit.
inline const RotationMatrix IDENTITY_MATRIX( 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f );

// Rotate point around normalized arbitrary axis axis by radians.
inline Vector::Vector3 RotatePointAboutArbitrary( float radians, const Vector::Vector3& axis, const Vector::Vector3& point )
{
    // Keep the intermediate named so the derivation below maps back to the old
    // formula comments and debugger watches.
    Vector::Vector3 vResult;

    // break rotation amount into vertical and horizontal components to
    // prepare for applying arbitrary 3d rotation matrix
    float sinTheta = sinf( radians );
    float cosTheta = cosf( radians );

    /*
        The following matrix for arbitrary axis rotation is explained in about 2.5
        pages in the '3D Math Primer for Graphics and Game Development' by
        Fletcher Dunn and Ian Parberry, pages 109-111.

        The matrix for arbitrary axis rotation is defined as follows:

            let radians = a
            let axis	 = n
            let point.x = x
                point.y = y
                point.z = z

            |    n.x^2*(1-cos(a))+cos(a)		n.x*n.y*(1-cos(a))+n.z*sin(a)		n.x*n.z*(1-cos(a))-n.y*sin(a) | | x
       | | n.x*n.y*(1-cos(a))-n.z*sin(a)		   n.y^2*(1-cos(a))+cos(a)			n.y*n.z*(1-cos*a))+n.x*sin(a) | | y
       | | n.x*n.z*(1-cos(a))+n.y*sin(a)		n.y*n.z*(1-cos(a))-n.x*sin(a)		   n.x^2*(1-cos(a))+cos(a)	  | | z
       |

            |	 (n.x^2*(1-cos(a))+cos(a))    * x   +	(n.x*n.y*(1-cos(a))+n.z*sin(a)) * y	  +
       (n.x*n.z*(1-cos(a))-n.y*sin(a)) * z | = | (n.x*n.y*(1-cos(a))-n.z*sin(a)) * x   +	   (n.y^2*(1-cos(a))+cos(a))
       * y	  +	  (n.y*n.z*(1-cos*a))+n.x*sin(a)) * z | | (n.x*n.z*(1-cos(a))+n.y*sin(a)) * x   +
       (n.y*n.z*(1-cos(a))-n.x*sin(a)) * y	  +	     (n.x^2*(1-cos(a))+cos(a))    * z |

            // old code...
            vResult.x = (axis.x * axis.x * (1 - cosTheta) + cosTheta)				* point.x +
                        (axis.x * axis.y * (1 - cosTheta) + axis.z * sinTheta)	* point.y +
                        (axis.x * axis.z * (1 - cosTheta) - axis.y * sinTheta)	* point.z ;

            vResult.y = (axis.x * axis.y * (1 - cosTheta) - axis.z * sinTheta)	* point.x +
                        (axis.y * axis.y * (1 - cosTheta) + cosTheta)				* point.y +
                        (axis.y * axis.z * (1 - cosTheta) + axis.x * sinTheta)	* point.z ;

            vResult.z = (axis.x * axis.z * (1 - cosTheta) + axis.y * sinTheta)	* point.x +
                        (axis.y * axis.z * (1 - cosTheta) - axis.x * sinTheta)	* point.y +
                        (axis.z * axis.z * (1 - cosTheta) + cosTheta)				* point.z ;
    */

    RotationMatrix matrix( ( axis.x * axis.x * ( 1 - cosTheta ) + cosTheta ),
                           ( axis.x * axis.y * ( 1 - cosTheta ) + axis.z * sinTheta ),
                           ( axis.x * axis.z * ( 1 - cosTheta ) - axis.y * sinTheta ),
                           ( axis.x * axis.y * ( 1 - cosTheta ) - axis.z * sinTheta ),
                           ( axis.y * axis.y * ( 1 - cosTheta ) + cosTheta ),
                           ( axis.y * axis.z * ( 1 - cosTheta ) + axis.x * sinTheta ),
                           ( axis.x * axis.z * ( 1 - cosTheta ) + axis.y * sinTheta ),
                           ( axis.y * axis.z * ( 1 - cosTheta ) - axis.x * sinTheta ),
                           ( axis.z * axis.z * ( 1 - cosTheta ) + cosTheta ) );

    return matrix * point;
}
} // namespace Transformation
} // namespace Math
} // namespace SkullbonezCore
