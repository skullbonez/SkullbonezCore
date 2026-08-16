/*
File: SkullbonezSource/Rendering/ContactManifoldPresentation.h
Purpose:
  Defines a feature-neutral contact presentation value for diagnostic line rendering.

Summary:
  Upper layers may publish two detached body poses and a bounded set of contact
  frames. Rendering consumes only geometry, orientation, and source-fidelity
  facts; it never learns which product workflow selected the presentation patch.

Glossary:
  Presentation patch: Detached bounded points and poses submitted together for drawing.
  Exact source point: Point copied from the original producer record rather
    than derived later from a surviving body pose and contact arm.

Invariants:
  - The packet owns all values and retains no source pointer or subsystem authority.
  - Eight points cover the engine's largest reduced object or terrain patch.
  - A valid point carries one world-space normal, two tangents, and penetration.

Related:
  - SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.h
  - SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h
  - SkullbonezSource/Physics/ObjectContactManifold.h
*/
#pragma once

#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Rendering
{
inline constexpr std::size_t CONTACT_MANIFOLD_PRESENTATION_POINT_CAPACITY = 8u;

struct ContactBodyPosePresentation
{
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    bool valid = false;
};

struct ContactPointPresentation
{
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
    float penetration = 0.0f;

    // True when the publisher supplied the original narrowphase point instead
    // of deriving the surviving row's point from a detached body pose and arm.
    bool exactSourcePoint = false;
};

struct ContactManifoldPresentation
{
    ContactBodyPosePresentation bodies[2];
    ContactPointPresentation points[CONTACT_MANIFOLD_PRESENTATION_POINT_CAPACITY];
    uint8_t bodyCount = 0;
    uint8_t pointCount = 0;

    bool HasGeometry() const noexcept
    {
        return pointCount > 0u;
    }
};
} // namespace SkullbonezCore::Rendering
