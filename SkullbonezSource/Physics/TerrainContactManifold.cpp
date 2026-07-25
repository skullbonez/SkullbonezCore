/*
File: SkullbonezSource/Physics/TerrainContactManifold.cpp
Purpose:
  Builds value-based terrain sweep and manifold reports for physics-owned bodies.

Summary:
  Terrain contact is geometry preparation only. It finds time of impact, contact
  points, tangents, and support policy; the persistent contact solver owns actual
  velocity response, warm starting, and sleep decisions.

Glossary:
  CCD (Continuous Collision Detection): Swept collision test that asks whether a
    body hits during a tick, not only where it ends the tick.
  OBB (Oriented Bounding Box): Box with rotation, used for exact terrain vertex
    tests.
  Contact patch: The set of terrain-touching features that become solver rows.

Invariants:
  - The helper must not mutate body, collider, terrain, or legacy model state.
  - A collision ratio in [ZERO_TAKE_TOLERANCE, 1] is converted to seconds exactly
    once by SweepTerrainContact.

Related:
  - SkullbonezSource/Physics/TerrainContactManifold.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/physics-overview.md
*/
#include "TerrainContactManifold.h"
#include "../Assets/AssetKeys.h"

#include "../Core/Common.h"
#include "../Core/FatalError.h"
#include "../Core/Profiler.h"
#include "../Maths/GeometricMath.h"
#include "../World/Terrain.h"
#include "../World/TerrainSupportClassifier.h"
#include "ContactSolverCommon.h"

#include <algorithm>
#include <type_traits>
#include <variant>

using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;

namespace
{
RotationMatrix GetOrientationMatrix( const TerrainContactBodyView& body )
{
    Quaternion q = body.orientation;
    return q.GetOrientationMatrix();
}

bool GetClosestBoxTerrainVertex( SkullbonezCore::Core::Profiler* profiler,
                                 const TerrainContactBodyView& body,
                                 const BoundingBox& box,
                                 Vector3& outVertex,
                                 float& outTerrainHeight,
                                 Plane& outPlane,
                                 float& outGap )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/BoxClosestVertexProbe" );

    if ( body.terrain == nullptr )
    {
        return false;
    }

    const Vector3& he = box.GetHalfExtents();
    const RotationMatrix rotMat = GetOrientationMatrix( body );

    bool found = false;
    float bestGap = 1.0e30f;
    for ( int v = 0; v < 8; ++v )
    {
        // The low three bits enumerate the OBB corner signs. Sampling each
        // world-space corner against its own terrain height keeps sleep/contact
        // decisions tied to the visible geometry instead of a center XZ sample.
        const Vector3 local( ( v & 1 ) ? he.x : -he.x, ( v & 2 ) ? he.y : -he.y, ( v & 4 ) ? he.z : -he.z );
        const Vector3 worldVertex = body.position + ( rotMat * local );

        if ( !body.terrain->IsInBounds( worldVertex.x, worldVertex.z ) )
        {
            continue;
        }

        float terrainHeight = 0.0f;
        Plane terrainPlane;
        body.terrain->GetTerrainHeightAndPlaneAt( worldVertex.x, worldVertex.z, terrainHeight, terrainPlane );
        const float gap = worldVertex.y - terrainHeight;
        if ( !found || gap < bestGap )
        {
            found = true;
            bestGap = gap;
            outVertex = worldVertex;
            outTerrainHeight = terrainHeight;
            outPlane = terrainPlane;
            outGap = gap;
        }
    }

    return found;
}

