#pragma once


// --- Includes ---
#include <variant>
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezBoundingBox.h"

namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
/* -- CollisionShape -------------------------------------------------------------------------------------------------------------------------------------------------

    A std::variant-based type-safe union of all collision shape types.
    Replaces the old DynamicsObject inheritance hierarchy with compile-time
    exhaustive dispatch via std::visit. Adding a new shape type to this
    variant will cause compiler errors at every unhandled dispatch site.

    Layman version: this is the engine's tagged box saying "this model's
    collision volume is either a sphere or a box." Callers do not ask through a
    base class; they visit the tag and run the shape-specific code directly.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
using CollisionShape = std::variant<BoundingSphere, BoundingBox>;

/* -- Free-function visitors -----------------------------------------------------------------------------------------------------------------------------------------

    These functions dispatch on the CollisionShape variant. For single-dispatch
    operations, each shape type provides a matching member function. For
    double-dispatch (collision testing), std::visit over two variants produces
    a compile-time dispatch table.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/

inline const Vector3& GetShapePosition( const CollisionShape& shape )
{
    return std::visit( []( const auto& s ) -> const Vector3&
                       { return s.GetPosition(); },
                       shape );
}

inline float GetShapeVolume( const CollisionShape& shape )
{
    return std::visit( []( const auto& s )
                       { return s.GetVolume(); },
                       shape );
}

inline float GetShapeDragCoefficient( const CollisionShape& shape )
{
    return std::visit( []( const auto& s )
                       { return s.GetDragCoefficient(); },
                       shape );
}

inline float GetShapeProjectedSurfaceArea( const CollisionShape& shape )
{
    return std::visit( []( const auto& s )
                       { return s.GetProjectedSurfaceArea(); },
                       shape );
}

inline float GetShapeSubmergedVolumePercent( const CollisionShape& shape, float fluidSurfaceHeight )
{
    return std::visit( [fluidSurfaceHeight]( const auto& s )
                       { return s.GetSubmergedVolumePercent( fluidSurfaceHeight ); },
                       shape );
}

inline float GetShapeBoundingRadius( const CollisionShape& shape )
{
    return std::visit( []( const auto& s )
                       { return s.GetBoundingRadius(); },
                       shape );
}

inline float GetShapeTerrainBottomOffset( const CollisionShape& shape )
{
    // For all shape types, the terrain bottom offset equals the bounding radius
    // (the farthest point from the shape's local origin). For a sphere this is
    // simply the radius. For a box it is the corner distance sqrt(a²+b²+c²).
    return std::visit( []( const auto& s )
                       { return s.GetBoundingRadius(); },
                       shape );
}

inline Transformation::Matrix4 GetShapeModelMatrix( const CollisionShape& shape, const Vector3& worldPos, const Transformation::Matrix4& rotation )
{
    return std::visit( [&]( const auto& s )
                       { return s.GetModelMatrix( worldPos, rotation ); },
                       shape );
}

/* -- Double-dispatch collision test ---------------------------------------------------------------------------------------------------------------------------------

    Tests collision between two CollisionShape variants. std::visit on two
    variants produces a compile-time N*N dispatch table. When new shape types
    are added, the compiler will enforce that all pair combinations are handled.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
inline float TestShapeCollision( const CollisionShape& focus, const CollisionShape& target, const Ray& focusRay, const Ray& targetRay )
{
    // Double visit is the collision-shape switchboard. If focus is a sphere and
    // target is a box, the compiler chooses BoundingSphere::TestCollision(box).
    // If both are boxes, it chooses BoundingBox::TestCollision(box), and so on.
    return std::visit( [&]( const auto& f, const auto& t )
                       { return f.TestCollision( t, targetRay, focusRay ); },
                       focus,
                       target );
}

} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
