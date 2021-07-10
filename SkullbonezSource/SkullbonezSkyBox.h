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
#ifndef SKULLBONEZ_SKY_BOX_H
#define SKULLBONEZ_SKY_BOX_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezTextureCollection.h"
#include "SkullbonezVector3.h"
#include "SkullbonezGeometricStructures.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Textures;



namespace SkullbonezCore
{
	namespace Geometry
	{
		/* -- Sky Box ----------------------------------------------------------------------------------------------------------------------------------------------------

			A singleton class to represent a skybox.  Textures must be square and contain 3 pixels of padding around the edges.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class SkyBox
		{

		private:

			static SkyBox*			pInstance;					// Singleton instance pointer
			Box						boundaries;					// Boundaries of sky box
			TextureCollection*		textures;					// Textures of the sky box

			SkyBox					(int xMin, 
									 int xMax,
									 int yMin,
									 int yMax,
									 int zMin,
									 int zMax);					// Overloaded constructor
			~SkyBox					(void);						// Destructor


		public:

			static SkyBox*	Instance		   (int xMin, 
												int xMax,
												int yMin,
												int yMax,
												int zMin,
												int zMax);		// Request for singleton instance
			static void		Destroy				(void);			// Destroy singleton instance
			void			Render				(void);			// Redner the sky box

		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/