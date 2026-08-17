/*
File: SkullbonezSource/Maths/DeterministicMath.h
Purpose:
  Declares the repository-owned deterministic angle-construction routines.

Summary:
  This narrow value API is intended for physics-visible rotations whose
  identical input bits must produce identical output bits without depending on
  a processor-selected C runtime transcendental implementation.

Glossary:
  Certified angle domain: Finite input interval whose reduction boundaries and
    approximation error are pinned by the deterministic-math tests.

Invariants:
  - ComputeCosSin accepts finite radians in the closed interval [-64*pi, 64*pi].
  - Atan2 accepts finite components and canonicalizes every (signed) zero pair
    to positive zero.
  - Implementations use only basic arithmetic, comparison, and sqrtf, with no
    lookup table, fused operation, or implementation-defined transcendental.

Related:
  - SkullbonezSource/Maths/DeterministicMath.cpp
  - SkullbonezTests/TestDeterministicMath.cpp
*/
#pragma once


namespace SkullbonezCore::Math::Deterministic
{
inline constexpr float PI = 3.14159274f;
inline constexpr float MAX_COS_SIN_INPUT = 64.0f * PI;

struct CosSin
{
    float cosine = 1.0f;
    float sine = 0.0f;
};

// Returns a normalized cosine/sine pair for a finite angle in the certified
// domain. Debug asserts on misuse; Release returns identity for invalid input.
CosSin ComputeCosSin( float radians ) noexcept;

// Returns the directed angle in [-pi, pi]. All forms of (0,0) return +0, so
// signed zero is deliberately not a separate geometric direction.
float Atan2( float y, float x ) noexcept;
} // namespace SkullbonezCore::Math::Deterministic
