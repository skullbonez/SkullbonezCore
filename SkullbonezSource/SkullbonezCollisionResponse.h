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
#ifndef SKULLBONEZ_COLLISION_RESPONSE_H
#define SKULLBONEZ_COLLISION_RESPONSE_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezGeometricStructures.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Geometry;



namespace SkullbonezCore
{
	namespace Physics
	{
		/* -- CollisionResponse ------------------------------------------------------------------------------------------------------------------------------------------
		
			Contains a series of static methods to assist in response to collisions
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class CollisionResponse
		{

		private:

			static void			SphereVsSphereLinear			(GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal);		// Sphere and sphere response, linear component
			static void			SphereVsSphereAngular		    (GameModel& gameModel1, GameModel& gameModel2, const Vector3& collisionNormal);		// Sphere and sphere response, angular component
			static Vector3		GetCollisionNormalSphereVsSphere(GameModel& gameModel1, GameModel& gameModel2);										// Calculate the collision normal for a sphere and a sphere
			static void			SphereVsPlaneRollResponse		(GameModel& gameModel);																// Sphere and plane rolling response
			static void			SphereVsPlaneLinearImpulse		(GameModel& gameModel, Vector3 totalVelocity, float projectedVelocity);				// Sphere and plane response, linear component
			static void			SphereVsPlaneAngularImpulse		(GameModel& gameModel);																// Sphere and plane response, angular component
			static Vector3		GetCollidedObjectWorldPosition	(GameModel& gameModel);																// Calculates the world position of the current collided dynamics object

		public:

			static void			RespondCollisionTerrain		(GameModel& gameModel);									// Performs a response based on the game model and the terrain
			static void			RespondCollisionGameModels	(GameModel& gameModel1, GameModel& gameModel2);			// Performs a response on the game models based on their current state
			static Ray			CalculateRay				(GameModel& gameModel, float changeInTime);				// Returns a ray representing the path travelled by the target in the supplied time frame
		};
	}
}


#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/