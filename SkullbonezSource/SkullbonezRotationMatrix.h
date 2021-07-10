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
#ifndef SKULLBONEZ_ROTATION_MATRIX_H
#define SKULLBONEZ_ROTATION_MATRIX_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;



namespace SkullbonezCore
{
	namespace Math
	{
		namespace Transformation
		{
			/* -- Rotation Matrix ----------------------------------------------------------------------------------------------------------------------------------------

				A matrix used to hold rotation data for multiplycation with vectors.
			-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
			class RotationMatrix
			{

			public:

				
									RotationMatrix			(void);						// Default constructor
									RotationMatrix			(float f11, 
															 float f12, 
															 float f13,
															 float f21,
															 float f22,
															 float f23,
															 float f31,
															 float f32,
															 float f33);				// Overloaded constructor
									~RotationMatrix			(void);						// Default destructor
				void				Identity				(void);						// Sets the matrix back to the identity value
				Vector3				operator*				(const Vector3& v) const;	// Rotation matrix multiplied by vector
				Vector3				operator*=				(const Vector3& v) const;	// *= overload



			private:

				float m11, m12, m13, m21, m22, m23, m31, m32, m33;						// Nine float matrix elements

			};
			/* -- END CLASS ROTATION MATRIX -----------------------------------------------------------------------------------------------------------------------------*/



			const RotationMatrix IDENTITY_MATRIX(1.0f, 0.0f, 0.0f,
												 0.0f, 1.0f, 0.0f,
												 0.0f, 0.0f, 1.0f);			// Identity matrix



			// Returns the rotated point (vPoint AFTER rotation) rotated about the arbitrary axis defined by vAxis, by quantity fRadians			
			inline Vector3 RotatePointAboutArbitrary(float    fRadians,
													 const Vector3& vAxis,
													 const Vector3& vPoint)
			{
				// temp vector to store rotated view vector
				Vector3 vResult;

				// break rotation amount into vertical and horizontal components to
				// prepare for applying arbitrary 3d rotation matrix
				float sinTheta = sinf(fRadians);
				float cosTheta = cosf(fRadians);


				/*
					The following matrix for arbitrary axis rotation is explained in about 2.5
					pages in the '3D Math Primer for Graphics and Game Development' by
					Fletcher Dunn and Ian Parberry, pages 109-111.

					The matrix for arbitrary axis rotation is defined as follows:
					
						let fRadians = a
						let vAxis	 = n
						let vPoint.x = x
							vPoint.y = y
							vPoint.z = z

						|    n.x^2*(1-cos(a))+cos(a)		n.x*n.y*(1-cos(a))+n.z*sin(a)		n.x*n.z*(1-cos(a))-n.y*sin(a) | | x |
						| n.x*n.y*(1-cos(a))-n.z*sin(a)		   n.y^2*(1-cos(a))+cos(a)			n.y*n.z*(1-cos*a))+n.x*sin(a) | | y |
						| n.x*n.z*(1-cos(a))+n.y*sin(a)		n.y*n.z*(1-cos(a))-n.x*sin(a)		   n.x^2*(1-cos(a))+cos(a)	  | | z |

						|	 (n.x^2*(1-cos(a))+cos(a))    * x   +	(n.x*n.y*(1-cos(a))+n.z*sin(a)) * y	  +	  (n.x*n.z*(1-cos(a))-n.y*sin(a)) * z |
					  = | (n.x*n.y*(1-cos(a))-n.z*sin(a)) * x   +	   (n.y^2*(1-cos(a))+cos(a))    * y	  +	  (n.y*n.z*(1-cos*a))+n.x*sin(a)) * z |
						| (n.x*n.z*(1-cos(a))+n.y*sin(a)) * x   +	(n.y*n.z*(1-cos(a))-n.x*sin(a)) * y	  +	     (n.x^2*(1-cos(a))+cos(a))    * z |


						// old code...
						vResult.x = (vAxis.x * vAxis.x * (1 - cosTheta) + cosTheta)				* vPoint.x +
									(vAxis.x * vAxis.y * (1 - cosTheta) + vAxis.z * sinTheta)	* vPoint.y +
									(vAxis.x * vAxis.z * (1 - cosTheta) - vAxis.y * sinTheta)	* vPoint.z ;

						vResult.y = (vAxis.x * vAxis.y * (1 - cosTheta) - vAxis.z * sinTheta)	* vPoint.x +
									(vAxis.y * vAxis.y * (1 - cosTheta) + cosTheta)				* vPoint.y +
									(vAxis.y * vAxis.z * (1 - cosTheta) + vAxis.x * sinTheta)	* vPoint.z ;

						vResult.z = (vAxis.x * vAxis.z * (1 - cosTheta) + vAxis.y * sinTheta)	* vPoint.x +
									(vAxis.y * vAxis.z * (1 - cosTheta) - vAxis.x * sinTheta)	* vPoint.y +
									(vAxis.z * vAxis.z * (1 - cosTheta) + cosTheta)				* vPoint.z ;
				*/

				RotationMatrix matrix((vAxis.x * vAxis.x * (1 - cosTheta) + cosTheta),
									  (vAxis.x * vAxis.y * (1 - cosTheta) + vAxis.z * sinTheta),
									  (vAxis.x * vAxis.z * (1 - cosTheta) - vAxis.y * sinTheta),
									  (vAxis.x * vAxis.y * (1 - cosTheta) - vAxis.z * sinTheta),
									  (vAxis.y * vAxis.y * (1 - cosTheta) + cosTheta),
									  (vAxis.y * vAxis.z * (1 - cosTheta) + vAxis.x * sinTheta),
									  (vAxis.x * vAxis.z * (1 - cosTheta) + vAxis.y * sinTheta),
									  (vAxis.y * vAxis.z * (1 - cosTheta) - vAxis.x * sinTheta),
									  (vAxis.z * vAxis.z * (1 - cosTheta) + cosTheta));

				return matrix * vPoint;
			}
		}
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/