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


	General TODO list:
	--------------------------------------------------------------------------
	- Add const to method level + check on pointers TO objects (ensure not 
	  just const on the pointer itself).
    - Inherit across namespace from Geometry::Ray to 
	  Math::DynamicsObject::CollisionRay
	- Return const references where appropriate, check returned references 
	  are always const
    - Turn all of the defines into global const variables
	- Use C++ casts instead of C style casts
	- Maximum framerate cap?
	--------------------------------------------------------------------------

-----------------------------------------------------------------------------------*/



/* -- INCLUDE GUARDS --------------------------------------------------------------*/
#ifndef SKULLBONEZ_COMMON_H
#define SKULLBONEZ_COMMON_H



/* -- JPEG INCLUDE ----------------------------------------------------------------*/
#include "jpeglib.h"



/* -- EXCLUDE RARELY USED HEADERS -------------------------------------------------*/
#define WIN32_LEAN_AND_MEAN



/* -- INCLUDES --------------------------------------------------------------------*/
#include <windows.h>  // Windows
#include <stdlib.h>	  // Standard Library
#include <stdio.h>	  // Standard Input/Output
#include <stdarg.h>	  // Arguments
#include <math.h>	  // Standard Math Functions
#include <assert.h>   // Assertions
#include <gl\gl.h>    // OpenGL Library
#include <gl\glu.h>   // OpenGL Utility Library



/* -- GL/GLU LIBS -----------------------------------------------------------------*/
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")



/* -- ENABLE HEAP DEBUGGING -------------------------------------------------------*/
#include <crtdbg.h>
#define CRTDBG_MAP_ALLOC



/* -- GLOBAL VARIABLES ------------------------------------------------------------*/
#define FULLSCREEN_MODE			FALSE					// Specify to run in full screen mode or not
#define WINDOW_NAME				"SkullbonezWindow"		// Window class name
#define TITLE_TEXT				"::SKULLBONEZ CORE::"	// Window title text
#define SCREEN_X				800 					// Screen resolution width
#define SCREEN_Y				580						// Screen resolution height
#define BITS_PER_PIXEL			32						// Full screen bits per pixel
#define REFRESH_RATE			75						// Full screen refresh rate
#define	MOUSE_MOVEMENT_CONST	0.05f					// Mouse movement scale constant
#define	KEY_MOVEMENT_CONST		20.0f					// Keyboard movement scale constant
#define TWEEN_RATE				3.0f					// Camera tween rate
#define	COLLISION_THRESHOLD		0.01f					// View/Up collision threshold (radians)
#define MIP_MAP_COMPONENTS		4						// Number of mipmap images to build per texture
#define FRUSTUM_CLIP_SHORT_QTY  1.0f					// Near clipping volume
#define FRUSTUM_CLIP_FAR_QTY    5500.0f					// Far clipping volume
#define	TOTAL_CAMERA_COUNT		3						// Total number of cameras
#define	TOTAL_TEXTURE_COUNT		9						// Total number of textures
#define TERRAIN_SCALING			5.0f					// Amount to scale terrain
#define _PI						3.14159265f				// PI constant
#define _2PI					6.2831853f				// 2 x PI constant
#define _HALF_PI				1.570796325f			// Half PI constant
#define	MIN_CAMERA_HEIGHT		1.5f					// Minimum height cameras can get to the terrain
#define MAX_CAMERA_HEIGHT		110.0f					// Maxmium height cameras can get to before being capped
#define MIN_CAMERA_VIEW_MAG		2.0f					// Minimum view magnitude allowable for a camera
#define MAX_CAMERA_VIEW_MAG		300.0f					// Maximum view magnitude allowable for a camera
#define TERRAIN_HEIGHT_SCALE    0.15f					// Quantity to scale the terrain height
#define SKY_BOX_OVERFLOW		1						// Amount to overflow sky box to avoid showing lines
#define SKY_BOX_SCALE			10.0f					// Scaling of sky box rendering
#define NO_COLLISION			1e30f					// Large number to signify no collision has taken place
#define VELOCITY_LIMIT			5.0f					// Limit for any one velocity component
#define TOLERANCE				0.00005f				// A small float just above zero to account for precision errors
#define ONE_PLUS_TOLERANCE      1.00005f				// One plus tolerance
#define ZERO_TAKE_TOLERANCE     -0.00005f				// Zero take tolerance (negative tolerance)
#define FOUR_OVER_THREE			1.33333f				// 4/3 decimal representation
#define ONE_OVER_THREE			0.33333f				// 1/3 decimal representation
#define RENDER_COL_VOL_TRANS	FALSE					// Should collision volumes be transparently rendered or not?
#define SPHERE_DRAG_COEFFICIENT 0.4f					// The drag coefficient of a sphere
#define SKYBOX_RENDER_HEIGHT	30.0f					// Height to render the skybox
#define	FLUID_HEIGHT			25.0f					// Height of fluid
#define GAS_DENSITY				0.0f					// Density of gas
#define FLUID_DENSITY			1.0f					// Density of fluid
#define GRAVITATIONAL_FORCE		-30.0f					// Gravitational force



/* -- GLOBAL PATHS ----------------------------------------------------------------*/
#define SKY_FRONT_PATH			"SkullbonezData/sky1.jpg"
#define SKY_LEFT_PATH			"SkullbonezData/sky2.jpg"
#define SKY_BACK_PATH			"SkullbonezData/sky3.jpg"
#define SKY_RIGHT_PATH			"SkullbonezData/sky4.jpg"
#define SKY_UP_PATH				"SkullbonezData/sky5.jpg"
#define SKY_DOWN_PATH			"SkullbonezData/sky6.jpg"
#define TERRAIN_TEXTURE_PATH	"SkullbonezData/ground.jpg"
#define BOUNDING_SPHERE_PATH	"SkullbonezData/boundingSphere.jpg"
#define WATER_PATH				"SkullbonezData/water.jpg"
#define TERRAIN_RAW_PATH		"SkullbonezData/terrain.raw"



/* -- MEM MANAGER INCLUDE ---------------------------------------------------------*/
// #include "mmgr.h"




/*-- LIVE DEBUG CODE ----------------------------------------------------------------

	// inlcude this for debug purposes only////////////////
	SkullbonezWindow* w = SkullbonezWindow::Instance();  //
	char b[20];											 //
	itoa((int)(DEBUG_VARIABLE), b, 10);					 //
	w->SetTitleText(b);									 //
	///////////////////////////////////////////////////////

	// include this for debug purposes only////
	#include "SkullbonezWindow.h"		     //
	using namespace SkullbonezCore::Basics;  //
	///////////////////////////////////////////

-- END LIVE DEBUG CODE ------------------------------------------------------------*/


#endif