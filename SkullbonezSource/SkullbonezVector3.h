/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
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
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/



/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_VECTOR3_H
#define SKULLBONEZ_VECTOR3_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"



namespace SkullbonezCore
{
	namespace Math
	{
		namespace Vector
		{
			/* -- Vector3 ------------------------------------------------------------------------------------------------------------------------------------------------
				
				Represents a 3D vector, no encapsulation required for this class.
			-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
			class Vector3
			{

			public:

				float x, y, z;											// Vector components

							Vector3			(void);						// Default constructor
							Vector3			(const Vector3& v);			// Copy constructor
							Vector3			(float fX, 
											 float fY, 
											 float fZ);					// Overloaded constructor
				void		Zero			(void);						// Set the vector to zero
				void		Normalise		(void);						// Normalise the vector
				void		Absolute		(void);						// Converts vector to its absolute value
				bool		IsCloseToZero	(void)				const;	// Returns true if vector is close to zero
				void		Simplify		(void);						// Converts tiny float components to zero
				void		SetAll			(float nx,
											 float ny,
											 float nz);					// Set all vector components
				Vector3&	operator=		(const Vector3& v);			// Vector assignment
				Vector3&	operator+=		(const Vector3& v);			// += Overload
				Vector3&	operator-=		(const Vector3& v);			// -= Overload
				Vector3&	operator*=		(float f);					// *= Overload
				Vector3&	operator/=		(float f);					// /= Overload
				Vector3&	operator/=		(const Vector3&);			// /= Overload
				Vector3		operator-		(void)			   const;	// Unary minus returns the negative of the vector
				Vector3		operator+		(const Vector3& v) const;	// Binary add vectors
				Vector3		operator-		(const Vector3& v) const;	// Binary subtract vectors
				Vector3		operator*		(float f)		   const;	// Multiplication by scalar
				Vector3		operator/		(float f)		   const;	// Division by scalar
				Vector3		operator/		(const Vector3& v) const;	// Division by vector (individual component division)
				bool		operator==		(const Vector3& v) const;	// Check for equality
				bool		operator!=		(const Vector3& v) const;	// Check for inequality
				float		operator*		(const Vector3& v) const;	// Vector dot product
			};
			/* -- END CLASS VECTOR3 -------------------------------------------------------------------------------------------------------------------------------------*/



			const Vector3 ZERO_VECTOR = Vector3(0.0f, 0.0f, 0.0f);		// Zero vector


			// Reflect incident vector about normal vector (arguments must be normalised)
			inline Vector3 VectorReflect(const Vector3& incident,const Vector3& normal)
			{
				return normal * (2 * (normal * incident)) - incident;	// Pg 153, Lengyel
			}


			// Multiply 2 vectors together, component by component
			inline Vector3 VectorMultiply(const Vector3& v1, const Vector3& v2)
			{
				return Vector3(v1.x * v2.x,
							   v1.y * v2.y,
							   v1.z * v2.z);
			}


			// Compute the magnitude of a vector
			inline float VectorMag(const Vector3& v)
			{
				return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
			}


			// Compute the squared magnitude of a vector
			inline float VectorMagSquared(const Vector3& v)
			{
				return v.x*v.x + v.y*v.y + v.z*v.z;
			}


			// Compute the cross product of two vectors
			inline Vector3 CrossProduct(const Vector3& v1, const Vector3& v2)
			{
				return Vector3(v1.y*v2.z - v1.z*v2.y,
							   v1.z*v2.x - v1.x*v2.z,
							   v1.x*v2.y - v1.y*v2.x);
			}


			// Compute the distance between two points
			inline float Distance(const Vector3& v1, const Vector3& v2)
			{
				float dx = v1.x - v2.x;
				float dy = v1.y - v2.y;
				float dz = v1.z - v2.z;
				return sqrtf(dx*dx + dy*dy + dz*dz);
			}


			// Compute the distance between two points, squared.  Often useful when
			// comparing distances since square root is a slow CPU instruction
			inline float DistanceSquared(const Vector3& v1, const Vector3& v2)
			{
				float dx = v1.x - v2.x;
				float dy = v1.y - v2.y;
				float dz = v1.z - v2.z;
				return dx*dx + dy*dy + dz*dz;
			}


			// Scalar on the left multiplication, for symmetry
			inline Vector3 operator *(float f, const Vector3& v)
			{
				return Vector3(f*v.x, f*v.y, f*v.z);
			}
		}
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/