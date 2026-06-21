/*
File: SkullbonezSource/Vector3.h
Purpose:
  Declares the engine 3D vector type and vector math operations.

Mental model:
  Math code is shared infrastructure. Coordinate conventions, units,
  handedness, and simplifications matter because subtle assumptions spread
  through rendering and physics.

Glossary:

Related:
  - SkullbonezSource/Vector3.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "Common.h"

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
    float x, y, z; // Vector components

    Vector3();
    Vector3( const Vector3& v );
    Vector3( float fX, float fY, float fZ );
    void Zero();
    void Normalise();           // Normalise the vector
    void Absolute();            // Component-wise absolute value; mutates this vector.
    bool IsCloseToZero() const; // Tolerance check for float noise near zero.
    void Simplify();            // Components within the engine epsilon snap to 0.0f.
    void SetAll( float nx, float ny, float nz );
    Vector3& operator=( const Vector3& v );      // Vector assignment
    Vector3& operator+=( const Vector3& v );     // += Overload
    Vector3& operator-=( const Vector3& v );     // -= Overload
    Vector3& operator*=( float f );              // *= Overload
    Vector3& operator/=( float f );              // /= Overload
    Vector3& operator/=( const Vector3& );       // /= Overload
    Vector3 operator-() const;                   // Unary minus returns the negative of the vector
    Vector3 operator+( const Vector3& v ) const; // Binary add vectors
    Vector3 operator-( const Vector3& v ) const; // Binary subtract vectors
    Vector3 operator*( float f ) const;          // Multiplication by scalar
    Vector3 operator/( float f ) const;          // Division by scalar
    Vector3 operator/( const Vector3& v ) const; // Division by vector (individual component division)
    bool operator==( const Vector3& v ) const;
    bool operator!=( const Vector3& v ) const;
    float operator*( const Vector3& v ) const; // Vector dot product
};

const Vector3 ZERO_VECTOR = Vector3( 0.0f, 0.0f, 0.0f ); // Zero vector

// Reflect incident vector about normal vector (arguments must be normalised)
inline Vector3 VectorReflect( const Vector3& incident, const Vector3& normal )
{
    return normal * ( 2 * ( normal * incident ) ) - incident; // Pg 153, Lengyel
}

// Multiply 2 vectors together, component by component
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

// Compute the distance between two points, squared.  Often useful when
// comparing distances since square root is a slow CPU instruction
inline float DistanceSquared( const Vector3& v1, const Vector3& v2 )
{
    float dx = v1.x - v2.x;
    float dy = v1.y - v2.y;
    float dz = v1.z - v2.z;
    return dx * dx + dy * dy + dz * dz;
}

// Scalar on the left multiplication, for symmetry
inline Vector3 operator*( float f, const Vector3& v )
{
    return Vector3( f * v.x, f * v.y, f * v.z );
}
} // namespace Vector
} // namespace Math
} // namespace SkullbonezCore
