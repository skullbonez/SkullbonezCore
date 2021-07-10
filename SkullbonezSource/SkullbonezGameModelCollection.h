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
#ifndef SKULLBONEZ_GAME_MODEL_COLLECTION_H
#define SKULLBONEZ_GAME_MODEL_COLLECTION_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include <list>
#include "SkullbonezCommon.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezVector3.h"



/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace std;
using namespace SkullbonezCore::Math::Vector;



namespace SkullbonezCore
{
	namespace GameObjects
	{
		/* -- Game Model Collection --------------------------------------------------------------------------------------------------------------------------------------
			
			Represents a collection of game models and operations to assist in managing the collection.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class GameModelCollection
		{

		private:

			GameModel**						gameModelArray;																// Collection of game models
			int								maxCount;																	// Max number of game models to create
			int								curCount;																	// Current quantity of game models created


		public:

											GameModelCollection					(int iMaxCount);						// Overloaded constructor
											~GameModelCollection				(void);									// Default destructor
			
			void							AddGameModel						(GameModel* gameModel);					// Adds a game model to the collection
			void							RunPhysics							(float fChangeInTime);					// Runs the physics for the specified time step
			void							RenderModels						(void);									// Renders the game models
			Vector3							GetModelPosition					(int index);							// Returns the position of the specified game model
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/