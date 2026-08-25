/*
File: SkullbonezTests/TestFrustum.cpp
Purpose:
  Proves DX12 frustum extraction and conservative sphere classification.

Summary:
  Named world-space spheres exercise perspective-camera and orthographic-light
  boundaries without involving renderer state or GPU output.

Glossary:
  Straddle: A sphere whose center is outside a plane but whose radius crosses it.

Invariants:
  - Tests use the production zero-to-one perspective matrix.
  - Boundary expectations prefer false negatives in culling, never missing
    potentially visible geometry.

Related:
  - SkullbonezSource/Maths/Frustum.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Maths/Frustum.h"

#include <cmath>
#include <limits>

using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Visibility::Frustum;

TEST_CASE( "Frustum: perspective sphere classifications are conservative" )
{
    const Matrix4 projection = Matrix4::PerspectiveZeroToOne( 90.0f, 1.0f, 1.0f, 20.0f );
    const Frustum frustum = Frustum::FromViewProjection( Matrix4(), projection );

    CHECK( frustum.IntersectsSphere( Vector3( 0.0f, 0.0f, -5.0f ), 0.5f ) );
    CHECK_FALSE( frustum.IntersectsSphere( Vector3( 8.0f, 0.0f, -5.0f ), 0.5f ) );
    CHECK_FALSE( frustum.IntersectsSphere( Vector3( 0.0f, 0.0f, 2.0f ), 0.5f ) );
    CHECK_FALSE( frustum.IntersectsSphere( Vector3( 0.0f, 0.0f, -25.0f ), 0.5f ) );
}
TEST_CASE( "Frustum: plane straddle and epsilon remain visible" )
{
    const Matrix4 projection = Matrix4::PerspectiveZeroToOne( 90.0f, 1.0f, 1.0f, 20.0f );
    const Frustum frustum = Frustum::FromViewProjection( Matrix4(), projection );

    CHECK( frustum.IntersectsSphere( Vector3( 5.2f, 0.0f, -5.0f ), 0.3f, 0.0f ) );
    CHECK_FALSE( frustum.IntersectsSphere( Vector3( 5.08f, 0.0f, -5.0f ), 0.0f, 0.0f ) );
    CHECK( frustum.IntersectsSphere( Vector3( 5.08f, 0.0f, -5.0f ), 0.0f, 0.1f ) );
    CHECK( frustum.IntersectsSphere( Vector3( 0.0f, 0.0f, -0.95f ), 0.1f, 0.0f ) );
}

TEST_CASE( "Frustum: translated camera classifies world-space spheres" )
{
    const Matrix4 view =
        Matrix4::LookAt( Vector3( 10.0f, 2.0f, 3.0f ), Vector3( 10.0f, 2.0f, -2.0f ), Vector3( 0.0f, 1.0f, 0.0f ) );
    const Matrix4 projection = Matrix4::PerspectiveZeroToOne( 90.0f, 1.0f, 1.0f, 20.0f );
    const Frustum frustum = Frustum::FromViewProjection( view, projection );

    CHECK( frustum.IntersectsSphere( Vector3( 10.0f, 2.0f, -2.0f ), 0.5f ) );
    CHECK_FALSE( frustum.IntersectsSphere( Vector3( 0.0f, 2.0f, -2.0f ), 0.5f ) );
}

TEST_CASE( "Frustum: orthographic light volume retains boundary casters" )
{
    const Matrix4 view =
        Matrix4::LookAt( Vector3( 0.0f, 0.0f, 10.0f ), Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ) );
    const Matrix4 projection = Matrix4::OrthoZeroToOne( -5.0f, 5.0f, -5.0f, 5.0f, 1.0f, 30.0f );
    const Frustum frustum = Frustum::FromViewProjection( view, projection );

    CHECK( frustum.IntersectsSphere( Vector3( 0.0f, 0.0f, 0.0f ), 0.5f ) );
    CHECK( frustum.IntersectsSphere( Vector3( 5.2f, 0.0f, 0.0f ), 0.3f, 0.0f ) );
    CHECK_FALSE( frustum.IntersectsSphere( Vector3( 6.0f, 0.0f, 0.0f ), 0.5f, 0.0f ) );
    CHECK_FALSE( frustum.IntersectsSphere( Vector3( 0.0f, 0.0f, 12.0f ), 0.5f, 0.0f ) );
}

TEST_CASE( "Frustum: reflection half-space keeps water-plane straddlers" )
{
    const float waterPlane[4] = { 0.0f, 1.0f, 0.0f, -2.0f };

    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 3.0f, 0.0f ), 0.25f, waterPlane ) );
    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.8f, 0.0f ), 0.25f, waterPlane, 0.0f ) );
    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.0f, 0.0f ), 0.25f, waterPlane, 0.0f ) );
}

TEST_CASE( "Frustum: a point exactly on a normalized plane remains contained" )
{
    const float plane[4] = { 0.0f, 1.0f, 0.0f, -2.0f };

    CHECK( Frustum::IntersectsHalfSpace( Vector3( 4.0f, 2.0f, -3.0f ), 0.0f, plane, 0.0f ) );
}


TEST_CASE( "Frustum: half-space classification is invariant to large finite plane scaling" )
{
    const float largestFinite = (std::numeric_limits<float>::max)();
    const float ordinaryPlane[4] = { 0.0f, 0.25f, 0.0f, -0.5f };
    const float scaledPlane[4] = { 0.0f, largestFinite * 0.25f, 0.0f, largestFinite * -0.5f };

    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 3.0f, 0.0f ), 0.25f, ordinaryPlane, 0.0f ) );
    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 3.0f, 0.0f ), 0.25f, scaledPlane, 0.0f ) );
    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.0f, 0.0f ), 0.25f, ordinaryPlane, 0.0f ) );
    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.0f, 0.0f ), 0.25f, scaledPlane, 0.0f ) );

    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.8f, 0.0f ), 0.0f, ordinaryPlane, 0.0f ) );
    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.8f, 0.0f ), 0.0f, scaledPlane, 0.0f ) );
    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.8f, 0.0f ), 0.0f, ordinaryPlane, 0.1f ) );
    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.8f, 0.0f ), 0.0f, scaledPlane, 0.1f ) );
    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.8f, 0.0f ), 0.0f, ordinaryPlane, 0.25f ) );
    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.8f, 0.0f ), 0.0f, scaledPlane, 0.25f ) );

    const float cancellationPlane[4] = { largestFinite * 0.25f, largestFinite * 0.25f, 0.0f, 0.0f };
    CHECK( Frustum::IntersectsHalfSpace( Vector3( 4.0f, -4.0f, 0.0f ), 0.0f, cancellationPlane, 0.0f ) );

    const float finiteLengthPlane[4] = { 1.0f, -1.0f, 0.0f, 0.0f };
    const float scaledFiniteLengthPlane[4] = { 2.0f, -2.0f, 0.0f, 0.0f };
    CHECK( Frustum::IntersectsHalfSpace( Vector3( largestFinite, largestFinite, 0.0f ), 0.0f, finiteLengthPlane, 0.0f ) );
    CHECK( Frustum::IntersectsHalfSpace( Vector3( largestFinite, largestFinite, 0.0f ), 0.0f,
                                         scaledFiniteLengthPlane, 0.0f ) );
    const float scaledFiniteLengthRejectingPlane[4] = { 2.0f, -2.0f, 0.0f, -100.0f };
    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( largestFinite, largestFinite, 0.0f ), 0.0f,
                                               scaledFiniteLengthRejectingPlane, 0.0f ) );

    const float expandedRadiusOverflowPlane[4] = { 2.0f, 0.0f, 0.0f, 0.0f };
    CHECK( Frustum::IntersectsHalfSpace( Vector3( largestFinite * -0.4f, 0.0f, 0.0f ), largestFinite * 0.75f,
                                         expandedRadiusOverflowPlane, 0.0f ) );
    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( -largestFinite, 0.0f, 0.0f ), largestFinite * 0.75f,
                                               expandedRadiusOverflowPlane, 0.0f ) );

    const float negativeScaledPlane[4] = { 0.0f, largestFinite * -0.25f, 0.0f, largestFinite * 0.5f };
    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 3.0f, 0.0f ), 0.0f, negativeScaledPlane, 0.0f ) );
    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, 1.0f, 0.0f ), 0.0f, negativeScaledPlane, 0.0f ) );
}


TEST_CASE( "Frustum: half-space degenerate threshold and invalid coefficients are conservative" )
{
    const float boundaryMagnitude = 1.0e-6f;
    const float belowMagnitude = std::nextafter( boundaryMagnitude, 0.0f );
    const float aboveMagnitude = std::nextafter( boundaryMagnitude, std::numeric_limits<float>::infinity() );
    const float belowPlane[4] = { 0.0f, belowMagnitude, 0.0f, 0.0f };
    const float boundaryPlane[4] = { 0.0f, boundaryMagnitude, 0.0f, 0.0f };
    const float abovePlane[4] = { 0.0f, aboveMagnitude, 0.0f, 0.0f };

    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, -1.0f, 0.0f ), 0.0f, belowPlane, 0.0f ) );
    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, -1.0f, 0.0f ), 0.0f, boundaryPlane, 0.0f ) );
    CHECK_FALSE( Frustum::IntersectsHalfSpace( Vector3( 0.0f, -1.0f, 0.0f ), 0.0f, abovePlane, 0.0f ) );

    const float invalidDistancePlane[4] = { 0.0f, 1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN() };
    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, -100.0f, 0.0f ), 0.0f, invalidDistancePlane, 0.0f ) );
    const float negativeInfinityDistancePlane[4] = { 0.0f, 1.0f, 0.0f,
                                                     -std::numeric_limits<float>::infinity() };
    CHECK( Frustum::IntersectsHalfSpace( Vector3( 0.0f, -100.0f, 0.0f ), 0.0f, negativeInfinityDistancePlane, 0.0f ) );
}
