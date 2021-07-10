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
#ifndef SKULLBONEZ_GEOMETRIC_MATH_H
#define SKULLBONEZ_GEOMETRIC_MATH_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezTerrain.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Geometry;



namespace SkullbonezCore
{
	namespace Math
	{
		/* -- Geometric Math ---------------------------------------------------------------------------------------------------------------------------------------------

			Provides a series of static methods to assist in linear algebra related mathematical tasks useful in games.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class GeometricMath
		{
		private:

			// The possible outcomes for testing a point against a plane
			enum   PointPlaneClassification					{ FrontSideOfPlane, BackSideOfPlane, CoincideWithPlane };

			// Classifies whether a point is on the front side, back side, or coincides with the specified plane
			static GeometricMath::PointPlaneClassification ClassifyPointAgainstPlane(const Plane& plane, const Vector3& point);

			// PRECONDITION:  Vector3 'point' MUST lie on the specified triangles plane. Returns true or false indicating whether the supplied point is inside the
			// boundaries of the triangle
			static bool		IsPointInsideTriangle			(const Triangle& triangle, const Vector3& point);

			// Barycentric coordinates are a weighting based on the 3 verticies composing Triangle 'triangle' indicating a point on the plane occupied by 'triangle'
			// for detailed discussion see page 260 of the '3D Math Primer for Games and Graphics Development' by Dunn and Parberry
			static Vector3	ComputeBarycentricCoordinates	(const Triangle& triangle, const Vector3&  point);

			// Returns the normal of the specified triangle based on counter-clockwise rotation
			static Vector3	ComputeTriangleNormal			(const Triangle& triangle);

			// Returns the distance the specified point is from the specified plane
			static float	DeterminePointDistFromPlane		(const Plane& plane, const Vector3& point);


		public:

			// Compute the plane in which 3 non-colinear points sit on
			static Plane	ComputePlane					(const Triangle& triangle);

			// Computes the point of intersection between the ray defined by vEnd-vBegin and the specified plane (overloaded)
			static Vector3	ComputeIntersectionPoint		(const Plane& plane, const Ray&	ray);
			static Vector3	ComputeIntersectionPoint		(const Ray&	ray, float fCollisionTime);

			// Computes the magnitute the ray can travel along before intersecting with the specified plane (overloaded)
			static float	CalculateIntersectionTime		(const Plane& plane,	   const Ray& ray);
			static float	CalculateIntersectionTime		(const Triangle& triangle, const Ray& ray);

			// returns the height of the plane represented by the specified triangle at the specified XZ coords detailed math reference 
			// @ http://www.simoneschbach.com/images/GetHeightOfPlaneAtXZ.gif
			static float	GetHeightFromPlane				(const Triangle& triangle, float xCoord, float zCoord);
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/