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

TEST_CASE( "Frustum: degenerate planes remain permissive instead of culling" )
{
    const float zeros[16] = {};
    const Matrix4 zeroMatrix( zeros );
    const Frustum frustum = Frustum::FromViewProjection( zeroMatrix, zeroMatrix );
    const float zeroPlane[4] = {};

    CHECK( frustum.IntersectsSphere( Vector3( 1000.0f, -2000.0f, 3000.0f ), 0.0f ) );
    CHECK( Frustum::IntersectsHalfSpace( Vector3( 1000.0f, -2000.0f, 3000.0f ), 0.0f, zeroPlane ) );
    for ( int index = 0; index < Frustum::PLANE_COUNT; ++index )
    {
        CHECK( frustum.Plane( index ).normal == Vector3( 0.0f, 0.0f, 0.0f ) );
        CHECK( frustum.Plane( index ).distance == doctest::Approx( 1.0f ) );
    }
}
