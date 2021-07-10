/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   ¦   .--. .--.   ¦
								   ¦ )/   ¦ ¦   \( ¦
								   ¦/ \__/   \__/ \¦
								   /      /^\      \
								   \__    '='    __/
								   	 ¦\         /¦
									 ¦\'"VUUUV"'/¦
									 \ `"""""""` /
									  `-._____.-'

								 www.simoneschbach.com
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezQuaternion.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Orientation;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
Quaternion::Quaternion(void) {}



/* -- OVERLOADED CONSTRUCTOR ------------------------------------------------------*/
Quaternion::Quaternion(float fX, 
					   float fY,
					   float fZ,
					   float fW) : x(fX), 
										 y(fY), 
										 z(fZ), 
										 w(fW) {}



/* -- DEFAULT DESTRUCTOR ---------------------------------------------------------*/
Quaternion::~Quaternion			(void) {}



/* -- IDENTITY -------------------------------------------------------------------*/
void Quaternion::Identity(void)
{
	this->x = 0.0f;
	this->y = 0.0f;
	this->z = 0.0f;
	this->w = 1.0f;
}



/* -- NORMALISE ------------------------------------------------------------------*/
void Quaternion::Normalise(void)
{
	float magSq = this->w*this->w +
				  this->x*this->x +
				  this->y*this->y +
				  this->z*this->z;

	if(!magSq) throw "Division by zero.  (Quaternion::Normalise)";

	float oneOverMag = 1.0f / sqrtf(magSq);

	this->w *= oneOverMag;
	this->x *= oneOverMag;
	this->y *= oneOverMag;
	this->z *= oneOverMag;
}



/* -- ROTATE ABOUT XYZ -----------------------------------------------------------*/
void Quaternion::RotateAboutXYZ(float xRadians,
								float yRadians,
								float zRadians)
{
	// rotate about x, y, and z respectively
	Quaternion xRotation = this->GetQtnRotatedAboutX(xRadians);
	Quaternion yRotation = this->GetQtnRotatedAboutY(yRadians);
	Quaternion zRotation = this->GetQtnRotatedAboutZ(zRadians);

	// accumulate the rotations
	*this *= xRotation * yRotation * zRotation;

	// normalise the quaternion
	this->Normalise();
}



/* -- ROTATE ABOUT XYZ -----------------------------------------------------------*/
void Quaternion::RotateAboutXYZ(const Vector3& vRadians)
{
	this->RotateAboutXYZ(vRadians.x, vRadians.y, vRadians.z);
}



/* -- GL ROTATE TO ORIENTATION ---------------------------------------------------*/
void Quaternion::GlRotateToOrientation(void)
{
	/*
		Formula for converting a quaternion to a matrix:
		Qrtn elements: w, x, y, z

		| 1-2y^2-2z^2     2xy-2wz       2xz+2wy     0 |
		|   2xy+2wz     1-2x^2-2z^2     2yz-2wx     0 |
		|   2xz-2wy       2yz+2wx     1-2x^2-2y^2   0 |
		|      0             0             0        1 |

		NOTE:  THIS IS FLIPPING THE ROTATION TO A LEFT
			   HANDED COORDINATE SYSTEM FOR OPENGL!
	*/

	float matrix[16] = 
	{
		1 - (2*this->y*this->y) - (2*this->z*this->z),
			(2*this->x*this->y) - (2*this->w*this->z),
			(2*this->x*this->z) + (2*this->w*this->y),
		0											 ,
			(2*this->x*this->y) + (2*this->w*this->z),
		1 - (2*this->x*this->x) - (2*this->z*this->z),
			(2*this->y*this->z) - (2*this->w*this->x),
		0											 ,
			(2*this->x*this->z) - (2*this->w*this->y),
			(2*this->y*this->z) + (2*this->w*this->x),
		1 - (2*this->x*this->x) - (2*this->y*this->y),
		0											 ,
		0											 ,
		0											 ,
		0											 ,
		1
	};

	// Apply the rotation matrix to the world matrix
	glMultMatrixf(matrix);
}



/* -- GET ORIENTATION MATRIX -----------------------------------------------------*/
RotationMatrix Quaternion::GetOrientationMatrix(void)
{
	// Return the RIGHT HANDED rotation matrix
	return RotationMatrix(1 - (2*this->y*this->y) - (2*this->z*this->z),
						      (2*this->x*this->y) + (2*this->w*this->z),
							  (2*this->x*this->z) - (2*this->w*this->y),
							  (2*this->x*this->y) - (2*this->w*this->z),
						  1 - (2*this->x*this->x) - (2*this->z*this->z),
							  (2*this->y*this->z) + (2*this->w*this->x),
							  (2*this->x*this->z) + (2*this->w*this->y),
							  (2*this->y*this->z) - (2*this->w*this->x),
						  1 - (2*this->x*this->x) - (2*this->y*this->y));
}



/* -- OPERATOR * -----------------------------------------------------------------*/
Quaternion Quaternion::operator *(const Quaternion& q) const
{
	Quaternion result;

	result.w = this->w*q.w - 
			   this->x*q.x -
			   this->y*q.y -
			   this->z*q.z;

	result.x = this->w*q.x +
			   this->x*q.w -
			   this->y*q.z +
			   this->z*q.y;

	result.y = this->w*q.y +
			   this->x*q.z +
			   this->y*q.w -
			   this->z*q.x;

	result.z = this->w*q.z -
			   this->x*q.y +
			   this->y*q.x +
			   this->z*q.w;

	return result;
}



/* -- OPERATOR *= ----------------------------------------------------------------*/
Quaternion& Quaternion::operator *=(const Quaternion& q)
{
	*this = *this * q;
	return *this;
}



/* -- GET QTN ROTATED ABOUT X ----------------------------------------------------*/
Quaternion Quaternion::GetQtnRotatedAboutX(float fRadians)
{
	float radiansDiv2 = fRadians * 0.5f;

	return Quaternion(sinf(radiansDiv2),
					  0.0f,
					  0.0f,
					  cosf(radiansDiv2));
}



/* -- GET QTN ROTATED ABOUT Y ----------------------------------------------------*/
Quaternion Quaternion::GetQtnRotatedAboutY(float fRadians)
{
	float radiansDiv2 = fRadians * 0.5f;

	return Quaternion(0.0f,
					  sinf(radiansDiv2),
					  0.0f,
					  cosf(radiansDiv2));
}



/* -- GET QTN ROTATED ABOUT Z ----------------------------------------------------*/
Quaternion Quaternion::GetQtnRotatedAboutZ(float fRadians)
{
	float radiansDiv2 = fRadians * 0.5f;

	return Quaternion(0.0f,
					  0.0f,
					  sinf(radiansDiv2),
					  cosf(radiansDiv2));
}