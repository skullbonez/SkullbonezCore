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
#ifndef SKULLBONEZ_WORLD_ENVIRONMENT_H
#define SKULLBONEZ_WORLD_ENVIRONMENT_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezVector3.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;



namespace SkullbonezCore
{
	namespace GameObjects	{ class GameModel; }  // Forward declaration


	namespace Environment
	{
		/* -- World Environment ------------------------------------------------------------------------------------------------------------------------------------------

			Defines the global world properties such as gravity, fluids, density etc.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class WorldEnvironment
		{

		public:			
						WorldEnvironment		(float fFluidSurfaceHeight,
												 float fFluidDensity,
												 float fGasDensity,
												 float fGravity);					// Overloaded constructor
						~WorldEnvironment		(void);								// Default destructor			


			void		RenderFluid				(void);								// Renders the water in the scene
			float		GetFluidSurfaceHeight	(void);								// Returns the fluid surface height
			void		AddWorldForces			(GameObjects::GameModel& target,
												 float changeInTime);				// Adds world forces to the referenced game model



		private:

			float	fluidSurfaceHeight;			// scalar
			float	fluidDensity;				// kg/m^3
			float	gasDensity;					// kg/m^3
			float	gravity;					// m/s^2
			

			float		CalculateGravity		(float objectMass);					// returns Y-component representing Newtons of gravity acting on object
			float		CalculateBuoyancy		(float submergedObjectVolume);		// returns Y-component representing Newtons of buoyancy acting on the object
			Vector3		CalculateViscousDrag	(Vector3 velocityVector,
												 float   submergedVolumePercent,
												 float   dragCoefficient,
												 float   projectedSurfaceArea);		// returns a vector representative of the supplied velocity vector after viscous drag has acted upon it
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/