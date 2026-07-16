/*
File: SkullbonezSource/Physics/Stages/Kernels/ForceKernel.cpp
Purpose:
  Implements pinned AVX2/FMA body-gravity and mutual-pair arithmetic.

Summary:
  Gravity updates eight consecutive velocity-Y rows with one explicit FMA.
  Mutual gravity broadcasts body A, gathers eight cold masses, loads contiguous
  body-B positions, and writes independent pair-table values for later serial
  reduction by PhysicsForceStage.

Glossary:
  Broadcast: Replicating one body-A scalar into every vector lane.
  Pair table: Triangular scratch with one force vector per `(i,j)` pair.
  Masked tail: Final partial eight-pair block whose absent lanes never access
    body or scratch storage.

Invariants:
  - Explicit FMA is confined to the enabled path; toggle OFF uses no symbol in
    this translation unit.
  - Mutual-gravity receive policy changes only whether a pair is computed, not
    the stage's later addition/subtraction order.
  - Invalid or zero masses produce zero pair rows, matching scalar skips.

Related:
  - SkullbonezSource/Physics/Stages/Kernels/ForceKernel.h
  - SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
  - Agentic/Plans/TODO/physics-soa-simd-1000-bodies.md
*/
#include "ForceKernel.h"

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
__m256i LoadLaneMask( const int32_t ( &rows )[FORCE_LANE_COUNT] )
{
    return _mm256_load_si256( reinterpret_cast<const __m256i*>( rows ) );
}
} // namespace

uint32_t ApplyGravityAvx2( const PhysicsBodyHotFieldsView& hotFields,
                           std::span<const uint8_t> sleepState,
                           int bodyBegin,
                           int bodyCount,
                           float gravity,
                           float deltaSeconds )
{
    if ( bodyBegin < 0 || bodyBegin >= bodyCount || deltaSeconds <= 0.0f )
    {
        return 0u;
    }

    const int laneCount = (std::min)( FORCE_LANE_COUNT, bodyCount - bodyBegin );
    alignas( 32 ) int32_t completionRows[FORCE_LANE_COUNT] = {};
    alignas( 32 ) int32_t gravityRows[FORCE_LANE_COUNT] = {};
    for ( int lane = 0; lane < laneCount; ++lane )
    {
        const std::size_t index = static_cast<std::size_t>( bodyBegin + lane );
        const bool sleeping = index < sleepState.size() && sleepState[index] != 0u;
        const bool completes = hotFields.fixed[index] == 0u && !sleeping;
        completionRows[lane] = completes ? -1 : 0;
        gravityRows[lane] = completes && hotFields.inverseMass[index] > 0.0f ? -1 : 0;
    }

    const __m256i completionMask = LoadLaneMask( completionRows );
    const __m256i gravityMask = LoadLaneMask( gravityRows );
    const uint32_t completionBits =
        static_cast<uint32_t>( _mm256_movemask_ps( _mm256_castsi256_ps( completionMask ) ) );
    if ( _mm256_movemask_ps( _mm256_castsi256_ps( gravityMask ) ) != 0 )
    {
        float* const velocityY = hotFields.linearVelocityY.data() + bodyBegin;
        const __m256 previous = _mm256_maskload_ps( velocityY, gravityMask );
        const __m256 next = _mm256_fmadd_ps( _mm256_set1_ps( gravity ), _mm256_set1_ps( deltaSeconds ), previous );
        _mm256_maskstore_ps( velocityY, gravityMask, next );
    }
    return completionBits;
}