bool GetClosestHullTerrainVertex( SkullbonezCore::Core::Profiler* profiler,
                                  const TerrainContactBodyView& body,
                                  const ConvexHullShape& hull,
                                  Vector3& outVertex,
                                  float& outTerrainHeight,
                                  Plane& outPlane,
                                  float& outGap )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/HullClosestVertexProbe" );

    if ( body.terrain == nullptr )
    {
        return false;
    }

    const RotationMatrix rotMat = GetOrientationMatrix( body );
    const Vector3 hullCenter = body.position + ( rotMat * hull.GetPosition() );

    bool found = false;
    float bestGap = 1.0e30f;
    const uint16_t vertexCount = hull.GetVertexCount();
    for ( uint16_t v = 0; v < vertexCount; ++v )
    {
        const Vector3 worldVertex = hullCenter + ( rotMat * hull.GetVertex( v ) );

        if ( !body.terrain->IsInBounds( worldVertex.x, worldVertex.z ) )
        {
            continue;
        }

        float terrainHeight = 0.0f;
        Plane terrainPlane;
        body.terrain->GetTerrainHeightAndPlaneAt( worldVertex.x, worldVertex.z, terrainHeight, terrainPlane );
        const float gap = worldVertex.y - terrainHeight;
        if ( !found || gap < bestGap )
        {
            found = true;
            bestGap = gap;
            outVertex = worldVertex;
            outTerrainHeight = terrainHeight;
            outPlane = terrainPlane;
            outGap = gap;
        }
    }

    return found;
}

