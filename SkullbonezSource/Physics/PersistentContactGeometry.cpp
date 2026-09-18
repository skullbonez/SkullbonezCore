#include "PersistentContactGeometry.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace SkullbonezCore::Physics
{
using namespace Math::Vector;
namespace
{
bool InsideFreshPatch( const Vector3& point, const ObjectContactManifold& patch, float distanceLimit )
{
    for ( uint8_t a = 0; a < patch.pointCount; ++a )
    {
        const auto delta = point - patch.points[a].point;
        if ( VectorMagSquared( delta ) <= distanceLimit * distanceLimit )
        {
            return true;
        }
        for ( uint8_t b = a + 1; b < patch.pointCount; ++b )
        {
            for ( uint8_t c = b + 1; c < patch.pointCount; ++c )
            {
                const auto ab = patch.points[b].point - patch.points[a].point;
                const auto ac = patch.points[c].point - patch.points[a].point;
                const float area = Dot( CrossProduct( ab, ac ), patch.normal );
                if ( fabsf( area ) <= 1.0e-6f * ( VectorMagSquared( ab ) + VectorMagSquared( ac ) ) )
                {
                    continue;
                }
                const float x = Dot( CrossProduct( patch.points[a].point - point, patch.points[b].point - point ), patch.normal );
                const float y = Dot( CrossProduct( patch.points[b].point - point, patch.points[c].point - point ), patch.normal );
                const float z = Dot( CrossProduct( patch.points[c].point - point, patch.points[a].point - point ), patch.normal );
                if ( ( x >= 0 && y >= 0 && z >= 0 ) || ( x <= 0 && y <= 0 && z <= 0 ) )
                {
                    return true;
                }
            }
        }
    }
    return false;
}
} // namespace

void RefreshHullContactPatch( std::span<const PersistentContactCacheEntry> cache, const PhysicsBodyHotFieldsConstView& hot, ObjectContactManifold& manifold )
{
    // A fresh point or edge is authoritative. Old face corners must not turn
    // that narrower geometry back into an area eligible for sleep.
    if ( manifold.pointCount < 3 || manifold.points[0].penetration < 0.0f )
    {
        return;
    }
    const Vector3 positionA = PhysicsBodyPosition( hot, manifold.bodyA );
    const Vector3 positionB = PhysicsBodyPosition( hot, manifold.bodyB );
    const auto rotationA = PhysicsBodyOrientation( hot, manifold.bodyA ).GetOrientationMatrix();
    const auto rotationB = PhysicsBodyOrientation( hot, manifold.bodyB ).GetOrientationMatrix();
    std::array<ObjectContactCandidate, 8> candidates {};
    int count = manifold.pointCount;
    float deepestFresh = 0.0f;
    for ( uint8_t i = 0; i < manifold.pointCount; ++i )
    {
        deepestFresh = (std::max)( deepestFresh, manifold.points[i].penetration );
    }
    for ( uint8_t i = 0; i < manifold.pointCount; ++i )
    {
        candidates[i] = { manifold.points[i].point, manifold.points[i].penetration, manifold.points[i].featureId };
    }
    const uint64_t pair = static_cast<uint64_t>( MakePersistentContactCacheKey( manifold.bodyA, manifold.bodyB, 0 ) ) & 0xffffffff00000000ull;
    auto first = std::lower_bound( cache.begin(), cache.end(), static_cast<int64_t>( pair ), []( const auto& entry, int64_t key ) { return entry.key < key; } );
    for ( auto entry = first; entry != cache.end() && ( static_cast<uint64_t>( entry->key ) & 0xffffffff00000000ull ) == pair; ++entry )
    {
        const auto& geometry = entry->geometry;
        if ( geometry.lifetime == 0 || Dot( rotationA * geometry.localNormalA, manifold.normal ) < 0.98f || Dot( rotationB * geometry.localNormalB, manifold.normal ) < 0.98f )
        {
            continue;
        }
        const Vector3 anchorA = positionA + rotationA * geometry.localAnchorA;
        const Vector3 anchorB = positionB + rotationB * geometry.localAnchorB;
        const Vector3 delta = anchorB - anchorA;
        const float separation = Dot( delta, manifold.normal );
        const Vector3 tangentDrift = delta - manifold.normal * separation;
        const Vector3 point = ( anchorA + anchorB ) * 0.5f;
        if ( separation > geometry.breakingDistance || -separation > deepestFresh + geometry.breakingDistance ||
             VectorMagSquared( tangentDrift ) > geometry.breakingDistance * geometry.breakingDistance || !InsideFreshPatch( point, manifold, geometry.breakingDistance ) )
        {
            continue;
        }
        const ObjectContactCandidate retained { point, -separation, static_cast<uint32_t>( entry->key ) };
        int replacement = -1;
        for ( int i = 0; i < count; ++i )
        {
            if ( candidates[i].featureId == retained.featureId || VectorMagSquared( candidates[i].point - point ) <= 1.0e-10f )
            {
                replacement = i;
                break;
            }
        }
        if ( replacement >= 0 )
        {
            candidates[replacement] = retained;
        }
        else if ( count < static_cast<int>( candidates.size() ) )
        {
            candidates[count++] = retained;
        }
    }
    // The shared reducer retains deepest plus maximum-area points and orders
    // ties by geometric feature identity, independent of arrival order.
    const auto selected = SelectObjectContactCandidateIndices( candidates.data(), count, manifold.normal );
    manifold.pointCount = selected.count;
    for ( uint8_t i = 0; i < selected.count; ++i )
    {
        const auto& candidate = candidates[selected.indices[i]];
        manifold.points[i] = { candidate.point, candidate.point - positionA, candidate.point - positionB, candidate.penetration, candidate.featureId, -candidate.penetration };
    }
}
} // namespace SkullbonezCore::Physics
