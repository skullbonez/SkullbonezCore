/*
File: SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.h
Purpose:
  Draws the physics broadphase grid as an explanatory debug overlay.

Summary:
  The broadphase grid is a visibility/debug view into candidate-pair generation.
  It should explain why objects are considered near each other without changing
  physics state.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Heat: Per-cell collision count used only to darken the debug color.

Invariants:
  - Visualizer state is explanatory overlay state and never participates in
    solver, broadphase, or narrowphase decisions.
  - m_cells has fixed capacity so enabling the overlay cannot introduce
    unbounded per-frame allocation.

Related:
  - SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdint>
#include <span>
#include <vector>
#include "../../Core/Common.h"
#include "../../Physics/PhysicsBroadphaseDebugView.h"
#include "../../Maths/Matrix4.h"

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12GeometryOwner;
}

namespace Physics
{

/* -- Broadphase Visualizer
------------------------------------------------------------------------------------------------------------------------------------------

    Real-time debug overlay for the spatial grid broadphase collision system.
    Renders grid cell boundaries as wireframe cubes with per-cell coloring:

    Layman version:
      This draws the invisible grid used to decide which object pairs are worth
      testing. A red cell means "objects here became collision candidates or
      produced narrowphase contact"; it does not mean the grid solved physics.

    - White:        empty cell (no objects)
    - Yellow→Blue:  ball just entered (fades over 0.5s)
    - Blue:         occupied (steady state)
    - Red→Black:    active collision (intensity deepens with collision count per frame)
    - Red/Black→Blue: collision ended (fades back to blue over 0.5s)

    Red overrides yellow entry transitions.  Toggle with G key.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class BroadphaseVisualizer
{

  private:
    static constexpr int MAX_TRACKED_CELLS = 2048;
    static constexpr float FADE_DURATION = 0.5f;
    static constexpr int MAX_COLLISION_HEAT = 10; // Collision count that saturates the black heat color.

    enum class CellState : uint8_t
    {
        Empty,                                    // White — no objects
        Entering,                                 // Yellow→Blue fade
        Occupied,                                 // Blue (steady)
        Colliding,                                // Red→Black (active collision heat)
        Fading,                                   // Red/Black→Blue (collision ended, fading back)
    };

    struct TrackedCell
    {
        int64_t key;                              // Packed cell coordinate key, stable across frames.
        int16_t ix, iy, iz;
        CellState state;
        float timer;                              // Seconds since the current visual state began.
        int collisionHeat;                        // Collision count driving the red-to-black gradient.
        bool activeThisFrame;
        bool collidedThisFrame;                   // Received a narrowphase collision this specific frame
    };

    TrackedCell m_cells[MAX_TRACKED_CELLS];
    int m_cellCount = 0;
    float m_cellSize = 24.0f;
    bool m_enabled = false;

    // Line vertex buffer: each vertex = [x,y,z,r,g,b] = 6 floats
    // Each cell wireframe cube = 12 edges × 2 verts = 24 verts × 6 floats = 144 floats
    std::vector<float> m_lineData;

    static int64_t PackKey( int16_t ix, int16_t iy, int16_t iz );
    int FindCell( int64_t key ) const;
    int FindOrAddCell( int64_t key, int16_t ix, int16_t iy, int16_t iz );
    void RemoveCell( int index );
    void ComputeCellColor( const TrackedCell& cell, float& r, float& g, float& b ) const;
    void EmitCubeWireframe( int16_t ix, int16_t iy, int16_t iz, float r, float g, float b );

  public:
    BroadphaseVisualizer();

    void SetCellSize( float size )
    {
        m_cellSize = size;
    }
    void SetEnabled( bool enabled )
    {
        m_enabled = enabled;
    }
    bool IsEnabled() const
    {
        return m_enabled;
    }
    void Toggle()
    {
        m_enabled = !m_enabled;
    }

    // Call once per frame after broadphase + narrowphase complete.
    // activeCells: cells that have objects this frame.
    // collisionCells: packed keys of cells where narrowphase collisions occurred.
    void Update( float dt, std::span<const PhysicsBroadphaseActiveCell> activeCells,
                 std::span<const int64_t> collisionKeys );

    // Generates line vertex data and submits it through the frame command context.
    // The caller owns renderer readiness and debug-line capability for the frame.
    void Render( const Math::Transformation::Matrix4& viewProj, Rendering::Dx12GeometryOwner& renderCommands,
                 bool supportsDebugLines );
};
} // namespace Physics
} // namespace SkullbonezCore
