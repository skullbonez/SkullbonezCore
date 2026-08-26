/*
File: SkullbonezSource/Runtime/Render/BroadphaseVisualizer.cpp
Purpose:
  Draws the physics broadphase grid as an explanatory debug overlay.

Summary:
  BroadphaseVisualizer turns bounded grid-cell facts into frame-scoped debug
  lines and retains short enter/collide/fade color transitions so candidate-
  pair locality can be inspected without feeding state back into physics.

Glossary:
  Debug line command: Frame-scoped renderer command that draws colored line
  vertices for overlays after the owner has checked renderer capability.

Invariants:
  - This overlay reads broadphase/debug cell data only; it must not feed state
    back into collision detection.
  - Tracked cell state is capped and visual-only, so overflow drops debug cells
    instead of reallocating during a frame.

Related:
  - SkullbonezSource/Runtime/Render/BroadphaseVisualizer.h
  - Agentic/Reference/engine-glossary.md
*/

// Concept: active broadphase cells render as wireframe cubes. Color encodes
// the short-lived visual state:
//
//   White  = empty (no objects in this cell)
//   Yellow = ball just entered (fading to blue over 0.5s)
//   Blue   = occupied (steady state)
//   Red    = collision active (deepens toward black with collision count)
//   Fade   = collision ended (red/black fading back to blue over 0.5s)
//
// The visualizer maintains per-cell state across frames and generates
// interleaved [position, color] vertex data for the DrawLinesColored API.
//


#include "BroadphaseVisualizer.h"
#include "../../Rendering/RenderCommandTypes.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"

#include <algorithm>
#include <cstring>


using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Rendering;
using SkullbonezCore::Math::Transformation::Matrix4;

namespace
{
constexpr PassRasterStateBucket BROADPHASE_LINE_RASTER = MakePassRasterStateBucket( 0,
                                                                                    { false, false, false, BlendFactor::One,
                                                                                      BlendFactor::Zero, CullMode::None } );
}


BroadphaseVisualizer::BroadphaseVisualizer()
{
    memset( m_cells, 0, sizeof( m_cells ) );

    // Invariant: every admitted tracked cell can emit its complete 12-edge cube
    // without growing CPU staging during the guarded render phase.
    m_lineData.reserve( DiagnosticRequiredLineFloatCapacity() );
}

void BroadphaseVisualizer::ResetTransientState()
{
    // Scene and enable epochs may reuse the same visualizer owner. Retain its
    // allocation while discarding every prior cell identity and staged line.
    m_cellCount = 0;
    m_lineData.clear();
}


int BroadphaseVisualizer::FindCell( int64_t key ) const
{
    for ( int i = 0; i < m_cellCount; ++i )
    {
        if ( m_cells[i].key == key )
        {
            return i;
        }
    }

    return -1;
}


int BroadphaseVisualizer::FindOrAddCell( int64_t key, int ix, int iy, int iz )
{
    int idx = FindCell( key );

    if ( idx >= 0 )
    {
        return idx;
    }

    if ( m_cellCount >= MAX_TRACKED_CELLS )
    {
        // Hazard: this is a debug renderer. Dropping excess visualization cells
        // is preferable to unbounded allocation while physics is stepping.
        return -1; // Silently drop if at capacity
    }

    TrackedCell& cell = m_cells[m_cellCount];
    cell.key = key;
    cell.ix = ix;
    cell.iy = iy;
    cell.iz = iz;
    cell.state = CellState::Empty;
    cell.timer = 0.0f;
    cell.collisionHeat = 0;
    cell.activeThisFrame = false;
    return m_cellCount++;
}


void BroadphaseVisualizer::RemoveCell( int index )
{
    if ( index < 0 || index >= m_cellCount )
    {
        return;
    }

    // Swap with last element for O(1) removal
    m_cells[index] = m_cells[m_cellCount - 1];
    --m_cellCount;
}


