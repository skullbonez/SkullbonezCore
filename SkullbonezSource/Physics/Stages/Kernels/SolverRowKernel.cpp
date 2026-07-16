/*
File: SkullbonezSource/Physics/Stages/Kernels/SolverRowKernel.cpp
Purpose:
  Implements eight-wide persistent-contact row preparation.

Summary:
  Each block builds deterministic tangent frames, gathers scalar inertia terms,
  and widens reciprocal effective-mass, anchor velocity, and penetration-bias
  arithmetic. Cache lookup, warm start application, and PGS remain scalar.

Glossary:
  Inertia term: Rotational resistance contributed by a contact anchor and axis.
  Anchor velocity: Contact-point velocity formed from linear and angular motion.
  PGS (Projected Gauss-Seidel): Ordered scalar iteration retained after setup.

Invariants:
  - Explicit FMA is confined to this default-OFF translation unit.
  - Gathered lanes are independent; there is no horizontal reduction.
  - Terrain body B contributes zero inverse mass, inertia, and velocity.

Related:
  - SkullbonezSource/Physics/Stages/Kernels/SolverRowKernel.h
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
  - Agentic/Plans/TODO/physics-soa-simd-1000-bodies.md
*/
#include "SolverRowKernel.h"

#include "../../ContactSolverCommon.h"
#include "../../../Maths/MathsCommon.h"

#include <algorithm>
#include <immintrin.h>

