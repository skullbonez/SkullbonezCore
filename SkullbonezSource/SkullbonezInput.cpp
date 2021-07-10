/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
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
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezInput.h"
#include "SkullbonezWindow.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Basics;



/* -- DEFINITIONS -----------------------------------------------------------------*/
/*
   0x8000 = (16^3)*8 + (16^2)*0 + (16^1)*0 + (16^0)*0
          = 32,768 (decimal)
		  = 1000 0000 0000 0000 (binary)
   This is the highest order bit of a 16 bit piece of data (SHORT)
 */
#define HIGHEST_ORDER_BIT_16 0x8000

/*
   0x1 = 0x0001 = (16^3)*0 + (16^2)*0 + (16^1)*0 + (16^0)*1
       = 1 (decimal)
       = 1 (binary)
       = 0000 0000 0000 0001 (binary)
   This is the lowest order bit of any sized piece of binary data
   */
#define LOWEST_ORDER_BIT_16 0x1



/* -- IS KEY DOWN -----------------------------------------------------------------*/
bool Input::IsKeyDown(const char cKey)
{
	/*
		recall that HIGHEST_ORDER_BIT_16 = 1000 0000 0000 0000 (binary)
		recall that a conditional statement in C++ simply checks if the value is
		nonzero, and if it is nonzero returns true, otherwise false.
		GetKeyState(cKey) will return a 16 bit SHORT and if the key is pressed,
		the SHORT returned will have the highest order bit set to 1.
		the binary AND operator '&' will do the comparision on the two SHORTs
		1000 0000 0000 0000 & 1000 0000 0000 0000 (if the key is pressed)
		   = 1000 0000 0000 0000 != 0
		1000 0000 0000 0000 & 0000 0000 0000 0000 (if the key is not pressed)
		   = 0000 0000 0000 0000 == 0
	*/
	return ((GetKeyState(cKey) & HIGHEST_ORDER_BIT_16) != 0);
}



/* -- IS KEY TOGGLED --------------------------------------------------------------*/
bool Input::IsKeyToggled(const char cKey)
{
	// lowest order bit is set to 1 if key is toggled, see Input::IsKeyDown
	// for an explanation on the conditional statement below
	return ((GetKeyState(cKey) & LOWEST_ORDER_BIT_16) != 0);
}



/* -- GET MOUSE COORDINATES -------------------------------------------------------*/
POINT Input::GetMouseCoordinates(void)
{
	POINT mousePos;
	if(!GetCursorPos(&mousePos))	// attempt to get the mouse position
		throw "Getting mouse coordinates failed (Input::GetMouseCoordinates).";

	return mousePos;
}



/* -- SET MOUSE COORDINATES -------------------------------------------------------*/
void Input::SetMouseCoordinates(const POINT &pNewCoordinates)
{
	// attempt to set the mouse position
	if(!SetCursorPos(pNewCoordinates.x, pNewCoordinates.y))
		throw "Setting mouse position failed (Input::SetMouseCoordinates).";
}



/* -- CENTRE MOUSE COORDINATES ----------------------------------------------------*/
void Input::CentreMouseCoordinates(void)
{
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
	SetCursorPos(cWindow->sWindowDimensions.x >> 1,
				 cWindow->sWindowDimensions.y >> 1);
}