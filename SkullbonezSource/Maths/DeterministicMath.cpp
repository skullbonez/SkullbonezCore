/*
File: SkullbonezSource/Maths/DeterministicMath.cpp
Purpose:
  Implements fixed-evaluation-order cosine, sine, and directed-angle estimates.

Summary:
  Angle reduction is repository code rather than a C runtime call. Bhaskara
  rational estimates and a minimax arctangent polynomial then use one explicit
  sequence of binary32 operations, followed by stable cosine/sine normalization.

Invariants:
  - Reduction maps the certified input domain into [-pi, pi] using subtraction
    only; its loop count is bounded by 32 turns.
  - Source statement order is part of the bit-pattern contract.
  - The Maths project disables floating-point contraction for this translation
    unit through Core/FloatingPointContract.h.

Related:
  - SkullbonezSource/Maths/DeterministicMath.h
  - SkullbonezTests/TestDeterministicMath.cpp
  - THIRD_PARTY_NOTICES.md
*/
#include "DeterministicMath.h"

#include <cassert>
#include <cmath>


namespace SkullbonezCore::Math::Deterministic
{
namespace
{
constexpr float TWO_PI = 2.0f * PI;

float ReduceAngle( float radians ) noexcept
{
    const bool valid = radians == radians && radians >= -MAX_COS_SIN_INPUT && radians <= MAX_COS_SIN_INPUT;
    assert( valid && "ComputeCosSin requires finite radians in [-64*pi, 64*pi]" );

    if ( !valid )
    {
        return 0.0f;
    }

    // Why: subtraction keeps range reduction inside the permitted deterministic
    // operation set. The public bound makes both loops finite and caps them at
    // 32 iterations; physics's measured argument is less than one turn.
    float reduced = radians;

    while ( reduced > PI )
    {
        reduced = reduced - TWO_PI;
    }

    while ( reduced < -PI )
    {
        reduced = reduced + TWO_PI;
    }

    return reduced;
}
} // namespace


CosSin ComputeCosSin( float radians ) noexcept
{
    const float x = ReduceAngle( radians );
    const float piSquared = PI * PI;

    // CATTO REF:
    // Erin Catto, Box3D math_functions.c at commit 30c67b5e: Bhaskara rational
    // cosine/sine estimates followed by unit-length normalization.
    //
    // ENGINE-SPECIFIC:
    // Why: platform remainderf implementations may produce different boundary
    // bits. The certified input range bounds this explicit binary32 reduction to
    // 32 iterations, preserving identical evaluation order across toolchains.
    float cosine = 0.0f;

    if ( x < -0.5f * PI )
    {
        const float y = x + PI;
        const float ySquared = y * y;
        const float numerator = piSquared - 4.0f * ySquared;
        const float denominator = piSquared + ySquared;
        cosine = -numerator / denominator;
    }
    else if ( x > 0.5f * PI )
    {
        const float y = x - PI;
        const float ySquared = y * y;
        const float numerator = piSquared - 4.0f * ySquared;
        const float denominator = piSquared + ySquared;
        cosine = -numerator / denominator;
    }
    else
    {
        const float xSquared = x * x;
        const float numerator = piSquared - 4.0f * xSquared;
        const float denominator = piSquared + xSquared;
        cosine = numerator / denominator;
    }

    float sine = 0.0f;

    if ( x < 0.0f )
    {
        const float y = x + PI;
        const float product = y * ( PI - y );
        const float numerator = -16.0f * product;
        const float denominator = 5.0f * piSquared - 4.0f * product;
        sine = numerator / denominator;
    }
    else
    {
        const float product = x * ( PI - x );
        const float numerator = 16.0f * product;
        const float denominator = 5.0f * piSquared - 4.0f * product;
        sine = numerator / denominator;
    }

    const float magnitudeSquared = sine * sine + cosine * cosine;
    const float magnitude = sqrtf( magnitudeSquared );
    const float inverseMagnitude = magnitude > 0.0f ? 1.0f / magnitude : 0.0f;
    return CosSin { cosine * inverseMagnitude, sine * inverseMagnitude };
}


float Atan2( float y, float x ) noexcept
{
    const bool valid = x == x && y == y && x - x == 0.0f && y - y == 0.0f;
    assert( valid && "Atan2 requires finite components" );

    if ( !valid )
    {
        return 0.0f;
    }

    // Canonicalize all signed-zero combinations before sign tests. Zero has no
    // directional information in the vector-angle callers this owner serves.
    if ( x == 0.0f && y == 0.0f )
    {
        return 0.0f;
    }

    const float absoluteX = x < 0.0f ? -x : x;
    const float absoluteY = y < 0.0f ? -y : y;
    const float maximum = absoluteY > absoluteX ? absoluteY : absoluteX;
    const float minimum = absoluteY < absoluteX ? absoluteY : absoluteX;
    const float ratio = minimum / maximum;

    // CATTO REF:
    // Erin Catto, Box3D math_functions.c at commit 30c67b5e: minimax atan
    // polynomial on [0,1], reflected into the other octants.
    const float ratioSquared = ratio * ratio;
    const float ratioCubed = ratioSquared * ratio;
    const float ratioFourth = ratioSquared * ratioSquared;
    float result = 0.024840285f * ratioFourth + 0.18681418f;
    const float correction = -0.094097948f * ratioFourth - 0.33213072f;
    result = result * ratioSquared + correction;
    result = result * ratioCubed + ratio;

    if ( absoluteY > absoluteX )
    {
        result = 0.5f * PI - result;
    }

    if ( x < 0.0f )
    {
        result = PI - result;
    }

    if ( y < 0.0f )
    {
        result = -result;
    }

    return result;
}
} // namespace SkullbonezCore::Math::Deterministic