namespace SkullbonezCore
{
namespace Physics::Kernels
{
namespace
{
Math::Vector::Vector3 ApplyInverseInertia( const SolverBodyState& body, const Math::Vector::Vector3& value )
{
    if ( !body.useWorldInertia )
    {
        return Math::Vector::VectorMultiply( body.invInertia, value );
    }
    const Math::Vector::Vector3 local = body.orientation.TransposeMultiply( value );
    return body.orientation * Math::Vector::VectorMultiply( body.invInertia, local );
}

float EffectiveDenominator( const SolverBodyState& a,
                            const SolverBodyState& b,
                            const Math::Vector::Vector3& axis,
                            const Math::Vector::Vector3& rA,
                            const Math::Vector::Vector3& rB )
{
    const auto angular = [&]( const SolverBodyState& body, const Math::Vector::Vector3& r )
    {
        return axis *
               Math::Vector::CrossProduct( ApplyInverseInertia( body, Math::Vector::CrossProduct( r, axis ) ), r );
    };
    return a.invMass + b.invMass + angular( a, rA ) + angular( b, rB );
}
} // namespace

void PrepareSolverRowsAvx2( std::span<PersistentContact> contacts,
                            std::span<const SolverBodyState> bodies,
                            int rowBegin,
                            float inverseDeltaSeconds,
                            float contactSlop,
                            float baumgarteBeta,
                            float maxBaumgarteBias,
                            SolverRowPrepBlock& outBlock )
{
    outBlock = {};
    if ( rowBegin < 0 || rowBegin >= static_cast<int>( contacts.size() ) )
    {
        return;
    }
    const int laneCount = (std::min)( SOLVER_ROW_LANE_COUNT, static_cast<int>( contacts.size() ) - rowBegin );
    alignas( 32 ) int32_t validRows[SOLVER_ROW_LANE_COUNT] = {};
    alignas( 32 ) float denominatorN[SOLVER_ROW_LANE_COUNT] = {};
    alignas( 32 ) float denominatorT1[SOLVER_ROW_LANE_COUNT] = {};
    alignas( 32 ) float denominatorT2[SOLVER_ROW_LANE_COUNT] = {};
#define SB_ROW_ARRAYS( name )                                                                                          \
    alignas( 32 ) float name##X[SOLVER_ROW_LANE_COUNT] = {};                                                           \
    alignas( 32 ) float name##Y[SOLVER_ROW_LANE_COUNT] = {};                                                           \
    alignas( 32 ) float name##Z[SOLVER_ROW_LANE_COUNT] = {}
    SB_ROW_ARRAYS( linearA );
    SB_ROW_ARRAYS( linearB );
    SB_ROW_ARRAYS( angularA );
    SB_ROW_ARRAYS( angularB );
    SB_ROW_ARRAYS( rA );
    SB_ROW_ARRAYS( rB );
    SB_ROW_ARRAYS( normal );
#undef SB_ROW_ARRAYS
    alignas( 32 ) float penetration[SOLVER_ROW_LANE_COUNT] = {};

    const SolverBodyState terrainBody;
    for ( int lane = 0; lane < laneCount; ++lane )
    {
        PersistentContact& contact = contacts[static_cast<std::size_t>( rowBegin + lane )];
        if ( contact.bodyA < 0 || contact.bodyA >= static_cast<int>( bodies.size() ) ||
             ( !contact.isTerrain && ( contact.bodyB < 0 || contact.bodyB >= static_cast<int>( bodies.size() ) ) ) )
        {
            continue;
        }
        validRows[lane] = -1;
        ContactSolver::BuildContactTangents( contact.normal, contact.tangent1, contact.tangent2 );
        const SolverBodyState& a = bodies[static_cast<std::size_t>( contact.bodyA )];
        const SolverBodyState& b = contact.isTerrain ? terrainBody : bodies[static_cast<std::size_t>( contact.bodyB )];
        denominatorN[lane] = EffectiveDenominator( a, b, contact.normal, contact.rA, contact.rB );
        denominatorT1[lane] = EffectiveDenominator( a, b, contact.tangent1, contact.rA, contact.rB );
        denominatorT2[lane] = EffectiveDenominator( a, b, contact.tangent2, contact.rA, contact.rB );
#define SB_COPY_VECTOR( name, value )                                                                                  \
    name##X[lane] = ( value ).x;                                                                                       \
    name##Y[lane] = ( value ).y;                                                                                       \
    name##Z[lane] = ( value ).z
        SB_COPY_VECTOR( linearA, a.linearVelocity );
        SB_COPY_VECTOR( linearB, b.linearVelocity );
        SB_COPY_VECTOR( angularA, a.angularVelocity );
        SB_COPY_VECTOR( angularB, b.angularVelocity );
        SB_COPY_VECTOR( rA, contact.rA );
        SB_COPY_VECTOR( rB, contact.rB );
        SB_COPY_VECTOR( normal, contact.normal );
#undef SB_COPY_VECTOR
        penetration[lane] = contact.penetration;
    }

    const __m256 tolerance = _mm256_set1_ps( TOLERANCE );
    const auto reciprocalMass = [&]( const float* values )
    {
        const __m256 denominator = _mm256_load_ps( values );
        return _mm256_and_ps( _mm256_cmp_ps( denominator, tolerance, _CMP_GT_OQ ),
                              _mm256_div_ps( _mm256_set1_ps( 1.0f ), denominator ) );
    };
    alignas( 32 ) float massN[SOLVER_ROW_LANE_COUNT] = {};
    alignas( 32 ) float massT1[SOLVER_ROW_LANE_COUNT] = {};
    alignas( 32 ) float massT2[SOLVER_ROW_LANE_COUNT] = {};
    _mm256_store_ps( massN, reciprocalMass( denominatorN ) );
    _mm256_store_ps( massT1, reciprocalMass( denominatorT1 ) );
    _mm256_store_ps( massT2, reciprocalMass( denominatorT2 ) );

    const __m256 aVelX = _mm256_fmadd_ps(
        _mm256_load_ps( angularAY ),
        _mm256_load_ps( rAZ ),
        _mm256_fnmadd_ps( _mm256_load_ps( angularAZ ), _mm256_load_ps( rAY ), _mm256_load_ps( linearAX ) ) );
    const __m256 aVelY = _mm256_fmadd_ps(
        _mm256_load_ps( angularAZ ),
        _mm256_load_ps( rAX ),
        _mm256_fnmadd_ps( _mm256_load_ps( angularAX ), _mm256_load_ps( rAZ ), _mm256_load_ps( linearAY ) ) );
    const __m256 aVelZ = _mm256_fmadd_ps(
        _mm256_load_ps( angularAX ),
        _mm256_load_ps( rAY ),
        _mm256_fnmadd_ps( _mm256_load_ps( angularAY ), _mm256_load_ps( rAX ), _mm256_load_ps( linearAZ ) ) );
    const __m256 bVelX = _mm256_fmadd_ps(
        _mm256_load_ps( angularBY ),
        _mm256_load_ps( rBZ ),
        _mm256_fnmadd_ps( _mm256_load_ps( angularBZ ), _mm256_load_ps( rBY ), _mm256_load_ps( linearBX ) ) );
    const __m256 bVelY = _mm256_fmadd_ps(
        _mm256_load_ps( angularBZ ),
        _mm256_load_ps( rBX ),
        _mm256_fnmadd_ps( _mm256_load_ps( angularBX ), _mm256_load_ps( rBZ ), _mm256_load_ps( linearBY ) ) );
    const __m256 bVelZ = _mm256_fmadd_ps(
        _mm256_load_ps( angularBX ),
        _mm256_load_ps( rBY ),
        _mm256_fnmadd_ps( _mm256_load_ps( angularBY ), _mm256_load_ps( rBX ), _mm256_load_ps( linearBZ ) ) );
    __m256 normalSpeed = _mm256_mul_ps( _mm256_sub_ps( bVelX, aVelX ), _mm256_load_ps( normalX ) );
    normalSpeed = _mm256_fmadd_ps( _mm256_sub_ps( bVelY, aVelY ), _mm256_load_ps( normalY ), normalSpeed );
    normalSpeed = _mm256_fmadd_ps( _mm256_sub_ps( bVelZ, aVelZ ), _mm256_load_ps( normalZ ), normalSpeed );
    _mm256_store_ps( outBlock.normalSpeed, normalSpeed );

    const __m256 error = _mm256_max_ps( _mm256_setzero_ps(),
                                        _mm256_sub_ps( _mm256_load_ps( penetration ), _mm256_set1_ps( contactSlop ) ) );
    __m256 bias = _mm256_mul_ps( error, _mm256_set1_ps( baumgarteBeta * inverseDeltaSeconds ) );
    if ( maxBaumgarteBias > 0.0f )
    {
        bias = _mm256_min_ps( bias, _mm256_set1_ps( maxBaumgarteBias ) );
    }
    _mm256_store_ps( outBlock.penetrationBias, bias );

    for ( int lane = 0; lane < laneCount; ++lane )
    {
        if ( validRows[lane] == 0 )
        {
            continue;
        }
        PersistentContact& contact = contacts[static_cast<std::size_t>( rowBegin + lane )];
        contact.normalMass = massN[lane];
        contact.tangentMass1 = massT1[lane];
        contact.tangentMass2 = massT2[lane];
    }
    outBlock.validBits = static_cast<uint32_t>( _mm256_movemask_ps(
        _mm256_castsi256_ps( _mm256_load_si256( reinterpret_cast<const __m256i*>( validRows ) ) ) ) );
}
} // namespace Physics::Kernels
} // namespace SkullbonezCore