float GetTerrainCollisionRatio( SkullbonezCore::Core::Profiler* profiler,
                                const TerrainContactBodyView& body,
                                const CollisionShape& shape,
                                float changeInTime,
                                Ray& outTestingRay,
                                Plane& outTestingPlane )
{
    // Swept terrain tests use the body's unobstructed path for the candidate
    // timestep. Keeping this local makes the ray construction explicit at the
    // point where terrain collision state is prepared.
    outTestingRay = Ray( body.position, body.linearVelocity * changeInTime );

    // If out of bounds, no collision has occurred.
    if ( !body.terrain->IsInBounds( body.position.x, body.position.z ) )
    {
        return NO_COLLISION;
    }

    const BoundingBox* box = std::get_if<BoundingBox>( &shape );
    const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &shape );
    float bottomOffset = body.boundingRadius;

    if ( box != nullptr )
    {
        // Closed-form lowest-vertex Y offset. For an OBB, the maximum downward
        // extent from centre is dot(abs(rotationRow_Y), halfExtents). This is
        // only an early-out aid; exact terrain contact below samples real vertices.
        const Vector3& he = box->GetHalfExtents();
        const RotationMatrix rotMat = GetOrientationMatrix( body );
        bottomOffset = rotMat.SupportExtentY( he );
    }
    else if ( const BoundingSphere* sphere = std::get_if<BoundingSphere>( &shape ) )
    {
        bottomOffset = sphere->GetRadius();
    }

    // Airborne early-out: if the object's lowest point cannot reach the terrain's
    // maximum height during this timestep, skip the expensive cached query.
    float minBottomY = body.position.y - bottomOffset;
    const float velY = body.linearVelocity.y;
    if ( velY < 0.0f )
    {
        minBottomY += velY * changeInTime;
    }

    if ( minBottomY > body.terrain->GetMaxHeight() )
    {
        return NO_COLLISION;
    }

    if ( box != nullptr )
    {
        // Boxes need a real vertex/terrain gap test before any center-based path
        // runs. On sloped terrain, a center sample can say the box is supported
        // while every real vertex is still visibly above the surface.
        Vector3 closestVertex;
        float terrainHeight = 0.0f;
        Plane terrainPlane;
        float gap = 0.0f;
        if ( !GetClosestBoxTerrainVertex( profiler, body, *box, closestVertex, terrainHeight, terrainPlane, gap ) )
        {
            return NO_COLLISION;
        }

        if ( gap <= body.contactEpsilon )
        {
            outTestingPlane = terrainPlane;
            return 0.0f;
        }

        if ( outTestingRay.vector3.IsCloseToZero() )
        {
            return NO_COLLISION;
        }

        const Vector3& he = box->GetHalfExtents();
        const RotationMatrix rotMat = GetOrientationMatrix( body );

        float earliestCollisionTime = NO_COLLISION;
        Plane earliestPlane;
        {
            // When no vertex is currently touching, sweep every box vertex along
            // the body's linear motion and take the earliest plane hit.
            PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/BoxSweptVertexProbe" );
            for ( int v = 0; v < 8; ++v )
            {
                const Vector3 local( ( v & 1 ) ? he.x : -he.x, ( v & 2 ) ? he.y : -he.y, ( v & 4 ) ? he.z : -he.z );
                const Vector3 worldVertex = body.position + ( rotMat * local );

                if ( !body.terrain->IsInBounds( worldVertex.x, worldVertex.z ) )
                {
                    continue;
                }

                float vertexTerrainHeight = 0.0f;
                Plane vertexPlane;
                body.terrain->GetTerrainHeightAndPlaneAt( worldVertex.x,
                                                          worldVertex.z,
                                                          vertexTerrainHeight,
                                                          vertexPlane );

                const Ray vertexRay( worldVertex, body.linearVelocity * changeInTime );
                const float vertexCollisionTime = GeometricMath::CalculateIntersectionTime( vertexPlane, vertexRay );
                if ( vertexCollisionTime >= ZERO_TAKE_TOLERANCE && vertexCollisionTime <= 1.0f &&
                     vertexCollisionTime < earliestCollisionTime )
                {
                    earliestCollisionTime = vertexCollisionTime;
                    earliestPlane = vertexPlane;
                }
            }
        }

        if ( earliestCollisionTime <= 1.0f )
        {
            outTestingPlane = earliestPlane;
            return earliestCollisionTime;
        }

        return NO_COLLISION;
    }

    if ( hull != nullptr )
    {
        Vector3 closestVertex;
        float terrainHeight = 0.0f;
        Plane terrainPlane;
        float gap = 0.0f;
        if ( !GetClosestHullTerrainVertex( profiler, body, *hull, closestVertex, terrainHeight, terrainPlane, gap ) )
        {
            return NO_COLLISION;
        }

        if ( gap <= body.contactEpsilon )
        {
            outTestingPlane = terrainPlane;
            return 0.0f;
        }

        if ( outTestingRay.vector3.IsCloseToZero() )
        {
            return NO_COLLISION;
        }

        const RotationMatrix rotMat = GetOrientationMatrix( body );
        const Vector3 hullCenter = body.position + ( rotMat * hull->GetPosition() );

        float earliestCollisionTime = NO_COLLISION;
        Plane earliestPlane;
        {
            PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/HullSweptVertexProbe" );
            const uint16_t vertexCount = hull->GetVertexCount();
            for ( uint16_t v = 0; v < vertexCount; ++v )
            {
                const Vector3 worldVertex = hullCenter + ( rotMat * hull->GetVertex( v ) );

                if ( !body.terrain->IsInBounds( worldVertex.x, worldVertex.z ) )
                {
                    continue;
                }

                float vertexTerrainHeight = 0.0f;
                Plane vertexPlane;
                body.terrain->GetTerrainHeightAndPlaneAt( worldVertex.x,
                                                          worldVertex.z,
                                                          vertexTerrainHeight,
                                                          vertexPlane );

                const Ray vertexRay( worldVertex, body.linearVelocity * changeInTime );
                const float vertexCollisionTime = GeometricMath::CalculateIntersectionTime( vertexPlane, vertexRay );
                if ( vertexCollisionTime >= ZERO_TAKE_TOLERANCE && vertexCollisionTime <= 1.0f &&
                     vertexCollisionTime < earliestCollisionTime )
                {
                    earliestCollisionTime = vertexCollisionTime;
                    earliestPlane = vertexPlane;
                }
            }
        }

        if ( earliestCollisionTime <= 1.0f )
        {
            outTestingPlane = earliestPlane;
            return earliestCollisionTime;
        }

        return NO_COLLISION;
    }

    // Cache-backed terrain lookup: one query returns the exact collision plane
    // and height for this XZ position, avoiding per-frame LocatePolygon work.
    float terrainHeight = 0.0f;
    body.terrain->GetTerrainHeightAndPlaneAt( body.position.x, body.position.z, terrainHeight, outTestingPlane );
    const float gap = body.position.y - bottomOffset - terrainHeight;
    if ( gap <= body.contactEpsilon )
    {
        return 0.0f;
    }

    // If the dynamic object is stationary and not in contact, no collision will occur.
    if ( outTestingRay.vector3.IsCloseToZero() )
    {
        return NO_COLLISION;
    }

    // Offset the ray origin for the swept sphere test.
    outTestingRay.origin.y -= bottomOffset;
    return GeometricMath::CalculateIntersectionTime( outTestingPlane, outTestingRay );
}
} // namespace

