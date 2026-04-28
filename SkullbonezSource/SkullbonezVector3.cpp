/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezVector3.h"

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
Vector3::Vector3()
{
}

/* -- COPY CONSTRUCTOR ------------------------------------------------------------*/
Vector3::Vector3( const Vector3& v )
    : x( v.x ), y( v.y ), z( v.z )
{
}

/* -- OVERLOADED CONSTRUCTOR ------------------------------------------------------*/
Vector3::Vector3( float fX,
                  float fY,
                  float fZ )
    : x( fX ), y( fY ), z( fZ )
{
}

/* -- ZERO ------------------------------------------------------------------------*/
void Vector3::Zero()
{
    x = y = z = 0.0f;
}

/* -- NORMALISE -------------------------------------------------------------------*/
void Vector3::Normalise()
{
    float magSq = x * x +
                  y * y +
                  z * z;

    if ( !magSq )
    {
        throw std::runtime_error( "Division by zero.  (Vector3::Normalise)" );
    }
    float oneOverMag = 1.0f / sqrtf( magSq );

    x *= oneOverMag;
    y *= oneOverMag;
    z *= oneOverMag;
}

/* -- ABSOLUTE --------------------------------------------------------------------*/
bool Vector3::IsCloseToZero() const
{
    return x<TOLERANCE&& x> ZERO_TAKE_TOLERANCE &&
           y<TOLERANCE&& y> ZERO_TAKE_TOLERANCE &&
           z<TOLERANCE&& z> ZERO_TAKE_TOLERANCE;
}

/* -- ABSOLUTE --------------------------------------------------------------------*/
void Vector3::Absolute()
{
    x = fabs( x );
    y = fabs( y );
    z = fabs( z );
}

/* -- SET ALL ---------------------------------------------------------------------*/
void Vector3::SetAll( float nx,
                      float ny,
                      float nz )
{
    x = nx;
    y = ny;
    z = nz;
}

/* -- OPERATOR = ------------------------------------------------------------------*/
Vector3& Vector3::operator=( const Vector3& v )
{
    x = v.x;
    y = v.y;
    z = v.z;
    return *this;
}

/* -- OPERATOR == -----------------------------------------------------------------*/
bool Vector3::operator==( const Vector3& v ) const
{
    return ( x == v.x &&
             y == v.y &&
             z == v.z );
}

/* -- OPERATOR != -----------------------------------------------------------------*/
bool Vector3::operator!=( const Vector3& v ) const
{
    return ( x != v.x ||
             y != v.y ||
             z != v.z );
}

/* -- OPERATOR - ------------------------------------------------------------------*/
Vector3 Vector3::operator-() const
{
    return Vector3( -x,
                    -y,
                    -z );
}

/* -- OPERATOR + ------------------------------------------------------------------*/
Vector3 Vector3::operator+( const Vector3& v ) const
{
    return Vector3( x + v.x,
                    y + v.y,
                    z + v.z );
}

/* -- OPERATOR - ------------------------------------------------------------------*/
Vector3 Vector3::operator-( const Vector3& v ) const
{
    return Vector3( x - v.x,
                    y - v.y,
                    z - v.z );
}

/* -- OPERATOR * ------------------------------------------------------------------*/
Vector3 Vector3::operator*( float f ) const
{
    return Vector3( x * f,
                    y * f,
                    z * f );
}

/* -- OPERATOR / ------------------------------------------------------------------*/
Vector3 Vector3::operator/( float f ) const
{
    if ( !f )
    {
        throw std::runtime_error( "Division by zero.  (Vector3::Operator/)" );
    }
    float oneOverA = 1.0f / f;
    return Vector3( x * oneOverA,
                    y * oneOverA,
                    z * oneOverA );
}

/* -- OPERATOR / ------------------------------------------------------------------*/
Vector3 Vector3::operator/( const Vector3& v ) const
{
    if ( !v.x || !v.y || !v.z )
    {
        throw std::runtime_error( "Division by zero.  (Vector3::Operator/)" );
    }

    return Vector3( x / v.x,
                    y / v.y,
                    z / v.z );
}

/* -- SIMPLIFY --------------------------------------------------------------------*/
void Vector3::Simplify()
{
    if ( x<TOLERANCE&& x> ZERO_TAKE_TOLERANCE )
    {
        x = 0.0f;
    }
    if ( y<TOLERANCE&& y> ZERO_TAKE_TOLERANCE )
    {
        y = 0.0f;
    }
    if ( z<TOLERANCE&& z> ZERO_TAKE_TOLERANCE )
    {
        z = 0.0f;
    }
}

/* -- OPERATOR += -----------------------------------------------------------------*/
Vector3& Vector3::operator+=( const Vector3& v )
{
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
}

/* -- OPERATOR -= -----------------------------------------------------------------*/
Vector3& Vector3::operator-=( const Vector3& v )
{
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
}

/* -- OPERATOR *= -----------------------------------------------------------------*/
Vector3& Vector3::operator*=( float f )
{
    x *= f;
    y *= f;
    z *= f;
    return *this;
}

/* -- OPERATOR /= -----------------------------------------------------------------*/
Vector3& Vector3::operator/=( float f )
{
    if ( !f )
    {
        throw std::runtime_error( "Division by zero.  (Vector3::Operator/=)" );
    }
    float oneOverA = 1.0f / f;
    x *= oneOverA;
    y *= oneOverA;
    z *= oneOverA;
    return *this;
}

/* -- OPERATOR /= -----------------------------------------------------------------*/
Vector3& Vector3::operator/=( const Vector3& v )
{
    if ( !v.x || !v.y || !v.z )
    {
        throw std::runtime_error( "Division by zero.  (Vector3::Operator/=)" );
    }
    x /= v.x;
    y /= v.y;
    z /= v.z;
    return *this;
}

/* -- OPERATOR *= -----------------------------------------------------------------*/
float Vector3::operator*( const Vector3& v ) const
{
    return x * v.x +
           y * v.y +
           z * v.z;
}
