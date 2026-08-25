/*
File: SkullbonezSource/Physics/CollisionShape.h
Purpose:
  Wraps supported collision shapes behind one variant-style interface.

Summary:
  CollisionShape owns one supported shape value for authoring and interchange.
  CollisionShapeReference is the non-owning runtime view used by dense collider
  rows to point into ColliderStore's separate sphere, box, and hull stores.
  Shared visitors keep both representations on the same exhaustive nonvirtual
  dispatch surface and compute cold minimum-thickness/farthest-point geometry.
  Direction-valid linear eligibility may additionally query the borrowed shape;
  convex hull support remains a bounded allocation-free vertex scan.

Glossary:
  Shape reference: Typed borrowed pointer; store-backed references also carry a
  per-kind storage index so their owner can rebind them after relocation.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Adding a supported shape kind must make unhandled visitors fail to compile

    for both owning and borrowed shape representations.
  - CollisionShapeReference never owns payload and remains valid only while its
    borrowed owning value lives; store-backed references are rebound whenever
    per-kind backing relocates.
  - ColliderStore may bind multiple hull references to one canonical
    path-plus-authored-scale row. Hull rows and their indices remain stable
    until the store is cleared.
  - Motion geometry is measured in metres about the body origin and is cached
    only at collider create/update boundaries.

Related:
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>
#include "BoundingSphere.h"
#include "BoundingBox.h"
#include "ConvexHullShape.h"
#include "../Maths/RotationMatrix.h"

namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
// Concept: a collision shape is a tagged value. Callers visit the active
// sphere, box, or convex hull directly, so a new shape makes every incomplete
// dispatch fail to compile instead of hiding behind a base-class fallback.
using CollisionShape = std::variant<BoundingSphere, BoundingBox, ConvexHullShape>;

// A typed read-only shape view. Collider rows include a per-kind storage index
// that survives store copies and backing relocation; transient views of owning
// CollisionShape values omit the index and live only for the call that borrows
// them.
class CollisionShapeReference
{
  public:
    static constexpr uint32_t INVALID_STORAGE_INDEX = ( std::numeric_limits<uint32_t>::max )();
    using Reference = std::variant<const BoundingSphere*, const BoundingBox*, const ConvexHullShape*>;

    CollisionShapeReference() = default;
    CollisionShapeReference( const BoundingSphere& sphere, uint32_t storageIndex )
        : m_reference( &sphere ), m_storageIndex( storageIndex )
    {
    }
    CollisionShapeReference( const BoundingBox& box, uint32_t storageIndex )
        : m_reference( &box ), m_storageIndex( storageIndex )
    {
    }
    CollisionShapeReference( const ConvexHullShape& hull, uint32_t storageIndex )
        : m_reference( &hull ), m_storageIndex( storageIndex )
    {
    }
    CollisionShapeReference( const CollisionShape& shape )
        : m_reference( std::visit( []( const auto& value ) -> Reference { return Reference { &value }; }, shape ) )
    {
    }

    bool IsValid() const noexcept
    {
        return std::visit( []( const auto* shape ) { return shape != nullptr; }, m_reference );
    }
    bool HasStorageIndex() const noexcept
    {
        return m_storageIndex != INVALID_STORAGE_INDEX;
    }
    uint32_t StorageIndex() const noexcept
    {
        return m_storageIndex;
    }
    const Reference& TypedReference() const noexcept
    {
        return m_reference;
    }

  private:
    Reference m_reference = static_cast<const BoundingSphere*>( nullptr );
    uint32_t m_storageIndex = INVALID_STORAGE_INDEX;
};

template <typename Visitor> decltype( auto ) VisitCollisionShape( const CollisionShape& shape, Visitor&& visitor )
{
    return std::visit( std::forward<Visitor>( visitor ), shape );
}

template <typename Visitor> decltype( auto ) VisitCollisionShape( const CollisionShapeReference& shape, Visitor&& visitor )
{
    assert( shape.IsValid() );
    return std::visit( [&]( const auto* value ) -> decltype( auto ) { return visitor( *value ); }, shape.TypedReference() );
}

template <typename ShapeT> const ShapeT* GetShapeIf( const CollisionShape* shape )
{
    return shape ? std::get_if<ShapeT>( shape ) : nullptr;
}

template <typename ShapeT> const ShapeT* GetShapeIf( const CollisionShapeReference* shape )
{
    if ( !shape || !shape->IsValid() )
    {
        return nullptr;
    }

    const auto* reference = std::get_if<const ShapeT*>( &shape->TypedReference() );
    return reference ? *reference : nullptr;
}

template <typename ShapeT, typename ShapeLike> bool HoldsShape( const ShapeLike& shape )
{
    return GetShapeIf<ShapeT>( &shape ) != nullptr;
}

inline CollisionShape CopyCollisionShape( const CollisionShapeReference& shape )
{
    // Explicit cold bridge for authoring transactions that truly require an
    // owned value. Frame, pick, editor-overlay, and replay drawing scans consume
    // CollisionShapeReference directly and therefore never copy hull payload.
    return VisitCollisionShape( shape, []( const auto& value ) -> CollisionShape { return value; } );
}

/*
Concept: Free-function collision-shape visitors

    These functions dispatch on either the owning CollisionShape variant or the
    borrowed CollisionShapeReference variant. Shared property operations use
    common member names. ObjectContactManifold owns swept-pair dispatch: generic
    pairs use their shape members, while both sphere-box orders use its exact
    SweepSphereAgainstBox branch.
*/

