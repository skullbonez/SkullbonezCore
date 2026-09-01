/*
Purpose:
  Defines checked, synchronous values shared by broadphase stages.

Invariants:
  - Activity rows use one dense body index domain; optional motion/expansion
    rows are either absent for conservative fallback or cover that domain.
  - Awake indices are unique, strictly increasing, and inside the domain.
  - Sweep/contact scalars are finite and non-negative.
*/
#pragma once

#include "../Core/FatalError.h"
#include "PhysicsMotionEligibility.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace SkullbonezCore
{
namespace Physics
{
class BroadphaseBodyActivityView
{
  private:
    int m_bodyCount = 0;
    std::span<const uint8_t> m_sleepState;
    std::span<const int> m_awakeBodyIndices;
    std::span<const uint8_t> m_motionEligibilityState;
    std::span<const float> m_angularExpansion;

  public:
    static bool IsValid( int bodyCount, std::span<const uint8_t> sleepState, std::span<const int> awakeBodyIndices,
                         std::span<const uint8_t> motionEligibilityState, std::span<const float> angularExpansion ) noexcept
    {
        if ( bodyCount < 0 || sleepState.size() != static_cast<std::size_t>( bodyCount ) ||
             ( !motionEligibilityState.empty() && motionEligibilityState.size() != static_cast<std::size_t>( bodyCount ) ) ||
             ( !angularExpansion.empty() && angularExpansion.size() != static_cast<std::size_t>( bodyCount ) ) )
        {
            return false;
        }

        int previousIndex = -1;

        for ( int bodyIndex : awakeBodyIndices )
        {
            if ( bodyIndex <= previousIndex || bodyIndex >= bodyCount )
            {
                return false;
            }

            previousIndex = bodyIndex;
        }

        return true;
    }

    BroadphaseBodyActivityView( int bodyCount, std::span<const uint8_t> sleepState, std::span<const int> awakeBodyIndices,
                                std::span<const uint8_t> motionEligibilityState, std::span<const float> angularExpansion )
        : m_bodyCount( bodyCount ), m_sleepState( sleepState ), m_awakeBodyIndices( awakeBodyIndices ),
          m_motionEligibilityState( motionEligibilityState ), m_angularExpansion( angularExpansion )
    {
        if ( !IsValid( bodyCount, sleepState, awakeBodyIndices, motionEligibilityState, angularExpansion ) )
        {
            SB_FATAL( "Physics/BroadphaseBodyActivityView",
                      "Broadphase activity rows are misaligned: bodies=%d sleep=%zu awake=%zu motion=%zu angular=%zu.",
                      bodyCount, sleepState.size(), awakeBodyIndices.size(), motionEligibilityState.size(),
                      angularExpansion.size() );
        }
    }

    int BodyCount() const noexcept
    {
        return m_bodyCount;
    }

    std::span<const int> AwakeBodyIndices() const noexcept
    {
        return m_awakeBodyIndices;
    }

    bool IsSleeping( int bodyIndex ) const noexcept
    {
        return bodyIndex >= 0 && bodyIndex < m_bodyCount && m_sleepState[static_cast<std::size_t>( bodyIndex )] != 0u;
    }

    bool IsLinearPromoted( int bodyIndex ) const noexcept
    {
        // Conservative fallback preserves the former missing-row policy.
        return bodyIndex < 0 || bodyIndex >= m_bodyCount || m_motionEligibilityState.empty() ||
               ( m_motionEligibilityState[static_cast<std::size_t>( bodyIndex )] &
                 PhysicsMotionEligibilityLinearPromoted ) != 0u;
    }

    float AngularExpansion( int bodyIndex ) const noexcept
    {
        return bodyIndex >= 0 && bodyIndex < m_bodyCount && !m_angularExpansion.empty()
                   ? m_angularExpansion[static_cast<std::size_t>( bodyIndex )]
                   : 0.0f;
    }
};

class BroadphaseSweepContactEnvelope
{
  private:
    float m_deltaTime = 0.0f;
    float m_contactSkin = 0.0f;
    float m_contactEpsilon = 0.0f;

  public:
    static bool IsValid( float deltaTime, float contactSkin, float contactEpsilon ) noexcept
    {
        return std::isfinite( deltaTime ) && deltaTime >= 0.0f && std::isfinite( contactSkin ) && contactSkin >= 0.0f &&
               std::isfinite( contactEpsilon ) && contactEpsilon >= 0.0f;
    }

    BroadphaseSweepContactEnvelope( float deltaTime, float contactSkin, float contactEpsilon )
        : m_deltaTime( deltaTime ), m_contactSkin( contactSkin ), m_contactEpsilon( contactEpsilon )
    {
        if ( !IsValid( deltaTime, contactSkin, contactEpsilon ) )
        {
            SB_FATAL( "Physics/BroadphaseSweepContactEnvelope",
                      "Broadphase sweep/contact values are invalid: dt=%.9g skin=%.9g epsilon=%.9g.", deltaTime, contactSkin,
                      contactEpsilon );
        }
    }

    float DeltaTime() const noexcept
    {
        return m_deltaTime;
    }

    float ContactSkin() const noexcept
    {
        return m_contactSkin;
    }

    float ContactEpsilon() const noexcept
    {
        return m_contactEpsilon;
    }
};
} // namespace Physics
} // namespace SkullbonezCore
