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
#ifndef SKULLBONEZ_RESPONSE_INFORMATION_H
#define SKULLBONEZ_RESPONSE_INFORMATION_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezGeometricStructures.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Geometry;



namespace SkullbonezCore
{
	namespace Physics
	{
		/* -- ResponseInformation ----------------------------------------------------------------------------------------------------------------------------------------
		
			Contains information related to collision responses
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		struct ResponseInformation
		{
			Geometry::Ray							testingRay;							// Ray the model is currently being tested against
			Geometry::Plane							testingPlane;						// Plane currently being tested for collision against
			Geometry::Ray							collidedRay;						// Ray the model has had a collision with
			Geometry::Plane							collidedPlane;						// Plane in which the model has collided with
			float									collisionTime;						// Time in which collidedRay intersects with collidedPlane
		};
	}
}


#endif