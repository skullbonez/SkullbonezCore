/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   ?   .--. .--.   ?
								   ? )/   ? ?   \( ?
								   ?/ \__/   \__/ \?
								   /      /^\      \
								   \__    '='    __/
								   	 ?\         /?
									 ?\"VUUUV"/?
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



/* ================================================================================
   WINDOWS IMPLEMENTATION
   ================================================================================ */
#ifdef _WIN32

#define HIGHEST_ORDER_BIT_16 0x8000
#define LOWEST_ORDER_BIT_16  0x1

bool Input::IsKeyDown(const char cKey)
{
	return ((GetKeyState(cKey) & HIGHEST_ORDER_BIT_16) != 0);
}

bool Input::IsKeyToggled(const char cKey)
{
	return ((GetKeyState(cKey) & LOWEST_ORDER_BIT_16) != 0);
}

POINT Input::GetMouseCoordinates(void)
{
	POINT mousePos;
	if(!GetCursorPos(&mousePos))
		throw std::runtime_error("Getting mouse coordinates failed (Input::GetMouseCoordinates).");
	return mousePos;
}

void Input::SetMouseCoordinates(const POINT& pNewCoordinates)
{
	if(!SetCursorPos(pNewCoordinates.x, pNewCoordinates.y))
		throw std::runtime_error("Setting mouse position failed (Input::SetMouseCoordinates).");
}

void Input::CentreMouseCoordinates(void)
{
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
	SetCursorPos(cWindow->sWindowDimensions.x >> 1,
				 cWindow->sWindowDimensions.y >> 1);
}


#else
/* ================================================================================
   LINUX / GLFW IMPLEMENTATION
   ================================================================================ */

/* -- Declaration of the key-toggle table in SkullbonezWindow.cpp ----------------- */
bool* SkullbonezWindow_GetKeyToggleTable();

/* -- Map Windows VK codes to GLFW key codes -------------------------------------- */
static int VkToGlfw(char vkCode)
{
	// For uppercase letters A-Z and digits 0-9, GLFW uses the same ASCII values.
	if (vkCode >= 'A' && vkCode <= 'Z') return vkCode;
	if (vkCode >= '0' && vkCode <= '9') return vkCode;

	// Special virtual-key codes that differ between Win32 and GLFW
	switch (static_cast<unsigned char>(vkCode))
	{
		case 0x10: return GLFW_KEY_LEFT_SHIFT;   // VK_SHIFT
		case 0x1B: return GLFW_KEY_ESCAPE;        // VK_ESCAPE
		case 0x20: return GLFW_KEY_SPACE;         // VK_SPACE (same value, but explicit)
		default:   return vkCode;
	}
}

/* -- IS KEY DOWN -----------------------------------------------------------------*/
bool Input::IsKeyDown(const char cKey)
{
	GLFWwindow* win = static_cast<GLFWwindow*>(SkullbonezWindow::Instance()->sWindow);
	if (!win) return false;
	int glfwKey = VkToGlfw(cKey);
	return glfwGetKey(win, glfwKey) == GLFW_PRESS;
}

/* -- IS KEY TOGGLED --------------------------------------------------------------*/
bool Input::IsKeyToggled(const char cKey)
{
	int glfwKey = VkToGlfw(cKey);
	if (glfwKey < 0 || glfwKey > GLFW_KEY_LAST) return false;
	return SkullbonezWindow_GetKeyToggleTable()[glfwKey];
}

/* -- GET MOUSE COORDINATES -------------------------------------------------------*/
POINT Input::GetMouseCoordinates(void)
{
	GLFWwindow* win = static_cast<GLFWwindow*>(SkullbonezWindow::Instance()->sWindow);
	double mx = 0.0, my = 0.0;
	if (win) glfwGetCursorPos(win, &mx, &my);
	POINT p;
	p.x = static_cast<int>(mx);
	p.y = static_cast<int>(my);
	return p;
}

/* -- SET MOUSE COORDINATES -------------------------------------------------------*/
void Input::SetMouseCoordinates(const POINT& pNewCoordinates)
{
	GLFWwindow* win = static_cast<GLFWwindow*>(SkullbonezWindow::Instance()->sWindow);
	if (win) glfwSetCursorPos(win, pNewCoordinates.x, pNewCoordinates.y);
}

/* -- CENTRE MOUSE COORDINATES ----------------------------------------------------*/
void Input::CentreMouseCoordinates(void)
{
	SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
	GLFWwindow* win = static_cast<GLFWwindow*>(cWindow->sWindow);
	if (win)
		glfwSetCursorPos(win,
						 cWindow->sWindowDimensions.x >> 1,
						 cWindow->sWindowDimensions.y >> 1);
}

#endif // _WIN32
