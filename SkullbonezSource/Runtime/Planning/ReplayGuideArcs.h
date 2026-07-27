/*
File: SkullbonezSource/Runtime/Planning/ReplayGuideArcs.h
Purpose:
  Owns the fixed-capacity analytic Earth and Mars guide-ring publication.

Summary:
  ReplayRuntime supplies three live body-state values and the scene's mutual
  gravity constant. This cold presentation owner converts the two planet states
  into elliptic elements, samples two bounded polylines, and retains them for
  faint Legacy overlay drawing.

Glossary:
  Guide ring: Analytic two-body orbit shape used as a visual reference.
  Refresh deadline: Simulation time after which live body state may be sampled
    and both analytic rings rebuilt.
  Cold refresh: Explicit or five-second rebuild, never per-frame resampling.

Invariants:
  - Storage is two compile-time 96-point arrays; no runtime growth is possible.
  - The toggle defaults off and disabled updates return before orbital math.
  - Invalid, missing, or non-elliptic state publishes no drawable ring.
  - The simulated Replay prediction ribbon remains the ship trajectory truth.

Related:
  - SkullbonezSource/Maths/OrbitalMechanics.h
  - ReplayRuntime.cpp resolves live Physics-owned body values.
  - SkullbonezTests/TestReplayGuideArcs.cpp
*/
#pragma once

#include "../../Maths/OrbitalMechanics.h"
#include "../../Physics/PhysicsHandles.h"

#include <array>
#include <cstddef>
#include <span>

namespace SkullbonezCore::Runtime
{
inline constexpr std::size_t REPLAY_GUIDE_ARC_POINT_COUNT = 96u;

struct ReplayGuideBodyState
{
    Physics::PhysicsSceneObjectId id;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;
    bool valid = false;
};

struct ReplayGuideArcsUpdateInput
{
    ReplayGuideBodyState sun;
    ReplayGuideBodyState earth;
    ReplayGuideBodyState mars;
    float gravitationalConstant = 0.0f;
    double nowSeconds = 0.0;
    bool mutualGravityEnabled = false;
};

struct ReplayGuideArcsView
{
    std::span<const Math::Vector::Vector3> earthPoints;
    std::span<const Math::Vector::Vector3> marsPoints;
    Physics::PhysicsSceneObjectId sunId;
    Physics::PhysicsSceneObjectId earthId;
    Physics::PhysicsSceneObjectId marsId;
    bool enabled = false;
    bool valid = false;
};

class ReplayGuideArcs
{
  public:

    // Inverts the Legacy affordance and makes the next enabled update due.
    void Toggle() noexcept;

    // Applies idempotent startup/scene policy without repeated calls flipping
    // the user-visible state.
    void SetEnabled( bool enabled ) noexcept;
    void Reset() noexcept;
    bool Enabled() const noexcept;

    // Lets the composition boundary avoid all store scans between deadlines.
    bool RefreshDue( double nowSeconds ) const noexcept;
    void Update( const ReplayGuideArcsUpdateInput& input ) noexcept;
    ReplayGuideArcsView View() const noexcept;

  private:
    void ClearPublication() noexcept;

    std::array<Math::Vector::Vector3, REPLAY_GUIDE_ARC_POINT_COUNT> m_earthPoints;
    std::array<Math::Vector::Vector3, REPLAY_GUIDE_ARC_POINT_COUNT> m_marsPoints;
    Physics::PhysicsSceneObjectId m_sunId;
    Physics::PhysicsSceneObjectId m_earthId;
    Physics::PhysicsSceneObjectId m_marsId;
    double m_nextRefreshSeconds = 0.0;
    bool m_enabled = false;
    bool m_valid = false;
};
} // namespace SkullbonezCore::Runtime
