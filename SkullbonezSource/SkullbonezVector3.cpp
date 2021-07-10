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
#include "SkullbonezVector3.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;



/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
Vector3::Vector3(void) {}



/* -- COPY CONSTRUCTOR ------------------------------------------------------------*/
Vector3::Vector3(const Vector3& v) : x(v.x), y(v.y), z(v.z) {}



/* -- OVERLOADED CONSTRUCTOR ------------------------------------------------------*/
Vector3::Vector3(float fX, 
				 float fY, 
				 float fZ) : x(fX), y(fY), z(fZ) {}



/* -- ZERO ------------------------------------------------------------------------*/
void Vector3::Zero(void) 
{ 
	this->x = this->y = this->z = 0.0f;
}



/* -- NORMALISE -------------------------------------------------------------------*/
void Vector3::Normalise(void)
{
	float magSq = this->x*this->x + 
				  this->y*this->y + 
				  this->z*this->z;

	if(!magSq) throw "Division by zero.  (Vector3::Normalise)";
	float oneOverMag = 1.0f / sqrtf(magSq);

	this->x *= oneOverMag;
	this->y *= oneOverMag;
	this->z *= oneOverMag;
}



/* -- ABSOLUTE --------------------------------------------------------------------*/
bool Vector3::IsCloseToZero(void) const
{
	return this->x < TOLERANCE && this->x > ZERO_TAKE_TOLERANCE &&
		   this->y < TOLERANCE && this->y > ZERO_TAKE_TOLERANCE &&
		   this->z < TOLERANCE && this->z > ZERO_TAKE_TOLERANCE;
}



/* -- ABSOLUTE --------------------------------------------------------------------*/
void Vector3::Absolute(void)
{
	this->x = fabs(this->x);
	this->y = fabs(this->y);
	this->z = fabs(this->z);
}



/* -- SET ALL ---------------------------------------------------------------------*/
void Vector3::SetAll(float nx,
					 float ny,
					 float nz)
{
	this->x = nx;
	this->y = ny;
	this->z = nz;
}



/* -- OPERATOR = ------------------------------------------------------------------*/
Vector3& Vector3::operator =(const Vector3& v) 
{
	this->x = v.x; 
	this->y = v.y; 
	this->z = v.z;
	return *this;
}



/* -- OPERATOR == -----------------------------------------------------------------*/
bool Vector3::operator ==(const Vector3& v) const 
{
	return (this->x == v.x && 
			this->y == v.y && 
			this->z == v.z);
}



/* -- OPERATOR != -----------------------------------------------------------------*/
bool Vector3::operator !=(const Vector3& v) const 
{
	return (this->x != v.x || 
			this->y != v.y || 
			this->z != v.z);
}



/* -- OPERATOR - ------------------------------------------------------------------*/
Vector3 Vector3::operator -(void) const
{ 
	return Vector3(-this->x,
				   -this->y,
				   -this->z); 
}



/* -- OPERATOR + ------------------------------------------------------------------*/
Vector3 Vector3::operator +(const Vector3& v) const
{
	return Vector3(this->x + v.x, 
				   this->y + v.y, 
				   this->z + v.z);
}



/* -- OPERATOR - ------------------------------------------------------------------*/
Vector3 Vector3::operator -(const Vector3& v) const
{
	return Vector3(this->x - v.x, 
				   this->y - v.y, 
				   this->z - v.z);
}



/* -- OPERATOR * ------------------------------------------------------------------*/
Vector3 Vector3::operator *(float f) const
{
	return Vector3(this->x * f, 
				   this->y * f, 
				   this->z * f);
}



/* -- OPERATOR / ------------------------------------------------------------------*/
Vector3 Vector3::operator /(float f) const
{
	if(!f) throw "Division by zero.  (Vector3::Operator/)";
	float oneOverA = 1.0f / f;
	return Vector3(this->x * oneOverA, 
				   this->y * oneOverA, 
				   this->z * oneOverA);
}



/* -- OPERATOR / ------------------------------------------------------------------*/
Vector3 Vector3::operator /(const Vector3& v) const
{
	if(!v.x || !v.y || !v.z) throw "Division by zero.  (Vector3::Operator/)";

	return Vector3(this->x / v.x,
				   this->y / v.y,
				   this->z / v.z);
}



/* -- SIMPLIFY --------------------------------------------------------------------*/
void Vector3::Simplify(void)
{
	if(this->x < TOLERANCE && this->x > ZERO_TAKE_TOLERANCE) this->x = 0.0f;
	if(this->y < TOLERANCE && this->y > ZERO_TAKE_TOLERANCE) this->y = 0.0f;
	if(this->z < TOLERANCE && this->z > ZERO_TAKE_TOLERANCE) this->z = 0.0f;
}



/* -- OPERATOR += -----------------------------------------------------------------*/
Vector3& Vector3::operator +=(const Vector3& v)
{
	this->x += v.x; 
	this->y += v.y; 
	this->z += v.z;
	return *this;
}



/* -- OPERATOR -= -----------------------------------------------------------------*/
Vector3& Vector3::operator -=(const Vector3& v)
{
	this->x -= v.x; 
	this->y -= v.y; 
	this->z -= v.z;
	return *this;
}



/* -- OPERATOR *= -----------------------------------------------------------------*/
Vector3& Vector3::operator *=(float f)
{
	this->x *= f; 
	this->y *= f; 
	this->z *= f;
	return *this;
}



/* -- OPERATOR /= -----------------------------------------------------------------*/
Vector3& Vector3::operator /=(float f)
{
	if(!f) throw "Division by zero.  (Vector3::Operator/=)";
	float oneOverA = 1.0f / f;
	this->x *= oneOverA; 
	this->y *= oneOverA; 
	this->z *= oneOverA;
	return *this;
}



/* -- OPERATOR /= -----------------------------------------------------------------*/
Vector3& Vector3::operator /=(const Vector3& v)
{
	if(!v.x || !v.y || !v.z) throw "Division by zero.  (Vector3::Operator/=)";
	this->x /= v.x; 
	this->y /= v.y; 
	this->z /= v.z;
	return *this;
}



/* -- OPERATOR *= -----------------------------------------------------------------*/
float Vector3::operator *(const Vector3& v) const
{
	return this->x * v.x + 
		   this->y * v.y + 
		   this->z * v.z;
}