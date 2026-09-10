#include "ConvexDistance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace SkullbonezCore::Physics
{
namespace
{
using namespace Math::Vector;
using namespace Math::CollisionDetection;

struct SupportVertex
{
    Vector3 point = ZERO_VECTOR;
    uint32_t index = 0;
};

SupportVertex LocalSupport( const BoundingSphere& sphere, const Vector3& )
{
    // Why: treating the sphere as a point plus a radius gives exact sphere
    // distances without iterating over a smooth support surface.
    return { sphere.GetPosition(), 0u };
}

SupportVertex LocalSupport( const BoundingBox& box, const Vector3& direction )
{
    const Vector3 extent = box.GetHalfExtents();
    const uint32_t index = ( direction.x > 0.0f ? 1u : 0u ) | ( direction.y > 0.0f ? 2u : 0u ) |
                           ( direction.z > 0.0f ? 4u : 0u );
    return { box.GetPosition() + Vector3( index & 1u ? extent.x : -extent.x, index & 2u ? extent.y : -extent.y,
                                          index & 4u ? extent.z : -extent.z ),
             index };
}

SupportVertex LocalSupport( const ConvexHullShape& hull, const Vector3& direction )
{
    SupportVertex result { hull.GetVertex( 0 ), 0u };
    float best = Dot( result.point, direction );
    for ( uint16_t index = 1; index < hull.GetVertexCount(); ++index )
    {
        const float projection = Dot( hull.GetVertex( index ), direction );
        if ( projection > best )
        {
            best = projection;
            result = { hull.GetVertex( index ), index };
        }
    }
    result.point += hull.GetPosition();
    return result;
}

SupportVertex WorldSupport( const ObjectContactBodyView& body, const CollisionShapeReference& shape,
                            const Vector3& direction )
{
    const auto rotation = body.orientation.GetOrientationMatrix();
    SupportVertex result = VisitCollisionShape( shape, [&]( const auto& value )
                                                { return LocalSupport( value, rotation.TransposeMultiply( direction ) ); } );
    result.point = body.position + rotation * result.point;
    return result;
}

float SphereMargin( const CollisionShapeReference& shape )
{
    const BoundingSphere* sphere = GetShapeIf<BoundingSphere>( &shape );
    return sphere ? sphere->GetRadius() : 0.0f;
}

struct DistanceVertex
{
    Vector3 pointA = ZERO_VECTOR;
    Vector3 pointB = ZERO_VECTOR;
    Vector3 difference = ZERO_VECTOR;
    uint32_t indexA = 0;
    uint32_t indexB = 0;
};

double PreciseDot( const Vector3& a, const Vector3& b )
{
    return static_cast<double>( a.x ) * b.x + static_cast<double>( a.y ) * b.y + static_cast<double>( a.z ) * b.z;
}

double PreciseTriple( const Vector3& a, const Vector3& b, const Vector3& c )
{
    return static_cast<double>( a.x ) * ( static_cast<double>( b.y ) * c.z - static_cast<double>( b.z ) * c.y ) +
           static_cast<double>( a.y ) * ( static_cast<double>( b.z ) * c.x - static_cast<double>( b.x ) * c.z ) +
           static_cast<double>( a.z ) * ( static_cast<double>( b.x ) * c.y - static_cast<double>( b.y ) * c.x );
}

// Invariant: weights describe the closest point within the selected simplex
// face. Negative barycentric coordinates reject its plane projection; its
// edges and vertices are considered separately, including degenerate faces.
bool ProjectFace( const std::array<DistanceVertex, 4>& vertices, const std::array<int, 4>& indices, int count,
                  std::array<double, 4>& weights )
{
    weights = {};
    weights[0] = 1.0f;
    if ( count == 1 )
    {
        return true;
    }
    const Vector3 origin = vertices[indices[0]].difference;
    const Vector3 u = vertices[indices[1]].difference - origin;
    const double uu = PreciseDot( u, u );
    if ( uu <= 0.0f )
    {
        return false;
    }
    if ( count == 2 )
    {
        weights[1] = -PreciseDot( origin, u ) / uu;
    }
    else
    {
        const Vector3 v = vertices[indices[2]].difference - origin;
        if ( count == 3 )
        {
            // Why: thin limbs against long wall edges create nearly parallel
            // simplex edges. Double products keep the Gram determinant and
            // barycentric projection from cycling after float cancellation.
            const double uv = PreciseDot( u, v );
            const double vv = PreciseDot( v, v );
            const double determinant = uu * vv - uv * uv;
            if ( determinant <= 1.0e-12f * uu * vv )
            {
                return false;
            }
            const double ou = PreciseDot( origin, u );
            const double ov = PreciseDot( origin, v );
            weights[1] = ( uv * ov - vv * ou ) / determinant;
            weights[2] = ( uv * ou - uu * ov ) / determinant;
        }
        else
        {
            const Vector3 w = vertices[indices[3]].difference - origin;
            const double determinant = PreciseTriple( u, v, w );
            if ( determinant == 0.0f )
            {
                return false;
            }
            weights[1] = -PreciseTriple( origin, v, w ) / determinant;
            weights[2] = -PreciseTriple( u, origin, w ) / determinant;
            weights[3] = -PreciseTriple( u, v, origin ) / determinant;
        }
    }
    for ( int index = 1; index < count; ++index )
    {
        weights[0] -= weights[index];
    }
    for ( int index = 0; index < count; ++index )
    {
        if ( !std::isfinite( weights[index] ) || weights[index] < 0.0f )
        {
            return false;
        }
    }
    return true;
}

class DistanceSimplex
{
  public:
    std::array<DistanceVertex, 4> vertices;
    std::array<double, 4> weights {};
    int count = 0;

    Vector3 Reduce()
    {
        float best = std::numeric_limits<float>::max();
        std::array<int, 4> bestIndices {};
        std::array<double, 4> bestWeights {};
        int bestCount = 0;
        Vector3 closest = ZERO_VECTOR;
        // ENGINE-SPECIFIC: exhaustive faces of a four-point simplex cost at
        // most 15 projections and use one explicit tie order in all builds.
        for ( unsigned mask = 1; mask < ( 1u << count ); ++mask )
        {
            std::array<int, 4> indices {};
            int faceCount = 0;
            for ( int index = 0; index < count; ++index )
            {
                if ( mask & ( 1u << index ) )
                {
                    indices[faceCount++] = index;
                }
            }
            std::array<double, 4> candidateWeights {};
            if ( !ProjectFace( vertices, indices, faceCount, candidateWeights ) )
            {
                continue;
            }
            Vector3 point = ZERO_VECTOR;
            for ( int index = 0; index < faceCount; ++index )
            {
                point += vertices[indices[index]].difference * static_cast<float>( candidateWeights[index] );
            }
            const float distanceSquared = Dot( point, point );
            if ( distanceSquared < best )
            {
                best = distanceSquared;
                closest = point;
                bestIndices = indices;
                bestWeights = candidateWeights;
                bestCount = faceCount;
            }
        }
        // Indices are increasing, so in-place compaction cannot clobber a
        // vertex that is still needed by the chosen face.
        for ( int index = 0; index < bestCount; ++index )
        {
            vertices[index] = vertices[bestIndices[index]];
        }
        weights = bestWeights;
        count = bestCount;
        return closest;
    }

    void Publish( ConvexDistanceResult& result ) const
    {
        std::array<uint32_t, 4> features {};
        for ( int index = 0; index < count; ++index )
        {
            result.pointA += vertices[index].pointA * static_cast<float>( weights[index] );
            result.pointB += vertices[index].pointB * static_cast<float>( weights[index] );
            features[index] = ( vertices[index].indexA << 16u ) | vertices[index].indexB;
        }
        std::sort( features.begin(), features.begin() + count );
        uint32_t hash = 2166136261u;
        for ( int index = 0; index < count; ++index )
        {
            hash = ( hash ^ features[index] ) * 16777619u;
        }
        result.featureId = hash;
    }
};
} // namespace

ConvexDistanceResult ComputeConvexDistance( const ObjectContactBodyView& a, const CollisionShapeReference& shapeA,
                                            const ObjectContactBodyView& b, const CollisionShapeReference& shapeB )
{
    // CATTO REF: https://box2d.org/files/ErinCatto_GJK_GDC2010.pdf
    // Distance GJK retains paired support witnesses while approaching the
    // origin of the Minkowski difference. Sphere radii are applied afterward.
    // Why: world-space subtraction at scene coordinates near 500 metres loses
    // the small gap used for convergence. Keep every simplex in A's translated
    // frame and move only the final witnesses back to world coordinates.
    ObjectContactBodyView localA = a;
    ObjectContactBodyView localB = b;
    localA.position = ZERO_VECTOR;
    localB.position -= a.position;
    DistanceSimplex simplex;
    Vector3 closest = localB.position;
    if ( Dot( closest, closest ) == 0.0f )
    {
        closest = Vector3( 1.0f, 0.0f, 0.0f );
    }
    ConvexDistanceResult result;
    std::array<uint64_t, 32> visitedSupports {};
    int visitedCount = 0;
    for ( int iteration = 0; iteration < 32; ++iteration )
    {
        result.iterations = iteration + 1;
        const SupportVertex supportA = WorldSupport( localA, shapeA, closest );
        const SupportVertex supportB = WorldSupport( localB, shapeB, -closest );
        const DistanceVertex vertex { supportA.point, supportB.point, supportB.point - supportA.point, supportA.index,
                                      supportB.index };
        const float distanceSquared = Dot( closest, closest );
        // Invariant: support identities include both shapes. Retain discarded
        // vertices too: roundoff at an edge can alternate between two faces
        // after simplex reduction, without improving the closest distance.
        const uint64_t supportIdentity = ( static_cast<uint64_t>( vertex.indexA ) << 32u ) | vertex.indexB;
        const bool duplicate = std::find( visitedSupports.begin(), visitedSupports.begin() + visitedCount,
                                          supportIdentity ) != visitedSupports.begin() + visitedCount;
        if ( simplex.count > 0 && ( duplicate || distanceSquared - Dot( closest, vertex.difference ) <=
                                                     1.0e-6f * (std::max)( 1.0f, distanceSquared ) ) )
        {
            result.converged = true;
            break;
        }
        visitedSupports[visitedCount++] = supportIdentity;
        simplex.vertices[simplex.count++] = vertex;
        closest = simplex.Reduce();
        if ( Dot( closest, closest ) <= 1.0e-12f || simplex.count == 4 )
        {
            result.converged = true;
            break;
        }
    }
    simplex.Publish( result );
    const Vector3 delta = result.pointB - result.pointA;
    const float distance = sqrtf( Dot( delta, delta ) );
    if ( distance > 1.0e-6f )
    {
        result.normal = delta / distance;
        result.pointA += result.normal * SphereMargin( shapeA );
        result.pointB -= result.normal * SphereMargin( shapeB );
    }
    result.separation = ( distance > 1.0e-6f ? distance : 0.0f ) - SphereMargin( shapeA ) - SphereMargin( shapeB );
    result.pointA += a.position;
    result.pointB += a.position;
    return result;
}
} // namespace SkullbonezCore::Physics
