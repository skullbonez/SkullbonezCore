/*
File: SkullbonezSource/Maths/RotationMatrix.cpp
Purpose:
  Implements rotation matrix helpers used by transforms and collision code.

Summary:
  RotationMatrix.cpp implements rotation matrix helpers used by transforms and
  collision code. As an implementation unit, keep edits anchored on units,
  basis conventions, and numerical assumptions and on the glossary/invariants
  below.

Glossary:
  Engine module: A source file with one focused responsibility inside the
  SkullbonezCore runtime.

Invariants:
  - RotationMatrix represents an orthogonal 3x3 rotation basis; TransposeMultiply
    is treated as inverse rotation.
  - Component layout matches the row/column use documented in RotationMatrix.h.

Related:
  - SkullbonezSource/Maths/RotationMatrix.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "RotationMatrix.h"


using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;


RotationMatrix::RotationMatrix()
    : m11( 1.0f ), m12( 0.0f ), m13( 0.0f ), m21( 0.0f ), m22( 1.0f ), m23( 0.0f ), m31( 0.0f ), m32( 0.0f ),
      m33( 1.0f ) // Identity matrix
{
}


RotationMatrix::RotationMatrix( float f11, float f12, float f13, float f21, float f22, float f23, float f31, float f32,
                                float f33 )
    : m11( f11 ), m12( f12 ), m13( f13 ), m21( f21 ), m22( f22 ), m23( f23 ), m31( f31 ), m32( f32 ), m33( f33 )
{
}


void RotationMatrix::Identity()
{
    m11 = 1.0f;
    m12 = 0.0f;
    m13 = 0.0f;

    m21 = 0.0f;
    m22 = 1.0f;
    m23 = 0.0f;

    m31 = 0.0f;
    m32 = 0.0f;
    m33 = 1.0f;
}


Vector3 RotationMatrix::operator*( const Vector3& v ) const
{
    return Vector3( m11 * v.x + m12 * v.y + m13 * v.z, m21 * v.x + m22 * v.y + m23 * v.z,
                    m31 * v.x + m32 * v.y + m33 * v.z );
}


Vector3 RotationMatrix::operator*=( const Vector3& v ) const
{
    return *this * v;
}


Vector3 RotationMatrix::TransposeMultiply( const Vector3& v ) const
{

    // For an orthogonal rotation matrix, R^T = R^-1.
    // Columns become rows: (R^T * v)_i = column_i . v
    return Vector3( m11 * v.x + m21 * v.y + m31 * v.z, m12 * v.x + m22 * v.y + m32 * v.z,
                    m13 * v.x + m23 * v.y + m33 * v.z );
}
