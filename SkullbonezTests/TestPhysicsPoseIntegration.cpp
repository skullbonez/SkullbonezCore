/*
File: TestPhysicsPoseIntegration.cpp
Purpose:
  Pins the deterministic scalar pose-integration contract used by Physics.

Summary:
  Focused rows separate the integration formula from terrain and solver stages.
  They prove the zero limit, the removed near-zero cutoff, exponential-map
  composition order, high-speed range, partial-time behavior, and normalization.

Glossary:
  Sub-threshold rotation: Angular magnitude below the retired 0.0001 rad/s
    branch cutoff but above component simplification tolerance.

Invariants:
  - Expected deltas use the same public deterministic angle owner as Physics.
  - Asymmetric starting orientation distinguishes left from right composition.
  - These tests pin candidate behavior; they do not authorize golden refreshes.

Related:
  - SkullbonezSource/Physics/PhysicsPoseIntegration.h
  - SkullbonezSource/Maths/DeterministicMath.h
  - Agentic/Reference/physics-overview.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Maths/DeterministicMath.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsPoseIntegration.h"

#include <bit>
#include <cmath>
#include <cstdint>


using SkullbonezCore::Math::Deterministic::ComputeCosSin;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::IntegrateBodyRecordPose;
using SkullbonezCore::Physics::PhysicsBodyHotState;

namespace
{
struct QuaternionComponents
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

QuaternionComponents ComponentsOf( const Quaternion& value )
{
    QuaternionComponents result;
    value.GetComponents( result.x, result.y, result.z, result.w );
    return result;
}

Quaternion ExpectedWorldDelta( const Vector3& omega, float deltaSeconds )
{
    const float magnitude = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
    const float halfAngle = 0.5f * magnitude * deltaSeconds;
    const auto cosSin = ComputeCosSin( halfAngle );
    const float sinc = halfAngle == 0.0f ? 1.0f : cosSin.sine / halfAngle;
    const float scale = 0.5f * deltaSeconds * sinc;
    return Quaternion( omega.x * scale, omega.y * scale, omega.z * scale, cosSin.cosine );
}

void CheckComponentsExact( const Quaternion& actual, const Quaternion& expected )
{
    const QuaternionComponents actualComponents = ComponentsOf( actual );
    const QuaternionComponents expectedComponents = ComponentsOf( expected );
    CHECK( std::bit_cast<uint32_t>( actualComponents.x ) == std::bit_cast<uint32_t>( expectedComponents.x ) );
    CHECK( std::bit_cast<uint32_t>( actualComponents.y ) == std::bit_cast<uint32_t>( expectedComponents.y ) );
    CHECK( std::bit_cast<uint32_t>( actualComponents.z ) == std::bit_cast<uint32_t>( expectedComponents.z ) );
    CHECK( std::bit_cast<uint32_t>( actualComponents.w ) == std::bit_cast<uint32_t>( expectedComponents.w ) );
}

void CheckUnitLength( const Quaternion& value )
{
    const QuaternionComponents components = ComponentsOf( value );
    const float magnitudeSquared = components.x * components.x + components.y * components.y + components.z * components.z +
                                   components.w * components.w;
    CHECK( std::isfinite( magnitudeSquared ) );
    CHECK( magnitudeSquared == doctest::Approx( 1.0f ).epsilon( 0.000001f ) );
}
} // namespace


TEST_CASE( "Physics pose integration: zero angular velocity has an exact sinc limit" )
{
    PhysicsBodyHotState hot;
    hot.position = Vector3( 1.0f, -2.0f, 3.0f );
    hot.linearVelocity = Vector3( 6.0f, 3.0f, -9.0f );

    IntegrateBodyRecordPose( hot, 1.0f / 120.0f );

    CHECK( hot.position.x == doctest::Approx( 1.05f ) );
    CHECK( hot.position.y == doctest::Approx( -1.975f ) );
    CHECK( hot.position.z == doctest::Approx( 2.925f ) );
    CheckComponentsExact( hot.orientation, Quaternion {} );
}


TEST_CASE( "Physics pose integration: sub-threshold angular velocity is no longer discarded" )
{
    PhysicsBodyHotState hot;
    hot.angularVelocity = Vector3( 0.000075f, 0.0f, 0.0f );

    IntegrateBodyRecordPose( hot, 1.0f / 120.0f );

    const Quaternion expected = ExpectedWorldDelta( hot.angularVelocity, 1.0f / 120.0f );
    CheckComponentsExact( hot.orientation, expected );
    CHECK( std::bit_cast<uint32_t>( ComponentsOf( hot.orientation ).x ) != 0u );
}


TEST_CASE( "Physics pose integration: ordinary rotation left-multiplies a world-space delta" )
{
    PhysicsBodyHotState hot;
    hot.orientation = Quaternion( 0.10101526f, -0.20203051f, 0.30304578f, 0.925379f );
    hot.orientation.Normalise();
    hot.angularVelocity = Vector3( 1.0f, 2.0f, -3.0f );
    const Quaternion before = hot.orientation;

    IntegrateBodyRecordPose( hot, 1.0f / 120.0f );

    Quaternion expected = ExpectedWorldDelta( hot.angularVelocity, 1.0f / 120.0f ) * before;
    expected.Normalise();
    CheckComponentsExact( hot.orientation, expected );

    Quaternion reversed = before * ExpectedWorldDelta( hot.angularVelocity, 1.0f / 120.0f );
    reversed.Normalise();
    const QuaternionComponents actual = ComponentsOf( hot.orientation );
    const QuaternionComponents wrongOrder = ComponentsOf( reversed );
    const bool orderIsObservable = std::bit_cast<uint32_t>( actual.x ) != std::bit_cast<uint32_t>( wrongOrder.x ) ||
                                   std::bit_cast<uint32_t>( actual.y ) != std::bit_cast<uint32_t>( wrongOrder.y ) ||
                                   std::bit_cast<uint32_t>( actual.z ) != std::bit_cast<uint32_t>( wrongOrder.z ) ||
                                   std::bit_cast<uint32_t>( actual.w ) != std::bit_cast<uint32_t>( wrongOrder.w );
    CHECK( orderIsObservable );
    CheckUnitLength( hot.orientation );
}


TEST_CASE( "Physics pose integration: measured high-speed rotation stays finite and normalized" )
{
    PhysicsBodyHotState hot;
    hot.angularVelocity = Vector3( 504.2644f, 0.0f, 0.0f );

    IntegrateBodyRecordPose( hot, 1.0f / 120.0f );

    Quaternion expected = ExpectedWorldDelta( hot.angularVelocity, 1.0f / 120.0f );
    expected.Normalise();
    CheckComponentsExact( hot.orientation, expected );
    CheckUnitLength( hot.orientation );
}


TEST_CASE( "Physics pose integration: partial-time rotation uses the supplied interval" )
{
    PhysicsBodyHotState hot;
    hot.angularVelocity = Vector3( 4.0f, -2.0f, 1.0f );
    constexpr float partialSeconds = 0.0025f;

    IntegrateBodyRecordPose( hot, partialSeconds );

    Quaternion expected = ExpectedWorldDelta( hot.angularVelocity, partialSeconds );
    expected.Normalise();
    CheckComponentsExact( hot.orientation, expected );
    CheckUnitLength( hot.orientation );
}