template <typename ShapeLike> inline Vector::Vector3 GetShapePosition( const ShapeLike& shape )
{
    return VisitCollisionShape( shape, []( const auto& s ) -> const Vector::Vector3& { return s.GetPosition(); } );
}

template <typename ShapeLike>
inline Vector::Vector3 GetWorldShapeCenter( const ShapeLike& shape, const Vector::Vector3& bodyPosition,
                                            const Transformation::RotationMatrix& bodyOrientation )
{
    // Invariant: collider offsets are body-local. Broadphase, narrowphase,
    // terrain, queries, and presentation must all rotate the offset before
    // adding the body origin. This rule intentionally changes byte-exact
    // Physics behavior only for non-zero-offset colliders.
    return bodyPosition + bodyOrientation * GetShapePosition( shape );
}

inline Vector::Vector3 GetRenderShapeCenter( const Vector::Vector3& bodyPosition,
                                             const Transformation::Matrix4& bodyRotation,
                                             const Vector::Vector3& localPosition ) noexcept
{
    // Hazard: render transforms are compared bit-for-bit across Debug,
    // Profile, and Release. Volatile stage results forbid optimized builds
    // from fusing or reassociating this presentation-only matrix translation.
    volatile float x = bodyRotation.m[0] * localPosition.x;
    x = x + bodyRotation.m[4] * localPosition.y;
    x = x + bodyRotation.m[8] * localPosition.z;
    x = bodyPosition.x + x;

    volatile float y = bodyRotation.m[1] * localPosition.x;
    y = y + bodyRotation.m[5] * localPosition.y;
    y = y + bodyRotation.m[9] * localPosition.z;
    y = bodyPosition.y + y;

    volatile float z = bodyRotation.m[2] * localPosition.x;
    z = z + bodyRotation.m[6] * localPosition.y;
    z = z + bodyRotation.m[10] * localPosition.z;
    z = bodyPosition.z + z;
    return Vector::Vector3( x, y, z );
}

template <typename ShapeLike> inline float GetShapeVolume( const ShapeLike& shape )
{
    return VisitCollisionShape( shape, []( const auto& s ) { return s.GetVolume(); } );
}

template <typename ShapeLike> inline float GetShapeDragCoefficient( const ShapeLike& shape )
{
    return VisitCollisionShape( shape, []( const auto& s ) { return s.GetDragCoefficient(); } );
}

