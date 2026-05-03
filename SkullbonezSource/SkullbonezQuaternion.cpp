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
    // q ← q / |q|,   |q|² = w² + x² + y² + z²
    const float magSq = m_w * m_w + m_x * m_x + m_y * m_y + m_z * m_z;
    if ( !magSq )
    {
        throw std::runtime_error( "Division by zero.  (Quaternion::Normalise)" );
    }
    const float invMag = 1.0f / sqrtf( magSq );
    m_w *= invMag;
    m_x *= invMag;
    m_y *= invMag;
    m_z *= invMag;
}


void Quaternion::RotateAboutXYZ( float xRadians,
                                 float yRadians,
                                 float zRadians )
{
    // q ← q · (q_x · q_y · q_z),   then re-normalise to combat float drift
    *this *= GetQtnRotatedAboutX( xRadians ) *
             GetQtnRotatedAboutY( yRadians ) *
             GetQtnRotatedAboutZ( zRadians );
    Normalise();
}


void Quaternion::RotateAboutXYZ( const Vector3& vRadians )
{
    RotateAboutXYZ( vRadians.x, vRadians.y, vRadians.z );
}


RotationMatrix Quaternion::GetOrientationMatrix()
{
    // Right-handed quaternion → 3×3 rotation matrix (column-major), passed via 9 floats:
    //
    //     | 1-2(y²+z²)   2(xy-wz)    2(xz+wy)  |
    // R = | 2(xy+wz)     1-2(x²+z²)  2(yz-wx)  |
    //     | 2(xz-wy)     2(yz+wx)    1-2(x²+y²)|
    return RotationMatrix( 1 - 2*m_y*m_y - 2*m_z*m_z,
                               2*m_x*m_y + 2*m_w*m_z,
                               2*m_x*m_z - 2*m_w*m_y,

                               2*m_x*m_y - 2*m_w*m_z,
                           1 - 2*m_x*m_x - 2*m_z*m_z,
                               2*m_y*m_z + 2*m_w*m_x,

                               2*m_x*m_z + 2*m_w*m_y,
                               2*m_y*m_z - 2*m_w*m_x,
                           1 - 2*m_x*m_x - 2*m_y*m_y );
}


Quaternion Quaternion::operator*( const Quaternion& q ) const
{
    // Hamilton product (q1 · q2):
    //   w = w1·w2 - v1·v2
    //   v = w1·v2 + w2·v1 + v1 × v2
    Quaternion r;
    r.m_w = m_w * q.m_w - m_x * q.m_x - m_y * q.m_y - m_z * q.m_z;
    r.m_x = m_w * q.m_x + m_x * q.m_w - m_y * q.m_z + m_z * q.m_y;
    r.m_y = m_w * q.m_y + m_x * q.m_z + m_y * q.m_w - m_z * q.m_x;
    r.m_z = m_w * q.m_z - m_x * q.m_y + m_y * q.m_x + m_z * q.m_w;
    return r;
}


Quaternion& Quaternion::operator*=( const Quaternion& q )
{
    *this = *this * q;
    return *this;
}


// Axis-angle → quaternion:  q = ( axis · sin(θ/2),  cos(θ/2) )
Quaternion Quaternion::GetQtnRotatedAboutX( float fRadians )
{
    const float h = fRadians * 0.5f;
    return Quaternion( sinf( h ), 0.0f, 0.0f, cosf( h ) );
}


Quaternion Quaternion::GetQtnRotatedAboutY( float fRadians )
{
    const float h = fRadians * 0.5f;
    return Quaternion( 0.0f, sinf( h ), 0.0f, cosf( h ) );
}


Quaternion Quaternion::GetQtnRotatedAboutZ( float fRadians )
{
    const float h = fRadians * 0.5f;
    return Quaternion( 0.0f, 0.0f, sinf( h ), cosf( h ) );
}
