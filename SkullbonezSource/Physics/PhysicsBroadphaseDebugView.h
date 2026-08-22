/*
File: SkullbonezSource/Physics/PhysicsBroadphaseDebugView.h
Purpose:
  Defines the bounded broadphase-cell values published for diagnostics.

Summary:
  Physics owns the spatial grid, while Runtime visualizers and automation need
  only cell coordinates, occupancy, and cell size. This value contract keeps
  those consumers detached from SpatialGrid storage and mutation authority.

Glossary:
  Diagnostic cell coordinate: Full-width signed grid index shared with
    SpatialGrid identity and Runtime visualization.

Invariants:
  - The 8,192-row bound is the SpatialGrid bucket-table capacity; the grid uses
    this constant directly so diagnostics cannot silently truncate live cells.
  - `objectCount` includes persistent occupants plus current swept-overlay rows.
  - Cell coordinates retain exact `int` identity through publication; consumers
    must not narrow or hash them with a lossy visualization-only scheme.
  - These values carry no SpatialGrid owner, storage, or mutation capability.

Related:
  - SkullbonezSource/Physics/SpatialGrid.h
  - SkullbonezSource/Physics/PhysicsEngine.h
  - SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "PhysicsStageCapacity.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Physics
{

inline constexpr int PHYSICS_BROADPHASE_ACTIVE_CELL_CAPACITY = static_cast<int>( PHYSICS_SPATIAL_GRID_BUCKET_COUNT );

struct PhysicsBroadphaseActiveCell
{
    int ix = 0;
    int iy = 0;
    int iz = 0;
    int objectCount = 0;
};

} // namespace Physics
} // namespace SkullbonezCore
