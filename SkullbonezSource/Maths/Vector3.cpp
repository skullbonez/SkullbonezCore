/*
File: SkullbonezSource/Maths/Vector3.cpp
Purpose:
  Implements the engine 3D vector type and vector math operations.

Mental model:
  Vector3.cpp implements the engine 3D vector type and vector math operations.
  As an implementation unit, keep edits anchored on units, basis conventions,
  and numerical assumptions and on the glossary/invariants below.

Glossary:
  Engine module: A source file with one focused responsibility inside the
  SkullbonezCore runtime.

Invariants:
  - Normalise and division treat zero magnitude/divisors as caller invariant
    failures; optional directions must test or provide a fallback first.
  - Debug default construction poisons components with NaN to expose
    use-before-init bugs.

Related:
  - SkullbonezSource/Maths/Vector3.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "Vector3.h"
#include "../Core/FatalError.h"
#include <limits>


using namespace SkullbonezCore::Math::Vector;


Vector3::Vector3()
{
#ifdef _DEBUG
    // Poison with NaN in debug so any use-before-init propagates visibly
    x = std::numeric_limits<float>::quiet_NaN();
    y = std::numeric_limits<float>::quiet_NaN();
    z = std::numeric_limits<float>::quiet_NaN();
#endif
}


Vector3::Vector3( const Vector3& v ) : x( v.x ), y( v.y ), z( v.z )
{
}


Vector3::Vector3( float fX, float fY, float fZ ) : x( fX ), y( fY ), z( fZ )
{
}


void Vector3::Zero()
{
    x = y = z = 0.0f;
}


void Vector3::Normalise()
{
    float magSq = x * x + y * y + z * z;

    if ( !magSq )
    {
        SB_FATAL( "Vector3", "Normalise requires a non-zero vector." );
    }
    float oneOverMag = 1.0f / sqrtf( magSq );

    x *= oneOverMag;
    y *= oneOverMag;
    z *= oneOverMag;
}


bool Vector3::IsCloseToZero() const
{
    return x < TOLERANCE && x > ZERO_TAKE_TOLERANCE && y < TOLERANCE && y > ZERO_TAKE_TOLERANCE && z < TOLERANCE &&
           z > ZERO_TAKE_TOLERANCE;
}


void Vector3::Absolute()
{
    x = fabsf( x );
    y = fabsf( y );
    z = fabsf( z );
}


void Vector3::SetAll( float nx, float ny, float nz )
{
    x = nx;
    y = ny;
    z = nz;
}


Vector3& Vector3::operator=( const Vector3& v )
{
    x = v.x;
    y = v.y;
    z = v.z;
    return *this;
}


bool Vector3::operator==( const Vector3& v ) const
{
    return ( x == v.x && y == v.y && z == v.z );
}


bool Vector3::operator!=( const Vector3& v ) const
{
    return ( x != v.x || y != v.y || z != v.z );
}


Vector3 Vector3::operator-() const
{
    return Vector3( -x, -y, -z );
}


Vector3 Vector3::operator+( const Vector3& v ) const
{
    return Vector3( x + v.x, y + v.y, z + v.z );
}


Vector3 Vector3::operator-( const Vector3& v ) const
{
    return Vector3( x - v.x, y - v.y, z - v.z );
}


Vector3 Vector3::operator*( float f ) const
{
    return Vector3( x * f, y * f, z * f );
}


Vector3 Vector3::operator/( float f ) const
{
    if ( !f )
    {
        SB_FATAL( "Vector3", "Scalar division requires a non-zero divisor." );
    }
    float oneOverA = 1.0f / f;
    return Vector3( x * oneOverA, y * oneOverA, z * oneOverA );
}


Vector3 Vector3::operator/( const Vector3& v ) const
{
    if ( !v.x || !v.y || !v.z )
    {
        SB_FATAL( "Vector3", "Component-wise division requires non-zero divisors. x=%f y=%f z=%f", v.x, v.y, v.z );
    }

    return Vector3( x / v.x, y / v.y, z / v.z );
}


void Vector3::Simplify()
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


Vector3& Vector3::operator+=( const Vector3& v )
{
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
}


Vector3& Vector3::operator-=( const Vector3& v )
{
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
}


Vector3& Vector3::operator*=( float f )
{
    x *= f;
    y *= f;
    z *= f;
    return *this;
}


Vector3& Vector3::operator/=( float f )
{
    if ( !f )
    {
        SB_FATAL( "Vector3", "Scalar divide-assign requires a non-zero divisor." );
    }
    float oneOverA = 1.0f / f;
    x *= oneOverA;
    y *= oneOverA;
    z *= oneOverA;
    return *this;
}


Vector3& Vector3::operator/=( const Vector3& v )
{
    if ( !v.x || !v.y || !v.z )
    {
        SB_FATAL( "Vector3", "Component-wise divide-assign requires non-zero divisors. x=%f y=%f z=%f", v.x, v.y, v.z );
    }
    x /= v.x;
    y /= v.y;
    z /= v.z;
    return *this;
}


float Vector3::operator*( const Vector3& v ) const
{
    return x * v.x + y * v.y + z * v.z;
}
