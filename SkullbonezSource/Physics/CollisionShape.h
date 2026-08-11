/*
File: SkullbonezSource/Physics/CollisionShape.h
Purpose:
  Wraps supported collision shapes behind one variant-style interface.

Summary:
  CollisionShape owns one supported shape value for authoring and interchange.
  CollisionShapeReference is the non-owning runtime view used by dense collider
  rows to point into ColliderStore's separate sphere, box, and hull stores.
  Shared visitors keep both representations on the same exhaustive nonvirtual
  dispatch surface.

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
    borrowed CollisionShapeReference variant. Each shape type provides matching
    member functions, and collision testing produces a compile-time table.
*/

template <typename ShapeLike> inline const Vector::Vector3& GetShapePosition( const ShapeLike& shape )
{
    return VisitCollisionShape( shape, []( const auto& s ) -> const Vector::Vector3& { return s.GetPosition(); } );
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

/*
Concept: Double-dispatch collision test

    Tests collision between any owning/reference shape pair. Nested exhaustive
    visits produce a compile-time N*N dispatch table, so adding a shape type
    requires every concrete collision pair to compile.
*/
template <typename FocusShape, typename TargetShape>
inline float TestShapeCollision( const FocusShape& focus, const TargetShape& target, const Geometry::Ray& focusRay,
                                 const Geometry::Ray& targetRay )
{

    // Double visit is the collision-shape switchboard. If focus is a sphere and
    // target is a box, the compiler chooses BoundingSphere::TestCollision(box).
    // If both are boxes, it chooses BoundingBox::TestCollision(box), and so on.
    return VisitCollisionShape( focus,
                                [&]( const auto& f )
                                {
                                    return VisitCollisionShape( target, [&]( const auto& t )
                                                                { return f.TestCollision( t, targetRay, focusRay ); } );
                                } );
}

} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
