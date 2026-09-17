// Pure grass contact and recovery rules. Time is measured in fixed Physics ticks;
// these presentation values never mutate a body or retain an engine reference.
#pragma once

#include "../../Maths/Vector3.h"
#include "../../Maths/Quaternion.h"
#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace SkullbonezCore::Runtime
{
// Matches surface_blades.hlsl's integer placement seed exactly. Sampling each
// generated root avoids planar interpolation across arbitrary terrain grids.
inline uint32_t GrassPlacementHash( uint32_t value ) noexcept
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ ( value >> 16 );
}
inline std::array<float, 2> GrassBladeUv( float x, float z, uint32_t blade ) noexcept
{
    const uint32_t seed = GrassPlacementHash( std::bit_cast<uint32_t>( x ) ^ GrassPlacementHash( std::bit_cast<uint32_t>( z ) ) ^ ( blade * 131u ) );
    return { ( GrassPlacementHash( seed ) & 65535u ) / 65535.0f, ( GrassPlacementHash( seed + 17u ) & 65535u ) / 65535.0f };
}

enum class GrassFootprintShape : uint8_t
{
    Sphere,
    Box
};

// Invariant: one footprint describes a single stable scene identity at one
// completed tick. Box axes are orthonormal; sphere radius is halfExtents.x.
struct GrassFootprint
{
    uint32_t sceneObjectId = 0;
    GrassFootprintShape shape = GrassFootprintShape::Sphere;
    Math::Vector::Vector3 center;
    Math::Vector::Vector3 halfExtents;
    Math::Orientation::Quaternion orientation;
    std::array<Math::Vector::Vector3, 3> axes { Math::Vector::Vector3( 1.0f, 0.0f, 0.0f ), Math::Vector::Vector3( 0.0f, 1.0f, 0.0f ), Math::Vector::Vector3( 0.0f, 0.0f, 1.0f ) };
    float bendX = 1.0f;
    float bendZ = 0.0f;
};

