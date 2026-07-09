/*
File: SkullbonezSource/Core/Common.h
Purpose:
  Defines shared constants, enums, and small cross-subsystem engine types.

Mental model:
  Common.h defines shared constants, enums, and small cross-subsystem engine
  types. As a public header, keep edits anchored on process-wide contracts,
  diagnostics, and validation-sensitive state and on the glossary/invariants
  below.

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

Invariants:
  - Shared constants in this file are compile-time engine contracts; changing
    capacities or runtime labels changes validation scope.
  - Cfg() is a compatibility accessor only; ownership remains with the config
    singleton type declared in its own subsystem header.

Alias deletion schedule:
  - Remove AssetKeys.h after profiler, replay, runtime, scene, render, physics,
    and asset callers include the hash-key owner directly.
  - Remove WindowConstants.h after window/title and DATA_ROOT path users include
    the runtime owner directly.
  - Remove MathsCommon.h, SceneCapacity.h, and PhysicsTimestep.h after their
    current direct-call users stop relying on Common.h for domain constants.
  - Config.h stays until the global-service cleanup plan finishes deleting the
    remaining Common.h service compatibility path.

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

// Why: authoritative-plan-02 owns removing service-accessor compatibility from
// Common.h. Keep this include local until that plan deletes the remaining
// Cfg() compatibility path and callers carry explicit config-owner includes.
// All other engine parameters live in EngineConfig (loaded from engine.cfg).
#include "Config.h"

inline int ActiveGameModelCapacity( const SkullbonezCore::Basics::EngineConfig& config )
{
    return std::clamp( config.gameModelCapacity, 1, MAX_GAME_MODELS );
}
