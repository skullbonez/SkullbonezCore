/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   ?   .--. .--.   ?
								   ? )/   ? ?   \( ?
								   ?/ \__/   \__/ \?
								   /      /^\      \
								   \__    '='    __/
								   	 ?\         /?
									 ?\'"VUUUV"'/?
									 \ `"""""""` /
									  `-._____.-'

								 www.simoneschbach.com
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezMatrix4.h"
#include "SkullbonezQuaternion.h"
#include <cmath>



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Vector;



/* -- DEFAULT CONSTRUCTOR (IDENTITY) ----------------------------------------------*/
Matrix4::Matrix4(void)
{
	m[0]  = 1.0f; m[4]  = 0.0f; m[8]  = 0.0f; m[12] = 0.0f;
	m[1]  = 0.0f; m[5]  = 1.0f; m[9]  = 0.0f; m[13] = 0.0f;
	m[2]  = 0.0f; m[6]  = 0.0f; m[10] = 1.0f; m[14] = 0.0f;
	m[3]  = 0.0f; m[7]  = 0.0f; m[11] = 0.0f; m[15] = 1.0f;
}



/* -- CONSTRUCT FROM ARRAY --------------------------------------------------------*/
Matrix4::Matrix4(const float* values)
{
	for (int i = 0; i < 16; ++i) m[i] = values[i];
}



/* -- PERSPECTIVE PROJECTION ------------------------------------------------------*/
Matrix4 Matrix4::Perspective(float fovDegrees, float aspect, float nearPlane, float farPlane)
{
	Matrix4 result;
	float fovRad  = fovDegrees * (_PI / 180.0f);
	float tanHalf = tanf(fovRad * 0.5f);

	for (int i = 0; i < 16; ++i) result.m[i] = 0.0f;

	result.m[0]  = 1.0f / (aspect * tanHalf);
	result.m[5]  = 1.0f / tanHalf;
	result.m[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
	result.m[11] = -1.0f;
	result.m[14] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);

	return result;
}



/* -- ORTHOGRAPHIC PROJECTION -----------------------------------------------------*/
Matrix4 Matrix4::Ortho(float left, float right, float bottom, float top,
					   float nearPlane, float farPlane)
{
	Matrix4 result;
	for (int i = 0; i < 16; ++i) result.m[i] = 0.0f;

	result.m[0]  =  2.0f / (right - left);
	result.m[5]  =  2.0f / (top - bottom);
	result.m[10] = -2.0f / (farPlane - nearPlane);
	result.m[12] = -(right + left) / (right - left);
	result.m[13] = -(top + bottom) / (top - bottom);
	result.m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
	result.m[15] =  1.0f;

	return result;
}



/* -- LOOK AT (VIEW MATRIX) -------------------------------------------------------*/
Matrix4 Matrix4::LookAt(const Vector3& eye, const Vector3& center, const Vector3& up)
{
	Vector3 f = center - eye;
	f.Normalise();

	Vector3 u = up;
	u.Normalise();

	Vector3 s = CrossProduct(f, u);
	s.Normalise();

	u = CrossProduct(s, f);

	Matrix4 result;
	result.m[0]  =  s.x;
	result.m[4]  =  s.y;
	result.m[8]  =  s.z;
	result.m[1]  =  u.x;
	result.m[5]  =  u.y;
	result.m[9]  =  u.z;
	result.m[2]  = -f.x;
	result.m[6]  = -f.y;
	result.m[10] = -f.z;
	result.m[12] = -(s * eye);
	result.m[13] = -(u * eye);
	result.m[14] =  (f * eye);

	return result;
}



/* -- TRANSLATION MATRIX ----------------------------------------------------------*/
Matrix4 Matrix4::Translate(const Vector3& v)
{
	return Translate(v.x, v.y, v.z);
}



/* -- TRANSLATION MATRIX (COMPONENT FORM) ----------------------------------------*/
Matrix4 Matrix4::Translate(float x, float y, float z)
{
	Matrix4 result;
	result.m[12] = x;
	result.m[13] = y;
	result.m[14] = z;
	return result;
}



/* -- SCALE MATRIX ----------------------------------------------------------------*/
Matrix4 Matrix4::Scale(const Vector3& v)
{
	return Scale(v.x, v.y, v.z);
}



/* -- SCALE MATRIX (COMPONENT FORM) ----------------------------------------------*/
Matrix4 Matrix4::Scale(float x, float y, float z)
{
	Matrix4 result;
	result.m[0]  = x;
	result.m[5]  = y;
	result.m[10] = z;
	return result;
}



/* -- UNIFORM SCALE MATRIX --------------------------------------------------------*/
Matrix4 Matrix4::Scale(float uniform)
{
	return Scale(uniform, uniform, uniform);
}



/* -- AXIS-ANGLE ROTATION ---------------------------------------------------------*/
Matrix4 Matrix4::RotateAxis(float angleDeg, float axisX, float axisY, float axisZ)
{
	float rad = angleDeg * (3.14159265f / 180.0f);
	float c   = cosf(rad);
	float s   = sinf(rad);
	float t   = 1.0f - c;

	// Normalise axis
	float mag = sqrtf(axisX * axisX + axisY * axisY + axisZ * axisZ);
	if (mag > 0.0f) { axisX /= mag; axisY /= mag; axisZ /= mag; }

	Matrix4 result;
	result.m[0]  = t * axisX * axisX + c;
	result.m[1]  = t * axisX * axisY + s * axisZ;
	result.m[2]  = t * axisX * axisZ - s * axisY;
	result.m[3]  = 0.0f;

	result.m[4]  = t * axisX * axisY - s * axisZ;
	result.m[5]  = t * axisY * axisY + c;
	result.m[6]  = t * axisY * axisZ + s * axisX;
	result.m[7]  = 0.0f;

	result.m[8]  = t * axisX * axisZ + s * axisY;
	result.m[9]  = t * axisY * axisZ - s * axisX;
	result.m[10] = t * axisZ * axisZ + c;
	result.m[11] = 0.0f;

	result.m[12] = 0.0f;
	result.m[13] = 0.0f;
	result.m[14] = 0.0f;
	result.m[15] = 1.0f;
	return result;
}



/* -- ROTATION MATRIX FROM QUATERNION ---------------------------------------------*/
Matrix4 Matrix4::FromQuaternion(const Quaternion& q)
{
	// Extract the 3x3 rotation via the quaternion's existing method.
	// GetOrientationMatrix returns a right-handed RotationMatrix.
	RotationMatrix r = const_cast<Quaternion&>(q).GetOrientationMatrix();

	// Transform basis vectors to extract columns
	Vector3 col0 = r * Vector3(1.0f, 0.0f, 0.0f);
	Vector3 col1 = r * Vector3(0.0f, 1.0f, 0.0f);
	Vector3 col2 = r * Vector3(0.0f, 0.0f, 1.0f);

	Matrix4 result;
	result.m[0]  = col0.x; result.m[4]  = col1.x; result.m[8]  = col2.x; result.m[12] = 0.0f;
	result.m[1]  = col0.y; result.m[5]  = col1.y; result.m[9]  = col2.y; result.m[13] = 0.0f;
	result.m[2]  = col0.z; result.m[6]  = col1.z; result.m[10] = col2.z; result.m[14] = 0.0f;
	result.m[3]  = 0.0f;   result.m[7]  = 0.0f;   result.m[11] = 0.0f;   result.m[15] = 1.0f;

	return result;
}



/* -- MATRIX MULTIPLICATION -------------------------------------------------------*/
Matrix4 Matrix4::operator*(const Matrix4& rhs) const
{
	Matrix4 result;
	for (int col = 0; col < 4; ++col)
	{
		for (int row = 0; row < 4; ++row)
		{
			result.m[col * 4 + row] =
				m[0 * 4 + row] * rhs.m[col * 4 + 0] +
				m[1 * 4 + row] * rhs.m[col * 4 + 1] +
				m[2 * 4 + row] * rhs.m[col * 4 + 2] +
				m[3 * 4 + row] * rhs.m[col * 4 + 3];
		}
	}
	return result;
}



/* -- IN-PLACE MATRIX MULTIPLICATION ----------------------------------------------*/
Matrix4& Matrix4::operator*=(const Matrix4& rhs)
{
	*this = *this * rhs;
	return *this;
}



/* -- DATA POINTER ----------------------------------------------------------------*/
const float* Matrix4::Data(void) const
{
	return m;
}
