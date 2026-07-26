/*
File: SkullbonezSource/Physics/PhysicsTerrainView.h
Purpose:
  Declares the Physics-owned value view over scene terrain collision planes.

Summary:
  World terrain construction publishes precomputed triangle planes as a
  scene-lifetime span. Physics retains only this detached value view and owns
  every heightfield lookup used by fixed-step collision and force paths.

Glossary:
  Terrain cell: One heightfield quad containing the two authored split planes.
  Terrain view: Borrowed scene-lifetime span plus immutable sampling metadata.
  Analytic slope: Terrain represented by one plane and the exact authored
    height expression instead of a cell grid.

Invariants:
  - The view stores no World type, owner pointer, callback, or virtual seam.
  - Heightfield cell order matches World terrain's row-major collision cache.
  - Quad selection, diagonal selection, and height arithmetic preserve the
    pre-boundary float-operation order.
  - The scene terrain owner outlives every retained view and clears Physics
    before replacing the backing cells.

Related:
  - SkullbonezSource/Physics/PhysicsTerrainView.cpp
  - SkullbonezSource/World/Terrain.cpp
  - SkullbonezSource/Runtime/Scene/SceneWorld.cpp
*/
#pragma once

#include <span>

#include "../Maths/GeometricStructures.h"

namespace SkullbonezCore::Physics
{
struct PhysicsTerrainCell
{
    Geometry::Plane triangleA;
    Geometry::Plane triangleB;
};

struct PhysicsTerrainView
{
    std::span<const PhysicsTerrainCell> cells;
    int quadsPerSide = 0;
    float scaledStepSize = 0.0f;
    float worldExtent = 0.0f;
    float maxHeight = 0.0f;
    bool flatSlope = false;
    float flatSlopeExtent = 0.0f;
    float slopeBaseY = 0.0f;
    float slopeX = 0.0f;
    float slopeZ = 0.0f;
    Geometry::Plane flatSlopePlane;

    bool IsValid() const noexcept;
    bool IsInBounds( float x, float z ) const noexcept;
    float HeightAt( float x, float z ) const;
    void HeightAndPlaneAt( float x,
                           float z,
                           float& outHeight,
                           Geometry::Plane& outPlane ) const;
    float MaxHeight() const noexcept;
};
} // namespace SkullbonezCore::Physics
