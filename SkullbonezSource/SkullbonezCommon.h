/*-----------------------------------------------------------------------------------
                                  THE SKULLBONEZ CORE
                                        _______
                                     .-"       "-.
                                    /             \
                                   /               \
                                   |   .--. .--.   |
                                   | )/   | |   \( |
                                   |/ \__/   \__/ \|
                                   /      /^\      \
                                   \__    '='    __/
                                     |\         /|
                                     |\'"VUUUV"'/|
                                     \ `"""""""` /
                                      `-._____.-'

                                 www.simoneschbach.com


    General TODO list:
    --------------------------------------------------------------------------
    - Add const to method level + check on pointers TO objects (ensure not just const on the pointer itself).
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
#include <windows.h> // Windows
#include <stdlib.h>  // Standard Library
#include <stdio.h>   // Standard Input/Output
#include <stdarg.h>  // Arguments
#include <math.h>    // Standard Math Functions
#include <assert.h>  // Assertions
#include <stdexcept> // std::runtime_error
#include <memory>    // std::unique_ptr
#include <vector>    // std::vector
#include <glad/gl.h> // GLAD OpenGL 3.3 Core Loader

/* -- GL LIBS ---------------------------------------------------------------------*/
#pragma comment( lib, "opengl32.lib" )

/* -- ENABLE HEAP DEBUGGING -------------------------------------------------------*/
#include <crtdbg.h>
#define CRTDBG_MAP_ALLOC

/* -- COMPILE-TIME CONSTANTS ------------------------------------------------------*/
// Array-sizing counts (must remain compile-time)
#define TOTAL_CAMERA_COUNT  3
#define TOTAL_TEXTURE_COUNT 8

// Window labels
#define WINDOW_NAME "SkullbonezWindow"
#define TITLE_TEXT  "::SKULLBONEZ CORE::"

// Math constants
constexpr float _PI              = 3.14159265f;
constexpr float _2PI             = 6.2831853f;
constexpr float _HALF_PI         = 1.570796325f;
constexpr float FOUR_OVER_THREE  = 1.33333f;
constexpr float ONE_OVER_THREE   = 0.33333f;

// Numeric sentinels / tolerances
constexpr float NO_COLLISION        = 1e30f;
constexpr float TOLERANCE           = 0.00005f;
constexpr float ONE_PLUS_TOLERANCE  = 1.00005f;
constexpr float ZERO_TAKE_TOLERANCE = -0.00005f;

/* -- RUNTIME CONFIGURATION -------------------------------------------------------*/
// All other engine parameters live in SkullbonezConfig (loaded from engine.cfg).
#include "SkullbonezConfig.h"

// Convenience accessor — use Cfg().fieldName anywhere SkullbonezCommon.h is included.
inline SkullbonezCore::Basics::SkullbonezConfig& Cfg()
{
    return SkullbonezCore::Basics::SkullbonezConfig::Instance();
}

/* -- HASH FUNCTION ---------------------------------------------------------------*/
// FNV-1a 32-bit compile-time hash for string keys
constexpr uint32_t HashStr( const char* s, uint32_t hash = 2166136261u )
{
    return ( *s == '\0' ) ? hash : HashStr( s + 1, ( hash ^ static_cast<uint32_t>( *s ) ) * 16777619u );
}

/* -- TEXTURE NAME HASHES ---------------------------------------------------------*/
constexpr uint32_t TEXTURE_GROUND = HashStr( "Ground" );
constexpr uint32_t TEXTURE_BOUNDING_SPHERE = HashStr( "BoundingSphere" );
constexpr uint32_t TEXTURE_SKY_LEFT = HashStr( "SkyLeft" );
constexpr uint32_t TEXTURE_SKY_RIGHT = HashStr( "SkyRight" );
constexpr uint32_t TEXTURE_SKY_FRONT = HashStr( "SkyFront" );
constexpr uint32_t TEXTURE_SKY_BACK = HashStr( "SkyBack" );
constexpr uint32_t TEXTURE_SKY_UP = HashStr( "SkyUp" );
constexpr uint32_t TEXTURE_SKY_DOWN = HashStr( "SkyDown" );

/* -- CAMERA NAME HASHES ----------------------------------------------------------*/
constexpr uint32_t CAMERA_GAME_MODEL_1 = HashStr( "GameModel1" );
constexpr uint32_t CAMERA_GAME_MODEL_2 = HashStr( "GameModel2" );
constexpr uint32_t CAMERA_FREE = HashStr( "Free" );

/* -- PERFORMANCE PROFILING SYSTEM --------------------------------------------------*/
// Master enable: set to 1 to enable profiling, 0 to disable
// Default: 1 in Debug, 0 in Release (user can override below)
#if defined( _DEBUG )
#define SKULLBONEZ_PROFILING_ENABLED 1
#else
#define SKULLBONEZ_PROFILING_ENABLED 0
#endif

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
