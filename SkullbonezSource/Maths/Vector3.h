/*
File: SkullbonezSource/Maths/Vector3.h
Purpose:
  Declares the engine 3D vector type and vector math operations.

Mental model:
  Math code is shared infrastructure. Coordinate conventions, units,
  handedness, and simplifications matter because subtle assumptions spread
  through rendering and physics.

Glossary:
  Component-wise: Operation applied independently to x, y, and z.
  Dot product: Scalar projection measure used for angles, support tests, and
  energy/velocity comparisons.
  Cross product: Vector perpendicular to two input directions; used to build
  tangent bases and angular directions.
  Normalized vector: Direction-only vector with length 1.0.
  POD (Plain Old Data): Simple public data layout used here so old math and
  physics paths can pass vectors cheaply without accessor overhead.

Related:
  - SkullbonezSource/Maths/Vector3.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"

namespace SkullbonezCore
{
namespace Math
{
namespace Vector
{
/* -- Vector3
------------------------------------------------------------------------------------------------------------------------------------------------

    Represents a 3D vector, no encapsulation required for this class.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Vector3
{

  public:
    float x, y, z;                                            // Public POD-style components; math hot paths access them directly.

    Vector3();
    Vector3( const Vector3& v );
    Vector3( float fX, float fY, float fZ );
    void Zero();
    void Normalise();                                         // Scales non-zero vectors to unit length for direction-only math.
    void Absolute();                                          // Component-wise absolute value; mutates this vector.
    bool IsCloseToZero() const;                               // Tolerance check for float noise near zero.
    void Simplify();                                          // Components within the engine epsilon snap to 0.0f.
    void SetAll( float nx, float ny, float nz );
    Vector3& operator=( const Vector3& v );                   // Copies all three components.
    Vector3& operator+=( const Vector3& v );                  // Component-wise addition into this vector.
    Vector3& operator-=( const Vector3& v );                  // Component-wise subtraction into this vector.
    Vector3& operator*=( float f );                           // Uniform scalar scale into this vector.
    Vector3& operator/=( float f );                           // Uniform scalar divide into this vector.
    Vector3& operator/=( const Vector3& );                    // Component-wise divide for axis-specific scaling.
    Vector3 operator-() const;                                // Unary minus returns the negative of the vector
    Vector3 operator+( const Vector3& v ) const;              // Binary add vectors
    Vector3 operator-( const Vector3& v ) const;              // Binary subtract vectors
    Vector3 operator*( float f ) const;                       // Multiplication by scalar
    Vector3 operator/( float f ) const;                       // Division by scalar
    Vector3 operator/( const Vector3& v ) const;              // Division by vector (individual component division)
    bool operator==( const Vector3& v ) const;
    bool operator!=( const Vector3& v ) const;
    float operator*( const Vector3& v ) const;                // Vector dot product
};

const Vector3 ZERO_VECTOR = Vector3( 0.0f, 0.0f, 0.0f );      // Shared origin/no-motion sentinel.

// Reflect incident about a normalized surface normal; callers own normalization.
inline Vector3 VectorReflect( const Vector3& incident, const Vector3& normal )
{
    return normal * ( 2 * ( normal * incident ) ) - incident; // Pg 153, Lengyel
}

// Component-wise multiplication for scale vectors and basis masks.
inline Vector3 VectorMultiply( const Vector3& v1, const Vector3& v2 )
{
    return Vector3( v1.x * v2.x, v1.y * v2.y, v1.z * v2.z );
}

inline float VectorMag( const Vector3& v )
{
    return sqrtf( v.x * v.x + v.y * v.y + v.z * v.z );
}

inline float VectorMagSquared( const Vector3& v )
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

inline Vector3 CrossProduct( const Vector3& v1, const Vector3& v2 )
{
    return Vector3( v1.y * v2.z - v1.z * v2.y, v1.z * v2.x - v1.x * v2.z, v1.x * v2.y - v1.y * v2.x );
}

inline float Distance( const Vector3& v1, const Vector3& v2 )
{
    float dx = v1.x - v2.x;
    float dy = v1.y - v2.y;
    float dz = v1.z - v2.z;
    return sqrtf( dx * dx + dy * dy + dz * dz );
}

// Squared distance is the hot-path comparison form; it avoids sqrt when the
// caller only needs ordering or threshold checks.
inline float DistanceSquared( const Vector3& v1, const Vector3& v2 )
{
    float dx = v1.x - v2.x;
    float dy = v1.y - v2.y;
    float dz = v1.z - v2.z;
    return dx * dx + dy * dy + dz * dz;
}

// Left scalar multiplication keeps formulas readable when coefficients lead.
inline Vector3 operator*( float f, const Vector3& v )
{
    return Vector3( f * v.x, f * v.y, f * v.z );
}
} // namespace Vector
} // namespace Math
} // namespace SkullbonezCore
