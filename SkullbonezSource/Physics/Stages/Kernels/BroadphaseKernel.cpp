/*
File: SkullbonezSource/Physics/Stages/Kernels/BroadphaseKernel.cpp
Purpose:
  Implements eight-wide broadphase displacement and AABB preparation.

Summary:
  The kernel gathers padded collider radii, loads contiguous position and
  velocity components, classifies swept bodies, and writes conservative bounds
  for immediate model-order insertion by PhysicsBroadphaseStage.

Glossary:
  Conservative bounds: Bounds that may include extra space but may never omit
    a cell touched by the scalar broadphase policy.
  Activity mask: Valid body lanes in the current full or partial block.
  Swept mask: Dynamic lanes whose squared displacement exceeds squared radius.

Invariants:
  - Explicit FMA affects only enabled swept end positions and is covered by the
    S5 A/B oracle; toggle OFF retains scalar SpatialGrid inputs.
  - Fixed lanes use their start position even if stale velocity is non-zero.
  - Output arrays are stack-owned and consumed before the next block.

Related:
  - SkullbonezSource/Physics/Stages/Kernels/BroadphaseKernel.h
  - SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
  - Agentic/Plans/TODO/physics-soa-simd-1000-bodies.md
*/
#include "BroadphaseKernel.h"

#include <algorithm>
#include <cstdint>
#include <immintrin.h>

namespace SkullbonezCore
{
namespace Physics::Kernels
{
void BuildBroadphaseBoundsAvx2( const PhysicsBodyHotFieldsConstView& hotFields,
                                std::span<const ColliderRecord> colliderRecords,
                                int bodyBegin,
                                int bodyCount,
                                float deltaSeconds,
                                float contactSkin,
                                BroadphaseBoundsBlock& outBounds )
{
    outBounds = {};
    if ( bodyBegin < 0 || bodyBegin >= bodyCount )
    {
        return;
    }

    const int laneCount = (std::min)( BROADPHASE_LANE_COUNT, bodyCount - bodyBegin );
    alignas( 32 ) int32_t validRows[BROADPHASE_LANE_COUNT] = {};
    alignas( 32 ) int32_t dynamicRows[BROADPHASE_LANE_COUNT] = {};
    for ( int lane = 0; lane < laneCount; ++lane )
    {
        const std::size_t index = static_cast<std::size_t>( bodyBegin + lane );
        validRows[lane] = -1;
        dynamicRows[lane] = hotFields.fixed[index] == 0u ? -1 : 0;
        outBounds.radius[lane] = colliderRecords[index].boundingRadius + contactSkin;
    }

    const __m256i validMask = _mm256_load_si256( reinterpret_cast<const __m256i*>( validRows ) );
    const __m256i dynamicMask = _mm256_load_si256( reinterpret_cast<const __m256i*>( dynamicRows ) );
    const __m256 radius = _mm256_load_ps( outBounds.radius );
    const __m256 dt = _mm256_set1_ps( deltaSeconds );
    const __m256 positionX = _mm256_maskload_ps( hotFields.positionX.data() + bodyBegin, validMask );
    const __m256 positionY = _mm256_maskload_ps( hotFields.positionY.data() + bodyBegin, validMask );
    const __m256 positionZ = _mm256_maskload_ps( hotFields.positionZ.data() + bodyBegin, validMask );
    const __m256 displacementX =
        _mm256_mul_ps( _mm256_maskload_ps( hotFields.linearVelocityX.data() + bodyBegin, validMask ), dt );
    const __m256 displacementY =
        _mm256_mul_ps( _mm256_maskload_ps( hotFields.linearVelocityY.data() + bodyBegin, validMask ), dt );
    const __m256 displacementZ =
        _mm256_mul_ps( _mm256_maskload_ps( hotFields.linearVelocityZ.data() + bodyBegin, validMask ), dt );
    __m256 displacementSq = _mm256_mul_ps( displacementX, displacementX );
    displacementSq = _mm256_fmadd_ps( displacementY, displacementY, displacementSq );
    displacementSq = _mm256_fmadd_ps( displacementZ, displacementZ, displacementSq );
    const __m256 radiusSq = _mm256_mul_ps( radius, radius );
    const __m256i sweptMask =
        _mm256_and_si256( dynamicMask, _mm256_castps_si256( _mm256_cmp_ps( displacementSq, radiusSq, _CMP_GT_OQ ) ) );

    const __m256 endX = _mm256_blendv_ps( positionX,
                                          _mm256_fmadd_ps( displacementX, _mm256_set1_ps( 1.0f ), positionX ),
                                          _mm256_castsi256_ps( sweptMask ) );
    const __m256 endY = _mm256_blendv_ps( positionY,
                                          _mm256_fmadd_ps( displacementY, _mm256_set1_ps( 1.0f ), positionY ),
                                          _mm256_castsi256_ps( sweptMask ) );
    const __m256 endZ = _mm256_blendv_ps( positionZ,
                                          _mm256_fmadd_ps( displacementZ, _mm256_set1_ps( 1.0f ), positionZ ),
                                          _mm256_castsi256_ps( sweptMask ) );

    _mm256_store_ps( outBounds.minX, _mm256_sub_ps( _mm256_min_ps( positionX, endX ), radius ) );
    _mm256_store_ps( outBounds.minY, _mm256_sub_ps( _mm256_min_ps( positionY, endY ), radius ) );
    _mm256_store_ps( outBounds.minZ, _mm256_sub_ps( _mm256_min_ps( positionZ, endZ ), radius ) );
    _mm256_store_ps( outBounds.maxX, _mm256_add_ps( _mm256_max_ps( positionX, endX ), radius ) );
    _mm256_store_ps( outBounds.maxY, _mm256_add_ps( _mm256_max_ps( positionY, endY ), radius ) );
    _mm256_store_ps( outBounds.maxZ, _mm256_add_ps( _mm256_max_ps( positionZ, endZ ), radius ) );
    _mm256_store_ps( outBounds.displacementX, displacementX );
    _mm256_store_ps( outBounds.displacementY, displacementY );
    _mm256_store_ps( outBounds.displacementZ, displacementZ );
    outBounds.validBits = static_cast<uint32_t>( _mm256_movemask_ps( _mm256_castsi256_ps( validMask ) ) );
    outBounds.sweptBits = static_cast<uint32_t>( _mm256_movemask_ps( _mm256_castsi256_ps( sweptMask ) ) );
}
} // namespace Physics::Kernels
} // namespace SkullbonezCore
