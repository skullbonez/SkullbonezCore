#include "SkullbonezObjectContactManifold.h"

#include <algorithm>
#include "SkullbonezBoundingBox.h"
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezCollisionShape.h"
#include "SkullbonezQuaternion.h"

using namespace SkullbonezCore::GameObjects;
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
// ENGINE-SPECIFIC / NOVEL:
//   This file is Skullbonez narrowphase policy, not Catto's 2D box clipping
//   implementation. It supplies exact sphere/sphere, closest-point sphere/OBB,
//   and SAT plus clipped OBB/OBB contacts for the existing 3D engine.
// LAYMAN VERSION:
//   Broadphase has already said "these two objects are close enough to inspect."
//   This file answers the expensive geometry question: exactly where are they
//   touching, and which direction should the solver push to separate them?
namespace
{
// ENGINE-SPECIFIC / NOVEL:
//   Feature IDs are compact and deterministic because the warm-start cache only
//   keeps the low 16 bits in GameModelCollection::makeKey. The kind bits make
//   sphere/box, face/face, and edge/edge contacts distinct even for the same
//   body pair.
constexpr uint32_t FEATURE_KIND_SPHERE_BOX = 1u;
constexpr uint32_t FEATURE_KIND_BOX_FACE = 2u;
constexpr uint32_t FEATURE_KIND_BOX_EDGE = 3u;

// ENGINE-SPECIFIC / NOVEL:
//   BoxWorld caches the OBB basis in world space. Catto's solver later needs
//   world-space r vectors and normals; doing this conversion once in narrowphase
//   avoids mixing local and world terms inside the PGS row solve.
struct BoxWorld
{
    Vector3 center = ZERO_VECTOR;
    Vector3 halfExtents = ZERO_VECTOR;
    Vector3 axes[3] = {
        Vector3( 1.0f, 0.0f, 0.0f ),
        Vector3( 0.0f, 1.0f, 0.0f ),
        Vector3( 0.0f, 0.0f, 1.0f ) };
};

struct SatResult
{
    // ENGINE-SPECIFIC / NOVEL:
    //   Stores the winning SAT axis. Catto's solver does not care how the axis
    //   was found; it only receives the final normal and contact rows.
    Vector3 normal = ZERO_VECTOR;
    float overlap = FLT_MAX;
    int axisType = 0; // 0 = A face, 1 = B face, 2 = edge-edge
    int axisA = -1;
    int axisB = -1;
};

// ENGINE-SPECIFIC / NOVEL:
//   ClipVertex keeps a small incident-face vertex ID through clipping. That ID
//   becomes part of the feature key, letting the Catto warm-start cache match
//   individual face-contact rows instead of treating a whole box pair as one
//   anonymous contact.
struct ClipVertex
{
    Vector3 point = ZERO_VECTOR;
    uint8_t id = 0;
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

BoxWorld MakeBoxWorld( const GameModel& model, const BoundingBox& box )
{
    // ENGINE-SPECIFIC / NOVEL:
    //   Convert the engine's local box shape plus body orientation into an OBB
    //   basis. Catto's equations downstream operate in world space; this is the
    //   bridge from Skullbonez render/shape state to solver row geometry.
    Quaternion q = model.GetOrientation();
    RotationMatrix rot = q.GetOrientationMatrix();

    BoxWorld out;
    out.halfExtents = box.GetHalfExtents();
    out.axes[0] = rot * Vector3( 1.0f, 0.0f, 0.0f );
    out.axes[1] = rot * Vector3( 0.0f, 1.0f, 0.0f );
    out.axes[2] = rot * Vector3( 0.0f, 0.0f, 1.0f );
    out.center = model.GetPosition() + rot * box.GetPosition();
    return out;
}

Vector3 SphereCenter( const GameModel& model, const BoundingSphere& sphere )
{
    // ENGINE-SPECIFIC:
    //   Spheres can carry a local shape offset. Rotate it through the body
    //   orientation before building Catto-style world-space contact arms.
    Quaternion q = model.GetOrientation();
    RotationMatrix rot = q.GetOrientationMatrix();
    return model.GetPosition() + rot * sphere.GetPosition();
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
    // ENGINE-SPECIFIC / NOVEL:
    //   A sphere/box row is identified by which body owns the box and which box
    //   face the sphere is touching. Catto requires stable identifiers for
    //   contact caching; the bit layout is Skullbonez policy.
    uint32_t boxCode = ( boxIsA ? 8u : 0u ) + faceId;
    return ( FEATURE_KIND_SPHERE_BOX << 14 ) | ( boxCode << 10 );
}

uint32_t EncodeBoxFaceFeature( bool referenceIsA, uint32_t referenceFace, uint32_t incidentFace, uint32_t pointId )
{
    // ENGINE-SPECIFIC / NOVEL:
    //   Face contacts include reference face, incident face, and clipped vertex
    //   ID so each row in a four-point manifold can warm start independently.
    uint32_t refCode = ( referenceIsA ? 0u : 8u ) + referenceFace;
    uint32_t incCode = ( referenceIsA ? 8u : 0u ) + incidentFace;
    return ( FEATURE_KIND_BOX_FACE << 14 ) |
           ( ( refCode & 0x0fu ) << 10 ) |
           ( ( incCode & 0x0fu ) << 6 ) |
           ( pointId & 0x3fu );
}

uint32_t EncodeBoxEdgeFeature( uint32_t edgeA, uint32_t edgeB )
{
    // ENGINE-SPECIFIC / NOVEL:
    //   Edge contacts use the two participating OBB edge IDs. There is only one
    //   row for edge-edge, but the ID still needs to survive across frames for
    //   Catto Section 8 temporal coherence.
    return ( FEATURE_KIND_BOX_EDGE << 14 ) |
           ( ( edgeA & 0x0fu ) << 10 ) |
           ( ( edgeB & 0x0fu ) << 6 );
}

void AddContactPoint( const GameModel& a,
                      const GameModel& b,
                      ObjectContactManifold& manifold,
                      const Vector3& point,
                      float penetration,
                      uint32_t featureId )
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
    cp.rA = point - a.GetPosition();
    cp.rB = point - b.GetPosition();
    cp.penetration = ( penetration > 0.0f ) ? penetration : 0.0f;
    cp.featureId = featureId;
}

// CATTO REF:
//   The result is Catto's simplest contact model: one point, one normal, and one
//   penetration value. rA/rB are filled in AddContactPoint for Equations 9-11.
// ENGINE-SPECIFIC / NOVEL:
//   Sphere centers may include local shape offsets, so SphereCenter applies the
//   current orientation before using the classic center-to-center normal.
bool BuildSphereSphere( const GameModel& a,
                        const BoundingSphere& sphereA,
                        const GameModel& b,
                        const BoundingSphere& sphereB,
                        float contactSkin,
                        ObjectContactManifold& out )
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

// ENGINE-SPECIFIC / NOVEL:
//   Sphere/OBB uses the closest point on the oriented box in box-local space.
//   When the sphere center is inside the box, the closest point is ambiguous; we
//   choose the nearest face so the normal and feature ID stay deterministic.
bool BuildSphereBoxOrdered( const GameModel& sphereModel,
                            const BoundingSphere& sphere,
                            const GameModel& boxModel,
                            const BoundingBox& box,
                            bool sphereIsA,
                            float contactSkin,
                            ObjectContactManifold& out )
{
    BoxWorld bw = MakeBoxWorld( boxModel, box );
    Vector3 sphereCenter = SphereCenter( sphereModel, sphere );
    Quaternion q = boxModel.GetOrientation();
    RotationMatrix rot = q.GetOrientationMatrix();
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
        float distances[3] = {
            bw.halfExtents.x - fabsf( local.x ),
            bw.halfExtents.y - fabsf( local.y ),
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
    AddContactPoint( sphereIsA ? sphereModel : boxModel,
                     sphereIsA ? boxModel : sphereModel,
                     out,
                     contactPoint,
                     penetration,
                     EncodeSphereBoxFeature( !sphereIsA, face ) );
    return out.pointCount > 0;
}

float ProjectBoxRadius( const BoxWorld& box, const Vector3& axis )
{
    // Imagine shining a light along "axis" and measuring the box's shadow on
    // that line. The projected radius is half the length of that shadow. SAT
    // uses this to ask whether two box shadows overlap on every possible axis.
    return box.halfExtents.x * fabsf( box.axes[0] * axis ) +
           box.halfExtents.y * fabsf( box.axes[1] * axis ) +
           box.halfExtents.z * fabsf( box.axes[2] * axis );
}

// ENGINE-SPECIFIC / NOVEL:
//   OBB/OBB detection uses the 15-axis separating-axis test: three face normals
//   from A, three from B, and nine edge cross-products. Catto consumes the final
//   manifold rows, but this SAT selection is local 3D narrowphase policy. Ties
//   prefer face axes before edge axes to keep stacks from flipping between
//   equivalent edge contacts when overlap is nearly equal.
bool AcceptSatAxis( const BoxWorld& a,
                    const BoxWorld& b,
                    const Vector3& axisRaw,
                    int axisType,
                    int axisA,
                    int axisB,
                    const Vector3& centerDelta,
                    float contactSkin,
                    SatResult& best )
{
    float magSq = VectorMagSquared( axisRaw );
    if ( magSq <= 1.0e-8f )
    {
        return true;
    }

    Vector3 axis = axisRaw / sqrtf( magSq );
    float distance = fabsf( centerDelta * axis );
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
        best.normal = ( centerDelta * axis < 0.0f ) ? -axis : axis;
    }

    return true;
}

// ENGINE-SPECIFIC / NOVEL:
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

// ENGINE-SPECIFIC / NOVEL:
//   Builds the four world-space vertices of one OBB face. The winding is stable
//   and deterministic so clipped contact point feature IDs do not reshuffle
//   across frames.
void BuildFaceVertices( const BoxWorld& box, int faceAxis, float faceSign, ClipVertex outVerts[4] )
{
    int side0 = ( faceAxis + 1 ) % 3;
    int side1 = ( faceAxis + 2 ) % 3;
    float sideSigns[4][2] = {
        { 1.0f, 1.0f },
        { -1.0f, 1.0f },
        { -1.0f, -1.0f },
        { 1.0f, -1.0f } };

    for ( int i = 0; i < 4; ++i )
    {
        outVerts[i].point = box.center +
                            box.axes[faceAxis] * ( faceSign * Component( box.halfExtents, faceAxis ) ) +
                            box.axes[side0] * ( sideSigns[i][0] * Component( box.halfExtents, side0 ) ) +
                            box.axes[side1] * ( sideSigns[i][1] * Component( box.halfExtents, side1 ) );
        outVerts[i].id = static_cast<uint8_t>( i );
    }
}

// ENGINE-SPECIFIC / NOVEL:
//   Sutherland-Hodgman clipping against one side plane of the reference face.
//   Catto discusses clipped box manifolds in 2D; this is the 3D Skullbonez
//   extension that clips an incident OBB face against the side planes of the
//   reference OBB face.
int ClipPolygonAgainstPlane( const ClipVertex* input,
                             int inputCount,
                             const Vector3& planePoint,
                             const Vector3& inwardNormal,
                             float contactSkin,
                             ClipVertex* output )
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
    float prevDist = ( prev.point - planePoint ) * inwardNormal;
    bool prevInside = prevDist >= -contactSkin;

