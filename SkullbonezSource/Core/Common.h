/*
File: SkullbonezSource/Core/Common.h
Purpose:
  Defines shared constants, enums, and small cross-subsystem engine types.

Mental model:
  Common.h is the platform/standard-library prelude retained by older engine
  headers. Domain constants and configuration policy come from their concrete
  owners rather than arriving transitively through this file.

Glossary:
  Prelude: Small platform and standard-library include set inherited by older
    engine headers while narrower include cleanup continues.

Invariants:
  - This file must not regain domain owners, service accessors, or compatibility
    aliases; consumers include the exact asset, window, math, scene, physics,
    or config contract they use.

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
#include <memory>    // std::unique_ptr
#include <algorithm> // std::clamp, std::min, std::max

#ifdef _DEBUG
#define CRTDBG_MAP_ALLOC // must precede crtdbg.h to redirect malloc → _malloc_dbg
#include <crtdbg.h>
#endif
