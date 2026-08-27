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
  Unit domain: Closed numeric interval [-1, 1] accepted by inverse cosine and
    inverse sine.
  Aliasing period: Core/Common.h includes this header while older callers still
    expect these names from Common.h. New Maths code should include the
    narrowest owning header, such as ScalarMath.h for scalar tolerances.

Invariants:
  - This header must not include Core/Common.h, Windows headers, Config.h, or
    Log.h; doing so reintroduces the upward dependency L2 is removing.
  - These constants are compile-time numeric contracts. Changing their values
    changes math, collision, and renderer-validation scope.
  - Normalized-vector inverse-trig inputs use ClampUnit so float rounding at a
    pole cannot escape the function domain.

Related:
  - SkullbonezSource/Core/Common.h
  - SkullbonezSource/Maths/ScalarMath.h
*/
#pragma once

#include "ScalarMath.h"

#include <cfloat> // FLT_MAX for math users that still include the common prelude.
#include <cmath>  // sqrtf, sinf, cosf, fabsf, acosf

// SSE/SIMD intrinsics are enabled in Release/Profile and disabled in Debug by
// default. An externally supplied SKULLBONEZ_INTRINSICS value retains the
// existing scalar/SSE override contract without this header defining a macro.
#if defined( SKULLBONEZ_INTRINSICS )
inline constexpr bool INTRINSICS_ENABLED = SKULLBONEZ_INTRINSICS != 0;
#elif !defined( _DEBUG )
inline constexpr bool INTRINSICS_ENABLED = true;
#else
inline constexpr bool INTRINSICS_ENABLED = false;
#endif

// Angle and fraction constants used by math and collision helpers.
constexpr float _PI = 3.14159265f;
constexpr float _2PI = 6.2831853f;
constexpr float _HALF_PI = 1.570796325f;
constexpr float FOUR_OVER_THREE = 1.33333f;
constexpr float ONE_OVER_THREE = 0.33333f;

// Collision sentinel retained for the legacy collision APIs.
constexpr float NO_COLLISION = 1e30f;
