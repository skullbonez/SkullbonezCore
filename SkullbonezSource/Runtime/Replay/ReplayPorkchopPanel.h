/*
File: SkullbonezSource/Runtime/Replay/ReplayPorkchopPanel.h
Purpose:
  Declares the bounded departure-window sweep behind Replay's porkchop panel.

Summary:
  The panel samples a fixed departure-delay/time-of-flight grid. Each cell
  propagates the departure and target orbits analytically, solves one Lambert
  transfer, and records the total departure-plus-arrival delta-v. Runtime owns
  visibility and refresh policy; the math owner retains no engine references.

Glossary:
  Porkchop plot: Heatmap of transfer cost over departure time and flight time.
  Departure delay: Time to wait before beginning the transfer burn.
  Time of flight: Duration between departure and target arrival.
  Delta-v: Magnitude of velocity change required by a manoeuvre.
  Failed cell: Sentinel row for a propagation or Lambert solve that did not
    produce a finite transfer.

Invariants:
  - Grid dimensions and per-frame work are compile-time bounded.
  - Cell storage is fixed and performs no allocation after construction.
  - Failed cells retain REPLAY_PORKCHOP_FAILED_DELTA_V.
  - Hidden panels perform no sweep work and request no scene-body reads.
  - Selection publishes values only from a completed, valid cell.

Related:
  - SkullbonezSource/Maths/OrbitalMechanics.h
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
  - SkullbonezTests/TestReplayPorkchopPanel.cpp
*/
#pragma once

#include "../../Maths/OrbitalMechanics.h"
#include "../../Physics/PhysicsHandles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace SkullbonezCore::Runtime
{
inline constexpr std::size_t REPLAY_PORKCHOP_COLUMNS = 64u;
inline constexpr std::size_t REPLAY_PORKCHOP_ROWS = 48u;
inline constexpr std::size_t REPLAY_PORKCHOP_CELL_COUNT = REPLAY_PORKCHOP_COLUMNS * REPLAY_PORKCHOP_ROWS;
inline constexpr std::size_t REPLAY_PORKCHOP_CELLS_PER_FRAME = 96u;
inline constexpr float REPLAY_PORKCHOP_DEPARTURE_MIN_SECONDS = 0.0f;
inline constexpr float REPLAY_PORKCHOP_DEPARTURE_MAX_SECONDS = 48.0f;
inline constexpr float REPLAY_PORKCHOP_TOF_MIN_SECONDS = 2.0f;
inline constexpr float REPLAY_PORKCHOP_TOF_MAX_SECONDS = 20.0f;
inline constexpr float REPLAY_PORKCHOP_FAILED_DELTA_V = -1.0f;

struct ReplayPorkchopBodyState
{
    Physics::PhysicsSceneObjectId id;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;
    bool valid = false;
};

struct ReplayPorkchopSweepInput
{
    ReplayPorkchopBodyState sun;
    ReplayPorkchopBodyState departure;
    ReplayPorkchopBodyState target;
    float gravitationalConstant = 0.0f;
    double epochSeconds = 0.0;
    bool mutualGravityEnabled = false;
};

struct ReplayPorkchopPanelView
{
    std::span<const float> deltaV;
    Physics::PhysicsSceneObjectId targetId;
    std::size_t completedCells = 0u;
    std::size_t minimumCell = 0u;
    int hoveredCell = -1;
    int selectedCell = -1;
    float minimumDeltaV = REPLAY_PORKCHOP_FAILED_DELTA_V;
    float maximumDeltaV = REPLAY_PORKCHOP_FAILED_DELTA_V;
    float selectedDepartureDelaySeconds = 0.0f;
    float selectedTimeOfFlightSeconds = 0.0f;
    float selectedDeltaV = REPLAY_PORKCHOP_FAILED_DELTA_V;
    float sweepAgeSeconds = 0.0f;
    double refreshComputeMilliseconds = 0.0;
    double maximumFrameComputeMilliseconds = 0.0;
    bool visible = false;
    bool available = false;
    bool building = false;
    bool evaluated = false;
    bool complete = false;
};

class ReplayPorkchopPanel
{
  public:
    void Toggle() noexcept;
    void Reset() noexcept;
    bool NeedsRefresh( Physics::PhysicsSceneObjectId targetId, bool mutualGravityEnabled ) const noexcept;
    void BeginSweep( const ReplayPorkchopSweepInput& input ) noexcept;
    void AdvanceSweep( double nowSeconds ) noexcept;
    void SetHoveredCell( int cellIndex ) noexcept;
    bool SelectCell( std::size_t cellIndex ) noexcept;

    bool Visible() const noexcept;
    const ReplayPorkchopPanelView& View() const noexcept;

    static float DepartureDelaySeconds( std::size_t column ) noexcept;
    static float TimeOfFlightSeconds( std::size_t row ) noexcept;

  private:
    bool ComputeCell( std::size_t cellIndex, float& outDeltaV ) const noexcept;
    void ClearSweep() noexcept;

    std::array<float, REPLAY_PORKCHOP_CELL_COUNT> m_deltaV{};
    // Lifetime: the span borrows the owner's fixed array for the owner's whole
    // lifetime. ReplayRuntime never copies or moves this concrete owner.
    ReplayPorkchopPanelView m_view{ m_deltaV };
    Math::Orbital::OrbitalElements m_departureOrbit;
    Math::Orbital::OrbitalElements m_targetOrbit;
    float m_mu = 0.0f;
    float m_selectedDepartureOffsetSeconds = 0.0f;
    double m_sweepEpochSeconds = 0.0;
    bool m_refreshRequested = false;
    bool m_lastMutualGravityEnabled = false;
};
} // namespace SkullbonezCore::Runtime
