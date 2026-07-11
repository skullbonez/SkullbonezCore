/*
File: SkullbonezTests/TestFrustum.cpp
Purpose:
  Proves DX12 frustum extraction and conservative sphere classification.

Mental model:
  Named camera-space spheres exercise inside, outside, straddle, and epsilon
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
    const Matrix4 view = Matrix4::LookAt( Vector3( 10.0f, 2.0f, 3.0f ),
                                         Vector3( 10.0f, 2.0f, -2.0f ),
                                         Vector3( 0.0f, 1.0f, 0.0f ) );
    const Matrix4 projection = Matrix4::PerspectiveZeroToOne( 90.0f, 1.0f, 1.0f, 20.0f );
    const Frustum frustum = Frustum::FromViewProjection( view, projection );

    CHECK( frustum.IntersectsSphere( Vector3( 10.0f, 2.0f, -2.0f ), 0.5f ) );
    CHECK_FALSE( frustum.IntersectsSphere( Vector3( 0.0f, 2.0f, -2.0f ), 0.5f ) );
}
