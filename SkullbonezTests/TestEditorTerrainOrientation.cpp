/*
File: SkullbonezTests/TestEditorTerrainOrientation.cpp
Purpose:
  Pins editor terrain-normal orientation at the antiparallel pole.

Summary:
  Placement assets delegate their numerical normal alignment to a pure editor
  policy. This test proves a fully inverted normal selects the deterministic
  world-X half-turn instead of normalizing a zero cross product.

Glossary:
  Antiparallel pole: Terrain normal exactly opposite the object's world-up axis.

Invariants:
  - An inverted normal returns finite quaternion and matrix components.
  - The returned orientation maps world up to world down.

Related:
  - SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.cpp
  - SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.h"

#include <cmath>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Runtime::EditorTerrainOrientationFromNormal;

TEST_CASE( "Editor terrain orientation: antiparallel up remains finite and aligned" )
{
    const auto orientation = EditorTerrainOrientationFromNormal( Vector3( 0.0f, -1.0f, 0.0f ) );
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
    orientation.GetComponents( x, y, z, w );
    CHECK( std::isfinite( x ) );
    CHECK( std::isfinite( y ) );
    CHECK( std::isfinite( z ) );
    CHECK( std::isfinite( w ) );

    const auto matrix = orientation.GetOrientationMatrix();
    const Vector3 aligned = matrix * Vector3( 0.0f, 1.0f, 0.0f );
    CHECK( std::isfinite( aligned.x ) );
    CHECK( std::isfinite( aligned.y ) );
    CHECK( std::isfinite( aligned.z ) );
    CHECK( std::fabs( aligned.x ) <= 0.00001f );
    CHECK( std::fabs( aligned.y + 1.0f ) <= 0.00001f );
    CHECK( std::fabs( aligned.z ) <= 0.00001f );
}
