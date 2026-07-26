/*
File: SkullbonezSource/Physics/PhysicsTerrainView.cpp
Purpose:
  Implements deterministic Physics-owned terrain sampling.

Summary:
  Sampling maps world X/Z coordinates to the exact precomputed triangle plane
  supplied by the scene terrain owner. Analytic slopes preserve their original
  direct height expression and plane.

Glossary:
  Quad posting: Integer X/Z cell coordinate derived from the fixed step size.
  Diagonal split: Authored choice between the two planes inside one terrain
    quad.

Invariants:
  - X/Z posting orientation intentionally preserves the legacy swapped mapping.
  - The diagonal comparison and plane-height expression are byte-order
    sensitive and must not be algebraically rearranged.
  - Out-of-bounds sampling is a lane-F caller invariant failure.

Related:
  - SkullbonezSource/Physics/PhysicsTerrainView.h
  - SkullbonezSource/World/Terrain.cpp
  - Agentic/Reports/2026-07-26/downward-domain-bleed-remediation-db0-census.md
*/
#include "PhysicsTerrainView.h"

#include "../Core/Common.h"
#include "../Core/FatalError.h"

#include <cmath>

using SkullbonezCore::Geometry::Plane;
using SkullbonezCore::Physics::PhysicsTerrainCell;
using SkullbonezCore::Physics::PhysicsTerrainView;

bool PhysicsTerrainView::IsValid() const noexcept
{
    const std::size_t requiredCellCount = quadsPerSide > 0 ? static_cast<std::size_t>( quadsPerSide ) *
                                                                 static_cast<std::size_t>( quadsPerSide )
                                                           : 0u;

    return flatSlope
               ? flatSlopeExtent > 0.0f
               : requiredCellCount > 0u && scaledStepSize > 0.0f && worldExtent > 0.0f && cells.size() >= requiredCellCount;
}

bool PhysicsTerrainView::IsInBounds( float x, float z ) const noexcept
{

    if ( !IsValid() )
    {
        return false;
    }

    const float extent = flatSlope ? flatSlopeExtent : worldExtent;
    return x >= 0.0f && x < extent && z >= 0.0f && z < extent;
}

float PhysicsTerrainView::HeightAt( float x, float z ) const
{
    float height = 0.0f;
    Plane plane;
    HeightAndPlaneAt( x, z, height, plane );
    return height;
}

void PhysicsTerrainView::HeightAndPlaneAt( float x, float z, float& outHeight, Plane& outPlane ) const
{

    if ( !IsInBounds( x, z ) )
    {
        SB_FATAL( "Physics/PhysicsTerrainView", "Coordinates out of terrain bounds: x=%.3f z=%.3f valid=%d.", x, z,
                  IsValid() ? 1 : 0 );
    }

    if ( flatSlope )
    {

        // Invariant: preserve base + slopeX*x + slopeZ*z exactly; regrouping
        // these terms changes the byte-exact physics oracle.
        outHeight = slopeBaseY + slopeX * x + slopeZ * z;
        outPlane = flatSlopePlane;
        return;
    }

    // Invariant: the historic terrain cache maps Z to xPosting and X to
    // zPosting. The names look crossed, but changing them rotates the surface.
    const int xPosting = static_cast<int>( floorf( z / scaledStepSize ) );
    const int zPosting = static_cast<int>( floorf( x / scaledStepSize ) );

    if ( xPosting < 0 || zPosting < 0 || xPosting >= quadsPerSide || zPosting >= quadsPerSide )
    {
        SB_FATAL( "Physics/PhysicsTerrainView",
                  "Terrain cache index out of range: x=%.3f z=%.3f xPosting=%d zPosting=%d quadsPerSide=%d.", x, z, xPosting,
                  zPosting, quadsPerSide );
    }

    const float localZ = z - ( xPosting * scaledStepSize );
    const float localX = x - ( zPosting * scaledStepSize );
    const bool isTriangleA = ( localX <= TOLERANCE ) || ( ( scaledStepSize - localZ ) > localX );
    const PhysicsTerrainCell& cell = cells[static_cast<std::size_t>( zPosting * quadsPerSide + xPosting )];
    const Plane& plane = isTriangleA ? cell.triangleA : cell.triangleB;

    // Invariant: keep the subtraction and division order byte-identical to the
    // retired World-owned query.
    outHeight = ( plane.m_distance - plane.m_normal.x * x - plane.m_normal.z * z ) / plane.m_normal.y;
    outPlane = plane;
}

float PhysicsTerrainView::MaxHeight() const noexcept
{
    return maxHeight;
}
