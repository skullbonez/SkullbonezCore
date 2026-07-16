/*
File: SkullbonezSource/Physics/Stages/Kernels/NarrowphasePruneKernel.cpp
Purpose:
  Implements eight-wide conservative swept-sphere candidate rejection.

Summary:
  The kernel gathers pair endpoints into aligned lane arrays, computes the
  closest point on each relative-motion segment, and returns the lanes whose
  conservative bounding spheres can overlap during this fixed tick.

Glossary:
  Relative segment: Body A's center motion expressed relative to body B.
  Closest parameter: Segment coordinate clamped to `[0,1]`.
  Lane mask: Bit set for each valid gathered candidate.

Invariants:
  - FMA is confined to the enabled dark path in this dedicated translation unit.
  - Invalid/non-finite radii remain accepted exactly like the scalar oracle.
  - No horizontal reduction changes pair order or couples independent lanes.

Related:
  - SkullbonezSource/Physics/Stages/Kernels/NarrowphasePruneKernel.h
  - SkullbonezSource/Physics/SpatialGrid.cpp
  - Agentic/Plans/TODO/physics-soa-simd-1000-bodies.md
*/
#include "NarrowphasePruneKernel.h"

#include "../../../Maths/MathsCommon.h"

#include <algorithm>
#include <cmath>
#include <immintrin.h>

namespace SkullbonezCore
{
namespace Physics::Kernels
{
uint32_t PruneNarrowphasePairsAvx2( const PhysicsBodyHotFieldsConstView& hotFields,
                                    std::span<const ColliderRecord> colliderRecords,
                                    std::span<const std::pair<int, int>> pairs,
                                    int modelCount,
                                    float deltaSeconds,
                                    float contactSkin )
{
    const int laneCount = (std::min)( NARROWPHASE_PRUNE_LANE_COUNT, static_cast<int>( pairs.size() ) );
    alignas( 32 ) int32_t activeRows[NARROWPHASE_PRUNE_LANE_COUNT] = {};
    alignas( 32 ) int32_t forcedAcceptRows[NARROWPHASE_PRUNE_LANE_COUNT] = {};
    alignas( 32 ) float startX[NARROWPHASE_PRUNE_LANE_COUNT] = {};
    alignas( 32 ) float startY[NARROWPHASE_PRUNE_LANE_COUNT] = {};
    alignas( 32 ) float startZ[NARROWPHASE_PRUNE_LANE_COUNT] = {};
    alignas( 32 ) float moveX[NARROWPHASE_PRUNE_LANE_COUNT] = {};
    alignas( 32 ) float moveY[NARROWPHASE_PRUNE_LANE_COUNT] = {};
    alignas( 32 ) float moveZ[NARROWPHASE_PRUNE_LANE_COUNT] = {};
    alignas( 32 ) float radius[NARROWPHASE_PRUNE_LANE_COUNT] = {};

    for ( int lane = 0; lane < laneCount; ++lane )
    {
        const int a = pairs[static_cast<std::size_t>( lane )].first;
        const int b = pairs[static_cast<std::size_t>( lane )].second;
        if ( a < 0 || b < 0 || a >= modelCount || b >= modelCount )
        {
            continue;
        }
        activeRows[lane] = -1;
        const std::size_t ai = static_cast<std::size_t>( a );
        const std::size_t bi = static_cast<std::size_t>( b );
        const float radiusA = colliderRecords[ai].boundingRadius;
        const float radiusB = colliderRecords[bi].boundingRadius;
        if ( !std::isfinite( radiusA ) || !std::isfinite( radiusB ) || radiusA < 0.0f || radiusB < 0.0f )
        {
            forcedAcceptRows[lane] = -1;
            continue;
        }
        startX[lane] = hotFields.positionX[ai] - hotFields.positionX[bi];
        startY[lane] = hotFields.positionY[ai] - hotFields.positionY[bi];
        startZ[lane] = hotFields.positionZ[ai] - hotFields.positionZ[bi];
        moveX[lane] = ( hotFields.linearVelocityX[ai] - hotFields.linearVelocityX[bi] ) * deltaSeconds;
        moveY[lane] = ( hotFields.linearVelocityY[ai] - hotFields.linearVelocityY[bi] ) * deltaSeconds;
        moveZ[lane] = ( hotFields.linearVelocityZ[ai] - hotFields.linearVelocityZ[bi] ) * deltaSeconds;
        radius[lane] = radiusA + radiusB + contactSkin;
    }

    const __m256 sx = _mm256_load_ps( startX );
    const __m256 sy = _mm256_load_ps( startY );
    const __m256 sz = _mm256_load_ps( startZ );
    const __m256 dx = _mm256_load_ps( moveX );
    const __m256 dy = _mm256_load_ps( moveY );
    const __m256 dz = _mm256_load_ps( moveZ );
    __m256 lengthSq = _mm256_fmadd_ps( dx, dx, _mm256_mul_ps( dy, dy ) );
    lengthSq = _mm256_fmadd_ps( dz, dz, lengthSq );
    __m256 dot = _mm256_fmadd_ps( sx, dx, _mm256_mul_ps( sy, dy ) );
    dot = _mm256_fmadd_ps( sz, dz, dot );
    const __m256 moving = _mm256_cmp_ps( lengthSq, _mm256_set1_ps( TOLERANCE * TOLERANCE ), _CMP_GT_OQ );
    __m256 t = _mm256_div_ps( _mm256_sub_ps( _mm256_setzero_ps(), dot ), lengthSq );
    t = _mm256_max_ps( _mm256_setzero_ps(), _mm256_min_ps( _mm256_set1_ps( 1.0f ), t ) );
    t = _mm256_and_ps( t, moving );
    const __m256 cx = _mm256_fmadd_ps( dx, t, sx );
    const __m256 cy = _mm256_fmadd_ps( dy, t, sy );
    const __m256 cz = _mm256_fmadd_ps( dz, t, sz );
    __m256 distanceSq = _mm256_fmadd_ps( cx, cx, _mm256_mul_ps( cy, cy ) );
    distanceSq = _mm256_fmadd_ps( cz, cz, distanceSq );
    const __m256 r = _mm256_load_ps( radius );
    const __m256 touches = _mm256_cmp_ps( distanceSq, _mm256_mul_ps( r, r ), _CMP_LE_OQ );
    const __m256i active = _mm256_load_si256( reinterpret_cast<const __m256i*>( activeRows ) );
    const __m256i forced = _mm256_load_si256( reinterpret_cast<const __m256i*>( forcedAcceptRows ) );
    const __m256 accepted =
        _mm256_and_ps( _mm256_castsi256_ps( active ), _mm256_or_ps( touches, _mm256_castsi256_ps( forced ) ) );
    return static_cast<uint32_t>( _mm256_movemask_ps( accepted ) );
}
} // namespace Physics::Kernels
} // namespace SkullbonezCore
