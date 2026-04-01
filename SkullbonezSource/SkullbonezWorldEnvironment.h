/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
																		  THE SKULLBONEZ CORE
																				_______
																			 .-"       "-.
																			/             \
																		   /               \
																		   �   .--. .--.   �
																		   � )/   � �   \( �
																		   �/ \__/   \__/ \�
																		   /      /^\      \
																		   \__    '='    __/
								   											 �\         /�
																			 �\'"VUUUV"'/�
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
#include "SkullbonezMatrix4.h"
#include "SkullbonezMesh.h"
#include "SkullbonezShader.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;



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
						WorldEnvironment		(void);								// Default constructor
						WorldEnvironment		(float fFluidSurfaceHeight,
												 float fFluidDensity,
												 float fGasDensity,
												 float fGravity);					// Overloaded constructor
						~WorldEnvironment		(void);								// Default destructor			
						WorldEnvironment		(WorldEnvironment&&) noexcept = default;		// Move constructor
						WorldEnvironment& operator=(WorldEnvironment&&) noexcept = default;	// Move assignment


			void		RenderFluid				(const Matrix4& proj);				// Renders the water in the scene
			void		ResetGLResources		(void);								// Rebuilds GPU resources after GL context recreation
			float		GetFluidSurfaceHeight	(void);								// Returns the fluid surface height
			void		AddWorldForces			(GameObjects::GameModel& target,
												 float changeInTime);				// Adds world forces to the referenced game model



		private:

			float	fluidSurfaceHeight;			// scalar
			float	fluidDensity;				// kg/m^3
			float	gasDensity;					// kg/m^3
			float	gravity;					// m/s^2
			std::unique_ptr<Mesh>	fluidMesh;	// Water quad mesh
			std::unique_ptr<Shader>	fluidShader;// Water shader

			void		BuildFluidMesh			(void);								// Builds the water quad mesh
			

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