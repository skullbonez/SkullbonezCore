// Shared scalar tolerances and unit-domain helpers for math value types.
#pragma once

#include <algorithm>

constexpr float TOLERANCE = 0.00005f;
constexpr float ONE_PLUS_TOLERANCE = 1.00005f;
constexpr float ZERO_TAKE_TOLERANCE = -0.00005f;

namespace SkullbonezCore::Math
{
// Invariant: clamp normalized float inputs before inverse trigonometry because
// rounding at a pole can otherwise move an exact value outside [-1, 1].
inline float ClampUnit( float value )
{
    return std::clamp( value, -1.0f, 1.0f );
}
} // namespace SkullbonezCore::Math