template <typename ShapeLike> inline float GetShapeProjectedSurfaceArea( const ShapeLike& shape )
{
    return VisitCollisionShape( shape, []( const auto& s ) { return s.GetProjectedSurfaceArea(); } );
}

template <typename ShapeLike> inline float GetShapeSubmergedVolumePercent( const ShapeLike& shape, float fluidSurfaceHeight )
{
    return VisitCollisionShape( shape, [fluidSurfaceHeight]( const auto& s )
                                { return s.GetSubmergedVolumePercent( fluidSurfaceHeight ); } );
}

template <typename ShapeLike> inline float GetShapeBoundingRadius( const ShapeLike& shape )
{
    return VisitCollisionShape( shape, []( const auto& s ) { return s.GetBoundingRadius(); } );
}

template <typename ShapeLike> inline float GetShapeBodyOriginBoundingRadius( const ShapeLike& shape )
{
    // Why: authored broadphase radii are measured about the body origin, while
    // concrete shape radii are measured about the collider centre. Adding the
    // local-offset length is the rotation-independent conservative envelope.
    return GetShapeBoundingRadius( shape ) + Vector::VectorMag( GetShapePosition( shape ) );
}

struct CollisionShapeMotionGeometry
{
    // Invariant: both metre-valued scalars describe one immutable authored
    // shape about its owning body origin and are cached in the same collider row.
    float minimumCollisionThickness = 0.0f;
    float maximumCenterOfMassRadius = 0.0f;
};

inline CollisionShapeMotionGeometry GetCollisionShapeMotionGeometry( const BoundingSphere& sphere )
{
    const float radius = (std::max)( 0.0f, sphere.GetRadius() );
    return { radius * 2.0f, Vector::VectorMag( sphere.GetPosition() ) + radius };
}

inline CollisionShapeMotionGeometry GetCollisionShapeMotionGeometry( const BoundingBox& box )
{
    const Vector::Vector3 half = box.GetHalfExtents();
    CollisionShapeMotionGeometry geometry;
    geometry.minimumCollisionThickness = 2.0f *
                                         (std::min)( { std::fabs( half.x ), std::fabs( half.y ), std::fabs( half.z ) } );

    float maxRadiusSq = 0.0f;

    for ( int sx : { -1, 1 } )
    {
        for ( int sy : { -1, 1 } )
        {
            for ( int sz : { -1, 1 } )
            {
                const Vector::Vector3 corner = box.GetPosition() + Vector::Vector3( half.x * static_cast<float>( sx ),
                                                                                    half.y * static_cast<float>( sy ),
                                                                                    half.z * static_cast<float>( sz ) );
                maxRadiusSq = (std::max)( maxRadiusSq, Vector::VectorMagSquared( corner ) );
            }
        }
    }

    geometry.maximumCenterOfMassRadius = std::sqrt( maxRadiusSq );
    return geometry;
}

inline CollisionShapeMotionGeometry GetCollisionShapeMotionGeometry( const ConvexHullShape& hull )
{
    CollisionShapeMotionGeometry geometry;
    float minimumWidth = ( std::numeric_limits<float>::max )();
    float maxRadiusSq = 0.0f;

    for ( uint16_t vertexIndex = 0; vertexIndex < hull.GetVertexCount(); ++vertexIndex )
    {
        const Vector::Vector3 point = hull.GetPosition() + hull.GetVertex( vertexIndex );
        maxRadiusSq = (std::max)( maxRadiusSq, Vector::VectorMagSquared( point ) );
    }

    // Invariant: a convex polyhedron's minimum width is attained along a
    // supporting face normal. Serialized normals need not be unit length, so
    // normalize them here to keep every projection and threshold in metres.
    for ( uint16_t faceIndex = 0; faceIndex < hull.GetFaceCount(); ++faceIndex )
    {
        const Vector::Vector3 authoredNormal = hull.GetFace( faceIndex ).normalLocal;
        const float normalLengthSquared = Vector::VectorMagSquared( authoredNormal );

        if ( !std::isfinite( normalLengthSquared ) || normalLengthSquared <= 0.0f )
        {
            minimumWidth = 0.0f;
            break;
        }

        const Vector::Vector3 normal = authoredNormal * ( 1.0f / std::sqrt( normalLengthSquared ) );
        float minProjection = ( std::numeric_limits<float>::max )();
        float maxProjection = ( std::numeric_limits<float>::lowest )();

        for ( uint16_t vertexIndex = 0; vertexIndex < hull.GetVertexCount(); ++vertexIndex )
        {
            const float projection = Vector::Dot( normal, hull.GetVertex( vertexIndex ) );
            minProjection = (std::min)( minProjection, projection );
            maxProjection = (std::max)( maxProjection, projection );
        }

        minimumWidth = (std::min)( minimumWidth, maxProjection - minProjection );
    }

    geometry.minimumCollisionThickness = std::isfinite( minimumWidth ) ? (std::max)( 0.0f, minimumWidth ) : 0.0f;
    geometry.maximumCenterOfMassRadius = std::sqrt( maxRadiusSq );
    return geometry;
}

