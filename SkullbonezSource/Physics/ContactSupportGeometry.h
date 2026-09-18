#pragma once

#include "../Maths/Vector3.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace SkullbonezCore::Physics
{
// A bounded support footprint projected perpendicular to the engine's vertical
// gravity. Points are world offsets from the supported body's center of mass.
// Rebuilt each tick: removing a contact cannot leave stale support behind.
class ContactSupportGeometry
{
    std::array<Math::Vector::Vector3, 8> m_points {};
    uint8_t m_count = 0;

    static float Cross( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b )
    {
        return a.x * b.z - a.z * b.x;
    }

    bool Contains( const Math::Vector::Vector3& target ) const
    {
        for ( uint8_t a = 0; a < m_count; ++a )
        {
            for ( uint8_t b = a + 1; b < m_count; ++b )
            {
                for ( uint8_t c = b + 1; c < m_count; ++c )
                {
                    const auto ab = m_points[b] - m_points[a];
                    const auto ac = m_points[c] - m_points[a];
                    const float area = Cross( ab, ac );
                    const float scale = ab.x * ab.x + ab.z * ab.z + ac.x * ac.x + ac.z * ac.z;
                    if ( fabsf( area ) <= 1.0e-6f * scale )
                    {
                        continue;
                    }
                    const float x = Cross( m_points[a] - target, m_points[b] - target );
                    const float y = Cross( m_points[b] - target, m_points[c] - target );
                    const float z = Cross( m_points[c] - target, m_points[a] - target );
                    if ( ( x >= 0 && y >= 0 && z >= 0 ) || ( x <= 0 && y <= 0 && z <= 0 ) )
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

  public:
    void Add( const Math::Vector::Vector3& offset )
    {
        if ( !std::isfinite( offset.x ) || !std::isfinite( offset.z ) )
        {
            return;
        }
        constexpr float directions[8][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
        // Keep actual extreme contact points in eight fixed directions. The
        // chosen subset is independent of row order, even when more than eight
        // contacts arrive. Missing an extreme direction can only keep a body
        // awake: no invented point can enlarge the real support footprint.
        for ( uint8_t i = 0; i < m_points.size(); ++i )
        {
            const float projection = directions[i][0] * offset.x + directions[i][1] * offset.z;
            const float previous = directions[i][0] * m_points[i].x + directions[i][1] * m_points[i].z;
            const bool canonicalTie = offset.x < m_points[i].x || ( offset.x == m_points[i].x && offset.z < m_points[i].z );
            if ( m_count == 0 || projection > previous || ( projection == previous && canonicalTie ) )
            {
                m_points[i] = offset;
            }
        }
        m_count = static_cast<uint8_t>( m_points.size() );
    }

    bool SupportsCenter() const
    {
        if ( m_count < 3 )
        {
            return false;
        }
        float radius = 0.0f;
        for ( uint8_t i = 0; i < m_count; ++i )
        {
            radius = (std::max)( radius, fabsf( m_points[i].x ) + fabsf( m_points[i].z ) );
        }
        const float margin = (std::max)( 1.0e-6f, radius * 1.0e-4f );
        // Test a small cross around the COM so an exact outer-edge balance
        // cannot sleep, while a triangulation diagonal through the COM can.
        return Contains( { margin, 0, 0 } ) && Contains( { -margin, 0, 0 } ) && Contains( { 0, 0, margin } ) && Contains( { 0, 0, -margin } );
    }
};
} // namespace SkullbonezCore::Physics
