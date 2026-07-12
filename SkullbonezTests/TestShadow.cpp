/*
File: SkullbonezTests/TestShadow.cpp
Purpose:
  Proves the shadow projection stability and quality-preset bias helpers.

Mental model:
  Shadow frame helpers are pure value transforms. These tests exercise the
  integer-texel invariant and the High/Ultra bias floors without a GPU.

Glossary:
  Texel grid: Integer coordinate lattice of one shadow depth texture.
  Peter-panning: Visible gap caused by moving a receiver comparison too far
    away from its caster depth.

Invariants:
  - Snapping changes only orthographic x/y translation.
  - Ultra bias floors never exceed the corresponding High floors.

Related:
  - SkullbonezSource/Rendering/Shadow.h
  - Agentic/Reports/2026-07-12/shadow-edge-quality-closure.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Rendering/Shadow.h"

#include <cmath>

using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;

TEST_CASE( "Shadow projection snapping lands the world origin on an integer texel" )
{
    Matrix4 view;
    view.m[12] = 13.271f;
    view.m[13] = -7.843f;
    Matrix4 projection = Matrix4::OrthoZeroToOne( -256.0f, 256.0f, -256.0f, 256.0f, 1.0f, 2048.0f );

    SnapShadowProjectionToTexelGrid( projection, view, 2048 );

    const Matrix4 snapped = projection * view;
    const float texelX = snapped.m[12] * 1024.0f;
    const float texelY = snapped.m[13] * 1024.0f;
    CHECK( std::fabs( texelX - std::round( texelX ) ) < 0.0001f );
    CHECK( std::fabs( texelY - std::round( texelY ) ) < 0.0001f );
}

TEST_CASE( "Shadow projection snapping ignores invalid map dimensions" )
{
    Matrix4 view;
    Matrix4 projection;
    projection.m[12] = 0.125f;
    projection.m[13] = -0.25f;

    SnapShadowProjectionToTexelGrid( projection, view, 0 );

    CHECK( projection.m[12] == doctest::Approx( 0.125f ) );
    CHECK( projection.m[13] == doctest::Approx( -0.25f ) );
}

TEST_CASE( "Shadow receiver bias scales down from High to Ultra" )
{
    ShadowFrameData high;
    high.mapSize = 2048;
    high.depthBias = 0.00005f;
    high.slopeBias = 0.00010f;
    ShadowFrameData ultra = high;
    ultra.mapSize = 4096;

    const ShadowReceiverBias highBias = ResolveShadowReceiverBias( high, true );
    const ShadowReceiverBias ultraBias = ResolveShadowReceiverBias( ultra, true );
    CHECK( highBias.depth == doctest::Approx( 0.00035f ) );
    CHECK( highBias.slope == doctest::Approx( 0.00075f ) );
    CHECK( ultraBias.depth == doctest::Approx( 0.000175f ) );
    CHECK( ultraBias.slope == doctest::Approx( 0.000375f ) );
}
