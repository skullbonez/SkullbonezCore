/*
File: SkullbonezSource/Core/Common.h
Purpose:
  Defines shared constants, enums, and small cross-subsystem engine types.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Maths prelude: Math-only constants and CRT math includes owned by
    Maths/MathsCommon.h during the Common.h aliasing period.
  Physics timestep: Fixed-step physics constants owned by
    Physics/PhysicsTimestep.h during the Common.h aliasing period.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Shared constants in this file are compile-time engine contracts; changing
    capacities or hash keys changes validation scope.
  - Log() is a convenience accessor only; ownership remains with the singleton
    type declared in its own subsystem header.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
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


#define WIN32_LEAN_AND_MEAN

#include <windows.h> // Windows
#include <cstdlib>   // std::atoi, std::atof, std::abs
#include <cstdio>    // std::sprintf_s, std::sscanf_s, std::FILE
#include <cstdarg>   // std::va_list, std::va_start, std::va_end
#include <cassert>   // assert()
#include <stdexcept> // std::runtime_error
#include <memory>    // std::unique_ptr
#include <algorithm> // std::clamp, std::min, std::max
#include "../Maths/MathsCommon.h"
#include "../Physics/PhysicsTimestep.h"

#ifdef _DEBUG
#define CRTDBG_MAP_ALLOC // must precede crtdbg.h to redirect malloc → _malloc_dbg
#include <crtdbg.h>
#endif

// Array-sizing counts (must remain compile-time)
constexpr int TOTAL_CAMERA_COUNT = 8;
constexpr int TOTAL_TEXTURE_COUNT = 8;
constexpr int DEFAULT_GAME_MODEL_CAPACITY = 4000;
constexpr int MAX_GAME_MODELS = 8192;
constexpr int DEFAULT_GAME_MODELS = 300;

// Window labels
constexpr const char* WINDOW_NAME = "SkullbonezWindow";
constexpr const char* TITLE_TEXT = "::SKULLBONEZ CORE::";
constexpr const char* DATA_ROOT = "SkullbonezData/";

// All other engine parameters live in EngineConfig (loaded from engine.cfg).
#include "Config.h"
#include "Log.h"

inline int ActiveGameModelCapacity( const SkullbonezCore::Basics::EngineConfig& config )
{
    return std::clamp( config.gameModelCapacity, 1, MAX_GAME_MODELS );
}

// Convenience accessor — use Log().Writef("file.csv", fmt, ...) anywhere (debug no-op in release).
inline SkullbonezCore::Basics::EngineLog& Log()
{
    return SkullbonezCore::Basics::EngineLog::Get();
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
