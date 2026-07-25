/*
File: SkullbonezSource/Runtime/Replay/ReplayGuideArcs.cpp
Purpose:
  Builds fixed-capacity analytic planet guide rings.

Summary:
  A refresh derives sun-relative orbital elements from live state, samples the
  shared orbital library into owner storage, translates points back to world
  space, and publishes only when both planet rings succeed.

Glossary:
  Central parameter: Scene gravitational constant multiplied by fixed sun mass.
  World translation: Adding the live sun position to a relative orbit point.

Invariants:
  - Refresh cadence is at least five simulation seconds after every attempt.
  - Both rings publish atomically; a partial orbital solve draws neither.
  - The final point reconnects to point zero without adding a duplicate sample.

Related:
  - ReplayGuideArcs.h
  - SkullbonezSource/Maths/OrbitalMechanics.cpp
*/
#include "ReplayGuideArcs.h"

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr double GUIDE_ARC_REFRESH_SECONDS = 5.0;

bool ValidGuideBody( const ReplayGuideBodyState& body ) noexcept
{
    return body.valid && body.id.value != 0 && body.mass > 0.0f;
}
} // namespace


void ReplayGuideArcs::Toggle() noexcept
{
    SetEnabled( !m_enabled );
}


void ReplayGuideArcs::SetEnabled( bool enabled ) noexcept
{
    if ( m_enabled == enabled )
    {
        return;
    }

    m_enabled = enabled;
    m_nextRefreshSeconds = 0.0;
    if ( !m_enabled )
    {
        ClearPublication();
    }
}


void ReplayGuideArcs::Reset() noexcept
{
    m_enabled = false;
    m_sunId = {};
    m_earthId = {};
    m_marsId = {};
    m_nextRefreshSeconds = 0.0;
    ClearPublication();
}


bool ReplayGuideArcs::Enabled() const noexcept
{
    return m_enabled;
}


bool ReplayGuideArcs::RefreshDue( double nowSeconds ) const noexcept
{
    return m_enabled && nowSeconds >= m_nextRefreshSeconds;
}


void ReplayGuideArcs::ClearPublication() noexcept
{
    m_valid = false;
}


void ReplayGuideArcs::Update( const ReplayGuideArcsUpdateInput& input ) noexcept
{
    // Why: the disabled fast path is the common case and does not even resolve
    // orbital elements, preserving the plan's zero-cost default presentation.
    if ( !m_enabled )
    {
        return;
    }

    if ( !input.mutualGravityEnabled )
    {
        m_nextRefreshSeconds = input.nowSeconds + GUIDE_ARC_REFRESH_SECONDS;
        ClearPublication();
        return;
    }

    // Invariant: all live-body resolution and orbital math remain behind this
    // five-second gate. Scene loads and toggles explicitly reset the deadline.
    if ( input.nowSeconds < m_nextRefreshSeconds )
    {
        return;
    }

    if ( input.gravitationalConstant <= 0.0f || !ValidGuideBody( input.sun ) || !ValidGuideBody( input.earth ) ||
         !ValidGuideBody( input.mars ) )
    {
        m_nextRefreshSeconds = input.nowSeconds + GUIDE_ARC_REFRESH_SECONDS;
        ClearPublication();
        return;
    }

    const float mu = input.gravitationalConstant * input.sun.mass;
    Math::Orbital::OrbitalElements earthElements;
    Math::Orbital::OrbitalElements marsElements;
    const Math::Orbital::OrbitalStatus earthStatus = Math::Orbital::ElementsFromState(
        input.earth.position - input.sun.position,
        input.earth.linearVelocity - input.sun.linearVelocity,
        mu,
        earthElements );

    const Math::Orbital::OrbitalStatus marsStatus = Math::Orbital::ElementsFromState(
        input.mars.position - input.sun.position,
        input.mars.linearVelocity - input.sun.linearVelocity,
        mu,
        marsElements );

    const std::size_t earthCount = earthStatus == Math::Orbital::OrbitalStatus::Ok
                                       ? Math::Orbital::SampleOrbitPolyline( earthElements, m_earthPoints )
                                       : 0u;

    const std::size_t marsCount = marsStatus == Math::Orbital::OrbitalStatus::Ok
                                      ? Math::Orbital::SampleOrbitPolyline( marsElements, m_marsPoints )
                                      : 0u;

    if ( earthCount != REPLAY_GUIDE_ARC_POINT_COUNT || marsCount != REPLAY_GUIDE_ARC_POINT_COUNT )
    {
        m_nextRefreshSeconds = input.nowSeconds + GUIDE_ARC_REFRESH_SECONDS;
        ClearPublication();
        return;
    }

    for ( Math::Vector::Vector3& point : m_earthPoints )
    {
        point += input.sun.position;
    }

    for ( Math::Vector::Vector3& point : m_marsPoints )
    {
        point += input.sun.position;
    }

    m_sunId = input.sun.id;
    m_earthId = input.earth.id;
    m_marsId = input.mars.id;
    m_nextRefreshSeconds = input.nowSeconds + GUIDE_ARC_REFRESH_SECONDS;
    m_valid = true;
}


ReplayGuideArcsView ReplayGuideArcs::View() const noexcept
{
    return {
        m_valid ? std::span<const Math::Vector::Vector3>( m_earthPoints ) : std::span<const Math::Vector::Vector3>(),
        m_valid ? std::span<const Math::Vector::Vector3>( m_marsPoints ) : std::span<const Math::Vector::Vector3>(),
        m_sunId,
        m_earthId,
        m_marsId,
        m_enabled,
        m_valid };
}
} // namespace SkullbonezCore::Runtime
