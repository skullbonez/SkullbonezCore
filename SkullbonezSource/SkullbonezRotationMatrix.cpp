/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezRotationMatrix.h"

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
RotationMatrix::RotationMatrix()
{
}

/* -- OVERLOADED CONSTRUCTOR ------------------------------------------------------*/
RotationMatrix::RotationMatrix( float f11,
                                float f12,
                                float f13,
                                float f21,
                                float f22,
                                float f23,
                                float f31,
                                float f32,
                                float f33 )
    : m11( f11 ),
      m12( f12 ),
      m13( f13 ),
      m21( f21 ),
      m22( f22 ),
      m23( f23 ),
      m31( f31 ),
      m32( f32 ),
      m33( f33 )
{
}

/* -- IDENTITY --------------------------------------------------------------------*/
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

/* -- OPERATOR * ------------------------------------------------------------------*/
Vector3 RotationMatrix::operator*( const Vector3& v ) const
{
    return Vector3( m11 * v.x + m12 * v.y + m13 * v.z,
                    m21 * v.x + m22 * v.y + m23 * v.z,
                    m31 * v.x + m32 * v.y + m33 * v.z );
}

/* -- OPERATOR *= -----------------------------------------------------------------*/
Vector3 RotationMatrix::operator*=( const Vector3& v ) const
{
    return *this * v;
}
