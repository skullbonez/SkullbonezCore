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
#include "SkullbonezRotationMatrix.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
RotationMatrix::RotationMatrix(void) {}



/* -- OVERLOADED CONSTRUCTOR ------------------------------------------------------*/
RotationMatrix::RotationMatrix(float f11, 
							   float f12, 
							   float f13,
							   float f21,
							   float f22,
							   float f23,
							   float f31,
							   float f32,
							   float f33) : m11(f11),
												  m12(f12),
												  m13(f13),
												  m21(f21),
												  m22(f22),
												  m23(f23),
												  m31(f31),
												  m32(f32),
												  m33(f33) {}

/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
RotationMatrix::~RotationMatrix(void) {}



/* -- IDENTITY --------------------------------------------------------------------*/
void RotationMatrix::Identity(void)
{
	this->m11 = 1.0f;
	this->m12 = 0.0f;
	this->m13 = 0.0f;

	this->m21 = 0.0f;
	this->m22 = 1.0f;
	this->m23 = 0.0f;

	this->m31 = 0.0f;
	this->m32 = 0.0f;
	this->m33 = 1.0f;
}



/* -- OPERATOR * ------------------------------------------------------------------*/
Vector3 RotationMatrix::operator*(const Vector3& v) const
{
	return Vector3(this->m11*v.x + this->m12*v.y + this->m13*v.z,
				   this->m21*v.x + this->m22*v.y + this->m23*v.z,
				   this->m31*v.x + this->m32*v.y + this->m33*v.z);
}



/* -- OPERATOR *= -----------------------------------------------------------------*/
Vector3 RotationMatrix::operator*=(const Vector3& v) const
{
	return *this * v;
}