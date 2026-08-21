/*
File: SkullbonezTests/TestDeterministicMath.cpp
Purpose:
  Pins the numerical and byte-level contracts of deterministic angle math.

Summary:
  These tests separate approximation quality from reproducibility: reference
  sweeps bound geometric error, while committed hexadecimal tables make any
  evaluation-order or toolchain bit change visible.

Glossary:
  Coefficient error: Largest absolute difference in either cosine or sine.
  Angular error: Smallest absolute angle between the estimated and reference
    unit-circle directions, measured in radians.

Invariants:
  - The measured fixed-step regression envelope is covered independently from
    the wider certified domain.
  - Every odd-pi range-reduction boundary in [-64*pi, 64*pi] is checked on,
    immediately below, and immediately above the boundary.
  - Bit-pattern oracles include axes, quadrants, signed zero, measured input,
    and both ends of the certified domain.

Related:
  - SkullbonezSource/Maths/DeterministicMath.h
  - SkullbonezSource/Maths/DeterministicMath.cpp
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Maths/DeterministicMath.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>


using SkullbonezCore::Math::Deterministic::Atan2;
using SkullbonezCore::Math::Deterministic::ComputeCosSin;
using SkullbonezCore::Math::Deterministic::CosSin;
using SkullbonezCore::Math::Deterministic::MAX_COS_SIN_INPUT;
using SkullbonezCore::Math::Deterministic::PI;

namespace
{
constexpr float MEASURED_REGRESSION_MAXIMUM = 504.2644f / 120.0f;
constexpr float MAX_COEFFICIENT_ERROR = 0.00167f;
constexpr float MAX_COS_SIN_ANGULAR_ERROR = 0.00171f;
constexpr float MAX_ATAN2_ANGULAR_ERROR = 0.000028f;

float FromBits( uint32_t bits )
{
    return std::bit_cast<float>( bits );
}

float AngularError( float actual, float expected )
{
    float error = fabsf( actual - expected );
    if ( error > PI )
    {
        error = 2.0f * PI - error;
    }
    return error;
}

struct CosSinError
{
    float coefficient = 0.0f;
    float angular = 0.0f;
    float coefficientInput = 0.0f;
    float angularInput = 0.0f;
};

void IncludeCosSinError( CosSinError& maximum, float angle )
{
    const CosSin actual = ComputeCosSin( angle );
    const float referenceCosine = cosf( angle );
    const float referenceSine = sinf( angle );
    const float cosineError = fabsf( actual.cosine - referenceCosine );
    const float sineError = fabsf( actual.sine - referenceSine );
    const float coefficientError = cosineError > sineError ? cosineError : sineError;
    if ( coefficientError > maximum.coefficient )
    {
        maximum.coefficient = coefficientError;
        maximum.coefficientInput = angle;
    }

    const float actualAngle = atan2f( actual.sine, actual.cosine );
    const float referenceAngle = atan2f( referenceSine, referenceCosine );
    const float angularError = AngularError( actualAngle, referenceAngle );
    if ( angularError > maximum.angular )
    {
        maximum.angular = angularError;
        maximum.angularInput = angle;
    }
}

CosSinError MeasureCosSinError( float minimum, float maximum, int intervals )
{
    CosSinError error;
    for ( int i = 0; i <= intervals; ++i )
    {
        const float alpha = static_cast<float>( i ) / static_cast<float>( intervals );
        IncludeCosSinError( error, minimum + ( maximum - minimum ) * alpha );
    }
    return error;
}
} // namespace


TEST_CASE( "Deterministic math: ComputeCosSin output bits are frozen" )
{
    struct OracleRow
    {
        uint32_t input;
        uint32_t cosine;
        uint32_t sine;
    };

    static constexpr OracleRow ORACLE[] = {
        { 0x00000000u, 0x3F800000u, 0x00000000u }, { 0x80000000u, 0x3F800000u, 0x80000000u },
        { 0x38D1B717u, 0x3F800000u, 0x38D59CDFu }, { 0xB8D1B717u, 0x3F800000u, 0xB8D564C2u },
        { 0x3D87E6E1u, 0x3F7F6B88u, 0x3D89C759u }, { 0xBD87E6E1u, 0x3F7F6B88u, 0xBD89C758u },
        { 0x40867873u, 0xBEFA4FAAu, 0xBF5F520Fu }, { 0x3FC90FDBu, 0x00000000u, 0x3F800000u },
        { 0xBFC90FDBu, 0x00000000u, 0xBF800000u }, { 0x40490FDBu, 0xBF800000u, 0x00000000u },
        { 0xC0490FDBu, 0xBF800000u, 0x80000000u }, { 0x40C90FDBu, 0x3F800000u, 0x00000000u },
        { 0xC0C90FDBu, 0x3F800000u, 0x00000000u }, { 0x43490FDBu, 0x3F800000u, 0xB83F7EA3u },
        { 0xC3490FDBu, 0x3F800000u, 0x383F7EA3u },
    };

    for ( const OracleRow row : ORACLE )
    {
        CAPTURE( row.input );
        const CosSin result = ComputeCosSin( FromBits( row.input ) );
        CHECK( std::bit_cast<uint32_t>( result.cosine ) == row.cosine );
        CHECK( std::bit_cast<uint32_t>( result.sine ) == row.sine );
    }
}


TEST_CASE( "Deterministic math: ComputeCosSin error is bounded over the measured regression envelope" )
{
    const CosSinError error = MeasureCosSinError( -MEASURED_REGRESSION_MAXIMUM, MEASURED_REGRESSION_MAXIMUM, 8192 );
    CAPTURE( error.coefficientInput );
    CAPTURE( error.coefficient );
    CAPTURE( error.angularInput );
    CAPTURE( error.angular );
    CHECK( error.coefficient <= MAX_COEFFICIENT_ERROR );
    CHECK( error.angular <= MAX_COS_SIN_ANGULAR_ERROR );
}


TEST_CASE( "Deterministic math: ComputeCosSin covers every certified range-reduction boundary" )
{
    struct BoundaryRow
    {
        uint32_t lower;
        uint32_t upper;
    };

    // Invariant: repeated binary32 subtraction moves high-turn transitions away from the
    // nominal odd-pi float. These are the actual adjacent transition pairs,
    // ordered from -63*pi through +63*pi, and are part of the byte contract.
    static constexpr BoundaryRow BOUNDARIES[] = {
        { 0xC345EB9Fu, 0xC345EB9Eu }, { 0xC33FA320u, 0xC33FA31Fu }, { 0xC3395AA1u, 0xC3395AA0u },
        { 0xC3331222u, 0xC3331221u }, { 0xC32CC9A3u, 0xC32CC9A2u }, { 0xC3268124u, 0xC3268123u },
        { 0xC32038A5u, 0xC32038A4u }, { 0xC319F026u, 0xC319F025u }, { 0xC313A7A7u, 0xC313A7A6u },
        { 0xC30D5F28u, 0xC30D5F27u }, { 0xC30716A9u, 0xC30716A8u }, { 0xC300CE2Au, 0xC300CE29u },
        { 0xC2F50B56u, 0xC2F50B55u }, { 0xC2E87A58u, 0xC2E87A57u }, { 0xC2DBE95Au, 0xC2DBE959u },
        { 0xC2CF585Cu, 0xC2CF585Bu }, { 0xC2C2C75Eu, 0xC2C2C75Du }, { 0xC2B63660u, 0xC2B6365Fu },
        { 0xC2A9A562u, 0xC2A9A561u }, { 0xC29D1464u, 0xC29D1463u }, { 0xC2908366u, 0xC2908365u },
        { 0xC283F268u, 0xC283F267u }, { 0xC26EC2D4u, 0xC26EC2D3u }, { 0xC255A0D9u, 0xC255A0D8u },
        { 0xC23C7EDEu, 0xC23C7EDDu }, { 0xC2235CE3u, 0xC2235CE2u }, { 0xC20A3AE8u, 0xC20A3AE7u },
        { 0xC1E231D8u, 0xC1E231D7u }, { 0xC1AFEDE1u, 0xC1AFEDE0u }, { 0xC17B53D3u, 0xC17B53D2u },
        { 0xC116CBE5u, 0xC116CBE4u }, { 0xC0490FDCu, 0xC0490FDBu }, { 0x40490FDBu, 0x40490FDCu },
        { 0x4116CBE4u, 0x4116CBE5u }, { 0x417B53D2u, 0x417B53D3u }, { 0x41AFEDE0u, 0x41AFEDE1u },
        { 0x41E231D7u, 0x41E231D8u }, { 0x420A3AE7u, 0x420A3AE8u }, { 0x42235CE2u, 0x42235CE3u },
        { 0x423C7EDDu, 0x423C7EDEu }, { 0x4255A0D8u, 0x4255A0D9u }, { 0x426EC2D3u, 0x426EC2D4u },
        { 0x4283F267u, 0x4283F268u }, { 0x42908365u, 0x42908366u }, { 0x429D1463u, 0x429D1464u },
        { 0x42A9A561u, 0x42A9A562u }, { 0x42B6365Fu, 0x42B63660u }, { 0x42C2C75Du, 0x42C2C75Eu },
        { 0x42CF585Bu, 0x42CF585Cu }, { 0x42DBE959u, 0x42DBE95Au }, { 0x42E87A57u, 0x42E87A58u },
        { 0x42F50B55u, 0x42F50B56u }, { 0x4300CE29u, 0x4300CE2Au }, { 0x430716A8u, 0x430716A9u },
        { 0x430D5F27u, 0x430D5F28u }, { 0x4313A7A6u, 0x4313A7A7u }, { 0x4319F025u, 0x4319F026u },
        { 0x432038A4u, 0x432038A5u }, { 0x43268123u, 0x43268124u }, { 0x432CC9A2u, 0x432CC9A3u },
        { 0x43331221u, 0x43331222u }, { 0x43395AA0u, 0x43395AA1u }, { 0x433FA31Fu, 0x433FA320u },
        { 0x4345EB9Eu, 0x4345EB9Fu },
    };

    CosSinError error = MeasureCosSinError( -MAX_COS_SIN_INPUT, MAX_COS_SIN_INPUT, 131072 );

    for ( const BoundaryRow row : BOUNDARIES )
    {
        const float lower = FromBits( row.lower );
        const float upper = FromBits( row.upper );
        CAPTURE( row.lower );
        CAPTURE( row.upper );
        CHECK( std::nextafter( lower, std::numeric_limits<float>::infinity() ) == upper );
        CHECK_FALSE( std::signbit( ComputeCosSin( lower ).sine ) );
        CHECK( std::signbit( ComputeCosSin( upper ).sine ) );

        IncludeCosSinError( error, std::nextafter( lower, -std::numeric_limits<float>::infinity() ) );
        IncludeCosSinError( error, lower );
        IncludeCosSinError( error, upper );
        IncludeCosSinError( error, std::nextafter( upper, std::numeric_limits<float>::infinity() ) );
    }

    CAPTURE( error.coefficientInput );
    CAPTURE( error.coefficient );
    CAPTURE( error.angularInput );
    CAPTURE( error.angular );
    CHECK( error.coefficient <= MAX_COEFFICIENT_ERROR );
    CHECK( error.angular <= MAX_COS_SIN_ANGULAR_ERROR );
}


TEST_CASE( "Deterministic math: Atan2 output bits pin quadrants axes and signed zero" )
{
    struct OracleRow
    {
        uint32_t y;
        uint32_t x;
        uint32_t result;
    };

    static constexpr OracleRow ORACLE[] = {
        { 0x00000000u, 0x00000000u, 0x00000000u }, { 0x80000000u, 0x00000000u, 0x00000000u },
        { 0x00000000u, 0x80000000u, 0x00000000u }, { 0x80000000u, 0x80000000u, 0x00000000u },
        { 0x00000000u, 0x3F800000u, 0x00000000u }, { 0x80000000u, 0x3F800000u, 0x00000000u },
        { 0x00000000u, 0xBF800000u, 0x40490FDBu }, { 0x80000000u, 0xBF800000u, 0x40490FDBu },
        { 0x3F800000u, 0x00000000u, 0x3FC90FDBu }, { 0xBF800000u, 0x00000000u, 0xBFC90FDBu },
        { 0x3F800000u, 0x3F800000u, 0x3F4911AAu }, { 0x3F800000u, 0xBF800000u, 0x4016CB70u },
        { 0xBF800000u, 0xBF800000u, 0xC016CB70u }, { 0xBF800000u, 0x3F800000u, 0xBF4911AAu },
        { 0x358637BDu, 0x3F800000u, 0x358637BDu }, { 0xB58637BDu, 0x3F800000u, 0xB58637BDu },
        { 0x358637BDu, 0xBF800000u, 0x40490FD7u }, { 0xB58637BDu, 0xBF800000u, 0xC0490FD7u },
        { 0x3F800000u, 0x358637BDu, 0x3FC90FD3u }, { 0x3F800000u, 0xB58637BDu, 0x3FC90FE3u },
    };

    for ( const OracleRow row : ORACLE )
    {
        CAPTURE( row.y );
        CAPTURE( row.x );
        CHECK( std::bit_cast<uint32_t>( Atan2( FromBits( row.y ), FromBits( row.x ) ) ) == row.result );
    }
}


TEST_CASE( "Deterministic math: Atan2 angular error is bounded around the full circle" )
{
    float maximumError = 0.0f;
    float maximumInput = 0.0f;
    constexpr int INTERVALS = 131072;
    for ( int i = 0; i <= INTERVALS; ++i )
    {
        const float alpha = static_cast<float>( i ) / static_cast<float>( INTERVALS );
        const float angle = -PI + 2.0f * PI * alpha;
        const float y = sinf( angle );
        const float x = cosf( angle );
        const float reference = atan2f( y, x );
        const float error = AngularError( Atan2( y, x ), reference );
        if ( error > maximumError )
        {
            maximumError = error;
            maximumInput = angle;
        }
    }

    CAPTURE( maximumInput );
    CAPTURE( maximumError );
    CHECK( maximumError <= MAX_ATAN2_ANGULAR_ERROR );
}


TEST_CASE( "Deterministic math: Atan2 resolves near-parallel and anti-parallel vectors" )
{
    constexpr float NEAR_PARALLEL_Y = 0.000001f;

    CHECK( Atan2( NEAR_PARALLEL_Y, 1.0f ) == doctest::Approx( NEAR_PARALLEL_Y ).epsilon( 0.000001f ) );
    CHECK( Atan2( -NEAR_PARALLEL_Y, 1.0f ) == doctest::Approx( -NEAR_PARALLEL_Y ).epsilon( 0.000001f ) );
    CHECK( Atan2( NEAR_PARALLEL_Y, -1.0f ) == doctest::Approx( PI - NEAR_PARALLEL_Y ).epsilon( 0.000001f ) );
    CHECK( Atan2( -NEAR_PARALLEL_Y, -1.0f ) == doctest::Approx( -PI + NEAR_PARALLEL_Y ).epsilon( 0.000001f ) );
}
