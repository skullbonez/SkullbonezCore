/*
File: SkullbonezSource/Physics/Stages/Kernels/IntegrationKernel.cpp
Purpose:
  Implements the pinned AVX2/FMA integration pilot for eight SoA body rows.

Summary:
  One call builds a deterministic activity mask, uses masked loads and stores
  for both full and partial blocks, simplifies the six velocity components, and
  advances three position components with explicit fused multiply-adds. The
  stage owner performs non-vector orientation and terrain completion afterward.

Glossary:
  Lane: One body row participating in an eight-wide vector instruction.
  Activity mask: Per-lane selection derived from bounds, fixed/awake flags,
    sleep state, and positive remaining time.
  FMA (Fused Multiply-Add): Explicit single-rounding position advance; this is
    intentionally different from the scalar toggle-OFF rounding shape.
  Masked load/store: Memory operation that touches only selected lane addresses.

Invariants:
  - Body count modulo eight changes only the mask, never the arithmetic shape of
    a valid lane; the tail does not fall back to scalar math.
  - Near-zero simplification uses the scalar path's strict open interval.
  - No lane reads or writes another lane, and no allocation occurs.

Related:
  - SkullbonezSource/Physics/Stages/Kernels/IntegrationKernel.h
  - SkullbonezSource/Maths/Vector3.h
  - Agentic/Plans/TODO/physics-soa-simd-1000-bodies.md
*/
#include "IntegrationKernel.h"

#include "../../../Maths/MathsCommon.h"

#include <algorithm>
#include <cstdint>
#include <immintrin.h>

namespace SkullbonezCore
{
namespace Physics::Kernels
{
namespace
{
__m256 SimplifyVelocity( __m256 value )
{
    const __m256 upper = _mm256_set1_ps( TOLERANCE );
    const __m256 lower = _mm256_set1_ps( ZERO_TAKE_TOLERANCE );
    const __m256 belowUpper = _mm256_cmp_ps( value, upper, _CMP_LT_OQ );
    const __m256 aboveLower = _mm256_cmp_ps( value, lower, _CMP_GT_OQ );
    const __m256 nearZero = _mm256_and_ps( belowUpper, aboveLower );
    return _mm256_andnot_ps( nearZero, value );
}

__m256 LoadSimplifyStore( std::span<float> values, int bodyBegin, __m256i activeMask )
{
    float* const row = values.data() + bodyBegin;
    const __m256 simplified = SimplifyVelocity( _mm256_maskload_ps( row, activeMask ) );
    _mm256_maskstore_ps( row, activeMask, simplified );
    return simplified;
}
} // namespace

uint32_t IntegratePositionAvx2( const PhysicsBodyHotFieldsView& hotFields,
                                std::span<const uint8_t> sleepState,
                                std::span<const float> timeRemaining,
                                int bodyBegin,
                                int bodyCount )
{
    if ( bodyBegin < 0 || bodyBegin >= bodyCount )
    {
        return 0u;
    }

    const int laneCount = (std::min)( INTEGRATION_LANE_COUNT, bodyCount - bodyBegin );
    alignas( 32 ) int32_t validRows[INTEGRATION_LANE_COUNT] = {};
    alignas( 32 ) int32_t eligibleRows[INTEGRATION_LANE_COUNT] = {};
    for ( int lane = 0; lane < laneCount; ++lane )
    {
        const std::size_t index = static_cast<std::size_t>( bodyBegin + lane );
        validRows[lane] = -1;
        eligibleRows[lane] =
            hotFields.fixed[index] == 0u && hotFields.awake[index] != 0u && sleepState[index] == 0u ? -1 : 0;
    }

    const __m256i validMask = _mm256_load_si256( reinterpret_cast<const __m256i*>( validRows ) );
    const __m256i eligibilityMask = _mm256_load_si256( reinterpret_cast<const __m256i*>( eligibleRows ) );
    const __m256 dt = _mm256_maskload_ps( timeRemaining.data() + bodyBegin, validMask );
    const __m256 positiveTime = _mm256_cmp_ps( dt, _mm256_setzero_ps(), _CMP_GT_OQ );
    const __m256i activeMask = _mm256_and_si256( eligibilityMask, _mm256_castps_si256( positiveTime ) );
    const uint32_t activeBits = static_cast<uint32_t>( _mm256_movemask_ps( _mm256_castsi256_ps( activeMask ) ) );
    if ( activeBits == 0u )
    {
        return 0u;
    }

    const __m256 velocityX = LoadSimplifyStore( hotFields.linearVelocityX, bodyBegin, activeMask );
    const __m256 velocityY = LoadSimplifyStore( hotFields.linearVelocityY, bodyBegin, activeMask );
    const __m256 velocityZ = LoadSimplifyStore( hotFields.linearVelocityZ, bodyBegin, activeMask );
    (void)LoadSimplifyStore( hotFields.angularVelocityX, bodyBegin, activeMask );
    (void)LoadSimplifyStore( hotFields.angularVelocityY, bodyBegin, activeMask );
    (void)LoadSimplifyStore( hotFields.angularVelocityZ, bodyBegin, activeMask );

    // Hazard: these are explicit FMA operations despite the global
    // fp_contract(off) pin. Their single-rounding divergence is certified by
    // S4-S7 A/B evidence and must never leak into the toggle-OFF path.
    float* const positionX = hotFields.positionX.data() + bodyBegin;
    float* const positionY = hotFields.positionY.data() + bodyBegin;
    float* const positionZ = hotFields.positionZ.data() + bodyBegin;
    const __m256 nextX = _mm256_fmadd_ps( velocityX, dt, _mm256_maskload_ps( positionX, activeMask ) );
    const __m256 nextY = _mm256_fmadd_ps( velocityY, dt, _mm256_maskload_ps( positionY, activeMask ) );
    const __m256 nextZ = _mm256_fmadd_ps( velocityZ, dt, _mm256_maskload_ps( positionZ, activeMask ) );
    _mm256_maskstore_ps( positionX, activeMask, nextX );
    _mm256_maskstore_ps( positionY, activeMask, nextY );
    _mm256_maskstore_ps( positionZ, activeMask, nextZ );
    return activeBits;
}
} // namespace Physics::Kernels
} // namespace SkullbonezCore
