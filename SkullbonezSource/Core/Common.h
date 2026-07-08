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
  Asset keys: Legacy string hashes owned by Assets/AssetKeys.h during the
    Common.h aliasing period.
  Window constants: Process window labels and data-root string owned by
    Runtime/WindowConstants.h during the Common.h aliasing period.
  Physics timestep: Fixed-step physics constants owned by
    Physics/PhysicsTimestep.h during the Common.h aliasing period.
  Scene capacity: Fixed model/camera/texture ceilings owned by
    GameObjects/SceneCapacity.h during the Common.h aliasing period.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Shared constants in this file are compile-time engine contracts; changing
    capacities or runtime labels changes validation scope.
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
#include "../Assets/AssetKeys.h"
#include "../Runtime/WindowConstants.h"
#include "../Maths/MathsCommon.h"
#include "../GameObjects/SceneCapacity.h"
#include "../Physics/PhysicsTimestep.h"

#ifdef _DEBUG
#define CRTDBG_MAP_ALLOC // must precede crtdbg.h to redirect malloc → _malloc_dbg
#include <crtdbg.h>
#endif

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