void BroadphaseVisualizer::ComputeCellColor( const TrackedCell& cell, float& r, float& g, float& b ) const
{
    float t = cell.timer / FADE_DURATION;

    if ( t > 1.0f )
    {
        t = 1.0f;
    }

    switch ( cell.state )
    {
    case CellState::Empty:

        // White
        r = 1.0f;
        g = 1.0f;
        b = 1.0f;
        break;

    case CellState::Entering:

        // Yellow (1,1,0) → Blue (0,0,1) over FADE_DURATION
        r = 1.0f - t;
        g = 1.0f - t;
        b = t;
        break;

    case CellState::Occupied:

        // Blue (steady)
        r = 0.0f;
        g = 0.0f;
        b = 1.0f;
        break;

    case CellState::Colliding:
    {
        // Red (1,0,0) → Black (0,0,0) based on collision heat
        float intensity = 1.0f - ( static_cast<float>( cell.collisionHeat ) / static_cast<float>( MAX_COLLISION_HEAT ) );

        if ( intensity < 0.0f )
        {
            intensity = 0.0f;
        }

        r = intensity;
        g = 0.0f;
        b = 0.0f;
        break;
    }

    case CellState::Fading:
    {
        // Current collision color → Blue (0,0,1) over FADE_DURATION
        float intensity = 1.0f - ( static_cast<float>( cell.collisionHeat ) / static_cast<float>( MAX_COLLISION_HEAT ) );

        if ( intensity < 0.0f )
        {
            intensity = 0.0f;
        }

        // Fade from (intensity, 0, 0) toward (0, 0, 1)
        r = intensity * ( 1.0f - t );
        g = 0.0f;
        b = t;
        break;
    }
    }
}


void BroadphaseVisualizer::EmitCubeWireframe( int ix, int iy, int iz, float r, float g, float b )
{
    // Grid indices become world-space cube corners for the debug wireframe.
    float x0 = static_cast<float>( ix ) * m_cellSize;
    float y0 = static_cast<float>( iy ) * m_cellSize;
    float z0 = static_cast<float>( iz ) * m_cellSize;
    float x1 = x0 + m_cellSize;
    float y1 = y0 + m_cellSize;
    float z1 = z0 + m_cellSize;

    // A cube has 12 edges. Each edge = 2 vertices × 6 floats = 12 floats.
    // Total: 12 edges × 12 floats = 144 floats per cell.
    auto emit = [&]( float ax, float ay, float az, float bx, float by, float bz )
    {
        m_lineData.push_back( ax );

        m_lineData.push_back( ay );
        m_lineData.push_back( az );
        m_lineData.push_back( r );
        m_lineData.push_back( g );
        m_lineData.push_back( b );
        m_lineData.push_back( bx );
        m_lineData.push_back( by );
        m_lineData.push_back( bz );
        m_lineData.push_back( r );
        m_lineData.push_back( g );
        m_lineData.push_back( b );
    };

    // Bottom face edges (y = y0)
    emit( x0, y0, z0, x1, y0, z0 );
    emit( x1, y0, z0, x1, y0, z1 );
    emit( x1, y0, z1, x0, y0, z1 );
    emit( x0, y0, z1, x0, y0, z0 );

    // Top face edges (y = y1)
    emit( x0, y1, z0, x1, y1, z0 );
    emit( x1, y1, z0, x1, y1, z1 );
    emit( x1, y1, z1, x0, y1, z1 );
    emit( x0, y1, z1, x0, y1, z0 );

    // Vertical edges connecting bottom and top
    emit( x0, y0, z0, x0, y1, z0 );
    emit( x1, y0, z0, x1, y1, z0 );
    emit( x1, y0, z1, x1, y1, z1 );
    emit( x0, y0, z1, x0, y1, z1 );
}


