/*
File: SkullbonezSource/Maths/RotationMatrix.cpp
Purpose:
  Implements rotation matrix helpers used by transforms and collision code.

Summary:
  RotationMatrix provides the orthogonal 3x3 basis operations used to rotate
  vectors, compose transforms, and apply transpose-as-inverse collision math.

Invariants:
  - RotationMatrix represents an orthogonal 3x3 rotation basis; TransposeMultiply
    is treated as inverse rotation.
  - Component layout matches the row/column use documented in RotationMatrix.h.

Related:
  - SkullbonezSource/Maths/RotationMatrix.h
  - Agentic/Reference/engine-glossary.md
*/
#include "RotationMatrix.h"


using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;


RotationMatrix::RotationMatrix()
    : m11( 1.0f ), m12( 0.0f ), m13( 0.0f ), m21( 0.0f ), m22( 1.0f ), m23( 0.0f ), m31( 0.0f ), m32( 0.0f ),
      m33( 1.0f ) // Identity matrix
{
}


RotationMatrix::RotationMatrix( const Vector3& row0, const Vector3& row1, const Vector3& row2 )
    : m11( row0.x ), m12( row0.y ), m13( row0.z ), m21( row1.x ), m22( row1.y ), m23( row1.z ), m31( row2.x ), m32( row2.y ),
      m33( row2.z )
{
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
