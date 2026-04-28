/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezQuaternion.h"

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Orientation;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
Quaternion::Quaternion()
{
}

/* -- OVERLOADED CONSTRUCTOR ------------------------------------------------------*/
Quaternion::Quaternion( float fX,
                        float fY,
                        float fZ,
                        float fW )
    : x( fX ),
      y( fY ),
      z( fZ ),
      w( fW )
{
}

/* -- IDENTITY -------------------------------------------------------------------*/
void Quaternion::Identity()
{
    this->x = 0.0f;
    this->y = 0.0f;
    this->z = 0.0f;
    this->w = 1.0f;
}

/* -- NORMALISE ------------------------------------------------------------------*/
void Quaternion::Normalise()
{
    float magSq = this->w * this->w +
                  this->x * this->x +
                  this->y * this->y +
                  this->z * this->z;

    if ( !magSq )
    {
        throw std::runtime_error( "Division by zero.  (Quaternion::Normalise)" );
    }

    float oneOverMag = 1.0f / sqrtf( magSq );

    this->w *= oneOverMag;
    this->x *= oneOverMag;
    this->y *= oneOverMag;
    this->z *= oneOverMag;
}

/* -- ROTATE ABOUT XYZ -----------------------------------------------------------*/
void Quaternion::RotateAboutXYZ( float xRadians,
                                 float yRadians,
                                 float zRadians )
{
    // rotate about x, y, and z respectively
    Quaternion xRotation = this->GetQtnRotatedAboutX( xRadians );
    Quaternion yRotation = this->GetQtnRotatedAboutY( yRadians );
    Quaternion zRotation = this->GetQtnRotatedAboutZ( zRadians );

    // accumulate the rotations
    *this *= xRotation * yRotation * zRotation;

    // normalise the quaternion
    this->Normalise();
}

/* -- ROTATE ABOUT XYZ -----------------------------------------------------------*/
void Quaternion::RotateAboutXYZ( const Vector3& vRadians )
{
    this->RotateAboutXYZ( vRadians.x, vRadians.y, vRadians.z );
}

/* -- GET ORIENTATION MATRIX -----------------------------------------------------*/
RotationMatrix Quaternion::GetOrientationMatrix()
{
    // Return the RIGHT HANDED rotation matrix
    return RotationMatrix( 1 - ( 2 * this->y * this->y ) - ( 2 * this->z * this->z ),
                           ( 2 * this->x * this->y ) + ( 2 * this->w * this->z ),
                           ( 2 * this->x * this->z ) - ( 2 * this->w * this->y ),
                           ( 2 * this->x * this->y ) - ( 2 * this->w * this->z ),
                           1 - ( 2 * this->x * this->x ) - ( 2 * this->z * this->z ),
                           ( 2 * this->y * this->z ) + ( 2 * this->w * this->x ),
                           ( 2 * this->x * this->z ) + ( 2 * this->w * this->y ),
                           ( 2 * this->y * this->z ) - ( 2 * this->w * this->x ),
                           1 - ( 2 * this->x * this->x ) - ( 2 * this->y * this->y ) );
}

/* -- OPERATOR * -----------------------------------------------------------------*/
Quaternion Quaternion::operator*( const Quaternion& q ) const
{
    Quaternion result;

    result.w = this->w * q.w -
               this->x * q.x -
               this->y * q.y -
               this->z * q.z;

    result.x = this->w * q.x +
               this->x * q.w -
               this->y * q.z +
               this->z * q.y;

    result.y = this->w * q.y +
               this->x * q.z +
               this->y * q.w -
               this->z * q.x;

    result.z = this->w * q.z -
               this->x * q.y +
               this->y * q.x +
               this->z * q.w;

    return result;
}

/* -- OPERATOR *= ----------------------------------------------------------------*/
Quaternion& Quaternion::operator*=( const Quaternion& q )
{
    *this = *this * q;
    return *this;
}

/* -- GET QTN ROTATED ABOUT X ----------------------------------------------------*/
Quaternion Quaternion::GetQtnRotatedAboutX( float fRadians )
{
    float radiansDiv2 = fRadians * 0.5f;

    return Quaternion( sinf( radiansDiv2 ),
                       0.0f,
                       0.0f,
                       cosf( radiansDiv2 ) );
}

/* -- GET QTN ROTATED ABOUT Y ----------------------------------------------------*/
Quaternion Quaternion::GetQtnRotatedAboutY( float fRadians )
{
    float radiansDiv2 = fRadians * 0.5f;

    return Quaternion( 0.0f,
                       sinf( radiansDiv2 ),
                       0.0f,
                       cosf( radiansDiv2 ) );
}

/* -- GET QTN ROTATED ABOUT Z ----------------------------------------------------*/
Quaternion Quaternion::GetQtnRotatedAboutZ( float fRadians )
{
    float radiansDiv2 = fRadians * 0.5f;

    return Quaternion( 0.0f,
                       0.0f,
                       sinf( radiansDiv2 ),
                       cosf( radiansDiv2 ) );
}