inline float GrassDot( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Returns occupied blade length, not a projected bounding rectangle. A ray
// begins at the terrain root and follows its unit normal up to bladeHeight.
inline float GrassFootprintCompression( const GrassFootprint& footprint, const Math::Vector::Vector3& root, const Math::Vector::Vector3& normal, float bladeHeight ) noexcept
{
    if ( footprint.sceneObjectId == 0 || !std::isfinite( bladeHeight ) || bladeHeight <= 0.0f )
    {
        return 0.0f;
    }

    const Math::Vector::Vector3 offset = root - footprint.center;
    float enter = 0.0f;
    float leave = bladeHeight;

    if ( footprint.shape == GrassFootprintShape::Sphere )
    {
        const float radius = footprint.halfExtents.x;
        const float along = GrassDot( offset, normal );
        const float discriminant = along * along - GrassDot( offset, offset ) + radius * radius;

        if ( radius <= 0.0f || discriminant < 0.0f )
        {
            return 0.0f;
        }

        const float reach = std::sqrt( discriminant );
        enter = (std::max)( enter, -along - reach );
        leave = (std::min)( leave, -along + reach );
    }
    else
    {
        const float extents[3] = { footprint.halfExtents.x, footprint.halfExtents.y, footprint.halfExtents.z };

        for ( std::size_t axis = 0; axis < 3; ++axis )
        {
            if ( extents[axis] <= 0.0f )
            {
                return 0.0f;
            }

            const float origin = GrassDot( offset, footprint.axes[axis] );
            const float direction = GrassDot( normal, footprint.axes[axis] );

            if ( std::fabs( direction ) < 1.0e-6f )
            {
                if ( std::fabs( origin ) > extents[axis] )
                {
                    return 0.0f;
                }
                continue;
            }

            // Keep a finite divisor explicit even when the compiler specializes
            // an axis-aligned footprint with a constant parallel direction.
            const float divisor = std::copysign( (std::max)( std::fabs( direction ), 1.0e-6f ), direction );
            float first = ( -extents[axis] - origin ) / divisor;
            float last = ( extents[axis] - origin ) / divisor;

            if ( first > last )
            {
                std::swap( first, last );
            }

            enter = (std::max)( enter, first );
            leave = (std::min)( leave, last );
        }
    }

    return leave >= enter ? std::clamp( 1.0f - enter / bladeHeight, 0.0f, 1.0f ) : 0.0f;
}

// Invariant: maximum release time is the upper envelope of linear finite
// recovery curves with the same duration. Order-independent overlap therefore
// needs one record per cell, not a growing list of colliding blades or stamps.
class GrassDeformationCell
{
  public:
    void Stamp( double tick, double recoveryTicks, float compression, const GrassFootprint& footprint ) noexcept
    {
        if ( footprint.sceneObjectId == 0 || !std::isfinite( tick ) || !std::isfinite( recoveryTicks ) || recoveryTicks <= 0.0 || !std::isfinite( compression ) || compression <= 0.0f ||
             !std::isfinite( footprint.bendX ) || !std::isfinite( footprint.bendZ ) )
        {
            return;
        }

        const double release = tick + std::clamp( compression, 0.0f, 1.0f ) * recoveryTicks;

        // Stable identity breaks equal-pressure ties independently of body-row
        // order. Later samples of the same body update its bending direction.
        if ( release < m_releaseTick || ( release == m_releaseTick && m_sourceId != 0 && ( footprint.sceneObjectId > m_sourceId || ( footprint.sceneObjectId == m_sourceId && tick < m_stampTick ) ) ) )
        {
            return;
        }

        const float length = std::sqrt( footprint.bendX * footprint.bendX + footprint.bendZ * footprint.bendZ );
        m_releaseTick = release;
        m_stampTick = tick;
        m_sourceId = footprint.sceneObjectId;
        m_bendX = length > 1.0e-6f ? footprint.bendX / length : 1.0f;
        m_bendZ = length > 1.0e-6f ? footprint.bendZ / length : 0.0f;
    }

    float Compression( double tick, double recoveryTicks ) const noexcept
    {
        if ( m_sourceId == 0 || !std::isfinite( tick ) || !std::isfinite( recoveryTicks ) || recoveryTicks <= 0.0 )
        {
            return 0.0f;
        }

        return static_cast<float>( std::clamp( ( m_releaseTick - tick ) / recoveryTicks, 0.0, 1.0 ) );
    }

    uint32_t SourceId() const noexcept
    {
        return m_sourceId;
    }
    float BendX() const noexcept
    {
        return m_bendX;
    }
    float BendZ() const noexcept
    {
        return m_bendZ;
    }

  private:
    double m_releaseTick = 0.0;
    double m_stampTick = 0.0;
    uint32_t m_sourceId = 0;
    float m_bendX = 0.0f;
    float m_bendZ = 0.0f;
};

// Invariant: one cache belongs to one generation/recording/branch/terrain
// revision. Reverse time and skipped ticks require reconstruction, never decay
// by a negative interval or reuse of current-world footprints in recorded time.
class GrassTimeCursor
{
  public:
    struct Context
    {
        uint64_t generation = 0;
        uint64_t recording = 0;
        uint64_t terrainRevision = 0;
        uint32_t branch = 0;
        bool operator==( const Context& ) const = default;
    };

    enum class Update : uint8_t
    {
        Rebuild,
        Advance,
        Unchanged
    };

    Update Select( const Context& context, int64_t tick ) noexcept
    {
        const bool sameContext = m_valid && context == m_context;
        const Update update = sameContext && tick == m_tick                                                            ? Update::Unchanged
                              : sameContext && m_tick != ( std::numeric_limits<int64_t>::max )() && tick == m_tick + 1 ? Update::Advance
                                                                                                                       : Update::Rebuild;
        m_context = context;
        m_tick = tick;
        m_valid = true;
        return update;
    }

    void Reset() noexcept
    {
        m_valid = false;
    }

  private:
    Context m_context;
    int64_t m_tick = 0;
    bool m_valid = false;
};
} // namespace SkullbonezCore::Runtime
