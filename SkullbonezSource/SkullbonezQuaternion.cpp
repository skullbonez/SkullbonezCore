// --- Includes ---
#include "SkullbonezQuaternion.h"


// --- Usings ---
using namespace SkullbonezCore::Math::Orientation;


Quaternion::Quaternion()
{
}


Quaternion::Quaternion( float fX,
                        float fY,
                        float fZ,
                        float fW )
    : m_x( fX ),
      m_y( fY ),
      m_z( fZ ),
      m_w( fW )
{
}


void Quaternion::Identity()
{
    m_x = 0.0f;
    m_y = 0.0f;
    m_z = 0.0f;
    m_w = 1.0f;
}


void Quaternion::Normalise()
{
    float magSq = m_w * m_w +
                  m_x * m_x +
                  m_y * m_y +
                  m_z * m_z;

    if ( !magSq )
    {
        throw std::runtime_error( "Division by zero.  (Quaternion::Normalise)" );
    }

    float oneOverMag = 1.0f / sqrtf( magSq );

    m_w *= oneOverMag;
    m_x *= oneOverMag;
    m_y *= oneOverMag;
    m_z *= oneOverMag;
}


void Quaternion::RotateAboutXYZ( float xRadians,
                                 float yRadians,
                                 float zRadians )
{
    // Treat XYZ inputs as one angular-displacement vector and integrate
    // with a single axis-angle update to avoid Euler-order coupling.
    float angleSq = xRadians * xRadians +
                    yRadians * yRadians +
                    zRadians * zRadians;

    if ( angleSq <= TOLERANCE * TOLERANCE )
    {
        return;
    }

    float angle = sqrtf( angleSq );
    float invAngle = 1.0f / angle;
    RotateAboutAxis( Vector3( xRadians * invAngle,
                              yRadians * invAngle,
                              zRadians * invAngle ),
                     angle );
}


void Quaternion::RotateAboutXYZ( const Vector3& vRadians )
{
    RotateAboutXYZ( vRadians.x, vRadians.y, vRadians.z );
}


void Quaternion::RotateAboutAxis( const Vector3& axis, float angle )
{
    // Single rotation about an arbitrary WORLD-space axis — no Euler decomposition,
    // no gimbal lock. This codebase uses "anti-Hamilton" quaternion multiplication
    // (operator* computes Hamilton(q2*q1) when called as q1*q2), and GetOrientationMatrix
    // returns the transpose of the Hamilton active-rotation matrix. The combination
    // means: to apply an ACTIVE world rotation by +angle about world axis to the
    // existing orientation we must left-multiply by the *inverse* delta in code-space
    // — i.e. negate the axis (or sin) component of delta and pre-multiply.
    float halfAngle = angle * 0.5f;
    float s = -sinf( halfAngle );
    Quaternion delta( axis.x * s, axis.y * s, axis.z * s, cosf( halfAngle ) );
    *this = delta * *this;
    Normalise();
}


RotationMatrix Quaternion::GetOrientationMatrix()
{
    // Return the RIGHT HANDED rotation matrix
    return RotationMatrix( 1 - ( 2 * m_y * m_y ) - ( 2 * m_z * m_z ),
                           ( 2 * m_x * m_y ) + ( 2 * m_w * m_z ),
                           ( 2 * m_x * m_z ) - ( 2 * m_w * m_y ),
                           ( 2 * m_x * m_y ) - ( 2 * m_w * m_z ),
                           1 - ( 2 * m_x * m_x ) - ( 2 * m_z * m_z ),
                           ( 2 * m_y * m_z ) + ( 2 * m_w * m_x ),
                           ( 2 * m_x * m_z ) + ( 2 * m_w * m_y ),
                           ( 2 * m_y * m_z ) - ( 2 * m_w * m_x ),
                           1 - ( 2 * m_x * m_x ) - ( 2 * m_y * m_y ) );
}


Quaternion Quaternion::operator*( const Quaternion& q ) const
{
    Quaternion result;

    result.m_w = m_w * q.m_w -
                 m_x * q.m_x -
                 m_y * q.m_y -
                 m_z * q.m_z;

    result.m_x = m_w * q.m_x +
                 m_x * q.m_w -
                 m_y * q.m_z +
                 m_z * q.m_y;

    result.m_y = m_w * q.m_y +
                 m_x * q.m_z +
                 m_y * q.m_w -
                 m_z * q.m_x;

    result.m_z = m_w * q.m_z -
                 m_x * q.m_y +
                 m_y * q.m_x +
                 m_z * q.m_w;

    return result;
}


Quaternion& Quaternion::operator*=( const Quaternion& q )
{
    *this = *this * q;
    return *this;
}


Quaternion Quaternion::GetQtnRotatedAboutX( float fRadians )
{
    float radiansDiv2 = fRadians * 0.5f;

    return Quaternion( sinf( radiansDiv2 ),
                       0.0f,
                       0.0f,
                       cosf( radiansDiv2 ) );
}


Quaternion Quaternion::GetQtnRotatedAboutY( float fRadians )
{
    float radiansDiv2 = fRadians * 0.5f;

    return Quaternion( 0.0f,
                       sinf( radiansDiv2 ),
                       0.0f,
                       cosf( radiansDiv2 ) );
}


Quaternion Quaternion::GetQtnRotatedAboutZ( float fRadians )
{
    float radiansDiv2 = fRadians * 0.5f;

    return Quaternion( 0.0f,
                       0.0f,
                       sinf( radiansDiv2 ),
                       cosf( radiansDiv2 ) );
}
