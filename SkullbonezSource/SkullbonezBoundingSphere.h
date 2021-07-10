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
#ifndef SKULLBONEZ_BOUNDING_SPHERE_H
#define SKULLBONEZ_BOUNDING_SPHERE_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezDynamicsObject.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;



namespace SkullbonezCore
{
	namespace Math
	{
		namespace CollisionDetection
		{
			/* -- BoundingSphere -----------------------------------------------------------------------------------------------------------------------------------------
			
				Represents a sphere for collision tests.
			-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
			class BoundingSphere : public DynamicsObject
			{

			private:

				float	radius;													// Radius of sphere

				float	CollisionDetect(const BoundingSphere& target,
										const Ray&			targetRay,
										const Ray&			focusRay);			// Collision detect against bounding sphere, see pages 288-290 of 3D Math Primer for Games and Graphics Development by Dunn + Parberry


			public:
				
								BoundingSphere					(void);									// Default constructor
								BoundingSphere					(float			fRadius,
																 const Vector3& vPosition);				// Overloaded constructor
								~BoundingSphere					(void);									// Default destructor
				void			DEBUG_RenderCollisionVolume		(const Vector3& worldSpaceCoords);		// Debug routine to render a representation of the collision volume
				float			GetVolume						(void);									// Returns the volume of the sphere
				float			GetSubmergedVolumePercent		(float fluidSurfaceHeight);				// Calculates the total volume of the sphere below the fluid surface height
				float			GetDragCoefficient				(void);									// Returns the drag coefficient of a sphere
				float			GetProjectedSurfaceArea			(void);									// Returns the surface area of a 2d-projected sphere
				float			GetRadius						(void);									// Returns the radius of the sphere
				float			TestCollision					(const DynamicsObject& target,
																 const Ray&			   targetRay,
																 const Ray&			   focusRay);		// Sweep test of bounding sphere with another collision object (target) travelling along vector totalMovement
			};
		}
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/