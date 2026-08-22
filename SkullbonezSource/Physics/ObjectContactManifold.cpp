/*
File: SkullbonezSource/Physics/ObjectContactManifold.cpp
Purpose:
  Builds precise object/object contact manifolds for the persistent solver.

Summary:
  Narrowphase dispatches each shape pair here, reduces its candidate geometry
  to stable world-space contact rows, and assigns deterministic feature ids for
  the persistent solver cache.

Glossary:
  Collider shape reference: Typed non-owning view into ColliderStore's per-kind
    shape payload, read by narrowphase during contact generation.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/ObjectContactManifold.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#include "ObjectContactManifold.h"

#include <algorithm>
#include <cstddef>
#include "BoundingBox.h"
#include "BoundingSphere.h"
#include "CollisionShape.h"
#include "ConvexHullShape.h"
#include "../Core/Profiler.h"
#include "../Maths/GeometricStructures.h"
#include "../Maths/Quaternion.h"

using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;

// CATTO REF:
//   Catto 2005 starts the solver from a manifold: contact points, contact
//   normals, penetration, and feature identifiers for temporal coherence. The
//   persistent solver maps every ObjectContactPoint below into one Catto-style
//   constraint row.
// ENGINE-SPECIFIC:
//   This file is Skullbonez narrowphase policy, not Catto's 2D box clipping
//   implementation. It supplies exact sphere/sphere, closest-point sphere/OBB,
//   and SAT plus clipped OBB/OBB contacts for the existing 3D engine.
// Concept:
//   Broadphase has already said "these two objects are close enough to inspect."
//   This file answers the expensive geometry question: exactly where are they
//   touching, and which direction should the solver push to separate them?
namespace
{
// ENGINE-SPECIFIC:
//   Feature IDs are compact and deterministic. The persistent solver key keeps
//   all 32 feature bits beside two 15-bit body indices, so the kind bits keep
//   sphere/box, face/face, edge/edge, and hull contacts distinct for one pair.
constexpr uint32_t FEATURE_KIND_SPHERE_BOX = 1u;
constexpr uint32_t FEATURE_KIND_BOX_FACE = 2u;
constexpr uint32_t FEATURE_KIND_BOX_EDGE = 3u;
constexpr uint32_t FEATURE_KIND_SPHERE_HULL = 4u;
constexpr uint32_t FEATURE_KIND_HULL_FACE = 5u;
constexpr uint32_t FEATURE_KIND_HULL_EDGE = 6u;

// ENGINE-SPECIFIC:
//   BoxWorld caches the OBB basis in world space. Catto's solver later needs
//   world-space r vectors and normals; doing this conversion once in narrowphase
//   avoids mixing local and world terms inside the PGS row solve.
struct BoxWorld
{
    Vector3 center = ZERO_VECTOR;
    Vector3 halfExtents = ZERO_VECTOR;
    Vector3 axes[3] = { Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, 0.0f, 1.0f ) };
};

struct SatResult
{
    // ENGINE-SPECIFIC:
    //   Stores the winning SAT axis. Catto's solver does not care how the axis
    //   was found; it only receives the final normal and contact rows.
    Vector3 normal = ZERO_VECTOR;
    float overlap = FLT_MAX;
    int axisType = 0; // 0 = A face, 1 = B face, 2 = edge-edge
    int axisA = -1;

    int axisB = -1;
    Vector3 faceNormal = ZERO_VECTOR;
    float faceOverlap = FLT_MAX;
    int faceAxisType = 0;
    int faceAxisA = -1;
    int faceAxisB = -1;
    bool hasFaceAxis = false;
};

// ENGINE-SPECIFIC:
//   ClipVertex keeps a small incident-face vertex ID through clipping. That ID
//   becomes part of the feature key, letting the Catto warm-start cache match
//   individual face-contact rows instead of treating a whole box pair as one
//   anonymous contact.
struct ClipVertex
{
    Vector3 point = ZERO_VECTOR;
    uint16_t id = 0;
};

struct PolyFaceWorld
{
    Vector3 normal = ZERO_VECTOR;
    float planeOffset = 0.0f;
    uint16_t firstIndex = 0;
    uint8_t indexCount = 0;
    uint16_t sourceId = 0;
};

struct PolyEdgeWorld
{
    uint16_t vertexA = 0;
    uint16_t vertexB = 0;
    uint16_t faceA = 0xffffu;
    uint16_t faceB = 0xffffu;
    uint16_t sourceId = 0;
};

struct PolytopeWorld
{
    Vector3 center = ZERO_VECTOR;
    Vector3 vertices[ConvexHullShape::MAX_VERTICES] = {};

    PolyFaceWorld faces[ConvexHullShape::MAX_FACES] = {};

    PolyEdgeWorld edges[ConvexHullShape::MAX_EDGES] = {};

    uint16_t faceIndices[ConvexHullShape::MAX_FACE_INDICES] = {};

    uint16_t vertexCount = 0;
    uint16_t faceCount = 0;
    uint16_t edgeCount = 0;
    uint16_t faceIndexCount = 0;
};

enum class SphereHullFeatureKind : uint8_t
{
    Face = 0,
    Edge = 1,
    Vertex = 2,
};

struct SphereHullClosestFeature
{
    Vector3 point = ZERO_VECTOR;
    SphereHullFeatureKind kind = SphereHullFeatureKind::Face;
    uint16_t sourceId = 0;
    float distSq = FLT_MAX;
};

float Component( const Vector3& v, int axis )
{
    // ENGINE-SPECIFIC:
    //   Small helper for indexing half-extents and local coordinates without
    //   changing Vector3's public API during this focused physics change.
    if ( axis == 0 )
    {
        return v.x;
    }

    if ( axis == 1 )
    {
        return v.y;
    }

    return v.z;
}

float ClampFloat( float value, float lo, float hi )
{
    // ENGINE-SPECIFIC:
    //   Narrowphase helpers use scalar clamps for closest-point and clipping
    //   math. Keeping the helper local avoids adding broad utility API surface.
    return (std::max)( lo, (std::min)( value, hi ) );
}

BoxWorld MakeBoxWorld( const ObjectContactBodyView& body, const BoundingBox& box )
{
    // ENGINE-SPECIFIC:
    //   Convert the engine's local box shape plus body orientation into an OBB
    //   basis. Catto's equations downstream operate in world space; this is the
    //   conversion from Skullbonez body pose and shape state to solver row geometry.
    const RotationMatrix rot = body.orientation.GetOrientationMatrix();

    BoxWorld out;
    out.halfExtents = box.GetHalfExtents();
    out.axes[0] = rot * Vector3( 1.0f, 0.0f, 0.0f );
    out.axes[1] = rot * Vector3( 0.0f, 1.0f, 0.0f );
    out.axes[2] = rot * Vector3( 0.0f, 0.0f, 1.0f );
    out.center = body.position + rot * box.GetPosition();
    return out;
}

Vector3 SphereCenter( const ObjectContactBodyView& body, const BoundingSphere& sphere )
{
    // ENGINE-SPECIFIC:
    //   Spheres can carry a local shape offset. Rotate it through the body
    //   orientation before building Catto-style world-space contact arms.
    const RotationMatrix rot = body.orientation.GetOrientationMatrix();
    return body.position + rot * sphere.GetPosition();
}

uint32_t FaceId( int axis, float sign )
{
    // ENGINE-SPECIFIC:
    //   Six OBB faces are encoded as axis*2 + sign. This is part of the stable
    //   feature key used by temporal warm starting.
    return static_cast<uint32_t>( axis * 2 + ( sign > 0.0f ? 1 : 0 ) );
}

uint32_t EncodeSphereBoxFeature( bool boxIsA, uint32_t faceId )
{
    // ENGINE-SPECIFIC:
    //   A sphere/box row is identified by which body owns the box and which box
    //   face the sphere is touching. Catto requires stable identifiers for
    //   contact caching; the bit layout is Skullbonez policy.
    uint32_t boxCode = ( boxIsA ? 8u : 0u ) + faceId;
    return ( FEATURE_KIND_SPHERE_BOX << 14 ) | ( boxCode << 10 );
}

uint32_t EncodeBoxFaceFeature( bool referenceIsA, uint32_t referenceFace, uint32_t incidentFace, uint32_t pointId )
{
    // ENGINE-SPECIFIC:
    //   Face contacts include reference face, incident face, and clipped vertex
    //   ID so each row in a four-point manifold can warm start independently.
    uint32_t refCode = ( referenceIsA ? 0u : 8u ) + referenceFace;
    uint32_t incCode = ( referenceIsA ? 8u : 0u ) + incidentFace;
    return ( FEATURE_KIND_BOX_FACE << 14 ) | ( ( refCode & 0x0fu ) << 10 ) | ( ( incCode & 0x0fu ) << 6 ) |
           ( pointId & 0x3fu );
}

uint32_t EncodeBoxEdgeFeature( uint32_t edgeA, uint32_t edgeB )
{
    // ENGINE-SPECIFIC:
    //   Edge contacts use the two participating OBB edge IDs. There is only one
    //   row for edge-edge, but the ID still needs to survive across frames for
    //   Catto Section 8 temporal coherence.
    return ( FEATURE_KIND_BOX_EDGE << 14 ) | ( ( edgeA & 0x0fu ) << 10 ) | ( ( edgeB & 0x0fu ) << 6 );
}

void AddContactPoint( const ObjectContactBodyView& a, const ObjectContactBodyView& b, ObjectContactManifold& manifold,
                      const Vector3& point, float penetration, uint32_t featureId )
{
    // CATTO REF:
    //   Catto Section 4 rows store a contact point plus r1/r2 arms. This helper
    //   centralizes that setup so every manifold path feeds the solver the same
    //   row shape.
    if ( manifold.pointCount >= 4 )
    {
        return;
    }

    ObjectContactPoint& cp = manifold.points[manifold.pointCount++];
    cp.point = point;
    cp.rA = point - a.position;
    cp.rB = point - b.position;
    cp.penetration = ( penetration > 0.0f ) ? penetration : 0.0f;
    cp.featureId = featureId;
}
} // namespace

ObjectContactCandidateSelection
SkullbonezCore::Physics::SelectObjectContactCandidateIndices( const ObjectContactCandidate* candidates, int candidateCount,
                                                              const Vector3& normal )
{
    // Invariant: the deepest row is always selected first. The remaining rows
    // maximize their minimum tangent-plane distance from the selected set, so a
    // clipped octagon does not collapse into four neighboring solver points.
    ObjectContactCandidateSelection selection;

    // Hazard: the selection bitmap is deliberately fixed-capacity for the hot
    // narrowphase path. Reject an invalid public borrow before indexing it;
    // production clipping owns 8-row and 32-row buffers under this ceiling.
    if ( !candidates || candidateCount <= 0 || candidateCount > MAX_OBJECT_CONTACT_CANDIDATES )
    {
        return selection;
    }

    bool selected[MAX_OBJECT_CONTACT_CANDIDATES] = {};

    auto betterPenetrationTie = [&]( int lhs, int rhs ) -> bool
    {
        if ( rhs < 0 )
        {
            return true;
        }

        if ( fabsf( candidates[lhs].penetration - candidates[rhs].penetration ) > 1.0e-5f )
        {
            return candidates[lhs].penetration > candidates[rhs].penetration;
        }

        return candidates[lhs].featureId < candidates[rhs].featureId;
    };

    int deepest = -1;

    for ( int i = 0; i < candidateCount; ++i )
    {
        if ( betterPenetrationTie( i, deepest ) )
        {
            deepest = i;
        }
    }

    if ( deepest < 0 )
    {
        return selection;
    }

    selected[deepest] = true;
    selection.indices[selection.count++] = deepest;

    const Vector3 tangentSeed = fabsf( normal.y ) < 0.9f ? Vector3( 0.0f, 1.0f, 0.0f ) : Vector3( 1.0f, 0.0f, 0.0f );
    Vector3 tangent0 = CrossProduct( tangentSeed, normal );
    const float tangent0MagSq = VectorMagSquared( tangent0 );

    if ( tangent0MagSq > 1.0e-8f )
    {
        tangent0 /= sqrtf( tangent0MagSq );
    }
    else
    {
        tangent0 = Vector3( 1.0f, 0.0f, 0.0f );
    }

    const Vector3 tangent1 = CrossProduct( normal, tangent0 );

    auto tangentDistanceSq = [&]( const Vector3& a, const Vector3& b ) -> float
    {
        const Vector3 d = a - b;

        const float x = Dot( d, tangent0 );
        const float y = Dot( d, tangent1 );
        return x * x + y * y;
    };

    while ( selection.count < 4 && selection.count < candidateCount )
    {
        int bestIndex = -1;
        float bestSpread = -1.0f;

        for ( int i = 0; i < candidateCount; ++i )
        {
            if ( selected[i] )
            {
                continue;
            }

            float minDistSq = FLT_MAX;

            for ( uint8_t selectedIndex = 0; selectedIndex < selection.count; ++selectedIndex )
            {
                const float distSq = tangentDistanceSq( candidates[i].point,
                                                        candidates[selection.indices[selectedIndex]].point );

                if ( distSq < minDistSq )
                {
                    minDistSq = distSq;
                }
            }

            constexpr float duplicatePointDistSq = 1.0e-6f;

            if ( minDistSq <= duplicatePointDistSq )
            {
                continue;
            }

            bool replace = minDistSq > bestSpread + 1.0e-5f;

            if ( !replace && fabsf( minDistSq - bestSpread ) <= 1.0e-5f )
            {
                replace = betterPenetrationTie( i, bestIndex );
            }

            if ( replace )
            {
                bestSpread = minDistSq;
                bestIndex = i;
            }
        }

        if ( bestIndex < 0 )
        {
            break;
        }

        selected[bestIndex] = true;
        selection.indices[selection.count++] = bestIndex;
    }

    return selection;
}

namespace
{
// CATTO REF:
//   The result is Catto's simplest contact model: one point, one normal, and one
//   penetration value. rA/rB are filled in AddContactPoint for Equations 9-11.
// ENGINE-SPECIFIC:
//   Sphere centers may include local shape offsets, so SphereCenter applies the
//   current orientation before using the classic center-to-center normal.
bool BuildSphereSphere( const ObjectContactBodyView& a, const BoundingSphere& sphereA, const ObjectContactBodyView& b,
                        const BoundingSphere& sphereB, float contactSkin, ObjectContactManifold& out )
{
    Vector3 centerA = SphereCenter( a, sphereA );
    Vector3 centerB = SphereCenter( b, sphereB );
    Vector3 delta = centerB - centerA;
    float distSq = VectorMagSquared( delta );
    float radiusSum = sphereA.GetRadius() + sphereB.GetRadius();
    float contactDistance = radiusSum + contactSkin;

    if ( distSq > contactDistance * contactDistance )
    {
        return false;
    }

    float dist = sqrtf( distSq );
    Vector3 normal = ( dist > TOLERANCE ) ? ( delta / dist ) : Vector3( 0.0f, 1.0f, 0.0f );
    Vector3 pointA = centerA + normal * sphereA.GetRadius();
    Vector3 pointB = centerB - normal * sphereB.GetRadius();
    out.normal = normal;
    AddContactPoint( a, b, out, ( pointA + pointB ) * 0.5f, radiusSum - dist, 0u );
    return out.pointCount > 0;
}

int ChooseDominantFace( const Vector3& localPoint, const Vector3& halfExtents, float& signOut )
{
    // When a sphere is inside or right against a box, several faces can look
    // plausible. Choose the face whose normalized coordinate is closest to the
    // box surface so the contact normal does not jump around frame to frame.
    float best = -FLT_MAX;
    int bestAxis = 0;
    float coords[3] = { localPoint.x, localPoint.y, localPoint.z };

    float extents[3] = { halfExtents.x, halfExtents.y, halfExtents.z };

    for ( int axis = 0; axis < 3; ++axis )
    {
        float ratio = fabsf( coords[axis] ) / ( extents[axis] > TOLERANCE ? extents[axis] : 1.0f );

        if ( ratio > best + 1.0e-5f )
        {
            best = ratio;
            bestAxis = axis;
        }
    }

    signOut = ( coords[bestAxis] >= 0.0f ) ? 1.0f : -1.0f;
    return bestAxis;
}

// ENGINE-SPECIFIC:
//   Sphere/OBB uses the closest point on the oriented box in box-local space.
//   When the sphere center is inside the box, the closest point is ambiguous; we
//   choose the nearest face so the normal and feature ID stay deterministic.
bool BuildSphereBoxOrdered( const ObjectContactBodyView& sphereBody, const BoundingSphere& sphere,
                            const ObjectContactBodyView& boxBody, const BoundingBox& box, bool sphereIsA, float contactSkin,
                            ObjectContactManifold& out )
{
    BoxWorld bw = MakeBoxWorld( boxBody, box );
    Vector3 sphereCenter = SphereCenter( sphereBody, sphere );
    const RotationMatrix rot = boxBody.orientation.GetOrientationMatrix();
    Vector3 local = rot.TransposeMultiply( sphereCenter - bw.center );

    Vector3 closestLocal( ClampFloat( local.x, -bw.halfExtents.x, bw.halfExtents.x ),
                          ClampFloat( local.y, -bw.halfExtents.y, bw.halfExtents.y ),
                          ClampFloat( local.z, -bw.halfExtents.z, bw.halfExtents.z ) );

    Vector3 closestWorld = bw.center + rot * closestLocal;
    Vector3 boxToSphere = sphereCenter - closestWorld;
    float distSq = VectorMagSquared( boxToSphere );

    Vector3 boxOutward = ZERO_VECTOR;
    float penetration = 0.0f;
    uint32_t face = 0u;

    if ( distSq > TOLERANCE * TOLERANCE )
    {
        float dist = sqrtf( distSq );

        if ( dist > sphere.GetRadius() + contactSkin )
        {
            return false;
        }

        boxOutward = boxToSphere / dist;
        float faceSign = 1.0f;
        int faceAxis = ChooseDominantFace( closestLocal, bw.halfExtents, faceSign );
        face = FaceId( faceAxis, faceSign );
        penetration = sphere.GetRadius() - dist;
    }
    else
    {
        float distances[3] = { bw.halfExtents.x - fabsf( local.x ), bw.halfExtents.y - fabsf( local.y ),
                               bw.halfExtents.z - fabsf( local.z ) };

        int faceAxis = 0;

        if ( distances[1] < distances[faceAxis] - 1.0e-5f )
        {
            faceAxis = 1;
        }

        if ( distances[2] < distances[faceAxis] - 1.0e-5f )
        {
            faceAxis = 2;
        }

        float faceSign = ( Component( local, faceAxis ) >= 0.0f ) ? 1.0f : -1.0f;
        boxOutward = bw.axes[faceAxis] * faceSign;
        face = FaceId( faceAxis, faceSign );
        penetration = sphere.GetRadius() + distances[faceAxis];
        closestWorld = sphereCenter - boxOutward * sphere.GetRadius();
    }

    Vector3 normalSphereToBox = -boxOutward;
    Vector3 finalNormal = sphereIsA ? normalSphereToBox : -normalSphereToBox;
    out.normal = finalNormal;

    Vector3 spherePoint = sphereCenter + normalSphereToBox * sphere.GetRadius();
    Vector3 contactPoint = ( spherePoint + closestWorld ) * 0.5f;
    AddContactPoint( sphereIsA ? sphereBody : boxBody, sphereIsA ? boxBody : sphereBody, out, contactPoint, penetration,
                     EncodeSphereBoxFeature( !sphereIsA, face ) );

    return out.pointCount > 0;
}

float ProjectBoxRadius( const BoxWorld& box, const Vector3& axis )
{
    // Imagine shining a light along "axis" and measuring the box's shadow on
    // that line. The projected radius is half the length of that shadow. SAT
    // uses this to ask whether two box shadows overlap on every possible axis.
    return box.halfExtents.x * fabsf( Dot( box.axes[0], axis ) ) + box.halfExtents.y * fabsf( Dot( box.axes[1], axis ) ) +
           box.halfExtents.z * fabsf( Dot( box.axes[2], axis ) );
}

// ENGINE-SPECIFIC:
//   OBB/OBB detection uses the 15-axis separating-axis test: three face normals
//   from A, three from B, and nine edge cross-products. Catto consumes the final
//   manifold rows, but this SAT selection is local 3D narrowphase policy. Ties
//   prefer face axes before edge axes to keep stacks from flipping between
//   equivalent edge contacts when overlap is nearly equal.
bool AcceptSatAxis( const BoxWorld& a, const BoxWorld& b, const Vector3& axisRaw, int axisType, int axisA, int axisB,
                    const Vector3& centerDelta, float contactSkin, SatResult& best )
{
    float magSq = VectorMagSquared( axisRaw );

    if ( magSq <= 1.0e-8f )
    {
        return true;
    }

    Vector3 axis = axisRaw / sqrtf( magSq );
    float distance = fabsf( Dot( centerDelta, axis ) );
    float overlap = ProjectBoxRadius( a, axis ) + ProjectBoxRadius( b, axis ) - distance;

    if ( overlap < -contactSkin )
    {
        return false;
    }

    constexpr float tieEpsilon = 1.0e-4f;
    bool better = overlap < best.overlap - tieEpsilon;

    if ( !better && fabsf( overlap - best.overlap ) <= tieEpsilon )
    {
        if ( axisType < best.axisType )
        {
            better = true;
        }
    }

    if ( better )
    {
        best.overlap = overlap;
        best.axisType = axisType;
        best.axisA = axisA;
        best.axisB = axisB;
        best.normal = ( Dot( centerDelta, axis ) < 0.0f ) ? -axis : axis;
    }

    return true;
}

// ENGINE-SPECIFIC:
//   Full SAT pass for oriented boxes. Returning the minimum-overlap axis gives a
//   stable contact normal for the later face clipping or edge-edge fallback.
bool BoxBoxSat( const BoxWorld& a, const BoxWorld& b, float contactSkin, SatResult& out )
{
    Vector3 centerDelta = b.center - a.center;

    for ( int i = 0; i < 3; ++i )
    {
        if ( !AcceptSatAxis( a, b, a.axes[i], 0, i, -1, centerDelta, contactSkin, out ) )
        {
            return false;
        }
    }

    for ( int i = 0; i < 3; ++i )
    {
        if ( !AcceptSatAxis( a, b, b.axes[i], 1, -1, i, centerDelta, contactSkin, out ) )
        {
            return false;
        }
    }

    for ( int i = 0; i < 3; ++i )
    {
        for ( int j = 0; j < 3; ++j )
        {
            Vector3 axis = CrossProduct( a.axes[i], b.axes[j] );

            if ( !AcceptSatAxis( a, b, axis, 2, i, j, centerDelta, contactSkin, out ) )
            {
                return false;
            }
        }
    }

    return out.overlap < FLT_MAX;
}

// ENGINE-SPECIFIC:
//   Builds the four world-space vertices of one OBB face. The winding is stable
//   and deterministic so clipped contact point feature IDs do not reshuffle
//   across frames.
void BuildFaceVertices( const BoxWorld& box, int faceAxis, float faceSign, ClipVertex outVerts[4] )
{
    int side0 = ( faceAxis + 1 ) % 3;
    int side1 = ( faceAxis + 2 ) % 3;
    float sideSigns[4][2] = { { 1.0f, 1.0f }, { -1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f } };

    for ( int i = 0; i < 4; ++i )
    {
        outVerts[i].point = box.center + box.axes[faceAxis] * ( faceSign * Component( box.halfExtents, faceAxis ) ) +
                            box.axes[side0] * ( sideSigns[i][0] * Component( box.halfExtents, side0 ) ) +
                            box.axes[side1] * ( sideSigns[i][1] * Component( box.halfExtents, side1 ) );

        outVerts[i].id = static_cast<uint8_t>( i );
    }
}

// ENGINE-SPECIFIC:
//   Sutherland-Hodgman clipping against one side plane of the reference face.
//   Catto discusses clipped box manifolds in 2D; this is the 3D Skullbonez
//   extension that clips an incident OBB face against the side planes of the
//   reference OBB face.
int ClipPolygonAgainstPlane( const ClipVertex* input, int inputCount, const Vector3& planePoint, const Vector3& inwardNormal,
                             float contactSkin, ClipVertex* output )
{
    // Keep only the portion of an incident face that lies inside one boundary
    // plane of the reference face. Repeating this for all four side planes trims
    // the touching face down to the actual contact patch.
    if ( inputCount <= 0 )
    {
        return 0;
    }

    int outputCount = 0;
    ClipVertex prev = input[inputCount - 1];
    float prevDist = Dot( ( prev.point - planePoint ), inwardNormal );
    bool prevInside = prevDist >= -contactSkin;

    for ( int i = 0; i < inputCount; ++i )
    {
        ClipVertex cur = input[i];
        float curDist = Dot( ( cur.point - planePoint ), inwardNormal );
        bool curInside = curDist >= -contactSkin;

        if ( curInside != prevInside )
        {
            float denom = prevDist - curDist;
            float t = ( fabsf( denom ) > TOLERANCE ) ? ( prevDist / denom ) : 0.0f;
            t = ClampFloat( t, 0.0f, 1.0f );
            ClipVertex clipped;
            clipped.point = prev.point + ( cur.point - prev.point ) * t;
            clipped.id = cur.id;

            if ( outputCount < 8 )
            {
                output[outputCount++] = clipped;
            }
        }

        if ( curInside && outputCount < 8 )
        {
            output[outputCount++] = cur;
        }

        prev = cur;
        prevDist = curDist;
        prevInside = curInside;
    }

    return outputCount;
}

// ENGINE-SPECIFIC:
//   Clip the incident face to the four side planes surrounding the reference
//   face. The surviving polygon vertices are the multi-point face manifold that
//   the persistent Catto-style solver can warm start independently.
int ClipIncidentFaceToReference( const BoxWorld& refBox, int refAxis, float refSign, const ClipVertex incident[4],
                                 float contactSkin, ClipVertex clipped[8] )
{
    ClipVertex tempA[8];
    ClipVertex tempB[8];
    int count = 4;

    for ( int i = 0; i < 4; ++i )
    {
        tempA[i] = incident[i];
    }

    Vector3 faceCenter = refBox.center + refBox.axes[refAxis] * ( refSign * Component( refBox.halfExtents, refAxis ) );

    for ( int sideIndex = 0; sideIndex < 3; ++sideIndex )
    {
        if ( sideIndex == refAxis )
        {
            continue;
        }

        Vector3 sideAxis = refBox.axes[sideIndex];
        float sideExtent = Component( refBox.halfExtents, sideIndex );

        Vector3 positivePoint = faceCenter + sideAxis * sideExtent;
        count = ClipPolygonAgainstPlane( tempA, count, positivePoint, -sideAxis, contactSkin, tempB );

        if ( count == 0 )
        {
            return 0;
        }

        Vector3 negativePoint = faceCenter - sideAxis * sideExtent;
        count = ClipPolygonAgainstPlane( tempB, count, negativePoint, sideAxis, contactSkin, tempA );

        if ( count == 0 )
        {
            return 0;
        }
    }

    for ( int i = 0; i < count; ++i )
    {
        clipped[i] = tempA[i];
    }

    return count;
}

// ENGINE-SPECIFIC:
//   Pick the incident face most anti-parallel to the reference normal. This is
//   the standard clipping-manifold choice, but the exact tie behavior is local
//   policy to keep deterministic feature IDs.
int ChooseIncidentFace( const BoxWorld& incidentBox, const Vector3& refNormal, float& faceSign )
{
    int faceAxis = 0;
    float bestDot = FLT_MAX;

    for ( int axis = 0; axis < 3; ++axis )
    {
        float dot = Dot( incidentBox.axes[axis], refNormal );
        float absDot = fabsf( dot );

        if ( absDot < bestDot )
        {
            bestDot = absDot;
        }
    }

    bestDot = FLT_MAX;

    for ( int axis = 0; axis < 3; ++axis )
    {
        float dotPositive = Dot( incidentBox.axes[axis], refNormal );
        float dotNegative = -dotPositive;

        if ( dotPositive < bestDot )
        {
            bestDot = dotPositive;
            faceAxis = axis;
            faceSign = 1.0f;
        }

        if ( dotNegative < bestDot )
        {
            bestDot = dotNegative;
            faceAxis = axis;
            faceSign = -1.0f;
        }
    }

    return faceAxis;
}

// CATTO REF:
//   Produces up to four contact points compatible with Catto Section 4 rows and
//   Section 8 warm-start identifiers.
// ENGINE-SPECIFIC:
//   Reference/incident face clipping is the engine's 3D OBB manifold generator.
//   Contact points are placed halfway through the residual separation so the
//   solver receives centered rA/rB arms for shallow overlap.
bool BuildBoxFaceContact( const ObjectContactBodyView& aBody, const ObjectContactBodyView& bBody, const BoxWorld& boxA,
                          const BoxWorld& boxB, bool referenceIsA, int referenceAxis, const Vector3& finalNormal,
                          float contactSkin, ObjectContactManifold& out )
{
    const BoxWorld& refBox = referenceIsA ? boxA : boxB;
    const BoxWorld& incBox = referenceIsA ? boxB : boxA;
    Vector3 refNormal = referenceIsA ? finalNormal : -finalNormal;
    float refSign = ( Dot( refBox.axes[referenceAxis], refNormal ) >= 0.0f ) ? 1.0f : -1.0f;

    float incidentSign = 1.0f;
    int incidentAxis = ChooseIncidentFace( incBox, refNormal, incidentSign );

    ClipVertex incident[4];
    BuildFaceVertices( incBox, incidentAxis, incidentSign, incident );

    ClipVertex clipped[8];
    int clippedCount = ClipIncidentFaceToReference( refBox, referenceAxis, refSign, incident, contactSkin, clipped );

    if ( clippedCount == 0 )
    {
        return false;
    }

    Vector3 refFaceCenter = refBox.center +
                            refBox.axes[referenceAxis] * ( refSign * Component( refBox.halfExtents, referenceAxis ) );

    uint32_t referenceFace = FaceId( referenceAxis, refSign );
    uint32_t incidentFace = FaceId( incidentAxis, incidentSign );

    ObjectContactCandidate candidates[8];
    int candidateCount = 0;

    for ( int i = 0; i < clippedCount; ++i )
    {
        float separation = Dot( ( clipped[i].point - refFaceCenter ), refNormal );

        if ( separation > contactSkin )
        {
            continue;
        }

        ObjectContactCandidate& candidate = candidates[candidateCount++];
        candidate.point = clipped[i].point - refNormal * ( separation * 0.5f );
        candidate.penetration = -separation;
        candidate.featureId = EncodeBoxFaceFeature( referenceIsA, referenceFace, incidentFace, clipped[i].id );
    }

    const ObjectContactCandidateSelection selection = SelectObjectContactCandidateIndices( candidates, candidateCount,
                                                                                           refNormal );

    for ( uint8_t selectedIndex = 0; selectedIndex < selection.count; ++selectedIndex )
    {
        const ObjectContactCandidate& candidate = candidates[selection.indices[selectedIndex]];
        AddContactPoint( aBody, bBody, out, candidate.point, candidate.penetration, candidate.featureId );
    }

    return out.pointCount > 0;
}

uint32_t EdgeId( int edgeAxis, int sign0, int sign1 )
{
    // A box has four edges running in each local axis direction. The two side
    // signs identify which of those four edges participated in an edge contact.
    int s0 = sign0 > 0 ? 1 : 0;
    int s1 = sign1 > 0 ? 1 : 0;
    return static_cast<uint32_t>( edgeAxis * 4 + s0 * 2 + s1 );
}

void BuildEdgeSegment( const BoxWorld& box, int edgeAxis, const Vector3& towardNormal, bool maximize, Vector3& p0,
                       Vector3& p1, uint32_t& edgeId )
{
    // Build the world-space line segment for the edge most exposed in the
    // contact direction. Edge/edge contacts need the actual two endpoints so the
    // closest-points calculation can find the single representative touch point.
    int side0 = ( edgeAxis + 1 ) % 3;
    int side1 = ( edgeAxis + 2 ) % 3;
    int sign0 = ( Dot( box.axes[side0], towardNormal ) >= 0.0f ) ? 1 : -1;
    int sign1 = ( Dot( box.axes[side1], towardNormal ) >= 0.0f ) ? 1 : -1;

    if ( !maximize )
    {
        sign0 = -sign0;
        sign1 = -sign1;
    }

    Vector3 center = box.center + box.axes[side0] * ( static_cast<float>( sign0 ) * Component( box.halfExtents, side0 ) ) +
                     box.axes[side1] * ( static_cast<float>( sign1 ) * Component( box.halfExtents, side1 ) );

    Vector3 edge = box.axes[edgeAxis] * Component( box.halfExtents, edgeAxis );
    p0 = center - edge;
    p1 = center + edge;
    edgeId = EdgeId( edgeAxis, sign0, sign1 );
}

// ENGINE-SPECIFIC:
//   Segment-segment closest points are used only for SAT edge-edge axes, where a
//   clipped face manifold would be under-constrained. The midpoint becomes one
//   Catto-style contact row with an edge-pair feature ID.
void ClosestPointsOnSegments( const Vector3& p1, const Vector3& q1, const Vector3& p2, const Vector3& q2, Vector3& c1,
                              Vector3& c2 )
{
    Vector3 d1 = q1 - p1;
    Vector3 d2 = q2 - p2;
    Vector3 r = p1 - p2;
    float a = Dot( d1, d1 );
    float e = Dot( d2, d2 );
    float f = Dot( d2, r );
    float s = 0.0f;
    float t = 0.0f;

    if ( a <= TOLERANCE && e <= TOLERANCE )
    {
        c1 = p1;
        c2 = p2;
        return;
    }

    if ( a <= TOLERANCE )
    {
        t = ClampFloat( f / e, 0.0f, 1.0f );
    }
    else
    {
        float c = Dot( d1, r );

        if ( e <= TOLERANCE )
        {
            s = ClampFloat( -c / a, 0.0f, 1.0f );
        }
        else
        {
            float b = Dot( d1, d2 );
            float denom = a * e - b * b;

            if ( denom != 0.0f )
            {
                s = ClampFloat( ( b * f - c * e ) / denom, 0.0f, 1.0f );
            }

            t = ( b * s + f ) / e;

            if ( t < 0.0f )
            {
                t = 0.0f;
                s = ClampFloat( -c / a, 0.0f, 1.0f );
            }
            else if ( t > 1.0f )
            {
                t = 1.0f;
                s = ClampFloat( ( b - c ) / a, 0.0f, 1.0f );
            }
        }
    }

    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

// ENGINE-SPECIFIC:
//   Edge contacts are the fallback for cross-product SAT axes. They intentionally
//   produce a single row because two OBB edges touching do not have a contact
//   patch to clip.
bool BuildBoxEdgeContact( const ObjectContactBodyView& aBody, const ObjectContactBodyView& bBody, const BoxWorld& boxA,
                          const BoxWorld& boxB, const SatResult& sat, ObjectContactManifold& out )
{
    Vector3 a0;
    Vector3 a1;
    Vector3 b0;
    Vector3 b1;
    uint32_t edgeA = 0;
    uint32_t edgeB = 0;
    BuildEdgeSegment( boxA, sat.axisA, sat.normal, true, a0, a1, edgeA );
    BuildEdgeSegment( boxB, sat.axisB, sat.normal, false, b0, b1, edgeB );

    Vector3 ca;
    Vector3 cb;
    ClosestPointsOnSegments( a0, a1, b0, b1, ca, cb );
    AddContactPoint( aBody, bBody, out, ( ca + cb ) * 0.5f, sat.overlap, EncodeBoxEdgeFeature( edgeA, edgeB ) );
    return out.pointCount > 0;
}

// ENGINE-SPECIFIC:
//   Box/box first chooses the SAT minimum-overlap axis, then maps face axes to a
//   clipped four-point manifold and edge axes to a one-point edge manifold. The
//   persistent solver downstream is Catto-style; this shape dispatch is local.
bool BuildBoxBox( const ObjectContactBodyView& aBody, const BoundingBox& aBox, const ObjectContactBodyView& bBody,
                  const BoundingBox& bBox, float contactSkin, ObjectContactManifold& out )
{
    BoxWorld boxA = MakeBoxWorld( aBody, aBox );
    BoxWorld boxB = MakeBoxWorld( bBody, bBox );
    SatResult sat;

    if ( !BoxBoxSat( boxA, boxB, contactSkin, sat ) )
    {
        return false;
    }

    out.normal = sat.normal;

    if ( sat.axisType == 0 )
    {
        return BuildBoxFaceContact( aBody, bBody, boxA, boxB, true, sat.axisA, sat.normal, contactSkin, out );
    }

    if ( sat.axisType == 1 )
    {
        return BuildBoxFaceContact( aBody, bBody, boxA, boxB, false, sat.axisB, sat.normal, contactSkin, out );
    }

    return BuildBoxEdgeContact( aBody, bBody, boxA, boxB, sat, out );
}

uint32_t EncodeSphereHullFeature( bool hullIsA, SphereHullFeatureKind kind, uint32_t sourceId )
{
    return ( FEATURE_KIND_SPHERE_HULL << 28 ) | ( ( hullIsA ? 1u : 0u ) << 27 ) |
           ( ( static_cast<uint32_t>( kind ) & 0x3u ) << 24 ) | ( sourceId & 0xffffu );
}

uint32_t EncodeHullFaceFeature( bool referenceIsA, uint32_t referenceFace, uint32_t incidentFace, uint32_t pointId )
{
    return ( FEATURE_KIND_HULL_FACE << 28 ) | ( ( referenceIsA ? 1u : 0u ) << 27 ) | ( ( referenceFace & 0x7fu ) << 20 ) |
           ( ( incidentFace & 0x7fu ) << 13 ) | ( pointId & 0x1ffu );
}

uint32_t EncodeHullEdgeFeature( uint32_t edgeA, uint32_t edgeB )
{
    return ( FEATURE_KIND_HULL_EDGE << 28 ) | ( ( edgeA & 0xffu ) << 12 ) | ( edgeB & 0xffu );
}

void AddPolyEdge( PolytopeWorld& poly, uint16_t a, uint16_t b, uint16_t sourceId, uint16_t faceA, uint16_t faceB )
{
    if ( poly.edgeCount >= ConvexHullShape::MAX_EDGES )
    {
        return;
    }

    poly.edges[poly.edgeCount].vertexA = a;
    poly.edges[poly.edgeCount].vertexB = b;
    poly.edges[poly.edgeCount].faceA = faceA;
    poly.edges[poly.edgeCount].faceB = faceB;
    poly.edges[poly.edgeCount].sourceId = sourceId;
    ++poly.edgeCount;
}

void AddPolyFace( PolytopeWorld& poly, const Vector3& normal, const uint16_t* indices, uint8_t count, uint16_t sourceId )
{
    if ( poly.faceCount >= ConvexHullShape::MAX_FACES || poly.faceIndexCount + count > ConvexHullShape::MAX_FACE_INDICES )
    {
        return;
    }

    PolyFaceWorld& face = poly.faces[poly.faceCount++];
    face.normal = normal;
    face.firstIndex = poly.faceIndexCount;
    face.indexCount = count;
    face.sourceId = sourceId;
    face.planeOffset = Dot( normal, poly.vertices[indices[0]] );

    for ( uint8_t i = 0; i < count; ++i )
    {
        poly.faceIndices[poly.faceIndexCount++] = indices[i];
    }
}

PolytopeWorld MakeBoxPolytope( const ObjectContactBodyView& body, const BoundingBox& box )
{
    BoxWorld bw = MakeBoxWorld( body, box );
    PolytopeWorld out;
    out.center = bw.center;
    out.vertexCount = 8;

    for ( uint16_t v = 0; v < 8; ++v )
    {
        const float sx = ( v & 1 ) ? 1.0f : -1.0f;
        const float sy = ( v & 2 ) ? 1.0f : -1.0f;
        const float sz = ( v & 4 ) ? 1.0f : -1.0f;
        out.vertices[v] = bw.center + bw.axes[0] * ( sx * bw.halfExtents.x ) + bw.axes[1] * ( sy * bw.halfExtents.y ) +
                          bw.axes[2] * ( sz * bw.halfExtents.z );
    }

    const uint16_t faceNegX[4] = { 0, 4, 6, 2 };
    const uint16_t facePosX[4] = { 1, 3, 7, 5 };

    const uint16_t faceNegY[4] = { 0, 1, 5, 4 };

    const uint16_t facePosY[4] = { 2, 6, 7, 3 };

    const uint16_t faceNegZ[4] = { 0, 2, 3, 1 };

    const uint16_t facePosZ[4] = { 4, 5, 7, 6 };

    AddPolyFace( out, -bw.axes[0], faceNegX, 4, static_cast<uint16_t>( FaceId( 0, -1.0f ) ) );
    AddPolyFace( out, bw.axes[0], facePosX, 4, static_cast<uint16_t>( FaceId( 0, 1.0f ) ) );
    AddPolyFace( out, -bw.axes[1], faceNegY, 4, static_cast<uint16_t>( FaceId( 1, -1.0f ) ) );
    AddPolyFace( out, bw.axes[1], facePosY, 4, static_cast<uint16_t>( FaceId( 1, 1.0f ) ) );
    AddPolyFace( out, -bw.axes[2], faceNegZ, 4, static_cast<uint16_t>( FaceId( 2, -1.0f ) ) );
    AddPolyFace( out, bw.axes[2], facePosZ, 4, static_cast<uint16_t>( FaceId( 2, 1.0f ) ) );

    for ( int axis = 0; axis < 3; ++axis )
    {
        int side0 = ( axis + 1 ) % 3;
        int side1 = ( axis + 2 ) % 3;

        for ( int sign0 = -1; sign0 <= 1; sign0 += 2 )
        {
            for ( int sign1 = -1; sign1 <= 1; sign1 += 2 )
            {
                uint16_t a = 0;
                uint16_t b = 0;

                for ( uint16_t v = 0; v < 8; ++v )
                {
                    int signs[3] = { ( v & 1 ) ? 1 : -1, ( v & 2 ) ? 1 : -1, ( v & 4 ) ? 1 : -1 };

                    if ( signs[side0] == sign0 && signs[side1] == sign1 )
                    {
                        if ( signs[axis] < 0 )
                        {
                            a = v;
                        }
                        else
                        {
                            b = v;
                        }
                    }
                }

                AddPolyEdge( out, a, b, static_cast<uint16_t>( EdgeId( axis, sign0, sign1 ) ),
                             static_cast<uint16_t>( FaceId( side0, static_cast<float>( sign0 ) ) ),
                             static_cast<uint16_t>( FaceId( side1, static_cast<float>( sign1 ) ) ) );
            }
        }
    }

    return out;
}

PolytopeWorld MakeHullPolytope( const ObjectContactBodyView& body, const ConvexHullShape& hull )
{
    const RotationMatrix rot = body.orientation.GetOrientationMatrix();

    PolytopeWorld out;
    out.center = body.position + rot * hull.GetPosition();
    out.vertexCount = hull.GetVertexCount();
    out.faceCount = hull.GetFaceCount();
    out.edgeCount = hull.GetEdgeCount();

    for ( uint16_t v = 0; v < out.vertexCount; ++v )
    {
        out.vertices[v] = out.center + rot * hull.GetVertex( v );
    }

    for ( uint16_t f = 0; f < out.faceCount; ++f )
    {
        const ConvexHullFace& src = hull.GetFace( f );
        PolyFaceWorld& face = out.faces[f];
        face.normal = rot * src.normalLocal;
        face.firstIndex = out.faceIndexCount;
        face.indexCount = src.indexCount;
        face.sourceId = f;

        for ( uint8_t i = 0; i < src.indexCount; ++i )
        {
            out.faceIndices[out.faceIndexCount++] = hull.GetFaceIndex( src.firstIndex + i );
        }

        face.planeOffset = Dot( face.normal, out.vertices[out.faceIndices[face.firstIndex]] );
    }

    for ( uint16_t e = 0; e < out.edgeCount; ++e )
    {
        const ConvexHullEdge& src = hull.GetEdge( e );
        out.edges[e].vertexA = src.vertexA;
        out.edges[e].vertexB = src.vertexB;
        out.edges[e].faceA = src.faceA;
        out.edges[e].faceB = src.faceB;
        out.edges[e].sourceId = e;
    }

    return out;
}

void ProjectPolytope( const PolytopeWorld& poly, const Vector3& axis, float& outMin, float& outMax )
{
    outMin = FLT_MAX;
    outMax = -FLT_MAX;

    for ( uint16_t i = 0; i < poly.vertexCount; ++i )
    {
        const float p = Dot( poly.vertices[i], axis );
        outMin = (std::min)( outMin, p );
        outMax = (std::max)( outMax, p );
    }
}

bool EdgeSupportsAxis( const PolytopeWorld& poly, const PolyEdgeWorld& edge, const Vector3& axis )
{
    if ( edge.faceA >= poly.faceCount || edge.faceB >= poly.faceCount )
    {
        return false;
    }

    constexpr float normalConeSlop = 1.0e-4f;
    return Dot( poly.faces[edge.faceA].normal, axis ) >= -normalConeSlop &&
           Dot( poly.faces[edge.faceB].normal, axis ) >= -normalConeSlop;
}

bool IsUsefulPolyEdgeAxis( const PolytopeWorld& a, const PolytopeWorld& b, const PolyEdgeWorld& edgeA,
                           const PolyEdgeWorld& edgeB, const Vector3& axisRaw )
{
    const float magSq = VectorMagSquared( axisRaw );

    if ( magSq <= 1.0e-8f )
    {
        return false;
    }

    Vector3 axis = axisRaw / sqrtf( magSq );

    if ( Dot( b.center - a.center, axis ) < 0.0f )
    {
        axis = -axis;
    }

    // Invariant: a saved edge-axis candidate must name the two support edges
    // that face one another along the final A-to-B normal. Accepting the reverse
    // support pair tests the same SAT line but later builds a contact between
    // back-side edges when parallel authored edges tie on overlap.
    return EdgeSupportsAxis( a, edgeA, axis ) && EdgeSupportsAxis( b, edgeB, -axis );
}

bool AcceptPolyAxis( const PolytopeWorld& a, const PolytopeWorld& b, const Vector3& axisRaw, int axisType, int axisA,
                     int axisB, float contactSkin, SatResult& best )
{
    float magSq = VectorMagSquared( axisRaw );

    if ( magSq <= 1.0e-8f )
    {
        return true;
    }

    Vector3 axis = axisRaw / sqrtf( magSq );
    float minA, maxA, minB, maxB;
    ProjectPolytope( a, axis, minA, maxA );
    ProjectPolytope( b, axis, minB, maxB );
    const float overlap = (std::min)( maxA, maxB ) - (std::max)( minA, minB );

    if ( overlap < -contactSkin )
    {
        return false;
    }

    constexpr float tieEpsilon = 1.0e-4f;
    bool better = overlap < best.overlap - tieEpsilon;

    if ( !better && fabsf( overlap - best.overlap ) <= tieEpsilon )
    {
        if ( axisType < best.axisType )
        {
            better = true;
        }
        else if ( axisType == best.axisType && axisA >= 0 && best.axisA >= 0 && axisA < best.axisA )
        {
            better = true;
        }
    }

    if ( better )
    {
        const Vector3 centerDelta = b.center - a.center;
        best.overlap = overlap;
        best.axisType = axisType;
        best.axisA = axisA;
        best.axisB = axisB;
        best.normal = ( Dot( centerDelta, axis ) < 0.0f ) ? -axis : axis;
    }

    return true;
}

bool PolytopeSat( const PolytopeWorld& a, const PolytopeWorld& b, float contactSkin, SatResult& out )
{
    SatResult faceBest;

    for ( uint16_t i = 0; i < a.faceCount; ++i )
    {
        if ( !AcceptPolyAxis( a, b, a.faces[i].normal, 0, i, -1, contactSkin, out ) )
        {
            return false;
        }

        AcceptPolyAxis( a, b, a.faces[i].normal, 0, i, -1, contactSkin, faceBest );
    }

    for ( uint16_t i = 0; i < b.faceCount; ++i )
    {
        if ( !AcceptPolyAxis( a, b, b.faces[i].normal, 1, -1, i, contactSkin, out ) )
        {
            return false;
        }

        AcceptPolyAxis( a, b, b.faces[i].normal, 1, -1, i, contactSkin, faceBest );
    }

    for ( uint16_t i = 0; i < a.edgeCount; ++i )
    {
        const Vector3 edgeA = a.vertices[a.edges[i].vertexB] - a.vertices[a.edges[i].vertexA];

        for ( uint16_t j = 0; j < b.edgeCount; ++j )
        {
            const Vector3 edgeB = b.vertices[b.edges[j].vertexB] - b.vertices[b.edges[j].vertexA];
            const Vector3 edgeAxis = CrossProduct( edgeA, edgeB );

            if ( !IsUsefulPolyEdgeAxis( a, b, a.edges[i], b.edges[j], edgeAxis ) )
            {
                continue;
            }

            if ( !AcceptPolyAxis( a, b, edgeAxis, 2, i, j, contactSkin, out ) )
            {
                return false;
            }
        }
    }

    if ( faceBest.overlap < FLT_MAX )
    {
        out.hasFaceAxis = true;
        out.faceNormal = faceBest.normal;
        out.faceOverlap = faceBest.overlap;
        out.faceAxisType = faceBest.axisType;
        out.faceAxisA = faceBest.axisA;
        out.faceAxisB = faceBest.axisB;
    }

    return out.overlap < FLT_MAX;
}

uint16_t EncodeClippedPolyVertexId( uint16_t prevId, uint16_t curId )
{
    return static_cast<uint16_t>( 0x100u | ( ( prevId & 0x0fu ) << 4 ) | ( curId & 0x0fu ) );
}

int ClipPolyAgainstPlaneLimited( const ClipVertex* input, int inputCount, const Vector3& planePoint,
                                 const Vector3& inwardNormal, float contactSkin, ClipVertex* output, int maxOutput )
{
    if ( inputCount <= 0 )
    {
        return 0;
    }

    int outputCount = 0;
    ClipVertex prev = input[inputCount - 1];
    float prevDist = Dot( ( prev.point - planePoint ), inwardNormal );
    bool prevInside = prevDist >= -contactSkin;

    for ( int i = 0; i < inputCount; ++i )
    {
        ClipVertex cur = input[i];
        float curDist = Dot( ( cur.point - planePoint ), inwardNormal );
        bool curInside = curDist >= -contactSkin;

        if ( curInside != prevInside )
        {
            float denom = prevDist - curDist;
            float t = ( fabsf( denom ) > TOLERANCE ) ? ( prevDist / denom ) : 0.0f;
            t = ClampFloat( t, 0.0f, 1.0f );

            if ( outputCount < maxOutput )
            {
                output[outputCount].point = prev.point + ( cur.point - prev.point ) * t;
                output[outputCount].id = EncodeClippedPolyVertexId( prev.id, cur.id );
                ++outputCount;
            }
        }

        if ( curInside && outputCount < maxOutput )
        {
            output[outputCount++] = cur;
        }

        prev = cur;
        prevDist = curDist;
        prevInside = curInside;
    }

    return outputCount;
}

int ChooseIncidentPolyFace( const PolytopeWorld& incident, const Vector3& refNormal )
{
    int bestFace = 0;
    float bestDot = FLT_MAX;

    for ( uint16_t f = 0; f < incident.faceCount; ++f )
    {
        const float dot = Dot( incident.faces[f].normal, refNormal );

        if ( dot < bestDot - 1.0e-5f )
        {
            bestDot = dot;
            bestFace = f;
        }
    }

    return bestFace;
}

int ChooseReferencePolyFace( const PolytopeWorld& reference, const Vector3& refNormal )
{
    int bestFace = 0;
    float bestDot = -FLT_MAX;

    for ( uint16_t f = 0; f < reference.faceCount; ++f )
    {
        const float dot = Dot( reference.faces[f].normal, refNormal );

        if ( dot > bestDot + 1.0e-5f )
        {
            bestDot = dot;
            bestFace = f;
        }
    }

    return bestFace;
}

bool PointInsidePolyFace( const PolytopeWorld& poly, const PolyFaceWorld& face, const Vector3& point, float tolerance )
{
    Vector3 faceCenter = ZERO_VECTOR;

    for ( uint8_t i = 0; i < face.indexCount; ++i )
    {
        faceCenter += poly.vertices[poly.faceIndices[face.firstIndex + i]];
    }

    faceCenter /= static_cast<float>( face.indexCount );

    for ( uint8_t i = 0; i < face.indexCount; ++i )
    {
        const Vector3 a = poly.vertices[poly.faceIndices[face.firstIndex + i]];
        const Vector3 b = poly.vertices[poly.faceIndices[face.firstIndex + ( ( i + 1 ) % face.indexCount )]];
        Vector3 inward = CrossProduct( face.normal, b - a );
        const float magSq = VectorMagSquared( inward );

        if ( magSq <= 1.0e-8f )
        {
            continue;
        }

        inward /= sqrtf( magSq );

        if ( Dot( ( faceCenter - a ), inward ) < 0.0f )
        {
            inward = -inward;
        }

        if ( Dot( ( point - a ), inward ) < -tolerance )
        {
            return false;
        }
    }

    return true;
}

Vector3 ClosestPointOnSegment( const Vector3& a, const Vector3& b, const Vector3& p, float& tOut )
{
    const Vector3 ab = b - a;
    const float denom = VectorMagSquared( ab );

    if ( denom <= 1.0e-8f )
    {
        tOut = 0.0f;
        return a;
    }

    tOut = ClampFloat( ( Dot( ( p - a ), ab ) ) / denom, 0.0f, 1.0f );
    return a + ab * tOut;
}

uint32_t SphereHullFeatureSortKey( SphereHullFeatureKind kind, uint32_t sourceId )
{
    return ( static_cast<uint32_t>( kind ) << 24 ) | sourceId;
}

void ConsiderSphereHullCandidate( SphereHullClosestFeature& best, const Vector3& point, const Vector3& sphereCenter,
                                  SphereHullFeatureKind kind, uint16_t sourceId )
{
    const float distSq = VectorMagSquared( sphereCenter - point );
    const float tieEpsilon = 1.0e-6f;
    bool replace = distSq < best.distSq - tieEpsilon;

    if ( !replace && fabsf( distSq - best.distSq ) <= tieEpsilon )
    {
        replace = SphereHullFeatureSortKey( kind, sourceId ) < SphereHullFeatureSortKey( best.kind, best.sourceId );
    }

    if ( replace )
    {
        best.point = point;
        best.kind = kind;
        best.sourceId = sourceId;
        best.distSq = distSq;
    }
}

SphereHullClosestFeature ClosestSphereHullBoundaryFeature( const PolytopeWorld& hullWorld, const Vector3& sphereCenter,
                                                           float contactSkin )
{
    SphereHullClosestFeature best;

    for ( uint16_t f = 0; f < hullWorld.faceCount; ++f )
    {
        const PolyFaceWorld& face = hullWorld.faces[f];
        const float signedDistance = ( Dot( face.normal, sphereCenter ) ) - face.planeOffset;
        const Vector3 projected = sphereCenter - face.normal * signedDistance;

        if ( PointInsidePolyFace( hullWorld, face, projected, contactSkin ) )
        {
            ConsiderSphereHullCandidate( best, projected, sphereCenter, SphereHullFeatureKind::Face, face.sourceId );
        }
    }

    for ( uint16_t e = 0; e < hullWorld.edgeCount; ++e )
    {
        const PolyEdgeWorld& edge = hullWorld.edges[e];
        float t = 0.0f;
        const Vector3 point = ClosestPointOnSegment( hullWorld.vertices[edge.vertexA], hullWorld.vertices[edge.vertexB],
                                                     sphereCenter, t );

        SphereHullFeatureKind kind = SphereHullFeatureKind::Edge;
        uint16_t sourceId = edge.sourceId;

        if ( t <= 1.0e-4f )
        {
            kind = SphereHullFeatureKind::Vertex;
            sourceId = edge.vertexA;
        }
        else if ( t >= 1.0f - 1.0e-4f )
        {
            kind = SphereHullFeatureKind::Vertex;
            sourceId = edge.vertexB;
        }

        ConsiderSphereHullCandidate( best, point, sphereCenter, kind, sourceId );
    }

    return best;
}

bool BuildPolyFaceContact( const ObjectContactBodyView& aBody, const ObjectContactBodyView& bBody,
                           const PolytopeWorld& polyA, const PolytopeWorld& polyB, bool referenceIsA, int referenceFaceIndex,
                           const Vector3& finalNormal, float contactSkin, ObjectContactManifold& out )
{
    constexpr int MAX_POLY_CLIP_VERTS = 32;

    const PolytopeWorld& ref = referenceIsA ? polyA : polyB;
    const PolytopeWorld& inc = referenceIsA ? polyB : polyA;
    const PolyFaceWorld& refFace = ref.faces[referenceFaceIndex];
    const Vector3 refNormal = referenceIsA ? finalNormal : -finalNormal;

    const int incidentFaceIndex = ChooseIncidentPolyFace( inc, refNormal );
    const PolyFaceWorld& incFace = inc.faces[incidentFaceIndex];

    ClipVertex workA[MAX_POLY_CLIP_VERTS];
    ClipVertex workB[MAX_POLY_CLIP_VERTS];
    int count = 0;

    for ( uint8_t i = 0; i < incFace.indexCount && count < MAX_POLY_CLIP_VERTS; ++i )
    {
        const uint16_t vertexIndex = inc.faceIndices[incFace.firstIndex + i];
        workA[count].point = inc.vertices[vertexIndex];
        workA[count].id = static_cast<uint8_t>( i );
        ++count;
    }

    Vector3 refCenter = ZERO_VECTOR;

    for ( uint8_t i = 0; i < refFace.indexCount; ++i )
    {
        refCenter += ref.vertices[ref.faceIndices[refFace.firstIndex + i]];
    }

    refCenter /= static_cast<float>( refFace.indexCount );

    for ( uint8_t i = 0; i < refFace.indexCount; ++i )
    {
        const Vector3 a = ref.vertices[ref.faceIndices[refFace.firstIndex + i]];
        const Vector3 b = ref.vertices[ref.faceIndices[refFace.firstIndex + ( ( i + 1 ) % refFace.indexCount )]];
        Vector3 inward = CrossProduct( refNormal, b - a );
        const float magSq = VectorMagSquared( inward );

        if ( magSq <= 1.0e-8f )
        {
            continue;
        }

        inward /= sqrtf( magSq );

        if ( Dot( ( refCenter - a ), inward ) < 0.0f )
        {
            inward = -inward;
        }

        count = ClipPolyAgainstPlaneLimited( workA, count, a, inward, contactSkin, workB, MAX_POLY_CLIP_VERTS );

        if ( count == 0 )
        {
            return false;
        }

        for ( int j = 0; j < count; ++j )
        {
            workA[j] = workB[j];
        }
    }

    ObjectContactCandidate candidates[MAX_POLY_CLIP_VERTS];
    int candidateCount = 0;
    const Vector3 refPlanePoint = ref.vertices[ref.faceIndices[refFace.firstIndex]];

    for ( int i = 0; i < count; ++i )
    {
        const float separation = Dot( ( workA[i].point - refPlanePoint ), refNormal );

        if ( separation > contactSkin )
        {
            continue;
        }

        if ( candidateCount < MAX_POLY_CLIP_VERTS )
        {
            candidates[candidateCount].point = workA[i].point - refNormal * ( separation * 0.5f );
            candidates[candidateCount].penetration = -separation;
            candidates[candidateCount].featureId = EncodeHullFaceFeature( referenceIsA, refFace.sourceId, incFace.sourceId,
                                                                          workA[i].id );

            ++candidateCount;
        }
    }

    const ObjectContactCandidateSelection selection = SelectObjectContactCandidateIndices( candidates, candidateCount,
                                                                                           refNormal );

    for ( uint8_t selectedIndex = 0; selectedIndex < selection.count && out.pointCount < 4; ++selectedIndex )
    {
        const ObjectContactCandidate& candidate = candidates[selection.indices[selectedIndex]];
        AddContactPoint( aBody, bBody, out, candidate.point, candidate.penetration, candidate.featureId );
    }

    return out.pointCount > 0;
}

bool BuildBestPolyFaceContact( const ObjectContactBodyView& aBody, const ObjectContactBodyView& bBody,
                               const PolytopeWorld& polyA, const PolytopeWorld& polyB, const Vector3& finalNormal,
                               int preferredReference, float contactSkin, ObjectContactManifold& out )
{
    const int faceA = ChooseReferencePolyFace( polyA, finalNormal );
    const int faceB = ChooseReferencePolyFace( polyB, -finalNormal );
    const float alignA = Dot( polyA.faces[faceA].normal, finalNormal );
    const float alignB = Dot( polyB.faces[faceB].normal, -finalNormal );

    constexpr float tieEpsilon = 1.0e-4f;
    const bool tryAFirst = alignA > alignB + tieEpsilon ||
                           ( fabsf( alignA - alignB ) <= tieEpsilon && preferredReference == 0 );

    auto tryBuild = [&]( bool referenceIsA, ObjectContactManifold& candidate ) -> bool
    {
        candidate.bodyA = out.bodyA;

        candidate.bodyB = out.bodyB;
        candidate.normal = finalNormal;
        return referenceIsA
                   ? BuildPolyFaceContact( aBody, bBody, polyA, polyB, true, faceA, finalNormal, contactSkin, candidate )
                   : BuildPolyFaceContact( aBody, bBody, polyA, polyB, false, faceB, finalNormal, contactSkin, candidate );
    };

    ObjectContactManifold candidateA;
    ObjectContactManifold candidateB;
    const bool builtA = tryBuild( true, candidateA );
    const bool builtB = tryBuild( false, candidateB );

    if ( !builtA && !builtB )
    {
        return false;
    }

    if ( builtA && !builtB )
    {
        out = candidateA;
        return true;
    }

    if ( !builtA && builtB )
    {
        out = candidateB;
        return true;
    }

    if ( candidateA.pointCount > candidateB.pointCount || ( candidateA.pointCount == candidateB.pointCount && tryAFirst ) )
    {
        out = candidateA;
        return true;
    }

    out = candidateB;
    return true;
}

bool BuildPolyEdgeContact( const ObjectContactBodyView& aBody, const ObjectContactBodyView& bBody,
                           const PolytopeWorld& polyA, const PolytopeWorld& polyB, const SatResult& sat,
                           ObjectContactManifold& out )
{
    const PolyEdgeWorld& edgeA = polyA.edges[sat.axisA];
    const PolyEdgeWorld& edgeB = polyB.edges[sat.axisB];
    Vector3 ca;
    Vector3 cb;
    ClosestPointsOnSegments( polyA.vertices[edgeA.vertexA], polyA.vertices[edgeA.vertexB], polyB.vertices[edgeB.vertexA],
                             polyB.vertices[edgeB.vertexB], ca, cb );

    AddContactPoint( aBody, bBody, out, ( ca + cb ) * 0.5f, sat.overlap,
                     EncodeHullEdgeFeature( edgeA.sourceId, edgeB.sourceId ) );

    return out.pointCount > 0;
}

bool BuildPolyPoly( const ObjectContactBodyView& aBody, const PolytopeWorld& polyA, const ObjectContactBodyView& bBody,
                    const PolytopeWorld& polyB, float contactSkin, ObjectContactManifold& out )
{
    SatResult sat;

    if ( !PolytopeSat( polyA, polyB, contactSkin, sat ) )
    {
        return false;
    }

    out.normal = sat.normal;

    if ( sat.axisType == 2 && sat.hasFaceAxis )
    {
        const float faceAxisTolerance = (std::max)( contactSkin * 2.0f, sat.overlap * 0.10f );

        if ( sat.faceOverlap <= sat.overlap + faceAxisTolerance )
        {
            ObjectContactManifold faceOut;
            faceOut.bodyA = out.bodyA;
            faceOut.bodyB = out.bodyB;
            const bool builtFace = BuildBestPolyFaceContact( aBody, bBody, polyA, polyB, sat.faceNormal, sat.faceAxisType,
                                                             contactSkin, faceOut );

            if ( builtFace && faceOut.pointCount >= 2 )
            {
                out = faceOut;
                return true;
            }
        }
    }

    if ( sat.axisType == 0 )
    {
        return BuildBestPolyFaceContact( aBody, bBody, polyA, polyB, sat.normal, 0, contactSkin, out );
    }

    if ( sat.axisType == 1 )
    {
        return BuildBestPolyFaceContact( aBody, bBody, polyA, polyB, sat.normal, 1, contactSkin, out );
    }

    return BuildPolyEdgeContact( aBody, bBody, polyA, polyB, sat, out );
}

bool BuildSphereHullOrdered( const ObjectContactBodyView& sphereBody, const BoundingSphere& sphere,
                             const ObjectContactBodyView& hullBody, const ConvexHullShape& hull, bool sphereIsA,
                             float contactSkin, ObjectContactManifold& out )
{
    const PolytopeWorld hullWorld = MakeHullPolytope( hullBody, hull );
    const Vector3 sphereCenter = SphereCenter( sphereBody, sphere );

    float maxSignedDistance = -FLT_MAX;
    int closestFace = 0;

    for ( uint16_t f = 0; f < hullWorld.faceCount; ++f )
    {
        const float signedDistance = ( Dot( hullWorld.faces[f].normal, sphereCenter ) ) - hullWorld.faces[f].planeOffset;

        if ( signedDistance > maxSignedDistance )
        {
            maxSignedDistance = signedDistance;
            closestFace = f;
        }
    }

    if ( maxSignedDistance > sphere.GetRadius() + contactSkin )
    {
        return false;
    }

    Vector3 closestPoint = sphereCenter;

    for ( uint16_t f = 0; f < hullWorld.faceCount; ++f )
    {
        const float signedDistance = ( Dot( hullWorld.faces[f].normal, closestPoint ) ) - hullWorld.faces[f].planeOffset;

        if ( signedDistance > 0.0f )
        {
            closestPoint -= hullWorld.faces[f].normal * signedDistance;
        }
    }

    Vector3 hullOutward = hullWorld.faces[closestFace].normal;
    float penetration = sphere.GetRadius() - maxSignedDistance;
    SphereHullFeatureKind featureKind = SphereHullFeatureKind::Face;
    uint16_t featureId = hullWorld.faces[closestFace].sourceId;

    if ( maxSignedDistance > 0.0f )
    {
        const SphereHullClosestFeature closest = ClosestSphereHullBoundaryFeature( hullWorld, sphereCenter, contactSkin );

        closestPoint = closest.point;
        const Vector3 centerToClosest = sphereCenter - closestPoint;
        const float distSq = closest.distSq;
        const float dist = sqrtf( distSq );

        if ( dist > sphere.GetRadius() + contactSkin )
        {
            return false;
        }

        if ( dist > TOLERANCE )
        {
            hullOutward = centerToClosest / dist;
        }

        penetration = sphere.GetRadius() - dist;
        featureKind = closest.kind;
        featureId = closest.sourceId;
    }
    else
    {
        closestPoint = sphereCenter - hullOutward * maxSignedDistance;
    }

    const Vector3 normalSphereToHull = -hullOutward;
    out.normal = sphereIsA ? normalSphereToHull : -normalSphereToHull;
    const Vector3 spherePoint = sphereCenter + normalSphereToHull * sphere.GetRadius();
    const Vector3 contactPoint = ( spherePoint + closestPoint ) * 0.5f;
    AddContactPoint( sphereIsA ? sphereBody : hullBody, sphereIsA ? hullBody : sphereBody, out, contactPoint, penetration,
                     EncodeSphereHullFeature( !sphereIsA, featureKind, featureId ) );

    return out.pointCount > 0;
}

bool BuildBoxHull( const ObjectContactBodyView& boxBody, const BoundingBox& box, const ObjectContactBodyView& hullBody,
                   const ConvexHullShape& hull, bool boxIsA, float contactSkin, ObjectContactManifold& out )
{
    const PolytopeWorld boxPoly = MakeBoxPolytope( boxBody, box );
    const PolytopeWorld hullPoly = MakeHullPolytope( hullBody, hull );

    if ( boxIsA )
    {
        return BuildPolyPoly( boxBody, boxPoly, hullBody, hullPoly, contactSkin, out );
    }

    return BuildPolyPoly( hullBody, hullPoly, boxBody, boxPoly, contactSkin, out );
}

bool BuildHullHull( const ObjectContactBodyView& aBody, const ConvexHullShape& aHull, const ObjectContactBodyView& bBody,
                    const ConvexHullShape& bHull, float contactSkin, ObjectContactManifold& out )
{
    const PolytopeWorld polyA = MakeHullPolytope( aBody, aHull );
    const PolytopeWorld polyB = MakeHullPolytope( bBody, bHull );
    return BuildPolyPoly( aBody, polyA, bBody, polyB, contactSkin, out );
}
} // namespace

namespace
{
template <typename ShapeA, typename ShapeB>
ObjectContactSweepResult SweepObjectContactImpl( const ObjectContactBodyView& a, const ShapeA& shapeA,
                                                 const Vector3& linearVelocityA, const ObjectContactBodyView& b,
                                                 const ShapeB& shapeB, const Vector3& linearVelocityB, float changeInTime )
{
    // Concept: CCD sweep is only a conservative front-end. It uses each body's
    // current position plus linear displacement to find the first candidate
    // overlap; precise contact geometry and velocity response remain with the
    // manifold builder and persistent solver.
    ObjectContactSweepResult result;
    result.collisionTime = changeInTime;

    const RotationMatrix rotationA = a.orientation.GetOrientationMatrix();
    const RotationMatrix rotationB = b.orientation.GetOrientationMatrix();

    // Invariant: concrete swept-shape helpers add their stored local centre to
    // the ray origin. Pre-adjust each body origin so that addition produces the
    // rotated world collider centre. This changes byte-exact results only for
    // non-zero-offset colliders and keeps the public shape sweep contract intact.
    const Vector3 focusOrigin = GetWorldShapeCenter( shapeA, a.position, rotationA ) - GetShapePosition( shapeA );
    const Vector3 targetOrigin = GetWorldShapeCenter( shapeB, b.position, rotationB ) - GetShapePosition( shapeB );
    const Ray targetRay( targetOrigin, linearVelocityB * changeInTime );
    const Ray focusRay( focusOrigin, linearVelocityA * changeInTime );
    const float collisionTime = TestShapeCollision( shapeA, shapeB, focusRay, targetRay );

    if ( collisionTime > 1.0f || collisionTime < ZERO_TAKE_TOLERANCE )
    {
        return result;
    }

    result.hit = true;
    result.collisionTime = collisionTime * changeInTime;
    return result;
}


// CATTO REF:
//   Public entry point that returns the contact rows Catto's iterative solver
//   expects: normal, rA/rB, penetration, and stable contact IDs.
// ENGINE-SPECIFIC:
//   Dispatches owning shapes or ColliderStore's borrowed per-kind shape
//   references to the local 3D manifold builders. The normal is always oriented
//   from body A toward body B so the solver can use one impulse sign convention.
template <typename ShapeA, typename ShapeB>
bool BuildObjectContactManifoldImpl( SkullbonezCore::Core::Profiler*, const ObjectContactBodyView& a, const ShapeA& shapeA,
                                     const ObjectContactBodyView& b, const ShapeB& shapeB, int bodyA, int bodyB,
                                     float contactSkin, ObjectContactManifold& out )
{
    PROFILE_SCOPED( "Frame/Physics/Narrowphase/ObjectManifold" );

    // Shape dispatch is intentionally explicit. The solver wants one uniform
    // manifold shape, but the geometry needed to produce it differs a lot:
    // sphere/sphere is center distance, sphere/box is closest point, and box/box
    // is SAT plus face clipping or edge fallback.
    out = ObjectContactManifold();
    out.bodyA = bodyA;
    out.bodyB = bodyB;

    return VisitCollisionShape( shapeA,
                                [&]( const auto& shapeValueA )
                                {
                                    return VisitCollisionShape( shapeB,
                                                                [&]( const auto& shapeValueB )
                                                                {
                                                                    using ShapeTypeA = std::decay_t<decltype( shapeValueA )>;
                                                                    using ShapeTypeB = std::decay_t<decltype( shapeValueB )>;

                                                                    if constexpr ( std::is_same_v<ShapeTypeA,
                                                                                                  BoundingSphere> &&
                                                                                   std::is_same_v<ShapeTypeB,
                                                                                                  BoundingSphere> )
                                                                    {
                                                                        PROFILE_SCOPED( "Frame/Physics/Narrowphase/"
                                                                                        "ObjectManifold/SphereSphere" );

                                                                        return BuildSphereSphere( a, shapeValueA, b,
                                                                                                  shapeValueB, contactSkin,
                                                                                                  out );
                                                                    }
                                                                    else if constexpr ( std::is_same_v<ShapeTypeA,
                                                                                                       BoundingSphere> &&
                                                                                        std::is_same_v<ShapeTypeB,
                                                                                                       BoundingBox> )
                                                                    {
                                                                        PROFILE_SCOPED( "Frame/Physics/Narrowphase/"
                                                                                        "ObjectManifold/SphereBox" );

                                                                        return BuildSphereBoxOrdered( a, shapeValueA, b,
                                                                                                      shapeValueB, true,
                                                                                                      contactSkin, out );
                                                                    }
                                                                    else if constexpr ( std::is_same_v<ShapeTypeA,
                                                                                                       BoundingSphere> &&
                                                                                        std::is_same_v<ShapeTypeB,
                                                                                                       ConvexHullShape> )
                                                                    {
                                                                        PROFILE_SCOPED( "Frame/Physics/Narrowphase/"
                                                                                        "ObjectManifold/SphereHull" );

                                                                        return BuildSphereHullOrdered( a, shapeValueA, b,
                                                                                                       shapeValueB, true,
                                                                                                       contactSkin, out );
                                                                    }
                                                                    else if constexpr ( std::is_same_v<ShapeTypeA,
                                                                                                       BoundingBox> &&
                                                                                        std::is_same_v<ShapeTypeB,
                                                                                                       BoundingSphere> )
                                                                    {
                                                                        PROFILE_SCOPED( "Frame/Physics/Narrowphase/"
                                                                                        "ObjectManifold/SphereBox" );

                                                                        return BuildSphereBoxOrdered( b, shapeValueB, a,
                                                                                                      shapeValueA, false,
                                                                                                      contactSkin, out );
                                                                    }
                                                                    else if constexpr ( std::is_same_v<ShapeTypeA,
                                                                                                       BoundingBox> &&
                                                                                        std::is_same_v<ShapeTypeB,
                                                                                                       BoundingBox> )
                                                                    {
                                                                        PROFILE_SCOPED( "Frame/Physics/Narrowphase/"
                                                                                        "ObjectManifold/BoxBox" );

                                                                        return BuildBoxBox( a, shapeValueA, b, shapeValueB,
                                                                                            contactSkin, out );
                                                                    }
                                                                    else if constexpr ( std::is_same_v<ShapeTypeA,
                                                                                                       BoundingBox> &&
                                                                                        std::is_same_v<ShapeTypeB,
                                                                                                       ConvexHullShape> )
                                                                    {
                                                                        PROFILE_SCOPED( "Frame/Physics/Narrowphase/"
                                                                                        "ObjectManifold/BoxHull" );

                                                                        return BuildBoxHull( a, shapeValueA, b, shapeValueB,
                                                                                             true, contactSkin, out );
                                                                    }
                                                                    else if constexpr ( std::is_same_v<ShapeTypeA,
                                                                                                       ConvexHullShape> &&
                                                                                        std::is_same_v<ShapeTypeB,
                                                                                                       BoundingSphere> )
                                                                    {
                                                                        PROFILE_SCOPED( "Frame/Physics/Narrowphase/"
                                                                                        "ObjectManifold/SphereHull" );

                                                                        return BuildSphereHullOrdered( b, shapeValueB, a,
                                                                                                       shapeValueA, false,
                                                                                                       contactSkin, out );
                                                                    }
                                                                    else if constexpr ( std::is_same_v<ShapeTypeA,
                                                                                                       ConvexHullShape> &&
                                                                                        std::is_same_v<ShapeTypeB,
                                                                                                       BoundingBox> )
                                                                    {
                                                                        PROFILE_SCOPED( "Frame/Physics/Narrowphase/"
                                                                                        "ObjectManifold/BoxHull" );

                                                                        return BuildBoxHull( b, shapeValueB, a, shapeValueA,
                                                                                             false, contactSkin, out );
                                                                    }
                                                                    else
                                                                    {
                                                                        static_assert( std::is_same_v<ShapeTypeA,
                                                                                                      ConvexHullShape> &&
                                                                                           std::is_same_v<ShapeTypeB,
                                                                                                          ConvexHullShape>,
                                                                                       "Every CollisionShape pair requires "
                                                                                       "explicit narrowphase dispatch." );

                                                                        PROFILE_SCOPED( "Frame/Physics/Narrowphase/"
                                                                                        "ObjectManifold/HullHull" );

                                                                        return BuildHullHull( a, shapeValueA, b, shapeValueB,
                                                                                              contactSkin, out );
                                                                    }
                                                                } );
                                } );
}
} // namespace

ObjectContactSweepResult SkullbonezCore::Physics::SweepObjectContact( const ObjectContactBodyView& a, const CollisionShape& shapeA, const Vector3& linearVelocityA,
                                                                      const ObjectContactBodyView& b, const CollisionShape& shapeB, const Vector3& linearVelocityB, float changeInTime )
{
    return SweepObjectContactImpl( a, shapeA, linearVelocityA, b, shapeB, linearVelocityB, changeInTime );
}

ObjectContactSweepResult SkullbonezCore::Physics::SweepObjectContact( const ObjectContactBodyView& a,
                                                                      const CollisionShapeReference& shapeA,
                                                                      const Vector3& linearVelocityA,
                                                                      const ObjectContactBodyView& b,
                                                                      const CollisionShapeReference& shapeB,
                                                                      const Vector3& linearVelocityB, float changeInTime )
{
    return SweepObjectContactImpl( a, shapeA, linearVelocityA, b, shapeB, linearVelocityB, changeInTime );
}

bool SkullbonezCore::Physics::BuildObjectContactManifold( Core::Profiler* profiler, const ObjectContactBodyView& a,
                                                          const CollisionShape& shapeA, const ObjectContactBodyView& b,
                                                          const CollisionShape& shapeB, int bodyA, int bodyB,
                                                          float contactSkin, ObjectContactManifold& out )
{
    return BuildObjectContactManifoldImpl( profiler, a, shapeA, b, shapeB, bodyA, bodyB, contactSkin, out );
}

bool SkullbonezCore::Physics::BuildObjectContactManifold( Core::Profiler* profiler, const ObjectContactBodyView& a,
                                                          const CollisionShapeReference& shapeA,
                                                          const ObjectContactBodyView& b,
                                                          const CollisionShapeReference& shapeB, int bodyA, int bodyB,
                                                          float contactSkin, ObjectContactManifold& out )
{
    return BuildObjectContactManifoldImpl( profiler, a, shapeA, b, shapeB, bodyA, bodyB, contactSkin, out );
}
