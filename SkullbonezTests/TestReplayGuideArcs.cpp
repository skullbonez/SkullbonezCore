//
// File: SkullbonezTests/TestReplayGuideArcs.cpp
// Purpose:
//   Verifies bounded analytic guide publication, cadence, and hidden defaults.
//
// Summary:
//   Circular Earth/Mars fixtures exercise the same orbital library used by the
//   GameUI overlay without requiring a renderer or a live Physics world.
//
// Glossary:
//   Published ring: One complete 96-point owner span exposed to drawing.
//   Cadence window: Five simulation seconds in which supplied values remain
//     unchanged even when a caller supplies newer body state.
//
// Invariants:
//   - Disabled and invalid inputs expose empty spans.
//   - Both planet rings publish atomically and remain fixed-capacity.
//   - Refresh attempts occur no faster than the documented five-second cadence.
//
// Related:
//   - SkullbonezSource/Runtime/Planning/ReplayGuideArcs.h
//   - SkullbonezSource/Maths/OrbitalMechanics.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Planning/ReplayGuideArcs.h"

#include <cmath>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Runtime::REPLAY_GUIDE_ARC_POINT_COUNT;
using SkullbonezCore::Runtime::ReplayGuideArcs;
using SkullbonezCore::Runtime::ReplayGuideArcsUpdateInput;

namespace
{
constexpr float GUIDE_TEST_G = 4.0f;
constexpr float GUIDE_TEST_SUN_MASS = 10000.0f;

ReplayGuideArcsUpdateInput CircularGuideInput( float earthRadius = 80.0f, double nowSeconds = 1.0 )
{
    const float mu = GUIDE_TEST_G * GUIDE_TEST_SUN_MASS;
    const Vector3 sunPosition( 10.0f, 3.0f, -5.0f );

    ReplayGuideArcsUpdateInput input;
    input.sun = { PhysicsSceneObjectId { 1u }, sunPosition, Vector3( 0.0f, 0.0f, 0.0f ), GUIDE_TEST_SUN_MASS, true };
    input.earth = { PhysicsSceneObjectId { 2u }, sunPosition + Vector3( earthRadius, 0.0f, 0.0f ),
                    Vector3( 0.0f, 0.0f, std::sqrt( mu / earthRadius ) ), 1.0f, true };
    input.mars = { PhysicsSceneObjectId { 3u }, sunPosition + Vector3( 120.0f, 0.0f, 0.0f ),
                   Vector3( 0.0f, 0.0f, std::sqrt( mu / 120.0f ) ), 0.1f, true };
    input.gravitationalConstant = GUIDE_TEST_G;
    input.nowSeconds = nowSeconds;
    input.mutualGravityEnabled = true;
    return input;
}

float RadiusFrom( const Vector3& point, const Vector3& center )
{
    const Vector3 relative = point - center;
    return std::sqrt( relative.x * relative.x + relative.y * relative.y + relative.z * relative.z );
}
} // namespace


TEST_CASE( "Replay guide arcs default hidden and publish two fixed circular rings" )
{
    ReplayGuideArcs guideArcs;
    const ReplayGuideArcsUpdateInput input = CircularGuideInput();

    guideArcs.Update( input );
    CHECK_FALSE( guideArcs.Enabled() );
    CHECK_FALSE( guideArcs.RefreshDue( input.nowSeconds ) );
    CHECK_FALSE( guideArcs.View().valid );
    CHECK( guideArcs.View().earthPoints.empty() );
    CHECK( guideArcs.View().marsPoints.empty() );

    guideArcs.Toggle();
    CHECK( guideArcs.RefreshDue( input.nowSeconds ) );
    guideArcs.Update( input );
    const auto view = guideArcs.View();
    REQUIRE( view.enabled );
    REQUIRE( view.valid );
    REQUIRE( view.earthPoints.size() == REPLAY_GUIDE_ARC_POINT_COUNT );
    REQUIRE( view.marsPoints.size() == REPLAY_GUIDE_ARC_POINT_COUNT );

    for ( const Vector3& point : view.earthPoints )
    {
        CHECK( RadiusFrom( point, input.sun.position ) == doctest::Approx( 80.0f ).epsilon( 0.001f ) );
    }

    for ( const Vector3& point : view.marsPoints )
    {
        CHECK( RadiusFrom( point, input.sun.position ) == doctest::Approx( 120.0f ).epsilon( 0.001f ) );
    }

    guideArcs.SetEnabled( true );
    CHECK( guideArcs.Enabled() );
    CHECK( guideArcs.View().valid );
    guideArcs.Toggle();
    CHECK_FALSE( guideArcs.View().valid );
    CHECK( guideArcs.View().earthPoints.empty() );
}


TEST_CASE( "Replay guide arcs refresh on cadence and hide outside mutual gravity" )
{
    ReplayGuideArcs guideArcs;
    guideArcs.Toggle();
    ReplayGuideArcsUpdateInput input = CircularGuideInput();
    guideArcs.Update( input );
    REQUIRE( guideArcs.View().valid );
    const Vector3 originalPoint = guideArcs.View().earthPoints.front();

    input = CircularGuideInput( 70.0f, 3.0 );
    CHECK_FALSE( guideArcs.RefreshDue( input.nowSeconds ) );
    guideArcs.Update( input );
    CHECK( guideArcs.View().earthPoints.front().x == doctest::Approx( originalPoint.x ) );
    CHECK( guideArcs.View().earthPoints.front().z == doctest::Approx( originalPoint.z ) );

    input.nowSeconds = 7.0;
    CHECK( guideArcs.RefreshDue( input.nowSeconds ) );
    guideArcs.Update( input );
    CHECK_FALSE( guideArcs.RefreshDue( input.nowSeconds ) );
    REQUIRE( guideArcs.View().valid );

    for ( const Vector3& point : guideArcs.View().earthPoints )
    {
        CHECK( RadiusFrom( point, input.sun.position ) == doctest::Approx( 70.0f ).epsilon( 0.001f ) );
    }

    input.mutualGravityEnabled = false;
    guideArcs.Update( input );
    CHECK( guideArcs.Enabled() );
    CHECK_FALSE( guideArcs.View().valid );
    CHECK( guideArcs.View().earthPoints.empty() );

    guideArcs.Reset();
    CHECK_FALSE( guideArcs.Enabled() );
}
