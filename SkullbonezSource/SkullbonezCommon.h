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

                                     SimonEschbach
                                          is
                                      SKULLBONEZ
-----------------------------------------------------------------------------------*/

#pragma once


// --- Includes ---
#define WIN32_LEAN_AND_MEAN

#include <windows.h>      // Windows
#include <stdlib.h>       // Standard Library
#include <stdio.h>        // Standard Input/Output
#include <stdarg.h>       // Arguments
#include <math.h>         // Standard Math Functions
#include <assert.h>       // Assertions
#include <stdexcept>      // std::runtime_error
#include <memory>         // std::unique_ptr
#include <vector>         // std::vector
#include <d3d11.h>        // DirectX 11
#include <dxgi.h>         // DXGI
#include <dxgi1_2.h>      // DXGI 1.2
#include <d3dcompiler.h>  // Shader compilation
#include <wrl/client.h>   // Windows Runtime COM helpers

#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "d3dcompiler.lib" )

#include <crtdbg.h>
#define CRTDBG_MAP_ALLOC

// Array-sizing counts (must remain compile-time)
constexpr int TOTAL_CAMERA_COUNT = 3;
constexpr int TOTAL_TEXTURE_COUNT = 8;
constexpr int MAX_GAME_MODELS = 512;
constexpr int DEFAULT_GAME_MODELS = 300;

// Window labels
constexpr const char* WINDOW_NAME = "SkullbonezWindow";
constexpr const char* TITLE_TEXT = "::SKULLBONEZ CORE::";

// Math constants
constexpr float _PI = 3.14159265f;
constexpr float _2PI = 6.2831853f;
constexpr float _HALF_PI = 1.570796325f;
constexpr float FOUR_OVER_THREE = 1.33333f;
constexpr float ONE_OVER_THREE = 0.33333f;

// Numeric sentinels / tolerances
constexpr float NO_COLLISION = 1e30f;
constexpr float TOLERANCE = 0.00005f;
constexpr float ONE_PLUS_TOLERANCE = 1.00005f;
constexpr float ZERO_TAKE_TOLERANCE = -0.00005f;

// All other engine parameters live in SkullbonezConfig (loaded from engine.cfg).
#include "SkullbonezConfig.h"

// Convenience accessor — use Cfg().fieldName anywhere SkullbonezCommon.h is included.
inline SkullbonezCore::Basics::SkullbonezConfig& Cfg()
{
    return SkullbonezCore::Basics::SkullbonezConfig::Instance();
}


// FNV-1a 32-bit compile-time hash for string keys
constexpr uint32_t HashStr( const char* s, uint32_t hash = 2166136261u )
{
    return ( *s == '\0' ) ? hash : HashStr( s + 1, ( hash ^ static_cast<uint32_t>( *s ) ) * 16777619u );
}


constexpr uint32_t TEXTURE_GROUND = HashStr( "Ground" );
constexpr uint32_t TEXTURE_BOUNDING_SPHERE = HashStr( "BoundingSphere" );
constexpr uint32_t TEXTURE_SKY_LEFT = HashStr( "SkyLeft" );
constexpr uint32_t TEXTURE_SKY_RIGHT = HashStr( "SkyRight" );
constexpr uint32_t TEXTURE_SKY_FRONT = HashStr( "SkyFront" );
constexpr uint32_t TEXTURE_SKY_BACK = HashStr( "SkyBack" );
constexpr uint32_t TEXTURE_SKY_UP = HashStr( "SkyUp" );
constexpr uint32_t TEXTURE_SKY_DOWN = HashStr( "SkyDown" );

constexpr uint32_t CAMERA_GAME_MODEL_1 = HashStr( "GameModel1" );
constexpr uint32_t CAMERA_GAME_MODEL_2 = HashStr( "GameModel2" );
constexpr uint32_t CAMERA_FREE = HashStr( "Free" );

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