uint32_t BuildMutualGravityPairsAvx2( std::span<const PhysicsBodyRecord> bodyRecords,
                                      const PhysicsBodyHotFieldsConstView& hotFields,
                                      std::span<const uint8_t> sleepState,
                                      int bodyAIndex,
                                      int bodyBBegin,
                                      int bodyCount,
                                      float softenedDistanceSq,
                                      float gravitationalConstant,
                                      Math::Vector::Vector3* outPairForces )
{
    if ( bodyAIndex < 0 || bodyAIndex >= bodyCount || bodyBBegin <= bodyAIndex || bodyBBegin >= bodyCount ||
         !outPairForces )
    {
        return 0u;
    }

    const std::size_t a = static_cast<std::size_t>( bodyAIndex );
    const float massA = bodyRecords[a].mass;
    if ( massA <= TOLERANCE )
    {
        return 0u;
    }
    const bool bodyAReceives = hotFields.fixed[a] == 0u && hotFields.inverseMass[a] > 0.0f &&
                               ( a >= sleepState.size() || sleepState[a] == 0u );
    const int laneCount = (std::min)( FORCE_LANE_COUNT, bodyCount - bodyBBegin );
    alignas( 32 ) int32_t activeRows[FORCE_LANE_COUNT] = {};
    alignas( 32 ) float masses[FORCE_LANE_COUNT] = {};
    for ( int lane = 0; lane < laneCount; ++lane )
    {
        const std::size_t b = static_cast<std::size_t>( bodyBBegin + lane );
        const bool bodyBReceives = hotFields.fixed[b] == 0u && hotFields.inverseMass[b] > 0.0f &&
                                   ( b >= sleepState.size() || sleepState[b] == 0u );
        masses[lane] = bodyRecords[b].mass;
        activeRows[lane] = masses[lane] > TOLERANCE && ( bodyAReceives || bodyBReceives ) ? -1 : 0;
        outPairForces[lane] = Math::Vector::ZERO_VECTOR;
    }

    const __m256i activeMask = LoadLaneMask( activeRows );
    const uint32_t activeBits = static_cast<uint32_t>( _mm256_movemask_ps( _mm256_castsi256_ps( activeMask ) ) );
    if ( activeBits == 0u )
    {
        return 0u;
    }

    const __m256 positionAX = _mm256_set1_ps( hotFields.positionX[a] );
    const __m256 positionAY = _mm256_set1_ps( hotFields.positionY[a] );
    const __m256 positionAZ = _mm256_set1_ps( hotFields.positionZ[a] );
    const __m256 dx =
        _mm256_sub_ps( _mm256_maskload_ps( hotFields.positionX.data() + bodyBBegin, activeMask ), positionAX );
    const __m256 dy =
        _mm256_sub_ps( _mm256_maskload_ps( hotFields.positionY.data() + bodyBBegin, activeMask ), positionAY );
    const __m256 dz =
        _mm256_sub_ps( _mm256_maskload_ps( hotFields.positionZ.data() + bodyBBegin, activeMask ), positionAZ );
    __m256 distanceSq = _mm256_fmadd_ps( dx, dx, _mm256_set1_ps( softenedDistanceSq ) );
    distanceSq = _mm256_fmadd_ps( dy, dy, distanceSq );
    distanceSq = _mm256_fmadd_ps( dz, dz, distanceSq );
    const __m256 invDistance = _mm256_div_ps( _mm256_set1_ps( 1.0f ), _mm256_sqrt_ps( distanceSq ) );
    const __m256 invDistanceSquared = _mm256_mul_ps( invDistance, invDistance );
    const __m256 invDistanceCubed = _mm256_mul_ps( invDistanceSquared, invDistance );
    const __m256 massScale = _mm256_mul_ps( _mm256_set1_ps( gravitationalConstant * massA ), _mm256_load_ps( masses ) );
    const __m256 scale = _mm256_mul_ps( massScale, invDistanceCubed );

    alignas( 32 ) float forceX[FORCE_LANE_COUNT] = {};
    alignas( 32 ) float forceY[FORCE_LANE_COUNT] = {};
    alignas( 32 ) float forceZ[FORCE_LANE_COUNT] = {};
    _mm256_store_ps( forceX, _mm256_mul_ps( dx, scale ) );
    _mm256_store_ps( forceY, _mm256_mul_ps( dy, scale ) );
    _mm256_store_ps( forceZ, _mm256_mul_ps( dz, scale ) );
    for ( int lane = 0; lane < laneCount; ++lane )
    {
        if ( ( activeBits & ( 1u << lane ) ) != 0u )
        {
            outPairForces[lane] = Math::Vector::Vector3( forceX[lane], forceY[lane], forceZ[lane] );
        }
    }
    return activeBits;
}
} // namespace Physics::Kernels
} // namespace SkullbonezCore