void BroadphaseVisualizer::Update( float dt, std::span<const PhysicsBroadphaseActiveCell> activeCells,
                                   std::span<const int64_t> collisionKeys )
{
    if ( !m_enabled )
    {
        return;
    }

    // The broadphase grid changes every physics tick, but a one-frame cell is
    // hard to see. This visual tracker gives cells enter/collide/fade lifetimes
    // purely for display, without changing pair generation.
    // Mark all existing cells as inactive for this frame
    for ( int i = 0; i < m_cellCount; ++i )
    {
        m_cells[i].activeThisFrame = false;
        m_cells[i].collidedThisFrame = false;
    }

    // Process active cells from the spatial grid
    for ( const PhysicsBroadphaseActiveCell& activeCell : activeCells )
    {
        int64_t key = DiagnosticCellKey( activeCell.ix, activeCell.iy, activeCell.iz );
        int idx = FindOrAddCell( key, activeCell.ix, activeCell.iy, activeCell.iz );

        if ( idx < 0 )
        {
            continue;
        }

        TrackedCell& cell = m_cells[idx];
        cell.activeThisFrame = true;

        // Transition logic for newly occupied cells
        if ( cell.state == CellState::Empty )
        {
            // Cell just gained objects — start entry animation
            cell.state = CellState::Entering;
            cell.timer = 0.0f;
        }
    }

    // Process collision cells — mark them as colliding with increased heat
    for ( int64_t collisionKey : collisionKeys )
    {
        int idx = FindCell( collisionKey );

        if ( idx < 0 )
        {
            continue;
        }

        TrackedCell& cell = m_cells[idx];

        // Red collision overrides yellow entry transition
        cell.state = CellState::Colliding;
        cell.timer = 0.0f;
        cell.collidedThisFrame = true;
        cell.collisionHeat = ( cell.collisionHeat < MAX_COLLISION_HEAT ) ? cell.collisionHeat + 1 : MAX_COLLISION_HEAT;
    }

    // Timers age out cells after their hit/visited highlight fades.
    for ( int i = 0; i < m_cellCount; )
    {
        TrackedCell& cell = m_cells[i];
        cell.timer += dt;

        switch ( cell.state )
        {
        case CellState::Empty:

            // If cell is no longer active, remove it after a brief hold
            if ( !cell.activeThisFrame && cell.timer > FADE_DURATION )
            {
                RemoveCell( i );
                continue;
            }

            break;

        case CellState::Entering:

            if ( cell.timer >= FADE_DURATION )
            {
                // Transition complete — now steadily occupied
                cell.state = CellState::Occupied;
                cell.timer = 0.0f;
            }

            break;

        case CellState::Occupied:

            if ( !cell.activeThisFrame )
            {
                // Lost all objects — transition to empty
                cell.state = CellState::Empty;
                cell.timer = 0.0f;
            }

            break;

        case CellState::Colliding:

            // Stay in colliding state only while collisions arrive each frame.
            // If no collision this frame, start fading back to blue.
            if ( !cell.collidedThisFrame )
            {
                cell.state = CellState::Fading;
                cell.timer = 0.0f;
            }

            break;

        case CellState::Fading:

            if ( cell.timer >= FADE_DURATION )
            {
                // Fade complete — return to occupied or empty
                if ( cell.activeThisFrame )
                {
                    cell.state = CellState::Occupied;
                }
                else
                {
                    cell.state = CellState::Empty;
                }

                cell.timer = 0.0f;
                cell.collisionHeat = 0;
            }

            break;
        }

        ++i;
    }
}


void BroadphaseVisualizer::Render( const Matrix4& viewProj, Dx12GeometryOwner& renderCommands, bool supportsDebugLines )
{
    if ( !m_enabled || m_cellCount == 0 || !supportsDebugLines )
    {
        return;
    }

    // Generate line vertex data for all tracked cells
    m_lineData.clear();

    for ( int i = 0; i < m_cellCount; ++i )
    {
        float r, g, b;
        ComputeCellColor( m_cells[i], r, g, b );
        EmitCubeWireframe( m_cells[i].ix, m_cells[i].iy, m_cells[i].iz, r, g, b );
    }

    if ( m_lineData.empty() )
    {
        return;
    }

    // Why: DebugOverlayPass owns renderer readiness for the frame; this module
    // owns only the physics overlay vertices, so it submits through the borrowed
    // command context instead of reopening the global renderer service.
    renderCommands.DrawLinesColored( m_lineData, viewProj, BROADPHASE_LINE_RASTER );
}