template <typename ShapeLike> inline CollisionShapeMotionGeometry GetCollisionShapeMotionGeometry( const ShapeLike& shape )
{
    // Why: eligibility runs every fixed tick, while exact shape topology changes
    // only at cold collider create/update boundaries. Resolve vertex/face work
    // once and cache the two scalars on the owning ColliderRecord.
    return VisitCollisionShape( shape, []( const auto& value ) { return GetCollisionShapeMotionGeometry( value ); } );
}

template <typename ShapeLike> inline float GetShapeTerrainBottomOffset( const ShapeLike& shape )
{
    // For all shape types, the terrain bottom offset equals the bounding radius
    // (the farthest point from the shape's local origin). For a sphere this is
    // simply the radius. For a box it is the corner distance sqrt(a²+b²+c²).
    return VisitCollisionShape( shape, []( const auto& s ) { return s.GetBoundingRadius(); } );
}

template <typename ShapeLike>
inline Transformation::Matrix4 GetShapeModelMatrix( const ShapeLike& shape, const Vector::Vector3& worldPos,
                                                    const Transformation::Matrix4& rotation )
{
    return VisitCollisionShape( shape, [&]( const auto& s ) { return s.GetModelMatrix( worldPos, rotation ); } );
}

template <typename ShapeLike>
inline bool ScaleShapeAxisFromBase( const ShapeLike& baseShape, int axis, float factor, CollisionShape& outScaledShape )
{
    if ( axis < 0 || axis > 2 || !std::isfinite( factor ) || factor <= 0.0f )
    {
        return false;
    }

    factor = std::clamp( factor, 0.05f, 20.0f );

    if ( const BoundingSphere* sphere = GetShapeIf<BoundingSphere>( &baseShape ) )
    {
        const float radius = (std::max)( 0.25f, sphere->GetRadius() * factor );
        outScaledShape = BoundingSphere( radius, sphere->GetPosition(), sphere->GetDragCoefficient() );
        return true;
    }

    if ( const BoundingBox* box = GetShapeIf<BoundingBox>( &baseShape ) )
    {
        Vector::Vector3 halfExtents = box->GetHalfExtents();

        if ( axis == 0 )
        {
            halfExtents.x = (std::max)( 0.25f, halfExtents.x * factor );
        }
        else if ( axis == 1 )
        {
            halfExtents.y = (std::max)( 0.25f, halfExtents.y * factor );
        }
        else
        {
            halfExtents.z = (std::max)( 0.25f, halfExtents.z * factor );
        }

        outScaledShape = BoundingBox( halfExtents, box->GetPosition() );
        return true;
    }

    if ( const ConvexHullShape* hullBase = GetShapeIf<ConvexHullShape>( &baseShape ) )
    {
        ConvexHullShape hull = *hullBase;
        hull.ScaleAxis( axis, factor );

        if ( hull.GetBoundingRadius() <= TOLERANCE )
        {
            return false;
        }

        outScaledShape = hull;
        return true;
    }

    return false;
}

} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
