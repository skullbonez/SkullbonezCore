#pragma once
#include "CollisionShape.h"
#include "../Maths/Quaternion.h"

namespace SkullbonezCore::Physics
{
// Bounds uninflated shape support relative to the body origin along a unit
// normal over a nonnegative duration, including the pose integrator's angular
// approximation. Translation is deliberately left to the calling geometry query.
float MaximumRotatedProjection( const Math::Orientation::Quaternion& orientation,
                                const Math::CollisionDetection::CollisionShapeReference& shape,
                                const Math::Vector::Vector3& angularVelocity,
                                const Math::Vector::Vector3& normal,
                                float stepDuration );
} // namespace SkullbonezCore::Physics
