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
#ifndef SKULLBONEZ_TEXT_H
#define SKULLBONEZ_TEXT_H



/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"



namespace SkullbonezCore
{
	namespace Text
	{
		/* -- Text 2d ----------------------------------------------------------------------------------------------------------------------------------------------------

			Provides a series of static methods to draw 2D text to the screen in OpenGL.  Note that this text is rendered into the scene as polygons so text may appear 
			aliased without multisampling.
		-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
		class Text2d
		{

		public:

			static UINT displayList;				// Holds GL display list reference


			
			// NOTES: positioning is relational to centre of client rect
			//		  xPosition and yPosition should be (< 0.5f) and (> - 0.5f)
			//		  fSize should be between 0 and 1
			//		  pass additional arguments to render variables (just like printf)
			static void		Render2dText		(float xPosition,
												 float yPosition,
												 float fSize,
												 const char* cRawText,
												 ...);								// Renders text to the scene
			static void		BuildFont			(const HDC hDC, 
												 const char* cFontName);			// Builds font into display list
			static void		DeleteFont			(void);								// Removes font from the display list

		private:

			static void		PrepareEnvironment	(float xPosition, 
												 float yPosition,
												 float fSize);						// Prepare OpenGL environment for text draw
			static void		RestoreEnvironment	(void);								// Restore OpenGL environment now text draw is complete
		};
	}
}

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/