    for ( int i = 0; i < inputCount; ++i )
    {
        ClipVertex cur = input[i];
        float curDist = ( cur.point - planePoint ) * inwardNormal;
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

// ENGINE-SPECIFIC / NOVEL:
//   Clip the incident face to the four side planes surrounding the reference
//   face. The surviving polygon vertices are the multi-point face manifold that
//   the persistent Catto-style solver can warm start independently.
int ClipIncidentFaceToReference( const BoxWorld& refBox,
                                 int refAxis,
                                 float refSign,
                                 const ClipVertex incident[4],
                                 float contactSkin,
                                 ClipVertex clipped[8] )
{
    ClipVertex tempA[8];
    ClipVertex tempB[8];
    int count = 4;
    for ( int i = 0; i < 4; ++i )
    {
        tempA[i] = incident[i];
    }

    Vector3 faceCenter = refBox.center +
                         refBox.axes[refAxis] * ( refSign * Component( refBox.halfExtents, refAxis ) );

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

// ENGINE-SPECIFIC / NOVEL:
//   Pick the incident face most anti-parallel to the reference normal. This is
//   the standard clipping-manifold choice, but the exact tie behavior is local
//   policy to keep deterministic feature IDs.
int ChooseIncidentFace( const BoxWorld& incidentBox, const Vector3& refNormal, float& faceSign )
{
    int faceAxis = 0;
    float bestDot = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        float dot = incidentBox.axes[axis] * refNormal;
        float absDot = fabsf( dot );
        if ( absDot < bestDot )
        {
            bestDot = absDot;
        }
    }

    bestDot = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        float dotPositive = incidentBox.axes[axis] * refNormal;
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
// ENGINE-SPECIFIC / NOVEL:
//   Reference/incident face clipping is the engine's 3D OBB manifold generator.
//   Contact points are placed halfway through the residual separation so the
//   solver receives centered rA/rB arms for shallow overlap.
bool BuildBoxFaceContact( const GameModel& aModel,
                          const GameModel& bModel,
                          const BoxWorld& boxA,
                          const BoxWorld& boxB,
                          bool referenceIsA,
                          int referenceAxis,
                          const Vector3& finalNormal,
                          float contactSkin,
                          ObjectContactManifold& out )
{
    const BoxWorld& refBox = referenceIsA ? boxA : boxB;
    const BoxWorld& incBox = referenceIsA ? boxB : boxA;
    Vector3 refNormal = referenceIsA ? finalNormal : -finalNormal;
    float refSign = ( refBox.axes[referenceAxis] * refNormal >= 0.0f ) ? 1.0f : -1.0f;

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

    for ( int i = 0; i < clippedCount && out.pointCount < 4; ++i )
    {
        float separation = ( clipped[i].point - refFaceCenter ) * refNormal;
        if ( separation > contactSkin )
        {
            continue;
        }

        Vector3 contactPoint = clipped[i].point - refNormal * ( separation * 0.5f );
        AddContactPoint( aModel,
                         bModel,
                         out,
                         contactPoint,
                         -separation,
                         EncodeBoxFaceFeature( referenceIsA, referenceFace, incidentFace, clipped[i].id ) );
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

void BuildEdgeSegment( const BoxWorld& box,
                       int edgeAxis,
                       const Vector3& towardNormal,
                       bool maximize,
                       Vector3& p0,
                       Vector3& p1,
                       uint32_t& edgeId )
{
    // Build the world-space line segment for the edge most exposed in the
    // contact direction. Edge/edge contacts need the actual two endpoints so the
    // closest-points calculation can find the single representative touch point.
    int side0 = ( edgeAxis + 1 ) % 3;
    int side1 = ( edgeAxis + 2 ) % 3;
    int sign0 = ( box.axes[side0] * towardNormal >= 0.0f ) ? 1 : -1;
    int sign1 = ( box.axes[side1] * towardNormal >= 0.0f ) ? 1 : -1;
    if ( !maximize )
    {
        sign0 = -sign0;
        sign1 = -sign1;
    }

    Vector3 center = box.center +
                     box.axes[side0] * ( static_cast<float>( sign0 ) * Component( box.halfExtents, side0 ) ) +
                     box.axes[side1] * ( static_cast<float>( sign1 ) * Component( box.halfExtents, side1 ) );
    Vector3 edge = box.axes[edgeAxis] * Component( box.halfExtents, edgeAxis );
    p0 = center - edge;
    p1 = center + edge;
    edgeId = EdgeId( edgeAxis, sign0, sign1 );
}

// ENGINE-SPECIFIC / NOVEL:
//   Segment-segment closest points are used only for SAT edge-edge axes, where a
//   clipped face manifold would be under-constrained. The midpoint becomes one
//   Catto-style contact row with an edge-pair feature ID.
void ClosestPointsOnSegments( const Vector3& p1,
                              const Vector3& q1,
                              const Vector3& p2,
                              const Vector3& q2,
                              Vector3& c1,
                              Vector3& c2 )
{
    Vector3 d1 = q1 - p1;
    Vector3 d2 = q2 - p2;
    Vector3 r = p1 - p2;
    float a = d1 * d1;
    float e = d2 * d2;
    float f = d2 * r;
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
        float c = d1 * r;
        if ( e <= TOLERANCE )
        {
            s = ClampFloat( -c / a, 0.0f, 1.0f );
        }
        else
        {
            float b = d1 * d2;
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

// ENGINE-SPECIFIC / NOVEL:
//   Edge contacts are the fallback for cross-product SAT axes. They intentionally
//   produce a single row because two OBB edges touching do not have a contact
//   patch to clip.
bool BuildBoxEdgeContact( const GameModel& aModel,
                          const GameModel& bModel,
                          const BoxWorld& boxA,
                          const BoxWorld& boxB,
                          const SatResult& sat,
                          ObjectContactManifold& out )
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
    AddContactPoint( aModel,
                     bModel,
                     out,
                     ( ca + cb ) * 0.5f,
                     sat.overlap,
                     EncodeBoxEdgeFeature( edgeA, edgeB ) );
    return out.pointCount > 0;
}

// ENGINE-SPECIFIC / NOVEL:
//   Box/box first chooses the SAT minimum-overlap axis, then maps face axes to a
//   clipped four-point manifold and edge axes to a one-point edge manifold. The
//   persistent solver downstream is Catto-style; this shape dispatch is local.
bool BuildBoxBox( const GameModel& aModel,
                  const BoundingBox& aBox,
                  const GameModel& bModel,
                  const BoundingBox& bBox,
                  float contactSkin,
                  ObjectContactManifold& out )
{
    BoxWorld boxA = MakeBoxWorld( aModel, aBox );
    BoxWorld boxB = MakeBoxWorld( bModel, bBox );
    SatResult sat;
    if ( !BoxBoxSat( boxA, boxB, contactSkin, sat ) )
    {
        return false;
    }

    out.normal = sat.normal;
    if ( sat.axisType == 0 )
    {
        return BuildBoxFaceContact( aModel, bModel, boxA, boxB, true, sat.axisA, sat.normal, contactSkin, out );
    }
    if ( sat.axisType == 1 )
    {
        return BuildBoxFaceContact( aModel, bModel, boxA, boxB, false, sat.axisB, sat.normal, contactSkin, out );
    }
    return BuildBoxEdgeContact( aModel, bModel, boxA, boxB, sat, out );
}
} // namespace

// CATTO REF:
//   Public entry point that returns the contact rows Catto's iterative solver
//   expects: normal, rA/rB, penetration, and stable contact IDs.
// ENGINE-SPECIFIC / NOVEL:
//   Dispatches Skullbonez collision shapes to the local 3D manifold builders.
//   The normal is always oriented from body A toward body B so the solver can use
//   one impulse sign convention for every pair.
bool SkullbonezCore::Physics::BuildObjectContactManifold( const GameModel& a,
                                                          const GameModel& b,
                                                          int bodyA,
                                                          int bodyB,
                                                          float contactSkin,
                                                          ObjectContactManifold& out )
{
    // Shape dispatch is intentionally explicit. The solver wants one uniform
    // manifold shape, but the geometry needed to produce it differs a lot:
    // sphere/sphere is center distance, sphere/box is closest point, and box/box
    // is SAT plus face clipping or edge fallback.
    out = ObjectContactManifold();
    out.bodyA = bodyA;
    out.bodyB = bodyB;

    const CollisionShape& shapeA = a.GetCollisionShape();
    const CollisionShape& shapeB = b.GetCollisionShape();

    if ( const BoundingSphere* sphereA = std::get_if<BoundingSphere>( &shapeA ) )
    {
        if ( const BoundingSphere* sphereB = std::get_if<BoundingSphere>( &shapeB ) )
        {
            return BuildSphereSphere( a, *sphereA, b, *sphereB, contactSkin, out );
        }
        if ( const BoundingBox* boxB = std::get_if<BoundingBox>( &shapeB ) )
        {
            return BuildSphereBoxOrdered( a, *sphereA, b, *boxB, true, contactSkin, out );
        }
    }

    if ( const BoundingBox* boxA = std::get_if<BoundingBox>( &shapeA ) )
    {
        if ( const BoundingSphere* sphereB = std::get_if<BoundingSphere>( &shapeB ) )
        {
            return BuildSphereBoxOrdered( b, *sphereB, a, *boxA, false, contactSkin, out );
        }
        if ( const BoundingBox* boxB = std::get_if<BoundingBox>( &shapeB ) )
        {
            return BuildBoxBox( a, *boxA, b, *boxB, contactSkin, out );
        }
    }

    return false;
}
