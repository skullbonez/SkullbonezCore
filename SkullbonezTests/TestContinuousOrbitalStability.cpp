/*
File: TestContinuousOrbitalStability.cpp
Purpose:
  Proves the Planning-owned authored orbital stability contract.

Summary:
  Planted complete tick values exercise stable motion, both envelope directions,
  sustained escape reset/latch timing, blocking and auxiliary collisions,
  missed numerical failures, conservation drift, and bounded chronological ribbon
  publication without running the private forecast worker.

Invariants:
  - The 5-second escape grace is exactly 600 complete 120 Hz ticks.
  - Auxiliary orbital failures never end the primary/core horizon.
  - A latched failure remains first while later ticks continue to be observed.
  - Wrapped presentation ranges stay chronological, omit a seam chord, and end
    every configured body at one coherent newest absolute tick.

Related:
  - SkullbonezSource/Runtime/Planning/ContinuousOrbitalStability.h
  - SkullbonezSource/Scene/OrbitalStabilityContract.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Runtime/Planning/ContinuousOrbitalForecast.h"
#include "../SkullbonezSource/Runtime/Planning/ContinuousOrbitalStability.h"

#include <array>
#include <cmath>
#include <limits>

namespace
{
using SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::MakePhysicsSceneObjectId;
using SkullbonezCore::Runtime::ContinuousOrbitalBodySample;
using SkullbonezCore::Runtime::ContinuousOrbitalContactSample;
using SkullbonezCore::Runtime::ContinuousOrbitalInstabilityCause;
using SkullbonezCore::Runtime::ContinuousOrbitalPresentation;
using SkullbonezCore::Runtime::ContinuousOrbitalPresentationMember;
using SkullbonezCore::Runtime::ContinuousOrbitalStabilityAnalyzer;
using SkullbonezCore::Runtime::ContinuousOrbitalTickInput;
using SkullbonezCore::Runtime::ContinuousPredictionSampleRing;
using SkullbonezCore::Scene::OrbitalStabilityContract;
using SkullbonezCore::Scene::OrbitalStabilityMemberContract;
using SkullbonezCore::Scene::OrbitalStabilityMemberRole;

constexpr std::uint32_t SUN_ID = 7101u;
constexpr std::uint32_t EARTH_ID = 7102u;
constexpr std::uint32_t MARS_ID = 7103u;
constexpr std::uint32_t SHIP_ID = 7104u;

OrbitalStabilityMemberContract Member( std::uint32_t id, OrbitalStabilityMemberRole role, double inner = 0.0,
                                       double outer = 0.0, double escape = 0.0 )
{
    OrbitalStabilityMemberContract member;
    member.sceneObjectId = MakePhysicsSceneObjectId( id );
    member.role = role;
    member.innerRadius = inner;
    member.outerRadius = outer;
    member.escapeStartRadius = escape;
    return member;
}

OrbitalStabilityContract SolarContract()
{
    OrbitalStabilityContract contract;
    contract.enabled = true;
    contract.escapeGraceSeconds = 5.0;
    contract.memberCount = 4u;
    contract.members[0] = Member( SUN_ID, OrbitalStabilityMemberRole::Primary );
    contract.members[1] = Member( EARTH_ID, OrbitalStabilityMemberRole::CoreOrbiter, 60.0, 100.0, 90.0 );
    contract.members[2] = Member( MARS_ID, OrbitalStabilityMemberRole::CoreOrbiter, 90.0, 155.0, 140.0 );
    contract.members[3] = Member( SHIP_ID, OrbitalStabilityMemberRole::Auxiliary, 60.0, 100.0, 90.0 );
    return contract;
}

ContinuousOrbitalBodySample Body( std::uint32_t id, Vector3 position, Vector3 velocity, double mass )
{
    ContinuousOrbitalBodySample body;
    body.sceneObjectId = MakePhysicsSceneObjectId( id );
    body.position = position;
    body.orientation = IDENTITY_QUATERNION;
    body.linearVelocity = velocity;
    body.angularVelocity = Vector3( 0.0f, 0.0f, 0.0f );
    body.mass = mass;
    return body;
}

std::array<ContinuousOrbitalBodySample, 4> StableBodies()
{
    return { Body( SUN_ID, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 40000.0 ),
             Body( EARTH_ID, Vector3( 80.0f, 0.0f, 0.0f ), Vector3( 0.0f, 22.360679775f, 0.0f ), 20.0 ),
             Body( MARS_ID, Vector3( 120.0f, 0.0f, 0.0f ), Vector3( 0.0f, 18.25741858f, 0.0f ), 15.0 ),
             Body( SHIP_ID, Vector3( 80.0f, 4.5f, 0.0f ), Vector3( 0.0f, 22.360679775f, 0.0f ), 0.5 ) };
}

bool Observe( ContinuousOrbitalStabilityAnalyzer& analyzer, std::uint64_t tick,
              const std::array<ContinuousOrbitalBodySample, 4>& bodies,
              std::span<const ContinuousOrbitalContactSample> contacts = {} )
{
    return analyzer.ObserveTick( ContinuousOrbitalTickInput { bodies, contacts, tick, true, true } );
}
} // namespace

TEST_CASE( "Continuous orbital stability: finite stable orbit advances diagnostics" )
{
    ContinuousOrbitalStabilityAnalyzer analyzer;
    auto bodies = StableBodies();
    REQUIRE( analyzer.Begin( SolarContract(), 1.0, 0.5, bodies ) );
    REQUIRE( Observe( analyzer, 1u, bodies ) );
    auto view = analyzer.View();
    CHECK( view.configured );
    CHECK( view.numericalHealthy );
    CHECK( view.systemOrbitalHealthy );
    CHECK( view.auxiliaryOrbitalHealthy );
    CHECK_FALSE( view.firstBlockingFailure.latched );
    CHECK( view.observedThroughTick == 1u );
    CHECK( view.conservation.energyDriftAvailable );
    CHECK( view.conservation.angularMomentumDriftAvailable );
    CHECK( view.conservation.energyDrift == doctest::Approx( 0.0 ) );
    CHECK( view.conservation.angularMomentumDrift == doctest::Approx( 0.0 ) );

    bodies[1].linearVelocity.y += 0.01f;
    REQUIRE( Observe( analyzer, 2u, bodies ) );
    view = analyzer.View();
    CHECK( std::abs( view.conservation.energyDrift ) > 0.0 );
    CHECK( view.conservation.maximumAbsoluteEnergyDrift == doctest::Approx( std::abs( view.conservation.energyDrift ) ) );
    CHECK( view.conservation.angularMomentumDrift > 0.0 );
    CHECK( view.conservation.maximumAngularMomentumDrift == doctest::Approx( view.conservation.angularMomentumDrift ) );
}

TEST_CASE( "Continuous orbital stability: radial envelope latches first core failure while observation continues" )
{
    ContinuousOrbitalStabilityAnalyzer analyzer;
    auto bodies = StableBodies();
    REQUIRE( analyzer.Begin( SolarContract(), 1.0, 0.5, bodies ) );
    bodies[1].position.x = 100.5f;
    REQUIRE( Observe( analyzer, 1u, bodies ) );
    auto view = analyzer.View();
    REQUIRE( view.firstBlockingFailure.latched );
    CHECK( view.firstBlockingFailure.cause == ContinuousOrbitalInstabilityCause::OuterEnvelope );
    CHECK( view.firstBlockingFailure.subject.value == EARTH_ID );
    CHECK( view.firstBlockingFailure.absoluteTick == 1u );
    CHECK( view.numericalHealthy );

    bodies = StableBodies();
    REQUIRE( Observe( analyzer, 2u, bodies ) );
    view = analyzer.View();
    CHECK( view.observedThroughTick == 2u );
    CHECK( view.firstBlockingFailure.absoluteTick == 1u );
    CHECK( view.firstBlockingFailure.cause == ContinuousOrbitalInstabilityCause::OuterEnvelope );
}

TEST_CASE( "Continuous orbital stability: blocking set includes primary contacts" )
{
    ContinuousOrbitalStabilityAnalyzer analyzer;
    const auto bodies = StableBodies();
    const std::array contacts = {
        ContinuousOrbitalContactSample { MakePhysicsSceneObjectId( SUN_ID ), MakePhysicsSceneObjectId( EARTH_ID ) },
    };
    REQUIRE( analyzer.Begin( SolarContract(), 1.0, 0.5, bodies ) );
    REQUIRE( Observe( analyzer, 1u, bodies, contacts ) );
    const auto view = analyzer.View();
    REQUIRE( view.firstBlockingFailure.latched );
    CHECK( view.firstBlockingFailure.cause == ContinuousOrbitalInstabilityCause::Collision );
    CHECK_FALSE( view.systemOrbitalHealthy );
    CHECK( view.numericalHealthy );
}

TEST_CASE( "Continuous orbital stability: escape must remain continuous for exactly 600 ticks" )
{
    ContinuousOrbitalStabilityAnalyzer analyzer;
    const auto seed = StableBodies();
    auto bodies = seed;
    REQUIRE( analyzer.Begin( SolarContract(), 1.0, 0.5, seed ) );
    bodies[1].position = Vector3( 95.0f, 0.0f, 0.0f );
    bodies[1].linearVelocity = Vector3( 30.0f, 0.0f, 0.0f );

    for ( std::uint64_t tick = 1u; tick <= 300u; ++tick )
    {
        REQUIRE( Observe( analyzer, tick, bodies ) );
    }

    auto transient = bodies;
    transient[1].linearVelocity = Vector3( -30.0f, 0.0f, 0.0f );
    REQUIRE( Observe( analyzer, 301u, transient ) );

    for ( std::uint64_t tick = 302u; tick <= 900u; ++tick )
    {
        REQUIRE( Observe( analyzer, tick, bodies ) );
    }

    CHECK_FALSE( analyzer.View().firstBlockingFailure.latched );
    REQUIRE( Observe( analyzer, 901u, bodies ) );
    const auto view = analyzer.View();
    REQUIRE( view.firstBlockingFailure.latched );
    CHECK( view.firstBlockingFailure.cause == ContinuousOrbitalInstabilityCause::SustainedEscape );
    CHECK( view.firstBlockingFailure.absoluteTick == 901u );
}

TEST_CASE( "Continuous orbital stability: invalid auxiliary numeric state blocks globally" )
{
    ContinuousOrbitalStabilityAnalyzer analyzer;
    auto bodies = StableBodies();
    REQUIRE( analyzer.Begin( SolarContract(), 1.0, 0.5, bodies ) );
    bodies[3].position.x = ( std::numeric_limits<float>::quiet_NaN )();
    CHECK_FALSE( Observe( analyzer, 1u, bodies ) );
    const auto view = analyzer.View();
    REQUIRE( view.firstBlockingFailure.latched );
    CHECK( view.firstBlockingFailure.cause == ContinuousOrbitalInstabilityCause::NonFiniteState );
    CHECK_FALSE( view.numericalHealthy );
    CHECK_FALSE( view.systemOrbitalHealthy );
}

TEST_CASE( "Continuous orbital stability: auxiliary orbital failure stays nonblocking" )
{
    ContinuousOrbitalStabilityAnalyzer analyzer;
    auto bodies = StableBodies();
    REQUIRE( analyzer.Begin( SolarContract(), 1.0, 0.5, bodies ) );
    bodies[3].position = Vector3( 100.5f, 0.0f, 0.0f );
    REQUIRE( Observe( analyzer, 1u, bodies ) );
    auto view = analyzer.View();
    REQUIRE( view.firstAuxiliaryFailure.latched );
    CHECK( view.firstAuxiliaryFailure.cause == ContinuousOrbitalInstabilityCause::OuterEnvelope );
    CHECK_FALSE( view.firstBlockingFailure.latched );
    CHECK( view.systemOrbitalHealthy );
    CHECK( view.numericalHealthy );

    bodies = StableBodies();
    const std::array contacts = {
        ContinuousOrbitalContactSample { MakePhysicsSceneObjectId( SHIP_ID ), MakePhysicsSceneObjectId( EARTH_ID ) },
    };
    REQUIRE( Observe( analyzer, 2u, bodies, contacts ) );
    view = analyzer.View();
    CHECK_FALSE( view.firstBlockingFailure.latched );
    CHECK( view.firstAuxiliaryFailure.cause == ContinuousOrbitalInstabilityCause::OuterEnvelope );
}

TEST_CASE( "Continuous orbital stability: private-step and publication failures are distinct blockers" )
{
    const auto bodies = StableBodies();
    ContinuousOrbitalStabilityAnalyzer stepAnalyzer;
    REQUIRE( stepAnalyzer.Begin( SolarContract(), 1.0, 0.5, bodies ) );
    REQUIRE( stepAnalyzer.ObserveTick( ContinuousOrbitalTickInput { bodies, {}, 1u, false, true } ) );
    CHECK( stepAnalyzer.View().firstBlockingFailure.cause == ContinuousOrbitalInstabilityCause::PrivateStepFailure );

    ContinuousOrbitalStabilityAnalyzer publicationAnalyzer;
    REQUIRE( publicationAnalyzer.Begin( SolarContract(), 1.0, 0.5, bodies ) );
    CHECK_FALSE( publicationAnalyzer.ObserveTick( ContinuousOrbitalTickInput { bodies, {}, 1u, true, false } ) );
    CHECK( publicationAnalyzer.View().firstBlockingFailure.cause == ContinuousOrbitalInstabilityCause::InvalidPublication );
}

TEST_CASE( "Continuous orbital stability: failed seed cannot admit later observations" )
{
    const auto bodies = StableBodies();
    OrbitalStabilityContract invalid = SolarContract();
    invalid.members[1].role = static_cast<OrbitalStabilityMemberRole>( 255u );

    ContinuousOrbitalStabilityAnalyzer analyzer;
    CHECK_FALSE( analyzer.Begin( invalid, 1.0, 0.5, bodies ) );
    CHECK_FALSE( Observe( analyzer, 1u, bodies ) );

    const auto view = analyzer.View();
    REQUIRE( view.firstBlockingFailure.latched );
    CHECK( view.firstBlockingFailure.cause == ContinuousOrbitalInstabilityCause::InvalidContract );
    CHECK( view.firstBlockingFailure.absoluteTick == 0u );
    CHECK( view.observedThroughTick == 0u );
}

TEST_CASE( "Continuous orbital presentation: wrapped logical rows never close the ring seam" )
{
    ContinuousPredictionSampleRing ring;
    REQUIRE( ring.Prepare( 3u, 2u ) );
    REQUIRE( ring.Start() );

    for ( std::uint64_t tick = 0u; tick < 4u; ++tick )
    {
        REQUIRE( ring.BeginRow( tick ) );
        REQUIRE( ring.WriteBodyPosition( 0u, Vector3( static_cast<float>( tick ), 0.0f, 0.0f ) ) );
        REQUIRE( ring.WriteBodyPosition( 1u, Vector3( 100.0f + static_cast<float>( tick ), 0.0f, 0.0f ) ) );
        REQUIRE( ring.PublishRow() );
    }

    const std::array members = {
        ContinuousOrbitalPresentationMember { 0u, EARTH_ID, 0.2f, 0.4f, 0.8f },
        ContinuousOrbitalPresentationMember { 1u, MARS_ID, 0.8f, 0.3f, 0.1f },
    };
    ContinuousOrbitalPresentation presentation;
    REQUIRE( presentation.Begin( members, 2u ) );
    REQUIRE( presentation.Publish( ring.AcquireSnapshot() ) );

    const auto view = presentation.View();
    const auto packet = presentation.Packet();
    REQUIRE( view.coherent );
    CHECK( view.wrapped );
    CHECK( view.oldestAbsoluteTick == 1u );
    CHECK( view.newestAbsoluteTick == 3u );
    CHECK( view.ribbonSegmentCount == 4u );
    CHECK( view.headMarkerCount == 2u );
    REQUIRE( packet.ribbonRanges.size() == 2u );
    REQUIRE( packet.compactRibbonRecords.size() == 4u * ContinuousOrbitalPresentation::FLOATS_PER_RIBBON_RECORD );

    const float* earth = packet.compactRibbonRecords.data();
    CHECK( earth[0] == doctest::Approx( 1.0f ) );
    CHECK( earth[3] == doctest::Approx( 2.0f ) );
    CHECK( earth[ContinuousOrbitalPresentation::FLOATS_PER_RIBBON_RECORD + 0u] == doctest::Approx( 2.0f ) );
    CHECK( earth[ContinuousOrbitalPresentation::FLOATS_PER_RIBBON_RECORD + 3u] == doctest::Approx( 3.0f ) );
    CHECK( earth[7] == doctest::Approx( 0.2f ) );

    const std::size_t marsFirst = static_cast<std::size_t>( packet.ribbonRanges[1].firstRecord ) *
                                  ContinuousOrbitalPresentation::FLOATS_PER_RIBBON_RECORD;
    CHECK( packet.compactRibbonRecords[marsFirst + 0u] == doctest::Approx( 101.0f ) );
    CHECK( packet.compactRibbonRecords[marsFirst + 3u] == doctest::Approx( 102.0f ) );
    REQUIRE( packet.coloredLineVertices.size() == 72u );
    CHECK( packet.coloredLineVertices[0] == doctest::Approx( 1.0f ) );
    CHECK( packet.coloredLineVertices[6] == doctest::Approx( 5.0f ) );
    CHECK( packet.sourceSequence == 3u );
}

TEST_CASE( "Continuous orbital presentation: visual downsampling preserves every newest head within fixed capacity" )
{
    constexpr std::size_t bodyCount = SkullbonezCore::Scene::ORBITAL_STABILITY_MEMBER_CAPACITY;
    constexpr std::size_t rowCount = 2000u;
    ContinuousPredictionSampleRing ring;
    REQUIRE( ring.Prepare( rowCount, bodyCount ) );
    REQUIRE( ring.Start() );

    for ( std::uint64_t tick = 0u; tick < rowCount; ++tick )
    {
        REQUIRE( ring.BeginRow( tick ) );

        for ( std::size_t body = 0u; body < bodyCount; ++body )
        {
            REQUIRE(
                ring.WriteBodyPosition( body, Vector3( static_cast<float>( tick ), static_cast<float>( body ), 0.0f ) ) );
        }

        REQUIRE( ring.PublishRow() );
    }

    std::array<ContinuousOrbitalPresentationMember, bodyCount> members = {};

    for ( std::size_t body = 0u; body < bodyCount; ++body )
    {
        members[body] = { body, 9000u + body, 0.1f * static_cast<float>( body % 5u ), 0.5f, 1.0f };
    }

    ContinuousOrbitalPresentation presentation;
    REQUIRE( presentation.Begin( members, bodyCount ) );
    REQUIRE( presentation.Publish( ring.AcquireSnapshot() ) );
    const auto view = presentation.View();
    const auto packet = presentation.Packet();
    CHECK( view.ribbonSegmentCount <= ContinuousOrbitalPresentation::RIBBON_RECORD_CAPACITY );
    CHECK( view.headMarkerCount == bodyCount );
    CHECK( packet.ribbonRanges.size() == bodyCount );

    for ( const auto& range : packet.ribbonRanges )
    {
        REQUIRE( range.recordCount > 0u );
        const std::size_t finalRecord = static_cast<std::size_t>( range.firstRecord + range.recordCount - 1u ) *
                                        ContinuousOrbitalPresentation::FLOATS_PER_RIBBON_RECORD;
        CHECK( packet.compactRibbonRecords[finalRecord + 3u] == doctest::Approx( static_cast<float>( rowCount - 1u ) ) );
    }
}
