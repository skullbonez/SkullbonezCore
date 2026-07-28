/*
File: SkullbonezSource/Maths/Vector3.h
Purpose:
  Defines the engine 3D vector type and its inline hot-path operations.

Summary:
  Vector3 keeps three public float components and defines basic arithmetic at
  the call site so Debug physics does not pay cross-translation-unit calls.

Glossary:
  Component-wise: Operation applied independently to x, y, and z.
  Dot product: Scalar projection measure used for angles, support tests, and
  energy/velocity comparisons.
  Cross product: Vector perpendicular to two input directions; used to build
  tangent bases and angular directions.
  Normalized vector: Direction-only vector with length 1.0.
  Trivially copyable: Copy construction/assignment can move the three-float
    object representation directly without user-authored copy behavior.

Invariants:
  - Vector3 is a public-component math type; hot paths read and write x, y, and
    z directly.
  - The object representation remains exactly three floats (12 bytes), and copy
    construction/assignment remain trivial.
  - Debug default construction poisons components with NaN; callers must assign
    a value before reading them.
  - Try normalization/division leaves the source and any output untouched on
    failure; plain operations assert only in Debug and propagate IEEE values in
    Release.
  - ZERO_VECTOR is a shared value sentinel, not mutable global state.

Related:
  - SkullbonezTests/TestVector3.cpp
  - SkullbonezSource/Maths/MathsCommon.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "MathsCommon.h"
#include <cassert>
#include <limits>
#include <type_traits>

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
    float x, y, z;                                                  // Public POD-style components; math hot paths access them directly.

    Vector3()
    {
#ifdef _DEBUG

        // Hazard: poison default-constructed components so use-before-init
        // propagates visibly through Debug math instead of mimicking a valid zero.
        x = std::numeric_limits<float>::quiet_NaN();
        y = std::numeric_limits<float>::quiet_NaN();
        z = std::numeric_limits<float>::quiet_NaN();
#endif
    }

    Vector3( const Vector3& v ) = default;
    Vector3( float fX, float fY, float fZ ) : x( fX ), y( fY ), z( fZ )
    {
    }

    void Zero()
    {
        x = y = z = 0.0f;
    }

    void Normalise()                                                // Debug-asserts on zero; Release propagates IEEE inf/NaN.
    {
        float magSq = x * x + y * y + z * z;

        // Why: a zero direction is a caller-reachable numeric edge, not a
        // lane-F engine invariant. TryNormalise reports it; the plain hot API
        // keeps only a Debug misuse tripwire and Release IEEE propagation.
        assert( magSq != 0.0f && "Vector3::Normalise requires a non-zero vector" );
        const float oneOverMag = 1.0f / sqrtf( magSq );
        x *= oneOverMag;
        y *= oneOverMag;
        z *= oneOverMag;
    }

    bool TryNormalise()
    {
        const float magSq = x * x + y * y + z * z;

        if ( magSq == 0.0f )
        {
            return false;
        }

        const float oneOverMag = 1.0f / sqrtf( magSq );
        x *= oneOverMag;
        y *= oneOverMag;
        z *= oneOverMag;
        return true;
    }

    bool TryNormalised( Vector3& out ) const
    {
        Vector3 candidate = *this;

        if ( !candidate.TryNormalise() )
        {
            return false;
        }

        out = candidate;
        return true;
    }

    void Absolute()                                                 // Component-wise absolute value; mutates this vector.
    {
        x = fabsf( x );
        y = fabsf( y );
        z = fabsf( z );
    }

    bool IsCloseToZero() const                                      // Tolerance check for float noise near zero.
    {
        return x < TOLERANCE && x > ZERO_TAKE_TOLERANCE && y < TOLERANCE && y > ZERO_TAKE_TOLERANCE && z < TOLERANCE &&
               z > ZERO_TAKE_TOLERANCE;
    }

    void Simplify()                                                 // Components within the engine epsilon snap to 0.0f.
    {

        if ( x < TOLERANCE && x > ZERO_TAKE_TOLERANCE )
        {
            x = 0.0f;
        }

        if ( y < TOLERANCE && y > ZERO_TAKE_TOLERANCE )
        {
            y = 0.0f;
        }

        if ( z < TOLERANCE && z > ZERO_TAKE_TOLERANCE )
        {
            z = 0.0f;
        }
    }

    void SetAll( float nx, float ny, float nz )
    {
        x = nx;
        y = ny;
        z = nz;
    }

    Vector3& operator=( const Vector3& v ) = default;

    Vector3& operator+=( const Vector3& v )
    {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }

    Vector3& operator-=( const Vector3& v )
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }

    Vector3& operator*=( float f )
    {
        x *= f;
        y *= f;
        z *= f;
        return *this;
    }

    Vector3& operator/=( float f )
    {
        assert( f != 0.0f && "Vector3 scalar divide-assign requires a non-zero divisor" );
        const float oneOverA = 1.0f / f;
        x *= oneOverA;
        y *= oneOverA;
        z *= oneOverA;
        return *this;
    }

    Vector3& operator/=( const Vector3& v )
    {
        assert( v.x != 0.0f && v.y != 0.0f && v.z != 0.0f && "Vector3 component divide-assign requires non-zero divisors" );
        x /= v.x;
        y /= v.y;
        z /= v.z;
        return *this;
    }

    bool TryDivide( float f )
    {

        if ( f == 0.0f )
        {
            return false;
        }

        const float oneOverA = 1.0f / f;
        x *= oneOverA;
        y *= oneOverA;
        z *= oneOverA;
        return true;
    }

    bool TryDivide( const Vector3& v )
    {

        if ( v.x == 0.0f || v.y == 0.0f || v.z == 0.0f )
        {
            return false;
        }

        x /= v.x;
        y /= v.y;
        z /= v.z;
        return true;
    }

    Vector3 operator-() const
    {
        return Vector3( -x, -y, -z );
    }

    Vector3 operator+( const Vector3& v ) const
    {
        return Vector3( x + v.x, y + v.y, z + v.z );
    }

    Vector3 operator-( const Vector3& v ) const
    {
        return Vector3( x - v.x, y - v.y, z - v.z );
    }

    Vector3 operator*( float f ) const
    {
        return Vector3( x * f, y * f, z * f );
    }

    Vector3 operator/( float f ) const
    {
        assert( f != 0.0f && "Vector3 scalar division requires a non-zero divisor" );
        const float oneOverA = 1.0f / f;
        return Vector3( x * oneOverA, y * oneOverA, z * oneOverA );
    }

    Vector3 operator/( const Vector3& v ) const
    {
        assert( v.x != 0.0f && v.y != 0.0f && v.z != 0.0f && "Vector3 component division requires non-zero divisors" );
        return Vector3( x / v.x, y / v.y, z / v.z );
    }

    bool TryDivided( float f, Vector3& out ) const
    {

        if ( f == 0.0f )
        {
            return false;
        }

        const float oneOverA = 1.0f / f;
        out = Vector3( x * oneOverA, y * oneOverA, z * oneOverA );
        return true;
    }

    bool TryDivided( const Vector3& v, Vector3& out ) const
    {

        if ( v.x == 0.0f || v.y == 0.0f || v.z == 0.0f )
        {
            return false;
        }

        out = Vector3( x / v.x, y / v.y, z / v.z );
        return true;
    }

    bool operator==( const Vector3& v ) const
    {
        return x == v.x && y == v.y && z == v.z;
    }

    bool operator!=( const Vector3& v ) const
    {
        return x != v.x || y != v.y || z != v.z;
    }
};

// Why: vector3-inline-hot-math promises memcpy-safe copies and preserves the
// three-float ABI used by physics/render stores; fail compilation on drift.
static_assert( std::is_trivially_copyable_v<Vector3> );
static_assert( sizeof( Vector3 ) == 12 );

inline const Vector3 ZERO_VECTOR { 0.0f, 0.0f, 0.0f };              // Shared origin/no-motion sentinel.

// Returns the dot product using the established x/y/z multiply-add order.
// Invariant: Physics byte-exact validation depends on this arithmetic spelling
// remaining lhs.x*rhs.x + lhs.y*rhs.y + lhs.z*rhs.z without reassociation.
inline float Dot( const Vector3& lhs, const Vector3& rhs )
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

// Reflect incident about a normalized surface normal; callers own normalization.
inline Vector3 VectorReflect( const Vector3& incident, const Vector3& normal )
{
    return normal * ( 2 * ( Dot( normal, incident ) ) ) - incident; // Pg 153, Lengyel
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