TerrainContactSweepResult SkullbonezCore::Physics::SweepTerrainContact( Core::Profiler* profiler,
                                                                        const TerrainContactBodyView& body,
                                                                        const CollisionShape& shape,
                                                                        float changeInTime )
{
    // This answers "how many seconds can this body move before it hits terrain?"
    // and returns the hit plane directly for the solver row builder.
    if ( body.terrain == nullptr )
    {
        SB_FATAL( "TerrainContactManifold", "Terrain pointer not valid in SweepTerrainContact." );
    }

    TerrainContactSweepResult result;
    result.collisionTime = changeInTime;

    Ray testingRay;
    Plane testingPlane;
    const float collisionRatio = GetTerrainCollisionRatio( profiler,
                                                           body,
                                                           shape,
                                                           changeInTime,
                                                           testingRay,
                                                           testingPlane );

    if ( collisionRatio > 1.0f || collisionRatio < ZERO_TAKE_TOLERANCE )
    {
        return result;
    }

    result.hit = true;
    result.collisionTime = collisionRatio * changeInTime;
    result.collidedPlane = testingPlane;
    result.collidedRay = testingRay;
    return result;
}


bool SkullbonezCore::Physics::BuildTerrainContactManifold( Core::Profiler* profiler,
                                                           const TerrainContactBodyView& body,
                                                           const CollisionShape& shape,
                                                           int bodyIndex,
                                                           const TerrainContactSweepResult& sweep,
                                                           float availableTime,
                                                           TerrainContactManifold& out )
{
    PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/Manifold" );
    PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/Manifold/Build" );

    // Geometry-only boundary for the shared terrain row path. This converts the
    // swept terrain hit into contact points, feature ids, tangent axes, and
    // support-policy metadata. It must not apply impulses or decide final sleep.
    if ( body.terrain == nullptr || body.isFixed || !sweep.hit )
    {
        return false;
    }

    out = TerrainContactManifold();
    out.bodyA = bodyIndex;
    out.bodyB = -1;
    out.normal = sweep.collidedPlane.m_normal;
    out.timeOfImpact = sweep.collisionTime;
    out.sweptHit = sweep.collisionTime > ZERO_TAKE_TOLERANCE && sweep.collisionTime < availableTime;

    // Build one stable tangent basis per terrain manifold. Every contact point
    // reuses this basis so friction rows are deterministic and do not drift
    // because of point ordering.
    ContactSolver::BuildContactTangents( out.normal, out.tangent1, out.tangent2 );

    const Plane colPlane = sweep.collidedPlane;
    const Vector3 planeNormal = out.normal;
    const Vector3 position = body.position;

    std::visit(
        [&]( const auto& shapeValue )
        {
            using ShapeT = std::decay_t<decltype( shapeValue )>;

            if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
            {
                // A sphere has one terrain point: the bottom pole along the
                // terrain normal. That becomes one normal row and two tangent rows.
                const float radius = shapeValue.GetRadius();
                const Vector3 contactWorldPos = position - planeNormal * radius;
                const float signedDist = ( contactWorldPos * planeNormal ) - colPlane.m_distance;

                TerrainContactPoint& point = out.points[0];
                point.point = contactWorldPos;
                point.rA = contactWorldPos - position;
                point.penetration = -signedDist;
                point.featureId = 0;
                out.pointCount = 1;
            }
            else if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
            {
                PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/Manifold/BoxVertices" );

                const Vector3& he = shapeValue.GetHalfExtents();
                const RotationMatrix rotMat = GetOrientationMatrix( body );
                Vector3 worldVerts[8];
                float signedDists[8];
                float minSignedDist = 1e10f;

                for ( int v = 0; v < 8; ++v )
                {
                    const Vector3 local = GetBoxTerrainLocalCorner( he, v );
                    worldVerts[v] = position + ( rotMat * local );
                    signedDists[v] = ( worldVerts[v] * planeNormal ) - colPlane.m_distance;
                    if ( signedDists[v] < minSignedDist )
                    {
                        minSignedDist = signedDists[v];
                    }
                }

                const float contactThreshold = (std::max)( 0.0f, body.terrainContactThreshold );
                const float cutoff = minSignedDist + contactThreshold;
                for ( int v = 0; v < 8; ++v )
                {
                    if ( signedDists[v] > cutoff )
                    {
                        continue;
                    }

                    const float penetration = -signedDists[v];
                    TerrainContactPoint& point = out.points[out.pointCount];
                    point.point = worldVerts[v];
                    point.rA = worldVerts[v] - position;
                    point.penetration = ( penetration > 0.0f ) ? penetration : 0.0f;
                    point.featureId = static_cast<uint32_t>( v + 1 );
                    ++out.pointCount;
                }
            }
            else if constexpr ( std::is_same_v<ShapeT, ConvexHullShape> )
            {
                PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/Manifold/HullVertices" );

                const RotationMatrix rotMat = GetOrientationMatrix( body );
                const Vector3 hullCenter = position + ( rotMat * shapeValue.GetPosition() );
                Vector3 worldVerts[ConvexHullShape::MAX_VERTICES];
                float signedDists[ConvexHullShape::MAX_VERTICES];
                float minSignedDist = 1e10f;

                const uint16_t vertexCount = shapeValue.GetVertexCount();
                for ( uint16_t v = 0; v < vertexCount; ++v )
                {
                    worldVerts[v] = hullCenter + ( rotMat * shapeValue.GetVertex( v ) );
                    signedDists[v] = ( worldVerts[v] * planeNormal ) - colPlane.m_distance;
                    if ( signedDists[v] < minSignedDist )
                    {
                        minSignedDist = signedDists[v];
                    }
                }

                const float contactThreshold = (std::max)( 0.0f, body.terrainContactThreshold );
                const float cutoff = minSignedDist + contactThreshold;
                for ( uint16_t v = 0; v < vertexCount && out.pointCount < 8; ++v )
                {
                    if ( signedDists[v] > cutoff )
                    {
                        continue;
                    }

                    const float penetration = -signedDists[v];
                    TerrainContactPoint& point = out.points[out.pointCount];
                    point.point = worldVerts[v];
                    point.rA = worldVerts[v] - position;
                    point.penetration = ( penetration > 0.0f ) ? penetration : 0.0f;
                    point.featureId = 0x6000u | static_cast<uint32_t>( v & 0x0fffu );
                    ++out.pointCount;
                }
            }
        },
        shape );

    if ( out.pointCount == 0 )
    {
        return false;
    }

    const float preVn = body.linearVelocity * planeNormal;
    if ( preVn < -body.restitutionThreshold && out.pointCount > 1 )
    {
        // For fast impacts, collapse a multi-point box footprint to a centroid
        // impact row. Resting contacts should use the full patch, but a high
        // speed bounce should not stack several restitution rows and over-launch.
        Vector3 centroid = ZERO_VECTOR;
        Vector3 centroidR = ZERO_VECTOR;
        float avgPen = 0.0f;
        for ( uint8_t i = 0; i < out.pointCount; ++i )
        {
            centroid += out.points[i].point;
            centroidR += out.points[i].rA;
            avgPen += out.points[i].penetration;
        }

        const float invCount = 1.0f / static_cast<float>( out.pointCount );
        out.points[0].point = centroid * invCount;
        out.points[0].rA = centroidR * invCount;
        out.points[0].penetration = avgPen * invCount;
        out.points[0].featureId = 0x7fffu;
        out.pointCount = 1;
    }

    const RotationMatrix orientMat = GetOrientationMatrix( body );
    const BoxTerrainSupportClassification terrainSupport = ClassifyBoxTerrainSupport( profiler,
                                                                                      shape,
                                                                                      position,
                                                                                      orientMat,
                                                                                      planeNormal,
                                                                                      body.terrain,
                                                                                      out.pointCount,
                                                                                      body.contactEpsilon,
                                                                                      true );

    // Support policy is metadata, not collision response. Unsupported edge or
    // point contacts still generate rows and solve penetration, but they cannot
    // seed sleep, receive rest-only gravity warm start, or keep cached impulses.
    out.supportsRestingPolicy = !( terrainSupport.isBox || terrainSupport.isConvexHull ) ||
                                terrainSupport.supportsRestingPolicy;

    out.allowsTangentFriction = !terrainSupport.isConvexHull || out.supportsRestingPolicy;
    out.inhibitsSleep = !out.supportsRestingPolicy;
    return true;
}
