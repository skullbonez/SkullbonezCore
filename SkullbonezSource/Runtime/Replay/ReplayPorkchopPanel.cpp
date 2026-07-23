/*
File: SkullbonezSource/Runtime/Replay/ReplayPorkchopPanel.cpp
Purpose:
  Implements Replay's allocation-free departure-window sweep.

Summary:
  A refresh captures two elliptic orbit records relative to the selected
  central body. AdvanceSweep evaluates a fixed number of cells, making frame
  cost independent of scene size and retaining the completed heatmap in a
  fixed array for rendering and selection.

Glossary:
  Heliocentric state: Position and velocity relative to the central sun.
  Lambert solve: Boundary-value solve for velocities joining two positions.
  Prograde: Transfer direction aligned with the authored orbital direction.

Invariants:
  - A sweep evaluates cells in stable row-major order.
  - Timing is diagnostic only and never changes results or work count.
  - Minimum and maximum ignore failed sentinels.
  - A new refresh invalidates prior hover and selection publications.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayPorkchopPanel.h
  - SkullbonezSource/Maths/OrbitalMechanics.cpp
*/
#include "ReplayPorkchopPanel.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace SkullbonezCore::Runtime
{
namespace
{
using Math::Orbital::LambertSolution;
using Math::Orbital::OrbitalStatus;
using Math::Vector::Vector3;

bool ValidBody( const ReplayPorkchopBodyState& body ) noexcept
{
    return body.valid && body.id.IsValid() && body.mass > 0.0f && std::isfinite( body.mass ) &&
           std::isfinite( body.position.x ) && std::isfinite( body.position.y ) && std::isfinite( body.position.z ) &&
           std::isfinite( body.linearVelocity.x ) && std::isfinite( body.linearVelocity.y ) &&
           std::isfinite( body.linearVelocity.z );
}

float Magnitude( const Vector3& value ) noexcept
{
    return std::sqrt( value.x * value.x + value.y * value.y + value.z * value.z );
}
} // namespace

void ReplayPorkchopPanel::Toggle() noexcept
{
    m_view.visible = !m_view.visible;
    m_view.hoveredCell = -1;
    if ( m_view.visible )
    {
        // Why: reopening is the explicit on-demand refresh affordance. Hidden
        // frames retain pixels but perform neither body reads nor sweep work.
        m_refreshRequested = true;
    }
}

void ReplayPorkchopPanel::Reset() noexcept
{
    m_view.visible = false;
    m_refreshRequested = false;
    m_lastMutualGravityEnabled = false;
    m_view.targetId = {};
    ClearSweep();
}

bool ReplayPorkchopPanel::NeedsRefresh( Physics::PhysicsSceneObjectId targetId,
                                        bool mutualGravityEnabled ) const noexcept
{
    return m_view.visible &&
           ( m_refreshRequested || targetId != m_view.targetId || mutualGravityEnabled != m_lastMutualGravityEnabled );
}

void ReplayPorkchopPanel::BeginSweep( const ReplayPorkchopSweepInput& input ) noexcept
{
    ClearSweep();
    m_refreshRequested = false;
    m_view.targetId = input.target.id;
    m_lastMutualGravityEnabled = input.mutualGravityEnabled;
    if ( !m_view.visible || !input.mutualGravityEnabled || !ValidBody( input.sun ) || !ValidBody( input.departure ) ||
         !ValidBody( input.target ) || input.sun.id == input.departure.id || input.sun.id == input.target.id ||
         input.departure.id == input.target.id || input.gravitationalConstant <= 0.0f ||
         !std::isfinite( input.gravitationalConstant ) )
    {
        return;
    }

    m_mu = input.gravitationalConstant * input.sun.mass;
    const Vector3 departurePosition = input.departure.position - input.sun.position;
    const Vector3 departureVelocity = input.departure.linearVelocity - input.sun.linearVelocity;
    const Vector3 targetPosition = input.target.position - input.sun.position;
    const Vector3 targetVelocity = input.target.linearVelocity - input.sun.linearVelocity;
    if ( Math::Orbital::ElementsFromState( departurePosition, departureVelocity, m_mu, m_departureOrbit ) !=
             OrbitalStatus::Ok ||
         Math::Orbital::ElementsFromState( targetPosition, targetVelocity, m_mu, m_targetOrbit ) != OrbitalStatus::Ok )
    {
        return;
    }

    m_view.available = true;
    m_view.building = true;
}

void ReplayPorkchopPanel::AdvanceSweep() noexcept
{
    if ( !m_view.visible || !m_view.building )
    {
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    const std::size_t end =
        (std::min)( REPLAY_PORKCHOP_CELL_COUNT, m_view.completedCells + REPLAY_PORKCHOP_CELLS_PER_FRAME );
    while ( m_view.completedCells < end )
    {
        float deltaV = REPLAY_PORKCHOP_FAILED_DELTA_V;
        if ( ComputeCell( m_view.completedCells, deltaV ) )
        {
            m_deltaV[m_view.completedCells] = deltaV;
            if ( m_view.minimumDeltaV < 0.0f || deltaV < m_view.minimumDeltaV )
            {
                m_view.minimumDeltaV = deltaV;
                m_view.minimumCell = m_view.completedCells;
            }
            m_view.maximumDeltaV = (std::max)( m_view.maximumDeltaV, deltaV );
        }
        ++m_view.completedCells;
    }
    const auto finished = std::chrono::steady_clock::now();
    m_view.refreshComputeMilliseconds += std::chrono::duration<double, std::milli>( finished - started ).count();

    if ( m_view.completedCells == REPLAY_PORKCHOP_CELL_COUNT )
    {
        m_view.building = false;
        m_view.complete = m_view.minimumDeltaV >= 0.0f;
    }
}

bool ReplayPorkchopPanel::ComputeCell( std::size_t cellIndex, float& outDeltaV ) const noexcept
{
    const std::size_t column = cellIndex % REPLAY_PORKCHOP_COLUMNS;
    const std::size_t row = cellIndex / REPLAY_PORKCHOP_COLUMNS;
    const float departureDelay = DepartureDelaySeconds( column );
    const float timeOfFlight = TimeOfFlightSeconds( row );

    Vector3 departurePosition;
    Vector3 departureVelocity;
    Vector3 targetPosition;
    Vector3 targetVelocity;
    if ( Math::Orbital::PropagateToTime( m_departureOrbit, departureDelay, departurePosition, departureVelocity ) !=
             OrbitalStatus::Ok ||
         Math::Orbital::PropagateToTime( m_targetOrbit,
                                         departureDelay + timeOfFlight,
                                         targetPosition,
                                         targetVelocity ) != OrbitalStatus::Ok )
    {
        return false;
    }

    LambertSolution transfer;
    if ( Math::Orbital::SolveLambert( departurePosition, targetPosition, timeOfFlight, m_mu, true, transfer ) !=
         OrbitalStatus::Ok )
    {
        return false;
    }

    const float deltaV = Magnitude( transfer.v1 - departureVelocity ) + Magnitude( targetVelocity - transfer.v2 );
    if ( !std::isfinite( deltaV ) )
    {
        return false;
    }
    outDeltaV = deltaV;
    return true;
}

void ReplayPorkchopPanel::SetHoveredCell( int cellIndex ) noexcept
{
    m_view.hoveredCell =
        cellIndex >= 0 && static_cast<std::size_t>( cellIndex ) < m_view.completedCells ? cellIndex : -1;
}

bool ReplayPorkchopPanel::SelectCell( std::size_t cellIndex ) noexcept
{
    if ( !m_view.complete || cellIndex >= m_deltaV.size() || m_deltaV[cellIndex] < 0.0f )
    {
        return false;
    }
    m_view.selectedCell = static_cast<int>( cellIndex );
    m_view.selectedDepartureDelaySeconds = DepartureDelaySeconds( cellIndex % REPLAY_PORKCHOP_COLUMNS );
    m_view.selectedTimeOfFlightSeconds = TimeOfFlightSeconds( cellIndex / REPLAY_PORKCHOP_COLUMNS );
    m_view.selectedDeltaV = m_deltaV[cellIndex];
    return true;
}

bool ReplayPorkchopPanel::Visible() const noexcept
{
    return m_view.visible;
}

const ReplayPorkchopPanelView& ReplayPorkchopPanel::View() const noexcept
{
    return m_view;
}

float ReplayPorkchopPanel::DepartureDelaySeconds( std::size_t column ) noexcept
{
    const float t = static_cast<float>( (std::min)( column, REPLAY_PORKCHOP_COLUMNS - 1u ) ) /
                    static_cast<float>( REPLAY_PORKCHOP_COLUMNS - 1u );
    return REPLAY_PORKCHOP_DEPARTURE_MIN_SECONDS +
           ( REPLAY_PORKCHOP_DEPARTURE_MAX_SECONDS - REPLAY_PORKCHOP_DEPARTURE_MIN_SECONDS ) * t;
}

float ReplayPorkchopPanel::TimeOfFlightSeconds( std::size_t row ) noexcept
{
    const float t = static_cast<float>( (std::min)( row, REPLAY_PORKCHOP_ROWS - 1u ) ) /
                    static_cast<float>( REPLAY_PORKCHOP_ROWS - 1u );
    return REPLAY_PORKCHOP_TOF_MIN_SECONDS + ( REPLAY_PORKCHOP_TOF_MAX_SECONDS - REPLAY_PORKCHOP_TOF_MIN_SECONDS ) * t;
}

void ReplayPorkchopPanel::ClearSweep() noexcept
{
    m_deltaV.fill( REPLAY_PORKCHOP_FAILED_DELTA_V );
    m_departureOrbit = {};
    m_targetOrbit = {};
    m_view.completedCells = 0u;
    m_view.minimumCell = 0u;
    m_view.hoveredCell = -1;
    m_view.selectedCell = -1;
    m_mu = 0.0f;
    m_view.minimumDeltaV = REPLAY_PORKCHOP_FAILED_DELTA_V;
    m_view.maximumDeltaV = REPLAY_PORKCHOP_FAILED_DELTA_V;
    m_view.selectedDepartureDelaySeconds = 0.0f;
    m_view.selectedTimeOfFlightSeconds = 0.0f;
    m_view.selectedDeltaV = REPLAY_PORKCHOP_FAILED_DELTA_V;
    m_view.refreshComputeMilliseconds = 0.0;
    m_view.available = false;
    m_view.building = false;
    m_view.complete = false;
}
} // namespace SkullbonezCore::Runtime
