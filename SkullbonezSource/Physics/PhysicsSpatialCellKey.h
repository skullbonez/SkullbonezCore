/*
File: SkullbonezSource/Physics/PhysicsSpatialCellKey.h
Purpose:
  Encodes supported broadphase coordinates into one exact shared identity.

Summary:
  SpatialGrid bucket membership, narrowphase collision events, and Runtime
  visualization share a reversible 57-bit packing of three signed 19-bit cell
  coordinates. Hashing may still choose a table home, but it can no longer
  merge distinct cells into one logical identity.

Invariants:
  - Each coordinate lies in the supported [-200,000, 200,000] grid range.
  - The returned non-negative int64_t is injective over that complete range.
  - Changing the packing changes broadphase identity and byte-exact collision-
    cell diagnostics, so it requires focused alias tests and Physics validation.

Related:
  - SkullbonezSource/Physics/SpatialGrid.cpp
  - SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp
*/
#pragma once

#include <cassert>
#include <cstdint>

namespace SkullbonezCore::Physics
{
inline constexpr int PHYSICS_MAX_ABSOLUTE_SPATIAL_CELL_COORDINATE = 200000;
inline constexpr uint64_t PHYSICS_SPATIAL_CELL_COORDINATE_MASK = ( uint64_t( 1 ) << 19u ) - 1u;

inline int64_t EncodeExactSpatialCellKey( int ix, int iy, int iz ) noexcept
{
    const auto encodeCoordinate = []( int value ) -> uint64_t
    {
        assert( value >= -PHYSICS_MAX_ABSOLUTE_SPATIAL_CELL_COORDINATE &&
                value <= PHYSICS_MAX_ABSOLUTE_SPATIAL_CELL_COORDINATE );

        // Zig-zag keeps the supported signed range compact without depending
        // on implementation-defined right shifts of negative integers.
        const int64_t wide = value;
        return static_cast<uint64_t>( wide >= 0 ? wide * 2 : -wide * 2 - 1 );
    };

    const uint64_t x = encodeCoordinate( ix );
    const uint64_t y = encodeCoordinate( iy );
    const uint64_t z = encodeCoordinate( iz );
    assert( x <= PHYSICS_SPATIAL_CELL_COORDINATE_MASK && y <= PHYSICS_SPATIAL_CELL_COORDINATE_MASK &&
            z <= PHYSICS_SPATIAL_CELL_COORDINATE_MASK );
    return static_cast<int64_t>( x | ( y << 19u ) | ( z << 38u ) );
}
} // namespace SkullbonezCore::Physics
