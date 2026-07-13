/*
File: SkullbonezSource/Maths/MathsCommon.h
Purpose:
  Owns the minimal math prelude shared by the Maths layer.

Summary:
  Maths is the bottom library candidate. It can depend on CRT math facilities
  and compile-time numeric contracts, but not on platform, runtime, config, or
  logging ownership from Core/Common.h.

Glossary:
  Maths prelude: The small set of includes, constants, and build toggles that
    math headers need before a standalone SkullbonezMaths library exists.
  Aliasing period: Core/Common.h includes this header while older callers still
    expect these names from Common.h; new Maths code should include this file
    directly.

Invariants:
  - This header must not include Core/Common.h, Windows headers, Config.h, or
    Log.h; doing so reintroduces the upward dependency L2 is removing.
  - These constants are compile-time numeric contracts. Changing their values
    changes math, collision, and renderer-validation scope.

Related:
  - SkullbonezSource/Core/Common.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cfloat> // FLT_MAX for math users that still include the common prelude.
#include <cmath>  // sqrtf, sinf, cosf, fabsf, acosf

// SSE/SIMD intrinsics are enabled in Release/Profile by default.
// Define SKULLBONEZ_INTRINSICS before this header to force a scalar fallback.
#ifndef SKULLBONEZ_INTRINSICS
#ifndef _DEBUG
#define SKULLBONEZ_INTRINSICS 1
#else
#define SKULLBONEZ_INTRINSICS 0
#endif
#endif

// Angle and fraction constants used by math and collision helpers.
constexpr float _PI = 3.14159265f;
constexpr float _2PI = 6.2831853f;
constexpr float _HALF_PI = 1.570796325f;
constexpr float FOUR_OVER_THREE = 1.33333f;
constexpr float ONE_OVER_THREE = 0.33333f;

// Numeric sentinels and tolerances shared by math and collision code.
constexpr float NO_COLLISION = 1e30f;
constexpr float TOLERANCE = 0.00005f;
constexpr float ONE_PLUS_TOLERANCE = 1.00005f;
constexpr float ZERO_TAKE_TOLERANCE = -0.00005f;
