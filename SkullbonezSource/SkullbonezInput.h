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
#ifndef SKULLBONEZ_INPUT_H
#define SKULLBONEZ_INPUT_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"



namespace SkullbonezCore
{
	namespace Hardware
	{
		/* -- Input State ------------------------------------------------------------------------------------------------------------------------------------------------

			Holds input state to help separate logic and input code.  Should be modified depending on the games input requirements.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		struct InputState
		{
			bool fUp, fDown, fLeft, fRight, fAux1, fAux2;
			long xMove, yMove;
		};


		/* -- Input ------------------------------------------------------------------------------------------------------------------------------------------------------

			Static methods to wrap up input functions from the Win32 API.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class Input
		{
		public:

			
			static bool		IsKeyDown				(const char cKey);				// Returns true if specified key is pressed (use upper case)
			static bool		IsKeyToggled			(const char cKey);				// Returns true if specified key is toggled (use upper case)
			static POINT	GetMouseCoordinates		(void);							// Returns the coordinates of the mouse cursor
			static void		SetMouseCoordinates		(const POINT &pNewCoordinates);	// Sets the mouse coordinates
			static void		CentreMouseCoordinates	(void);							// Sets the mouse cursor to the centre of the screen
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/