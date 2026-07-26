/*
File: SkullbonezSource/Physics/TerrainSupportClassifier.h
Purpose:
  Classifies terrain contacts as stable support, edge support, or sleep-inhibiting contact.

Summary:
  Physics owns support classification because it interprets contact footprint
  and sleep policy. The classifier consumes the detached Physics terrain view;
  it never reaches a World terrain owner.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Support probes consume only Physics-owned value contracts.

Related:
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once
#include "CollisionShape.h"
#include "PhysicsTerrainView.h"
#include "../Core/Profiler.h"
#include <cmath>

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
}
namespace Physics
{
// Terrain support classification is not the collision response itself. It is a
// safety check for the sleep/resting layer: "is this terrain contact stable
// enough that the body may be treated as supported?" A box can collide with
// terrain on one corner, and the solver should still push it out, but that
// corner contact should not let the body go to sleep as if it were lying flat.

// These terrain-support constants are intentionally kept beside the classifier
// instead of buried inside a terrain response routine. They are not Catto solver row
// constants; they describe Skullbonez policy for deciding whether a box/terrain
// contact is credible enough to seed resting support and sleeping.
static constexpr float BOX_TERRAIN_VERTEX_SUPPORT_SLACK = 0.15f;
static constexpr float BOX_TERRAIN_STABLE_FACE_DOT = 0.95f;  // ~18 degrees from the contact plane normal.
static constexpr float BOX_TERRAIN_STABLE_PATCH_DOT = 0.99f; // ~8 degrees from the terrain plane normal.

struct BoxTerrainVertexSupportProbe
{
    // Number of oriented-box corners whose world-space height is close enough
    // to their sampled terrain height to count as a real terrain footprint.
    // A high count means the box is likely resting on a face/patch rather than
    // balancing on one unstable corner.
    int supportedVertices = 0;

    // False when all corners are outside the terrain bounds. Callers use this
    // to avoid writing meaningless min/max values into repro snapshots.
    bool hasTerrainGaps = false;

    // Signed corner height range relative to the terrain sample underneath each
    // in-bounds corner. Negative means penetration below the heightfield sample;
    // positive means air gap above it.
    float minTerrainGap = 0.0f;
    float maxTerrainGap = 0.0f;
};

struct BoxTerrainSupportClassification
{
    // Sphere-style terrain contacts preserve the legacy support behavior. The
    // stricter face/vertex footprint checks apply to shapes that can balance on
    // a narrow edge or point: OBBs and authored convex hulls.
    bool isBox = false;
    bool isConvexHull = false;

    // Best absolute alignment between any support face normal and the terrain
    // contact normal. Values near one mean a face is close to parallel with the
    // terrain plane; lower values indicate edge/point style contact geometry.
    float bestFaceNormalDot = 1.0f;

    BoxTerrainVertexSupportProbe vertices;

    // Final policy answer consumed by the terrain solver and sleep gate. This
    // deliberately does not change the contact rows themselves: Catto rows still
    // resolve penetration and impact, while this flag controls engine-specific
    // resting support, rest friction budget, and sleep eligibility.
    bool supportsRestingPolicy = true;
};

inline Math::Vector::Vector3 GetBoxTerrainLocalCorner( const Math::Vector::Vector3& halfExtents, int cornerIndex )
{
    // cornerIndex uses three bits as signs: bit 0 chooses +/-X, bit 1 chooses
    // +/-Y, and bit 2 chooses +/-Z. This compactly enumerates all eight box
    // corners without a table.
    return Math::Vector::Vector3( ( cornerIndex & 1 ) ? halfExtents.x : -halfExtents.x,
                                  ( cornerIndex & 2 ) ? halfExtents.y : -halfExtents.y,
                                  ( cornerIndex & 4 ) ? halfExtents.z : -halfExtents.z );
}

inline float ComputeBoxTerrainBestFaceNormalDotImpl( const Math::Transformation::RotationMatrix& orientation,
                                                     const Math::Vector::Vector3& terrainNormal )
{
    using Math::Vector::Vector3;

    // A box face normal is one of the oriented local axes. Taking absolute dots
    // with the terrain normal lets either side of a face count, which is what we
    // want for support classification: the question is "is any face aligned with
    // this terrain plane?", not "which side of the box is facing up?".
    const Vector3 axisX = orientation * Vector3( 1.0f, 0.0f, 0.0f );
    const Vector3 axisY = orientation * Vector3( 0.0f, 1.0f, 0.0f );
    const Vector3 axisZ = orientation * Vector3( 0.0f, 0.0f, 1.0f );

    float bestDot = fabsf( axisX * terrainNormal );
    const float absDotY = fabsf( axisY * terrainNormal );
    if ( absDotY > bestDot )
    {
        bestDot = absDotY;
    }

    const float absDotZ = fabsf( axisZ * terrainNormal );
    if ( absDotZ > bestDot )
    {
        bestDot = absDotZ;
    }

    return bestDot;
}

inline float ComputeBoxTerrainBestFaceNormalDot( Core::Profiler* profiler,
                                                 const Math::Transformation::RotationMatrix& orientation,
                                                 const Math::Vector::Vector3& terrainNormal,
                                                 bool profile )
{
    if ( profile )
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/BoxSupportPolicyFaceAxes" );
        return ComputeBoxTerrainBestFaceNormalDotImpl( orientation, terrainNormal );
    }

    return ComputeBoxTerrainBestFaceNormalDotImpl( orientation, terrainNormal );
}

inline BoxTerrainVertexSupportProbe
ProbeBoxTerrainVerticesImpl( const Math::CollisionDetection::BoundingBox& box,
                             const Math::Vector::Vector3& position,
                             const Math::Transformation::RotationMatrix& orientation,
                             const PhysicsTerrainView& terrain,
                             float contactEpsilon )
{
    // Sample visible box corners against the heightfield. The solver may have a
    // contact row from the terrain plane, but sleep support needs a footprint:
    // enough real corners close to terrain that a human would say the box is
    // resting, not balanced on a single point.
    BoxTerrainVertexSupportProbe result;
    const Math::Vector::Vector3& halfExtents = box.GetHalfExtents();

    // The solver contact epsilon catches very small numerical disagreement. The
    // additional slack allows a slightly tilted or heightfield-sampled footprint
    // to count as stable support, while still rejecting the original bad case:
    // a box with only a corner or unsupported edge near terrain.
    const float supportGap = contactEpsilon + BOX_TERRAIN_VERTEX_SUPPORT_SLACK;

    for ( int corner = 0; corner < 8; ++corner )
    {
        const Math::Vector::Vector3 local = GetBoxTerrainLocalCorner( halfExtents, corner );
        const Math::Vector::Vector3 worldVertex = position + ( orientation * local );
        if ( !terrain.IsInBounds( worldVertex.x, worldVertex.z ) )
        {
            continue;
        }

        const float terrainHeight = terrain.HeightAt( worldVertex.x, worldVertex.z );
        const float gap = worldVertex.y - terrainHeight;
        if ( gap <= supportGap )
        {
            ++result.supportedVertices;
        }

        if ( !result.hasTerrainGaps )
        {
            result.minTerrainGap = gap;
            result.maxTerrainGap = gap;
            result.hasTerrainGaps = true;
        }
        else
        {
            if ( gap < result.minTerrainGap )
            {
                result.minTerrainGap = gap;
            }
            if ( gap > result.maxTerrainGap )
            {
                result.maxTerrainGap = gap;
            }
        }
    }

    return result;
}

inline BoxTerrainVertexSupportProbe ProbeBoxTerrainVertices( Core::Profiler* profiler,
                                                             const Math::CollisionDetection::BoundingBox& box,
                                                             const Math::Vector::Vector3& position,
                                                             const Math::Transformation::RotationMatrix& orientation,
                                                             const PhysicsTerrainView& terrain,
                                                             float contactEpsilon,
                                                             bool profile )
{
    if ( profile )
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/BoxSupportPolicyVerts" );
        return ProbeBoxTerrainVerticesImpl( box, position, orientation, terrain, contactEpsilon );
    }

    return ProbeBoxTerrainVerticesImpl( box, position, orientation, terrain, contactEpsilon );
}

inline float ComputeConvexHullTerrainBestFaceNormalDotImpl( const Math::CollisionDetection::ConvexHullShape& hull,
                                                            const Math::Transformation::RotationMatrix& orientation,
                                                            const Math::Vector::Vector3& terrainNormal )
{
    float bestDot = 0.0f;
    const uint16_t faceCount = hull.GetFaceCount();
    for ( uint16_t faceIndex = 0; faceIndex < faceCount; ++faceIndex )
    {
        const Math::CollisionDetection::ConvexHullFace& face = hull.GetFace( faceIndex );
        const Math::Vector::Vector3 worldNormal = orientation * face.normalLocal;
        const float absDot = fabsf( worldNormal * terrainNormal );
        if ( absDot > bestDot )
        {
            bestDot = absDot;
        }
    }

    return bestDot;
}

inline float ComputeConvexHullTerrainBestFaceNormalDot( Core::Profiler* profiler,
                                                        const Math::CollisionDetection::ConvexHullShape& hull,
                                                        const Math::Transformation::RotationMatrix& orientation,
                                                        const Math::Vector::Vector3& terrainNormal,
                                                        bool profile )
{
    if ( profile )
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/HullSupportPolicyFaces" );
        return ComputeConvexHullTerrainBestFaceNormalDotImpl( hull, orientation, terrainNormal );
    }

    return ComputeConvexHullTerrainBestFaceNormalDotImpl( hull, orientation, terrainNormal );
}

inline BoxTerrainVertexSupportProbe
ProbeConvexHullTerrainVerticesImpl( const Math::CollisionDetection::ConvexHullShape& hull,
                                    const Math::Vector::Vector3& position,
                                    const Math::Transformation::RotationMatrix& orientation,
                                    const PhysicsTerrainView& terrain,
                                    float contactEpsilon )
{
    BoxTerrainVertexSupportProbe result;
    const Math::Vector::Vector3 hullCenter = position + ( orientation * hull.GetPosition() );
    const float supportGap = contactEpsilon + BOX_TERRAIN_VERTEX_SUPPORT_SLACK;

    const uint16_t vertexCount = hull.GetVertexCount();
    for ( uint16_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex )
    {
        const Math::Vector::Vector3 worldVertex = hullCenter + ( orientation * hull.GetVertex( vertexIndex ) );
        if ( !terrain.IsInBounds( worldVertex.x, worldVertex.z ) )
        {
            continue;
        }

        const float terrainHeight = terrain.HeightAt( worldVertex.x, worldVertex.z );
        const float gap = worldVertex.y - terrainHeight;
        if ( gap <= supportGap )
        {
            ++result.supportedVertices;
        }

        if ( !result.hasTerrainGaps )
        {
            result.minTerrainGap = gap;
            result.maxTerrainGap = gap;
            result.hasTerrainGaps = true;
        }
        else
        {
            if ( gap < result.minTerrainGap )
            {
                result.minTerrainGap = gap;
            }
            if ( gap > result.maxTerrainGap )
            {
                result.maxTerrainGap = gap;
            }
        }
    }

    return result;
}

inline BoxTerrainVertexSupportProbe
ProbeConvexHullTerrainVertices( const Math::CollisionDetection::ConvexHullShape& hull,
                                Core::Profiler* profiler,
                                const Math::Vector::Vector3& position,
                                const Math::Transformation::RotationMatrix& orientation,
                                const PhysicsTerrainView& terrain,
                                float contactEpsilon,
                                bool profile )
{
    if ( profile )
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/HullSupportPolicyVerts" );
        return ProbeConvexHullTerrainVerticesImpl( hull, position, orientation, terrain, contactEpsilon );
    }

    return ProbeConvexHullTerrainVerticesImpl( hull, position, orientation, terrain, contactEpsilon );
}

inline BoxTerrainSupportClassification
ClassifyBoxTerrainSupportImpl( Core::Profiler* profiler,
                               const Math::CollisionDetection::CollisionShape& shape,
                               const Math::Vector::Vector3& position,
                               const Math::Transformation::RotationMatrix& orientation,
                               const Math::Vector::Vector3& terrainNormal,
                               const PhysicsTerrainView& terrain,
                               int contactCount,
                               float contactEpsilon,
                               bool profileChildren )
{
    using Math::CollisionDetection::BoundingBox;
    using Math::CollisionDetection::ConvexHullShape;

    BoxTerrainSupportClassification result;
    const BoundingBox* box = std::get_if<BoundingBox>( &shape );
    const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &shape );
    if ( !box && !hull )
    {
        // Sphere/capsule-style terrain contacts do not have an oriented face
        // footprint to test here, so leave supportsRestingPolicy at the legacy
        // default. The shared terrain rows still solve their Catto
        // normal/friction impulses.
        return result;
    }

    result.isBox = box != nullptr;
    result.isConvexHull = hull != nullptr;
    result.bestFaceNormalDot = box ? ComputeBoxTerrainBestFaceNormalDot( profiler,
                                                                         orientation,
                                                                         terrainNormal,
                                                                         profileChildren )
                                   : ComputeConvexHullTerrainBestFaceNormalDot( profiler,
                                                                                *hull,
                                                                                orientation,
                                                                                terrainNormal,
                                                                                profileChildren );

    bool supportsRestingPolicy = true;
    if ( contactCount > 0 && contactCount < 4 )
    {
        // A one- or two-point manifold may be a valid impact row, but it is not
        // enough evidence that the box is resting on a stable face. Require face
        // alignment first so an edge grazing the terrain does not get rest-only
        // support policy or sleep eligibility.
        supportsRestingPolicy = result.bestFaceNormalDot >= BOX_TERRAIN_STABLE_FACE_DOT;
    }

    if ( supportsRestingPolicy )
    {
        if ( terrain.IsValid() )
        {
            result.vertices = box ? ProbeBoxTerrainVertices( profiler,
                                                             *box,
                                                             position,
                                                             orientation,
                                                             terrain,
                                                             contactEpsilon,
                                                             profileChildren )
                                  : ProbeConvexHullTerrainVertices( *hull,
                                                                    profiler,
                                                                    position,
                                                                    orientation,
                                                                    terrain,
                                                                    contactEpsilon,
                                                                    profileChildren );

            const bool hasHeightfieldFootprint = result.vertices.supportedVertices >= 3;
            const bool hasStablePlanePatch = result.vertices.supportedVertices >= 2 && contactCount >= 3 &&
                                             result.bestFaceNormalDot >= BOX_TERRAIN_STABLE_PATCH_DOT;

            // Three or more supported corners is a real terrain footprint. Two
            // corners only count when the solver manifold already has at least
            // three rows and the face/terrain alignment is extremely close; that
            // permits uneven-but-flat terrain patches while rejecting a narrow
            // edge balance as a sleep seed.
            supportsRestingPolicy = hasHeightfieldFootprint || hasStablePlanePatch;
        }
        else
        {
            // Without terrain samples we cannot prove a heightfield footprint,
            // so choose the conservative answer. The contact can still solve;
            // it simply cannot seed rest/sleep policy from this classifier.
            supportsRestingPolicy = false;
        }
    }

    result.supportsRestingPolicy = supportsRestingPolicy;
    return result;
}

inline BoxTerrainSupportClassification
ClassifyBoxTerrainSupport( Core::Profiler* profiler,
                           const Math::CollisionDetection::CollisionShape& shape,
                           const Math::Vector::Vector3& position,
                           const Math::Transformation::RotationMatrix& orientation,
                           const Math::Vector::Vector3& terrainNormal,
                           const PhysicsTerrainView& terrain,
                           int contactCount,
                           float contactEpsilon,
                           bool profile )
{
    if ( profile )
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Terrain/BoxSupportPolicy" );
        return ClassifyBoxTerrainSupportImpl( profiler,
                                              shape,
                                              position,
                                              orientation,
                                              terrainNormal,
                                              terrain,
                                              contactCount,
                                              contactEpsilon,
                                              true );
    }

    return ClassifyBoxTerrainSupportImpl( profiler,
                                          shape,
                                          position,
                                          orientation,
                                          terrainNormal,
                                          terrain,
                                          contactCount,
                                          contactEpsilon,
                                          false );
}
} // namespace Physics
} // namespace SkullbonezCore
